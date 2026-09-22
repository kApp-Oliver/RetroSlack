#ifndef MAC_RHTTP_H
#define MAC_RHTTP_H

/* Retro68 / classic Mac */
#include <MacTypes.h>
#include <Errors.h>

#ifdef __cplusplus
extern "C" {
#endif

OSErr RHTTPOpen(void);
OSErr RHTTPClose(void);

/* Optional yield while blocked on serial I/O (keep UI alive). */
typedef void (*RHTTPYieldProc)(void);
void RHTTPSetYieldProc(RHTTPYieldProc proc);

/* Perform HTTP request via serial NSE. outBody is a Handle the caller must DisposeHandle. */
OSErr RHTTPRequest(
    const char *method,
    const char *url,
    const char *extraHeaders,
    const void *body,
    long bodyLen,
    short *outStatus,
    Handle *outBody);

/* Non-blocking: 1 if the host sent METHOD STOP (harness CLI). */
int RHTTPPollStop(void);

#ifdef __cplusplus
}
#endif

#endif
