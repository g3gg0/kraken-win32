
#include "A5AtiStubs.h"

#include <stdio.h>
#include "Globals.h"



#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <list>
using namespace std;



static bool isDllLoaded = false;
static bool isDllError  = false;

static bool (*fPipelineInfo)(int &length) = NULL;
static bool (*fInit)(int max, int cond, unsigned int mask, int mult) = NULL;
static int  (*fSubmit)(uint64_t value, unsigned int start,
                uint32_t id, void* ctx) = NULL;
static int  (*fSubmitPartial)(uint64_t value, unsigned int stop,
                       uint32_t id, void* ctx ) = NULL;
static bool (*fPopResult)(uint64_t& start, uint64_t& stop, void** ctx) = NULL;
static bool (*fIsIdle)(void) = NULL;
static void (*fClear)(void) = NULL;
static void (*fCancel)(list<void*> ctxList) = NULL;
static void (*fSpinLock)(bool state) = NULL;
static void (*fShutdown)(void) = NULL;

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

    void* lHandle = DL_OPEN("./A5Ati"DL_EXT);

#ifndef WIN32
    char* lError = dlerror();
    if (lError) {
        fprintf(stderr, "Error when opening A5Ati"DL_EXT": %s\n", lError);
        return;
    }
#else
	if (lHandle == NULL) {
		if(GetLastError() != ERROR_MOD_NOT_FOUND) {
			fprintf(stderr, "Will not use A5Ati"DL_EXT". Error Code: 0x%08X\n", GetLastError());
		}
		else {
			fprintf(stderr, "No A5Ati"DL_EXT" found. Will not use an ATI graphics card.\n");
		}
    return;
    }
#endif

    LoadDllSym(lHandle, "A5PipelineInfo", (void**)&fPipelineInfo);
    LoadDllSym(lHandle, "A5Init", (void**)&fInit);
    LoadDllSym(lHandle, "A5Submit", (void**)&fSubmit);
    LoadDllSym(lHandle, "A5SubmitPartial", (void**)&fSubmitPartial);
    LoadDllSym(lHandle, "A5PopResult", (void**)&fPopResult);
    LoadDllSym(lHandle, "A5IsIdle", (void**)&fIsIdle);
	LoadDllSym(lHandle, "A5Clear", (void**)&fClear);
	LoadDllSym(lHandle, "A5Cancel", (void**)&fCancel);
	LoadDllSym(lHandle, "A5SpinLock", (void**)&fSpinLock);
    LoadDllSym(lHandle, "A5Shutdown", (void**)&fShutdown);

    isDllLoaded = !isDllError;
}


bool A5AtiInit(int max_rounds, int condition, unsigned int gpu_mask,
                 int pipemul)
{
    LoadDLL();
    if (isDllLoaded) {
        return fInit(max_rounds, condition, gpu_mask, pipemul);
    } else {
        return false;
    }
}

bool A5AtiPipelineInfo(int &length)
{
    if (isDllLoaded) {
        return fPipelineInfo(length);
    } else {
        return false;
    }
}
  
int A5AtiSubmit(uint64_t start_value, unsigned int start_round,
                  uint32_t advance, void* context)
{
    if (isDllLoaded) {
        return fSubmit(start_value, start_round, advance, context);
    } else {
        return -1;
    }
}

int  A5AtiSubmitPartial(uint64_t start_value, unsigned int stop_round,
                          uint32_t advance, void* context)
{
    if (isDllLoaded) {
        return fSubmitPartial(start_value, stop_round, advance, context);
    } else {
        return -1;
    }
}
  
bool A5AtiIsIdle()
{
    if (isDllLoaded) {
        return fIsIdle();
    } else {
        return false;
    }
}

void A5AtiClear()
{
	if (isDllLoaded) {
		fClear();
	}  
}

void A5AtiCancel(list<void*> ctxList)
{
	if (isDllLoaded) {
		fCancel(ctxList);
	}  
}

void A5AtiSpinLock(bool state)
{
  if (isDllLoaded) {
      fSpinLock(state);
  }  
}

bool A5AtiPopResult(uint64_t& start_value, uint64_t& stop_value,
                      void** context)
{
    if (isDllLoaded) {
        return fPopResult(start_value, stop_value, context);
    } else {
        return false;
    }
}
  
void A5AtiShutdown()
{
    if (isDllLoaded) {
        fShutdown();
    } 
}
