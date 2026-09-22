#ifndef MAC_HARNESS_H
#define MAC_HARNESS_H

#include <Files.h>
#include <MacTypes.h>
#include <Processes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Install System 7 Quit Apple Event → *doneFlag = true. Safe no-op on System 6. */
void HarnessInstallQuitAE(Boolean *doneFlag);

/* Set *doneFlag from HarnessInstallQuitAE. No-op if that was never called. */
void HarnessRequestQuit(void);

/* 1 after HarnessInstallQuitAE; guests use this to unwind to main on STOP. */
int HarnessHasQuitFlag(void);

/*
 * Guest runtime registers a hook so serial is closed and event traps are
 * restored before System 6 _Launch. Not used by the HelloMacintosh loader.
 */
void HarnessSetReturnHook(void (*fn)(void));

/* Process a kHighLevelEvent (call from the app event loop). */
void HarnessProcessAppleEvent(EventRecord *ev);

/*
 * After a STOP (or when the guest is done): bring HelloMacintosh forward if it
 * is still running, otherwise launch HelloMacintosh from the volume (parent of
 * the Harness folder, or this app's folder). On System 6 this uses _Launch
 * and does not return on success — dispose windows first.
 */
void HarnessReturnToLoader(void);

/* Persist this app's location so guests can find HelloMacintosh again. */
OSErr HarnessSaveLoaderSpec(short vRefNum, long dirID, const unsigned char *pName);

/*
 * Launch an application in dirID on vRefNum.
 * System 7 / MultiFinder: LaunchApplication (caller stays resident).
 * System 6: classic _Launch (does not return on success).
 * outPSN (optional) receives the new process on System 7.
 */
OSErr HarnessLaunchApp(short vRefNum, long dirID, const unsigned char *pName,
                       ProcessSerialNumber *outPSN);

/* 1 if LaunchApplication exists (System 7 / Process Manager). */
int HarnessHasProcessMgr(void);

/* Non-blocking serial poll for a host STOP command. Uses SerModem*.
 * Guest apps normally get this from harness_guest.c (event-trap patch). */
int HarnessPollStop(void);

#ifdef __cplusplus
}
#endif

#endif
