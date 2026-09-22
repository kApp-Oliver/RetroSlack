#ifndef MAC_HARNESS_GUEST_H
#define MAC_HARNESS_GUEST_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Guest runtime linked into every HelloMacintosh guest (see add_harness_guest
 * in mac/CMakeLists.txt). A constructor patches GetNextEvent / WaitNextEvent
 * so the app answers hellomacintosh ping / stop without a poll in its own loop.
 *
 * The HelloMacintosh loader itself must not link this file.
 *
 * Apps that open the modem via RHTTPOpen should call HarnessGuestSetPoll
 * afterwards so STOP is read from that port instead of a second OpenDriver.
 */
void HarnessGuestInit(void);
void HarnessGuestSetPoll(int (*poll)(void));

#ifdef __cplusplus
}
#endif

#endif
