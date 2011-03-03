#ifndef __GLOBALS_H__
#define __GLOBALS_H__

/* random token for SVN version push: d4tarfa45tq */

//#define MEMDEBUG


#ifdef WIN32
#include <compat-win32.h>
#define DL_OPEN(n)     LoadLibraryA(n)
#define DL_CLOSE(h)    FreeLibrary((HMODULE)h)
#define DL_SYM(h,n)    GetProcAddress((HMODULE)h,n)
#define DL_EXT         ".dll"
#define KRAKEN_VERSION "Kraken-win32 ($Revision$, g3gg0.de, win32)"
#else
#include <dlfcn.h>
#define DL_OPEN(x)     dlopen (x,RTLD_LAZY | RTLD_GLOBAL)
#define DL_CLOSE       dlclose
#define DL_SYM         dlsym
#define DL_EXT         ".so"
#define KRAKEN_VERSION ""
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
