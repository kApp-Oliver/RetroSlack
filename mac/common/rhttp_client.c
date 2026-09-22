/*
 * RHTTP client for classic Mac — stock Serial Manager only (modem .AIn/.AOut).
 * Same binary path for Snow and real Macintosh Plus.
 */

#include "rhttp.h"
#include "rhttp_debug.h"

#include <Devices.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Serial.h>
#include <stdio.h>
#include <string.h>

/* Local copies of encode/decode to avoid linking host codec object formats.
 * Keep layout identical to protocol/rhttp_codec.c */

enum {
    kRhttpOk = 0,
    kRhttpBad = -1
};

static short g_ain;
static short g_aout;
static int g_open;
static char g_ser_inbuf[2048];
static uint32_t g_next_id = 1;
static RHTTPYieldProc g_yield;
static int g_yielding;
static unsigned char g_unrd[512];
static long g_unrd_len;

void RHTTPSetYieldProc(RHTTPYieldProc proc) {
    g_yield = proc;
}

static void rhttp_yield(void) {
    SystemTask();
    if (g_yield && !g_yielding) {
        g_yielding = 1;
        g_yield();
        g_yielding = 0;
    }
}

static void wr_u16_le(unsigned char *p, unsigned short v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void wr_u32_le(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static unsigned short rd_u16_le(const unsigned char *p) {
    return (unsigned short)(p[0] | (p[1] << 8));
}

static unsigned long rd_u32_le(const unsigned char *p) {
    return (unsigned long)p[0]
        | ((unsigned long)p[1] << 8)
        | ((unsigned long)p[2] << 16)
        | ((unsigned long)p[3] << 24);
}

static OSErr ser_write(short ref, const void *data, long len) {
    IOParam pb;
    memset(&pb, 0, sizeof(pb));
    pb.ioRefNum = ref;
    pb.ioBuffer = (Ptr)data;
    pb.ioReqCount = len;
    return PBWriteSync((ParmBlkPtr)&pb);
}

static OSErr ser_read_some(short ref, void *data, long len, long *got) {
    IOParam pb;
    OSErr err;
    long avail;

    *got = 0;
    if (g_unrd_len > 0) {
        long n = g_unrd_len;
        if (n > len) {
            n = len;
        }
        memcpy(data, g_unrd, (size_t)n);
        g_unrd_len -= n;
        if (g_unrd_len > 0) {
            memmove(g_unrd, g_unrd + n, (size_t)g_unrd_len);
        }
        *got = n;
        return noErr;
    }

    err = SerGetBuf(ref, &avail);
    if (err != noErr) {
        return err;
    }
    if (avail <= 0) {
        *got = 0;
        return noErr;
    }
    if (avail > len) {
        avail = len;
    }
    memset(&pb, 0, sizeof(pb));
    pb.ioRefNum = ref;
    pb.ioBuffer = (Ptr)data;
    pb.ioReqCount = avail;
    err = PBReadSync((ParmBlkPtr)&pb);
    if (err != noErr) {
        return err;
    }
    *got = pb.ioActCount;
    return noErr;
}

static OSErr ser_read_exact(short ref, void *data, long need, long timeout_ticks) {
    long got_total = 0;
    long start = TickCount();
    unsigned char *p = (unsigned char *)data;

    while (got_total < need) {
        long chunk = 0;
        OSErr err = ser_read_some(ref, p + got_total, need - got_total, &chunk);
        if (err != noErr) {
            return err;
        }
        if (chunk > 0) {
            got_total += chunk;
            continue;
        }
        if ((TickCount() - start) > timeout_ticks) {
            return ioErr;
        }
        /* Keep system / UI responsive while blocked on serial. */
        rhttp_yield();
    }
    return noErr;
}

static OSErr configure_port(short ref) {
    CntrlParam cb;
    memset(&cb, 0, sizeof(cb));
    cb.ioCRefNum = ref;
    cb.csCode = 8;
    /* 19200 8N1 — matches RHTTP_BAUD */
    cb.csParam[0] = (short)(baud19200 | data8 | noParity | stop10);
    return PBControlSync((ParmBlkPtr)&cb);
}

/* Default Serial Manager input buffer is 64 bytes — too small for RHTTP RES. */
static OSErr configure_inbuf(short ref) {
    CntrlParam cb;
    memset(&cb, 0, sizeof(cb));
    cb.ioCRefNum = ref;
    cb.csCode = 9; /* SerSetBuf */
    *(Ptr *)cb.csParam = g_ser_inbuf;
    *(short *)((char *)cb.csParam + 4) = (short)sizeof(g_ser_inbuf);
    return PBControlSync((ParmBlkPtr)&cb);
}

/* No XON/XOFF or CTS — pin 1–2 jumper then optional. */
static OSErr configure_handshake(short ref) {
    CntrlParam cb;
    memset(&cb, 0, sizeof(cb));
    cb.ioCRefNum = ref;
    cb.csCode = 10; /* SerHShake, SerShk all zero */
    return PBControlSync((ParmBlkPtr)&cb);
}

OSErr RHTTPOpen(void) {
    OSErr err;
    unsigned char ping[20];
    unsigned char resp[20];
    unsigned long id = 1;

    if (g_open) {
        return noErr;
    }

    err = OpenDriver("\p.AIn", &g_ain);
    if (err != noErr) {
        rhttp_dprintf("Open AIn %d\n", (int)err);
        return err;
    }
    err = OpenDriver("\p.AOut", &g_aout);
    if (err != noErr) {
        CloseDriver(g_ain);
        return err;
    }
    err = configure_port(g_ain);
    if (err != noErr) {
        goto fail;
    }
    err = configure_port(g_aout);
    if (err != noErr) {
        goto fail;
    }
    err = configure_inbuf(g_ain);
    if (err != noErr) {
        goto fail;
    }
    err = configure_handshake(g_aout);
    if (err != noErr) {
        goto fail;
    }

    /* Build PING frame */
    ping[0] = 'R'; ping[1] = 'H'; ping[2] = 'T'; ping[3] = 'P';
    ping[4] = 1;
    ping[5] = 4; /* PING */
    wr_u16_le(ping + 6, 0);
    wr_u32_le(ping + 8, id);
    wr_u32_le(ping + 12, 0);
    wr_u32_le(ping + 16, 0);

    err = ser_write(g_aout, ping, 20);
    if (err != noErr) {
        goto fail;
    }

    /* Wait for PONG magic + header */
    err = ser_read_exact(g_ain, resp, 20, 60 * 3); /* ~3 seconds */
    if (err != noErr) {
        rhttp_dprintf("PING timeout\n");
        goto fail;
    }
    if (resp[0] != 'R' || resp[5] != 5) {
        rhttp_dprintf("bad PONG\n");
        err = ioErr;
        goto fail;
    }

    g_open = 1;
    g_unrd_len = 0;
    rhttp_dprintf("RHTTP open ok\n");
    return noErr;

fail:
    CloseDriver(g_aout);
    CloseDriver(g_ain);
    g_ain = g_aout = 0;
    return err;
}

OSErr RHTTPClose(void) {
    if (!g_open) {
        return noErr;
    }
    CloseDriver(g_aout);
    CloseDriver(g_ain);
    g_ain = g_aout = 0;
    g_open = 0;
    g_unrd_len = 0;
    return noErr;
}

static OSErr parse_status(const char *hdr, long hdr_len, short *outStatus) {
    /* Look for "STATUS NNN" */
    long i;
    for (i = 0; i + 8 < hdr_len; i++) {
        if (hdr[i] == 'S' && strncmp(hdr + i, "STATUS ", 7) == 0) {
            short v = 0;
            const char *p = hdr + i + 7;
            while (*p >= '0' && *p <= '9') {
                v = (short)(v * 10 + (*p - '0'));
                p++;
            }
            *outStatus = v;
            return noErr;
        }
    }
    *outStatus = 0;
    return noErr;
}

OSErr RHTTPRequest(
    const char *method,
    const char *url,
    const char *extraHeaders,
    const void *body,
    long bodyLen,
    short *outStatus,
    Handle *outBody) {
    char meta[1024];
    long meta_len;
    unsigned long id;
    unsigned char hdr[20];
    unsigned char rhdr[20];
    unsigned long header_len;
    unsigned long body_len;
    Handle h = NULL;
    OSErr err;
    long total_meta;

    if (!g_open) {
        err = RHTTPOpen();
        if (err != noErr) {
            return err;
        }
    }
    if (!method || !url || !outStatus || !outBody) {
        return paramErr;
    }
    *outBody = NULL;
    *outStatus = 0;

    meta_len = 0;
    meta_len += sprintf(meta + meta_len, "METHOD %s\nURL %s\n", method, url);
    if (extraHeaders && extraHeaders[0]) {
        long el = (long)strlen(extraHeaders);
        if (meta_len + el < (long)sizeof(meta) - 1) {
            memcpy(meta + meta_len, extraHeaders, (size_t)el);
            meta_len += el;
            if (meta[meta_len - 1] != '\n') {
                meta[meta_len++] = '\n';
            }
            meta[meta_len] = '\0';
        }
    }
    total_meta = meta_len;
    if (bodyLen < 0) {
        bodyLen = 0;
    }
    if ((unsigned long)total_meta > 2048ul || (unsigned long)bodyLen > 65536ul) {
        return memFullErr;
    }

    id = g_next_id++;
    hdr[0] = 'R'; hdr[1] = 'H'; hdr[2] = 'T'; hdr[3] = 'P';
    hdr[4] = 1;
    hdr[5] = 1; /* REQ */
    wr_u16_le(hdr + 6, 0);
    wr_u32_le(hdr + 8, id);
    wr_u32_le(hdr + 12, (unsigned long)total_meta);
    wr_u32_le(hdr + 16, (unsigned long)bodyLen);

    err = ser_write(g_aout, hdr, 20);
    if (err != noErr) {
        return err;
    }
    err = ser_write(g_aout, meta, total_meta);
    if (err != noErr) {
        return err;
    }
    if (bodyLen > 0 && body) {
        err = ser_write(g_aout, body, bodyLen);
        if (err != noErr) {
            return err;
        }
    }

    rhttp_dprintf("REQ %lu sent\n", id);

    /* Read response header (resync on magic) */
    for (;;) {
        unsigned char b;
        err = ser_read_exact(g_ain, &b, 1, 60 * 30);
        if (err != noErr) {
            return err;
        }
        if (b != 'R') {
            continue;
        }
        rhdr[0] = b;
        err = ser_read_exact(g_ain, rhdr + 1, 19, 60 * 5);
        if (err != noErr) {
            return err;
        }
        if (rhdr[1] == 'H' && rhdr[2] == 'T' && rhdr[3] == 'P') {
            break;
        }
    }

    if (rhdr[4] != 1) {
        return ioErr;
    }
    header_len = rd_u32_le(rhdr + 12);
    body_len = rd_u32_le(rhdr + 16);
    if (header_len > 2048ul || body_len > 65536ul) {
        return memFullErr;
    }

    h = NewHandle((Size)(header_len + body_len + 1));
    if (!h) {
        return memFullErr;
    }
    HLock(h);
    if (header_len + body_len > 0) {
        err = ser_read_exact(g_ain, *h, (long)(header_len + body_len), 60 * 60);
        if (err != noErr) {
            HUnlock(h);
            DisposeHandle(h);
            return err;
        }
    }
    (*h)[header_len + body_len] = 0;

    if (rhdr[5] == 3) { /* ERR */
        rhttp_dprintf("ERR from NSE\n");
        HUnlock(h);
        DisposeHandle(h);
        return ioErr;
    }
    if (rhdr[5] != 2) { /* RES */
        HUnlock(h);
        DisposeHandle(h);
        return ioErr;
    }

    parse_status((char *)*h, (long)header_len, outStatus);

    /* Return body only as handle */
    if (body_len == 0) {
        HUnlock(h);
        DisposeHandle(h);
        *outBody = NewHandle(0);
        return noErr;
    }
    {
        Handle bout = NewHandle((Size)body_len);
        if (!bout) {
            HUnlock(h);
            DisposeHandle(h);
            return memFullErr;
        }
        HLock(bout);
        memcpy(*bout, *h + header_len, (size_t)body_len);
        HUnlock(bout);
        HUnlock(h);
        DisposeHandle(h);
        *outBody = bout;
    }
    rhttp_dprintf("RES status=%d len=%lu\n", (int)*outStatus, body_len);
    (void)rd_u16_le;
    return noErr;
}

static void rhttp_unread(const unsigned char *p, long n) {
    if (!p || n <= 0) {
        return;
    }
    if (g_unrd_len + n > (long)sizeof(g_unrd)) {
        n = (long)sizeof(g_unrd) - g_unrd_len;
    }
    if (n <= 0) {
        return;
    }
    memcpy(g_unrd + g_unrd_len, p, (size_t)n);
    g_unrd_len += n;
}

static int meta_is_stop(const char *meta, unsigned long len) {
    unsigned long i;
    if (!meta || len < 10) {
        return 0;
    }
    for (i = 0; i + 11 <= len; i++) {
        if ((i == 0 || meta[i - 1] == '\n')
            && strncmp(meta + i, "METHOD STOP", 11) == 0) {
            return 1;
        }
    }
    return 0;
}

int RHTTPPollStop(void) {
    unsigned char hdr[20];
    unsigned char meta[256];
    long got = 0;
    long avail = 0;
    unsigned long header_len;
    unsigned long body_len;
    OSErr err;

    if (!g_open) {
        return 0;
    }
    err = SerGetBuf(g_ain, &avail);
    if (err != noErr) {
        avail = 0;
    }
    if (g_unrd_len + avail < 20) {
        return 0;
    }

    err = ser_read_some(g_ain, hdr, 20, &got);
    if (err != noErr || got < 20) {
        if (got > 0) {
            rhttp_unread(hdr, got);
        }
        return 0;
    }
    if (hdr[0] != 'R' || hdr[1] != 'H' || hdr[2] != 'T' || hdr[3] != 'P') {
        rhttp_unread(hdr, 20);
        return 0;
    }
    if (hdr[5] == 4) {
        unsigned char pong[20];
        memcpy(pong, hdr, 20);
        pong[5] = 5;
        ser_write(g_aout, pong, 20);
        return 0;
    }
    if (hdr[5] != 1) {
        rhttp_unread(hdr, 20);
        return 0;
    }
    header_len = rd_u32_le(hdr + 12);
    body_len = rd_u32_le(hdr + 16);
    if (header_len == 0 || header_len > sizeof(meta) || body_len != 0) {
        rhttp_unread(hdr, 20);
        return 0;
    }
    err = SerGetBuf(g_ain, &avail);
    if (err != noErr) {
        avail = 0;
    }
    if (g_unrd_len + avail < (long)header_len) {
        rhttp_unread(hdr, 20);
        return 0;
    }
    {
        long filled = 0;
        while (filled < (long)header_len) {
            err = ser_read_some(g_ain, meta + filled, (long)header_len - filled, &got);
            if (err != noErr || got <= 0) {
                rhttp_unread(hdr, 20);
                if (filled > 0) {
                    rhttp_unread(meta, filled);
                }
                return 0;
            }
            filled += got;
        }
    }
    if (meta_is_stop((char *)meta, header_len)) {
        unsigned char reshdr[20];
        static const char res[] = "STATUS 200\nSTATE stopping\n";
        memcpy(reshdr, hdr, 20);
        reshdr[5] = 2;
        wr_u32_le(reshdr + 12, (unsigned long)(sizeof(res) - 1));
        wr_u32_le(reshdr + 16, 0);
        ser_write(g_aout, reshdr, 20);
        ser_write(g_aout, res, (long)(sizeof(res) - 1));
        return 1;
    }
    rhttp_unread(hdr, 20);
    rhttp_unread(meta, (long)header_len);
    return 0;
}
