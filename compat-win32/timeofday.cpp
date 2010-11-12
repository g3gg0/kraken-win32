
#include <sys/time.h>
#include <sys/timeb.h>
 
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    struct _timeb current_time;
    
    _ftime(&current_time);
	tv->tv_sec = current_time.time;
	tv->tv_usec = current_time.millitm * 1000;
    
    return 0;
}

