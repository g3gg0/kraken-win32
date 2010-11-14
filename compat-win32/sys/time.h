#ifndef __SYS_TIME_H_
#define __SYS_TIME_H_

#include <time.h>
#include <sys/timeb.h>
#include <windows.h>
 
struct timezone 
{
	int  tz_minuteswest;
	int  tz_dsttime;
};

/* implementing gettimeofday as a static function to include it in every module. 
   so its possible to keep it in the headers only. */
static int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    struct _timeb current_time;
    
    _ftime_s(&current_time);
	tv->tv_sec = (long)current_time.time;
	tv->tv_usec = current_time.millitm * 1000;
    
    return 0;
}

#endif
