#ifndef MAC_SER_MODEM_H
#define MAC_SER_MODEM_H

#include <MacTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*SerModemYieldProc)(void);

OSErr SerModemOpen(void);
OSErr SerModemClose(void);
int SerModemIsOpen(void);

void SerModemSetYield(SerModemYieldProc proc);

OSErr SerModemWrite(const void *data, long len);
OSErr SerModemReadSome(void *data, long len, long *got);
OSErr SerModemReadExact(void *data, long need, long timeout_ticks);
long SerModemAvail(void);

#ifdef __cplusplus
}
#endif

#endif
