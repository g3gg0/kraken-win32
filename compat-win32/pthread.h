#ifndef _PTHREAD_H
#define _PTHREAD_H

#include "windows.h"

typedef DWORD pthread_t;

#define pthread_create(handle,attr,routine,arg) CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)routine,arg,0,handle)
#define pthread_join(handle) WaitForSingleObject((HANDLE)handle,INFINITE)

#endif
