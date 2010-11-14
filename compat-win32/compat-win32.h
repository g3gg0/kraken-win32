
#ifndef __MISC_H_
#define __MISC_H_

#include <windows.h>

#define usleep(x) Sleep((x>1000)?(x/1000):1)
#define strdup(x) _strdup(x)
#define snprintf  _snprintf
#define lseek64   _lseeki64

#endif