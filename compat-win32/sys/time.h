#ifndef __SYS_TIME_H_
#define __SYS_TIME_H_

#include <time.h>
#include <windows.h>
 
struct timezone 
{
	int  tz_minuteswest;
	int  tz_dsttime;
};
 
int gettimeofday(struct timeval *tv, struct timezone *tz);

#endif
