#ifndef __GLOBALS_H__
#define __GLOBALS_H__

/* random token for SVN version push: asdfa4rafra4t */

//#define MEMDEBUG


#ifdef WIN32
#include <compat-win32.h>
#define DL_OPEN(n)     LoadLibraryA(n)
#define DL_CLOSE(h)    FreeLibrary((HMODULE)h)
#define DL_SYM(h,n)    GetProcAddress((HMODULE)h,n)
#define DL_EXT         ".dll"
#define DIR_SEP        '\\'
#define KRAKEN_VERSION "Kraken-win32 ($Revision$, g3gg0.de, win32)"

typedef CRITICAL_SECTION t_mutex;

/* assuming the inital value of the semaphore is 1 */
#define mutex_init(mutex)               InitializeCriticalSection(mutex)
#define mutex_destroy(mutex)            DeleteCriticalSection(mutex)
#define mutex_unlock(mutex)             LeaveCriticalSection(mutex)
#define mutex_lock(mutex)               EnterCriticalSection(mutex)

#else
#include <dlfcn.h>
#include <pthread.h>

#define DL_OPEN(x)     dlopen (x,RTLD_LAZY | RTLD_GLOBAL)
#define DL_CLOSE       dlclose
#define DL_SYM         dlsym
#define DL_EXT         ".so"
#define DIR_SEP        '/'
#define KRAKEN_VERSION "Kraken-win32 ($Revision$, g3gg0.de, linux)"

typedef pthread_mutex_t t_mutex;

#define mutex_init(mutex)               do {pthread_mutexattr_t mutexattr; pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE_NP); pthread_mutex_init(mutex, &mutexattr); } while (0)
#define mutex_destroy(mutex)            pthread_mutex_destroy(mutex)
#define mutex_unlock(mutex)             pthread_mutex_unlock(mutex)
#define mutex_lock(mutex)               pthread_mutex_lock(mutex)

#endif


#ifdef MEMDEBUG
#include <crtdbg.h>
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new        new( _NORMAL_BLOCK, __FILE__, __LINE__)
#define malloc(s)  _malloc_dbg(s, _NORMAL_BLOCK, __FILE__, __LINE__)
#define MEMCHECK() if(!_CrtCheckMemory()){printf("Failed in %s:%i",__FUNCTION__,__LINE__);}
#else
#define MEMCHECK() if(false){}
#endif


#endif // __GLOBALS_H__
