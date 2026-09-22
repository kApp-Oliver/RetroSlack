#ifndef MAC_RHTTP_DEBUG_H
#define MAC_RHTTP_DEBUG_H

#ifdef RHTTP_DEBUG
void rhttp_dprintf(const char *fmt, ...);
#else
#define rhttp_dprintf(...) ((void)0)
#endif

#endif
