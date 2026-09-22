/*
 * Automatic hellomacintosh stop / ping for guest apps. Linked only into guests
 * (add_harness_guest). HelloMacintosh keeps its own serial loop.
 */

#include "harness.h"
#include "harness_guest.h"
#include "ser_modem.h"

#include <Events.h>
#include <OSUtils.h>
#include <Traps.h>

static ProcPtr gOldGNE;
static ProcPtr gOldWNE;
static int gPatched;
static int gInside;
static int gStopping;
static int gOpenedSerial;
static int (*gPoll)(void);

static int guest_poll(void) {
    if (gPoll) {
        return gPoll();
    }
    if (!SerModemIsOpen()) {
        if (SerModemOpen() != noErr) {
            return 0;
        }
        gOpenedSerial = 1;
    }
    return HarnessPollStop();
}

static void guest_unpatch(void) {
    if (!gPatched) {
        return;
    }
    if (gOldGNE) {
        SetToolTrapAddress(gOldGNE, _GetNextEvent);
    }
    if (gOldWNE) {
        SetToolTrapAddress(gOldWNE, _WaitNextEvent);
    }
    gPatched = 0;
}

static void guest_shutdown(void) {
    guest_unpatch();
    if (gOpenedSerial && SerModemIsOpen()) {
        SerModemClose();
        gOpenedSerial = 0;
    }
}

/*
 * Never _Launch HelloMacintosh from inside the WaitNextEvent patch: System 6
 * Launch needs a shallow stack, and the serial driver still owns a buffer
 * in this heap. Unwind to main via the quit flag instead.
 */
static void guest_stop(void) {
    if (gStopping) {
        return;
    }
    gStopping = 1;
    guest_shutdown();
    if (HarnessHasQuitFlag()) {
        HarnessRequestQuit();
        return;
    }
    HarnessReturnToLoader();
    ExitToShell();
}

static void guest_tick(void) {
    if (gInside || gStopping) {
        return;
    }
    gInside = 1;
    if (guest_poll()) {
        guest_stop();
    }
    gInside = 0;
}

static pascal Boolean patched_gne(INTEGER em, EventRecord *evt) {
    Boolean got;

    guest_tick();
    SetToolTrapAddress(gOldGNE, _GetNextEvent);
    got = GetNextEvent(em, evt);
    if (!gStopping) {
        SetToolTrapAddress((ProcPtr)patched_gne, _GetNextEvent);
        guest_tick();
    }
    return got;
}

static pascal Boolean patched_wne(INTEGER mask, EventRecord *ev, LONGINT sleep,
                                  RgnHandle mouse) {
    Boolean got;

    guest_tick();
    SetToolTrapAddress(gOldWNE, _WaitNextEvent);
    gInside = 1;
    got = WaitNextEvent(mask, ev, sleep, mouse);
    gInside = 0;
    if (!gStopping) {
        SetToolTrapAddress((ProcPtr)patched_wne, _WaitNextEvent);
        guest_tick();
    }
    return got;
}

void HarnessGuestSetPoll(int (*poll)(void)) {
    gPoll = poll;
}

void HarnessGuestInit(void) {
    if (gPatched) {
        return;
    }
    gOldGNE = GetToolTrapAddress(_GetNextEvent);
    gOldWNE = GetToolTrapAddress(_WaitNextEvent);
    if (gOldGNE) {
        SetToolTrapAddress((ProcPtr)patched_gne, _GetNextEvent);
    }
    if (gOldWNE) {
        SetToolTrapAddress((ProcPtr)patched_wne, _WaitNextEvent);
    }
    gPatched = 1;
    HarnessSetReturnHook(guest_shutdown);
}

static void guest_ctor(void) __attribute__((constructor));
static void guest_ctor(void) {
    HarnessGuestInit();
}

static void guest_dtor(void) __attribute__((destructor));
static void guest_dtor(void) {
    guest_shutdown();
}
