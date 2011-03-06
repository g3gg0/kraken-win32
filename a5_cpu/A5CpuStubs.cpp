
#include "A5CpuStubs.h"

#include <stdio.h>

#include "Globals.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <list>
using namespace std;

static bool isDllLoaded = false;
static bool isDllError  = false;


static bool (*fInit)(int max_rounds, int condition, int threads) = NULL;
static int  (*fSubmit)(uint64_t start_value, int32_t start_round, uint32_t advance, void* context) = NULL;
static int  (*fKeySearch)(uint64_t start_value, uint64_t target, int32_t start_round, int32_t stop_round,
                   uint32_t advance, void* context) = NULL;
static bool (*fPopResult)(uint64_t& start_value, uint64_t& stop_value,
                   int32_t& start_round, void** context) = NULL;
static void (*fShutdown)(void) = NULL;
static bool (*fIsIdle)(void) = NULL;
static void (*fClear)(void) = NULL;
static void (*fCancel)(list<void*> ctxList) = NULL;
static void (*fSpinLock)(bool state) = NULL;

static bool LoadDllSym(void* handle, const char* name, void** func)
{
    *func = DL_SYM(handle, name);
#ifndef WIN32
    char* lError = dlerror();
    if (lError) {
        fprintf(stderr, "Error when loading symbol (%s): %s\n", name, lError);
        isDllError = true;
        return false;
    }
#else
    if (*func == NULL) {
        fprintf(stderr, "Error when loading symbol (%s): 0x%08X\n", name, GetLastError());
        isDllError = true;
        return false;
    }
#endif
    return true;
}

static void LoadDLL(void)
{
  if (isDllError) return;

  void* lHandle = DL_OPEN("./A5Cpu"DL_EXT);

#ifndef WIN32
    char* lError = dlerror();
    if (lError) {
        fprintf(stderr, "Error when opening A5Cpu"DL_EXT": %s\n", lError);
        return;
    }
#else
    if (lHandle == NULL) {
        fprintf(stderr, "Error when opening A5Cpu"DL_EXT": 0x%08X\n", GetLastError());
        return;
    }
#endif

  LoadDllSym(lHandle, "A5Init", (void**)&fInit);
  LoadDllSym(lHandle, "A5Submit", (void**)&fSubmit);
  LoadDllSym(lHandle, "A5KeySearch", (void**)&fKeySearch);
  LoadDllSym(lHandle, "A5PopResult", (void**)&fPopResult);
  LoadDllSym(lHandle, "A5IsIdle", (void**)&fIsIdle);
  LoadDllSym(lHandle, "A5Clear", (void**)&fClear);
  LoadDllSym(lHandle, "A5Cancel", (void**)&fCancel);
  LoadDllSym(lHandle, "A5SpinLock", (void**)&fSpinLock);
  LoadDllSym(lHandle, "A5Shutdown", (void**)&fShutdown);

  isDllLoaded = !isDllError;

}


bool A5CpuInit(int max_rounds, int condition, int threads)
{
  LoadDLL();
  if (isDllLoaded) {
    return fInit(max_rounds, condition, threads);
  } else {
    return false;
  }
}
  
int  A5CpuSubmit(uint64_t start_value, int32_t start_round, uint32_t advance, void* context)
{
  if (isDllLoaded) {
      return fSubmit(start_value, start_round, advance, context);
  } else {
    return -1;
  }
}

int  A5CpuKeySearch(uint64_t start_value, uint64_t target, int32_t start_round,
                    int32_t stop_round, uint32_t advance, void* context)
{
  if (isDllLoaded) {
      return fKeySearch(start_value, target, start_round, stop_round, advance, context);
  } else {
      return -1;
  }
}
  
bool A5CpuPopResult(uint64_t& start_value, uint64_t& stop_value,
                    int32_t& start_round, void** context)
{
  if (isDllLoaded) {
      return fPopResult(start_value, stop_value, start_round, context);
  } else {
    return false;
  }
}
  
bool A5CpuIsIdle()
{
    if (isDllLoaded) {
        return fIsIdle();
    } else {
        return false;
    }
}

void A5CpuClear()
{
  if (isDllLoaded) {
      fClear();
  }  
}

void A5CpuCancel(list<void*> ctxList)
{
  if (isDllLoaded) {
      fCancel(ctxList);
  }  
}

void A5CpuSpinLock(bool state)
{
  if (isDllLoaded) {
      fSpinLock(state);
  }  
}

void A5CpuShutdown()
{
  if (isDllLoaded) {
     fShutdown();
  } 
}

