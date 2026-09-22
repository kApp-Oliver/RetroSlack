#include "harness.h"
#include "ser_modem.h"

#include "../../protocol/rhttp.h"

#include <AppleEvents.h>
#include <Gestalt.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Processes.h>
#include <Resources.h>
#include <string.h>

#ifndef gestaltLaunchControl
#define gestaltLaunchControl 3
#endif
#ifndef launchNoFileFlags
#define launchNoFileFlags 0x0800
#endif

static Boolean *gDoneFlag;
static void (*gReturnHook)(void);
static short gLoaderVRef;
static long gLoaderDir;
static Str31 gLoaderName;
static int gHaveLoader;

static int has_apple_events(void) {
    long attr = 0;
    if (Gestalt(gestaltAppleEventsAttr, &attr) != noErr) {
        return 0;
    }
    return attr != 0;
}

static int has_process_mgr(void) {
    long vers = 0;
    long attr = 0;

    /* System 6's $A9F2 is classic _Launch, not LaunchApplication. */
    if (Gestalt(gestaltSystemVersion, &vers) != noErr || (vers & 0xFFFF) < 0x0700) {
        return 0;
    }
    if (Gestalt(gestaltOSAttr, &attr) != noErr) {
        return 0;
    }
    return (attr & (1L << gestaltLaunchControl)) != 0;
}

int HarnessHasProcessMgr(void) {
    return has_process_mgr();
}

static pascal OSErr handle_quit(const AppleEvent *evt, AppleEvent *reply, int32_t refcon) {
    (void)evt;
    (void)reply;
    (void)refcon;
    if (gDoneFlag) {
        *gDoneFlag = true;
    }
    return noErr;
}

void HarnessInstallQuitAE(Boolean *doneFlag) {
    gDoneFlag = doneFlag;
    if (!has_apple_events()) {
        return;
    }
    AEInstallEventHandler(
        kCoreEventClass,
        kAEQuitApplication,
        NewAEEventHandlerUPP(handle_quit),
        0,
        false);
}

void HarnessRequestQuit(void) {
    if (gDoneFlag) {
        *gDoneFlag = true;
    }
}

int HarnessHasQuitFlag(void) {
    return gDoneFlag != NULL;
}

void HarnessSetReturnHook(void (*fn)(void)) {
    gReturnHook = fn;
}

void HarnessProcessAppleEvent(EventRecord *ev) {
    if (!ev) {
        return;
    }
    if (!has_apple_events()) {
        return;
    }
    AEProcessAppleEvent(ev);
}

OSErr HarnessSaveLoaderSpec(short vRefNum, long dirID, const unsigned char *pName) {
    if (!pName || pName[0] == 0) {
        return paramErr;
    }
    gLoaderVRef = vRefNum;
    gLoaderDir = dirID;
    memcpy(gLoaderName, pName, pName[0] + 1);
    gHaveLoader = 1;
    return noErr;
}

static int name_eq_p(const unsigned char *a, const unsigned char *b) {
    int i;
    if (!a || !b || a[0] != b[0]) {
        return 0;
    }
    for (i = 1; i <= a[0]; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int bring_loader_front(void) {
    ProcessSerialNumber psn;
    ProcessInfoRec info;
    FSSpec spec;
    Str31 pname;

    if (!has_process_mgr()) {
        return 0;
    }

    psn.highLongOfPSN = 0;
    psn.lowLongOfPSN = kNoProcess;
    for (;;) {
        if (GetNextProcess(&psn) != noErr) {
            break;
        }
        memset(&info, 0, sizeof(info));
        info.processInfoLength = sizeof(info);
        info.processName = pname;
        info.processAppSpec = &spec;
        if (GetProcessInformation(&psn, &info) != noErr) {
            continue;
        }
        if (info.processSignature == 'HMac') {
            SetFrontProcess(&psn);
            return 1;
        }
        if (gHaveLoader && name_eq_p(spec.name, gLoaderName)) {
            SetFrontProcess(&psn);
            return 1;
        }
    }
    return 0;
}

/*
 * System 6 _Launch (Tech Note PS 01). A0 → this record, not the filename:
 *   +0  StringPtr  Pascal name
 *   +4  short      param (must be 0 — alt screen/sound bits)
 *   +6  'LC'       extended block follows
 *   +8  long       extBlockLen = 6
 *   +12 short      Finder fdFlags
 *   +14 long       launchFlags (0 = replace current app)
 * Passing A0 = the Pascal string itself makes Launch treat "Hell" as a
 * pointer and the next letters as param bits → "busy or damaged".
 */
static Str31 gLnName __attribute__((aligned(4)));
static unsigned char gLnBlk[20] __attribute__((aligned(4)));

static void wr_be32(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void wr_be16(unsigned char *p, unsigned short v) {
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static OSErr launch_a9f2_block(void) {
    OSErr err;

    __asm__ volatile (
        "lea %1, %%a0\n\t"
        ".short 0xA9F2\n\t"
        "move.w %%d0, %0"
        : "=d"(err)
        : "m"(gLnBlk[0])
        : "a0", "d0", "a1", "d1", "d2", "cc", "memory");
    return err;
}

static OSErr launch_sys6(short vRef, long dirID, const unsigned char *pName) {
    WDPBRec wd;
    CInfoPBRec cat;
    Str255 empty;
    unsigned long nameAddr;
    short wdRef;
    short flags;
    OSErr err;
    int i;
    int n;

    if (!pName || pName[0] == 0) {
        return paramErr;
    }
    n = pName[0] > 31 ? 31 : pName[0];
    gLnName[0] = (unsigned char)n;
    for (i = 1; i <= n; i++) {
        gLnName[i] = pName[i];
    }
    for (i = n + 1; i < 32; i++) {
        gLnName[i] = 0;
    }

    empty[0] = 0;
    FlushVol(empty, vRef);

    memset(&cat, 0, sizeof(cat));
    cat.hFileInfo.ioNamePtr = gLnName;
    cat.hFileInfo.ioVRefNum = vRef;
    cat.hFileInfo.ioDirID = dirID;
    cat.hFileInfo.ioFDirIndex = 0;
    err = PBGetCatInfoSync(&cat);
    flags = (err == noErr) ? cat.hFileInfo.ioFlFndrInfo.fdFlags : 0;

    memset(&wd, 0, sizeof(wd));
    wd.ioNamePtr = NULL;
    wd.ioVRefNum = vRef;
    wd.ioWDDirID = dirID;
    wd.ioWDProcID = 'ERIK';
    err = PBOpenWDSync(&wd);
    if (err != noErr) {
        return err;
    }
    wdRef = wd.ioVRefNum;

    err = SetVol(NULL, wdRef);
    if (err != noErr) {
        memset(&wd, 0, sizeof(wd));
        wd.ioVRefNum = wdRef;
        PBCloseWDSync(&wd);
        return err;
    }

    memset(gLnBlk, 0, sizeof(gLnBlk));
    nameAddr = (unsigned long)&gLnName[0];
    wr_be32(gLnBlk + 0, nameAddr);
    wr_be16(gLnBlk + 4, 0);
    gLnBlk[6] = 'L';
    gLnBlk[7] = 'C';
    wr_be32(gLnBlk + 8, 6);
    wr_be16(gLnBlk + 12, (unsigned short)flags);
    wr_be32(gLnBlk + 14, 0);

    err = launch_a9f2_block();
    memset(&wd, 0, sizeof(wd));
    wd.ioVRefNum = wdRef;
    PBCloseWDSync(&wd);
    return err;
}

static OSErr launch_fsspec(FSSpec *spec, ProcessSerialNumber *outPSN) {
    LaunchParamBlockRec lpb;
    OSErr err;

    memset(&lpb, 0, sizeof(lpb));
    lpb.launchBlockID = extendedBlock;
    lpb.launchEPBLength = extendedBlockLen;
    lpb.launchFileFlags = 0;
    lpb.launchControlFlags = launchContinue | launchNoFileFlags;
    lpb.launchAppSpec = spec;
    err = LaunchApplication(&lpb);
    if (err == noErr && outPSN) {
        ProcessSerialNumber self;
        Boolean same = false;

        *outPSN = lpb.launchProcessSN;
        if (GetCurrentProcess(&self) == noErr
            && SameProcess(outPSN, &self, &same) == noErr && same) {
            outPSN->highLongOfPSN = 0;
            outPSN->lowLongOfPSN = kNoProcess;
        }
    }
    return err;
}

OSErr HarnessLaunchApp(short vRefNum, long dirID, const unsigned char *pName,
                       ProcessSerialNumber *outPSN) {
    FSSpec spec;
    int n;
    int i;

    if (outPSN) {
        outPSN->highLongOfPSN = 0;
        outPSN->lowLongOfPSN = kNoProcess;
    }
    if (!pName || pName[0] == 0) {
        return paramErr;
    }
    if (!has_process_mgr()) {
        return launch_sys6(vRefNum, dirID, pName);
    }
    spec.vRefNum = vRefNum;
    spec.parID = dirID;
    n = pName[0] > 31 ? 31 : pName[0];
    spec.name[0] = (unsigned char)n;
    for (i = 1; i <= n; i++) {
        spec.name[i] = pName[i];
    }
    return launch_fsspec(&spec, outPSN);
}

static OSErr cat_parent(short vRef, long dirID, long *parent) {
    CInfoPBRec cpb;
    OSErr err;

    memset(&cpb, 0, sizeof(cpb));
    cpb.dirInfo.ioNamePtr = NULL;
    cpb.dirInfo.ioVRefNum = vRef;
    cpb.dirInfo.ioDrDirID = dirID;
    cpb.dirInfo.ioFDirIndex = -1;
    err = PBGetCatInfoSync(&cpb);
    if (err != noErr) {
        return err;
    }
    *parent = cpb.dirInfo.ioDrParID;
    return noErr;
}

static OSErr file_in_dir(short vRef, long dirID, const unsigned char *pName) {
    CInfoPBRec cpb;
    Str31 name;
    int i;

    name[0] = pName[0] > 31 ? 31 : pName[0];
    for (i = 1; i <= name[0]; i++) {
        name[i] = pName[i];
    }
    memset(&cpb, 0, sizeof(cpb));
    cpb.hFileInfo.ioNamePtr = name;
    cpb.hFileInfo.ioVRefNum = vRef;
    cpb.hFileInfo.ioDirID = dirID;
    cpb.hFileInfo.ioFDirIndex = 0;
    return PBGetCatInfoSync(&cpb);
}

void HarnessReturnToLoader(void) {
    FCBPBRec fcb;
    long parent;
    Size grow;
    static const unsigned char kLoader[] = "\pHelloMacintosh";

    if (gReturnHook) {
        void (*hook)(void) = gReturnHook;
        gReturnHook = NULL;
        hook();
    }
    /* Compact so _Launch has heap/stack room in this partition. */
    MaxMem(&grow);

    if (bring_loader_front()) {
        return;
    }
    if (gHaveLoader) {
        HarnessLaunchApp(gLoaderVRef, gLoaderDir, gLoaderName, NULL);
        return;
    }

    memset(&fcb, 0, sizeof(fcb));
    fcb.ioNamePtr = NULL;
    fcb.ioVRefNum = 0;
    fcb.ioRefNum = CurResFile();
    fcb.ioFCBIndx = 0;
    if (PBGetFCBInfoSync((FCBPBPtr)&fcb) != noErr) {
        return;
    }
    /* Same folder as this app (Harness/), then the parent (volume root). */
    if (file_in_dir(fcb.ioFCBVRefNum, fcb.ioFCBParID, kLoader) == noErr) {
        HarnessLaunchApp(fcb.ioFCBVRefNum, fcb.ioFCBParID, kLoader, NULL);
        return;
    }
    if (cat_parent(fcb.ioFCBVRefNum, fcb.ioFCBParID, &parent) == noErr
        && file_in_dir(fcb.ioFCBVRefNum, parent, kLoader) == noErr) {
        HarnessLaunchApp(fcb.ioFCBVRefNum, parent, kLoader, NULL);
    }
}

static int meta_is_stop(const char *meta, unsigned long len) {
    unsigned long i;
    if (!meta || len < 10) {
        return 0;
    }
    for (i = 0; i + 10 <= len; i++) {
        if ((i == 0 || meta[i - 1] == '\n')
            && strncmp(meta + i, "METHOD STOP", 11) == 0) {
            return 1;
        }
    }
    return 0;
}

static unsigned long rd_u32_le(const unsigned char *p) {
    return (unsigned long)p[0]
        | ((unsigned long)p[1] << 8)
        | ((unsigned long)p[2] << 16)
        | ((unsigned long)p[3] << 24);
}

static void wr_u32_le(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void poll_send_hdr(unsigned char type, unsigned long id, unsigned long meta_len) {
    unsigned char buf[20];
    buf[0] = 'R';
    buf[1] = 'H';
    buf[2] = 'T';
    buf[3] = 'P';
    buf[4] = 1;
    buf[5] = type;
    buf[6] = 0;
    buf[7] = 0;
    wr_u32_le(buf + 8, id);
    wr_u32_le(buf + 12, meta_len);
    wr_u32_le(buf + 16, 0);
    SerModemWrite(buf, 20);
}

int HarnessPollStop(void) {
    unsigned char hdr[20];
    unsigned char meta[256];
    long got = 0;
    unsigned long header_len;
    unsigned long body_len;
    OSErr err;

    if (!SerModemIsOpen()) {
        return 0;
    }
    if (SerModemAvail() < 20) {
        return 0;
    }

    /* Resync cheaply: only attempt if first available bytes can be read. */
    err = SerModemReadSome(hdr, 20, &got);
    if (err != noErr || got <= 0) {
        return 0;
    }
    if (got < 20 || hdr[0] != 'R' || hdr[1] != 'H' || hdr[2] != 'T' || hdr[3] != 'P') {
        /* Drop junk byte by keeping 19 in a local discard; caller retries. */
        return 0;
    }
    if (hdr[5] != RHTTP_TYPE_REQ && hdr[5] != RHTTP_TYPE_PING) {
        return 0;
    }
    if (hdr[5] == RHTTP_TYPE_PING) {
        unsigned char pong[20];
        memcpy(pong, hdr, 20);
        pong[5] = RHTTP_TYPE_PONG;
        SerModemWrite(pong, 20);
        return 0;
    }

    header_len = rd_u32_le(hdr + 12);
    body_len = rd_u32_le(hdr + 16);
    if (header_len > sizeof(meta) || body_len > 4096ul) {
        return 0;
    }
    if (header_len > 0) {
        if (SerModemReadExact(meta, (long)header_len, 60) != noErr) {
            return 0;
        }
    }
    while (body_len > 0) {
        unsigned char sink[64];
        long take = (long)body_len;
        if (take > 64) {
            take = 64;
        }
        if (SerModemReadExact(sink, take, 60) != noErr) {
            break;
        }
        body_len -= (unsigned long)take;
    }
    if (meta_is_stop((char *)meta, header_len)) {
        static const char res[] = "STATUS 200\nSTATE stopping\n";
        poll_send_hdr(RHTTP_TYPE_RES, rd_u32_le(hdr + 8),
                      (unsigned long)(sizeof(res) - 1));
        SerModemWrite(res, (long)(sizeof(res) - 1));
        return 1;
    }
    return 0;
}
