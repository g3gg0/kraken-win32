#ifndef _PTHREAD_H
#define _PTHREAD_H

#include "windows.h"

typedef HANDLE pthread_t;

#define pthread_create(handle,attr,routine,arg) *handle = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)routine,arg,0,NULL)
#define pthread_join(handle,value) WaitForSingleObject((HANDLE)handle,INFINITE)

#endif
