
#ifndef __MEMDEBUG_H__
#define __MEMDEBUG_H__

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

#endif