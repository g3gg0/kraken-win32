#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include "windows.h"

typedef CRITICAL_SECTION sem_t;

/* assuming the inital value of the semaphore is 1 */
#define sem_init(sem,pshared,value) InitializeCriticalSection(sem)
#define sem_destroy(sem) DeleteCriticalSection(sem)
#define sem_post(sem) LeaveCriticalSection(sem)
#define sem_wait(sem) EnterCriticalSection(sem)

#endif
