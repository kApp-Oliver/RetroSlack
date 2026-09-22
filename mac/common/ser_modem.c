/*
 * Modem-port Serial Manager link (stock .AIn/.AOut). Shared by guest apps —
 * not the RHTTP client, which keeps its own port state.
 */

#include "ser_modem.h"

#include <Devices.h>
#include <Errors.h>
#include <OSUtils.h>
#include <Serial.h>
#include <string.h>

static short g_ain;
static short g_aout;
static int g_open;
static char g_ser_inbuf[2048];
static unsigned char g_even[256];
static SerModemYieldProc g_yield;
static int g_yielding;

static int ptr_odd(const void *p) {
    return ((unsigned long)p & 1ul) != 0;
}

static void copy_bytes(void *dst, const void *src, long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n-- > 0) {
        *d++ = *s++;
    }
}

void SerModemSetYield(SerModemYieldProc proc) {
    g_yield = proc;
}

static void ser_yield(void) {
    SystemTask();
    if (g_yield && !g_yielding) {
        g_yielding = 1;
        g_yield();
        g_yielding = 0;
    }
}

static OSErr ser_write(short ref, const void *data, long len) {
    IOParam pb;
    const unsigned char *src = (const unsigned char *)data;

    while (len > 0) {
        long take = len;
        const void *buf = src;
        OSErr err;

        if (ptr_odd(src)) {
            take = len;
            if (take > (long)sizeof(g_even)) {
                take = (long)sizeof(g_even);
            }
            copy_bytes(g_even, src, take);
            buf = g_even;
        }
        memset(&pb, 0, sizeof(pb));
        pb.ioRefNum = ref;
        pb.ioBuffer = (Ptr)buf;
        pb.ioReqCount = take;
        err = PBWriteSync((ParmBlkPtr)&pb);
        if (err != noErr) {
            return err;
        }
        src += take;
        len -= take;
    }
    return noErr;
}

static OSErr configure_port(short ref) {
    CntrlParam cb;
    memset(&cb, 0, sizeof(cb));
    cb.ioCRefNum = ref;
    cb.csCode = 8;
    cb.csParam[0] = (short)(baud19200 | data8 | noParity | stop10);
    return PBControlSync((ParmBlkPtr)&cb);
}

static OSErr configure_inbuf(short ref) {
    CntrlParam cb;
    memset(&cb, 0, sizeof(cb));
    cb.ioCRefNum = ref;
    cb.csCode = 9;
    *(Ptr *)cb.csParam = g_ser_inbuf;
    *(short *)((char *)cb.csParam + 4) = (short)sizeof(g_ser_inbuf);
    return PBControlSync((ParmBlkPtr)&cb);
}

static OSErr configure_handshake(short ref) {
    CntrlParam cb;
    memset(&cb, 0, sizeof(cb));
    cb.ioCRefNum = ref;
    cb.csCode = 10;
    return PBControlSync((ParmBlkPtr)&cb);
}

OSErr SerModemOpen(void) {
    OSErr err;

    if (g_open) {
        return noErr;
    }

    err = OpenDriver("\p.AIn", &g_ain);
    if (err != noErr) {
        return err;
    }
    err = OpenDriver("\p.AOut", &g_aout);
    if (err != noErr) {
        CloseDriver(g_ain);
        g_ain = 0;
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

    g_open = 1;
    return noErr;

fail:
    CloseDriver(g_aout);
    CloseDriver(g_ain);
    g_ain = g_aout = 0;
    return err;
}

OSErr SerModemClose(void) {
    if (!g_open) {
        return noErr;
    }
    CloseDriver(g_aout);
    CloseDriver(g_ain);
    g_ain = g_aout = 0;
    g_open = 0;
    return noErr;
}

int SerModemIsOpen(void) {
    return g_open;
}

OSErr SerModemWrite(const void *data, long len) {
    if (!g_open) {
        return notOpenErr;
    }
    if (len <= 0) {
        return noErr;
    }
    return ser_write(g_aout, data, len);
}

OSErr SerModemReadSome(void *data, long len, long *got) {
    IOParam pb;
    OSErr err;
    long avail;

    if (!g_open) {
        return notOpenErr;
    }
    *got = 0;
    err = SerGetBuf(g_ain, &avail);
    if (err != noErr) {
        return err;
    }
    if (avail <= 0) {
        return noErr;
    }
    if (avail > len) {
        avail = len;
    }
    if (ptr_odd(data) && avail > (long)sizeof(g_even)) {
        avail = (long)sizeof(g_even);
    }
    memset(&pb, 0, sizeof(pb));
    pb.ioRefNum = g_ain;
    if (ptr_odd(data)) {
        pb.ioBuffer = (Ptr)g_even;
    } else {
        pb.ioBuffer = (Ptr)data;
    }
    pb.ioReqCount = avail;
    err = PBReadSync((ParmBlkPtr)&pb);
    if (err != noErr) {
        return err;
    }
    if (ptr_odd(data) && pb.ioActCount > 0) {
        copy_bytes(data, g_even, pb.ioActCount);
    }
    *got = pb.ioActCount;
    return noErr;
}

OSErr SerModemReadExact(void *data, long need, long timeout_ticks) {
    long got_total = 0;
    long start = TickCount();
    unsigned char *p = (unsigned char *)data;

    if (!g_open) {
        return notOpenErr;
    }
    while (got_total < need) {
        long chunk = 0;
        OSErr err = SerModemReadSome(p + got_total, need - got_total, &chunk);
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
        ser_yield();
    }
    return noErr;
}

long SerModemAvail(void) {
    long avail = 0;
    if (!g_open) {
        return 0;
    }
    if (SerGetBuf(g_ain, &avail) != noErr) {
        return 0;
    }
    return avail;
}
