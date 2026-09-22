/*
 * Minimal RHTTP smoke test for the Mac — GET example.com via NSE.
 */
#include "../common/rhttp.h"
#include "../common/strutil.h"

#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    WindowPtr w;
    Rect bounds;
    OSErr err;
    short status = 0;
    Handle body = NULL;
    char msg[256];

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitDialogs(NULL);
    InitCursor();

    SetRect(&bounds, 40, 40, 420, 180);
    w = NewWindow(NULL, &bounds, "\pRHTTP Test", true, documentProc, (WindowPtr)-1, true, 0);
    SetPort(w);

    err = RHTTPOpen();
    if (err != noErr) {
        sprintf(msg, "RHTTPOpen failed: %d", (int)err);
    } else {
        err = RHTTPRequest("GET", "https://example.com/", NULL, NULL, 0, &status, &body);
        if (err != noErr) {
            sprintf(msg, "RHTTPRequest failed: %d", (int)err);
        } else {
            sprintf(msg, "OK status=%d body=%ld bytes", (int)status,
                    body ? GetHandleSize(body) : 0);
        }
        RHTTPClose();
    }

    {
        EventRecord ev;
        Boolean done = false;
        while (!done) {
            if (WaitNextEvent(everyEvent, &ev, 60, NULL)) {
                if (ev.what == updateEvt) {
                    BeginUpdate(w);
                    EraseRect(&w->portRect);
                    MoveTo(10, 40);
                    {
                        Str255 p;
                        c_to_pstr(p, msg);
                        DrawString(p);
                    }
                    EndUpdate(w);
                } else if (ev.what == mouseDown) {
                    done = true;
                }
            }
        }
    }

    if (body) {
        DisposeHandle(body);
    }
    DisposeWindow(w);
    return 0;
}
