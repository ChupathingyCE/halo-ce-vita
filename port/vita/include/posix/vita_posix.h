/*
VITA_POSIX.H

POSIX the platform layer uses that VitaSDK's newlib leaves out, declared for
every platform unit of the Vita build (port/vita/host/vita_posix.c defines
them, with the SDK's ABI: every parameter a 32-bit scalar or a pointer).
*/

#ifndef __VITA_POSIX_H
#define __VITA_POSIX_H

#include <time.h>

#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif

#ifndef UTIME_OMIT
#define UTIME_NOW -2L
#define UTIME_OMIT -1L
#endif

int clock_nanosleep(clockid_t clock, int flags, const struct timespec *request, struct timespec *remain);

#endif
