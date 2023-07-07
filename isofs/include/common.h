#ifndef _ISOFS_COMMON_H
#define _ISOFS_COMMON_H

#define MAJOR_VER       0x00
#define MINOR_VER       0x05

#define VERBOSE_LEVEL   0
#define DBG_printf(__level, __args...) if(__level <= VERBOSE_LEVEL) { printf(__args); }

#endif // _ISOFS_COMMON_H
