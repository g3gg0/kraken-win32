#ifndef __GLOBALS_H__
#define __GLOBALS_H__

/* random token for SVN version push: fg45t45gaer4 */

//#define MEMDEBUG

/* max length for messages */
#define MAX_MSG_LENGTH 2048

/* macros to string-ify compiler defines etc */
#define STRINGIZE_WRAP(z) #z
#define STRINGIZE(z) STRINGIZE_WRAP(z)

/* generic architecture detection */
#ifdef _WIN64
#define COMPILER_ARCH "x64"
#else
#define COMPILER_ARCH "x32"
#endif


/* check for MSVC compiler version */
#ifdef _MSC_VER
#define COMPILER_VERSION "Microsoft Visual C++ v" STRINGIZE(_MSC_VER) " (" COMPILER_ARCH ")"
#endif

/* check for intel compiler version, this overrides MSVC macro intentionally! */
#ifdef __INTEL_COMPILER
#define COMPILER_VERSION "Intel Compiler v" STRINGIZE(__INTEL_COMPILER) " (" COMPILER_ARCH ")"
#endif

/* check for GCC compiler version */
#ifdef __GNUC__
#if (defined __GNUC_MINOR__) && (defined __GNUC_PATCHLEVEL__) && (defined __VERSION__)
#define COMPILER_VERSION "GNU GCC v" STRINGIZE(__GNUC__) "." STRINGIZE(__GNUC_MINOR__) "." STRINGIZE(__GNUC_PATCHLEVEL__) " (" COMPILER_ARCH ")  '" __VERSION__ "'"
#else
#define COMPILER_VERSION "GNU GCC v" STRINGIZE(__GNUC__) ".?.? (" COMPILER_ARCH ")"
#endif
#endif

/* none detected */
#ifndef COMPILER_VERSION
#define COMPILER_VERSION "unknown (" COMPILER_ARCH ")"
#endif


#ifdef WIN32
#include <compat-win32.h>
#define DL_OPEN(n)     LoadLibraryA(n)
#define DL_CLOSE(h)    FreeLibrary((HMODULE)h)
#define DL_SYM(h,n)    GetProcAddress((HMODULE)h,n)
#define DL_EXT         ".dll"
#define DIR_SEP        '\\'
#define KRAKEN_VERSION "Kraken-win32 ($Revision$, g3gg0.de, win32)"


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



#endif // __GLOBALS_H__
