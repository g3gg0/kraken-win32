#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include "windows.h"

typedef CRITICAL_SECTION t_mutex;

/* assuming the inital value of the semaphore is 1 */
#define mutex_init(sem)               InitializeCriticalSection(sem)
#define mutex_destroy(sem)            DeleteCriticalSection(sem)
#define mutex_unlock(sem)             LeaveCriticalSection(sem)
#define mutex_lock(sem)               EnterCriticalSection(sem)

#endif
