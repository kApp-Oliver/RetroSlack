#include "rhttp_debug.h"

#ifdef RHTTP_DEBUG

#include <Devices.h>
#include <Serial.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static short g_bout = 0;

static OSErr ensure_bout(void) {
    OSErr err;
    if (g_bout) {
        return noErr;
    }
    err = OpenDriver("\p.BOut", &g_bout);
    if (err != noErr) {
        return err;
    }
    {
        CntrlParam cb;
        memset(&cb, 0, sizeof(cb));
        cb.ioCRefNum = g_bout;
        cb.csCode = 8; /* SerReset */
        cb.csParam[0] = (short)(baud19200 | data8 | noParity | stop10);
        err = PBControlSync((ParmBlkPtr)&cb);
    }
    return err;
}

void rhttp_dprintf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    IOParam pb;
    OSErr err;

    if (ensure_bout() != noErr) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    memset(&pb, 0, sizeof(pb));
    pb.ioRefNum = g_bout;
    pb.ioBuffer = (Ptr)buf;
    pb.ioReqCount = (long)strlen(buf);
    err = PBWriteSync((ParmBlkPtr)&pb);
    (void)err;
}

#endif /* RHTTP_DEBUG */
