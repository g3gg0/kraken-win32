#include "A5AtiStubs.h"
#include <stdio.h>

#ifdef WIN32
#include <compat-win32.h>
#define DL_OPEN(n) LoadLibraryA(n)
#define DL_CLOSE(h) FreeLibrary((HMODULE)h)
#define DL_SYM(h,n)  GetProcAddress((HMODULE)h,n)
#define DL_EXT ".dll"
#else
#include <dlfcn.h>
#define DL_OPEN(x)   dlopen (x,RTLD_LAZY | RTLD_GLOBAL)
#define DL_CLOSE     dlclose
#define DL_SYM       dlsym
#define DL_EXT ".so"
#endif

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

    LoadDllSym(lHandle, "A5AtiPipelineInfo", (void**)&fPipelineInfo);
    LoadDllSym(lHandle, "A5AtiInit", (void**)&fInit);
    LoadDllSym(lHandle, "A5AtiSubmit", (void**)&fSubmit);
    LoadDllSym(lHandle, "A5AtiSubmitPartial", (void**)&fSubmitPartial);
    LoadDllSym(lHandle, "A5AtiPopResult", (void**)&fPopResult);
    LoadDllSym(lHandle, "A5AtiIsIdle", (void**)&fIsIdle);
	LoadDllSym(lHandle, "A5AtiClear", (void**)&fClear);
    LoadDllSym(lHandle, "A5AtiShutdown", (void**)&fShutdown);

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
