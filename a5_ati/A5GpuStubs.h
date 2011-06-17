#ifndef A5_GPU_STUBS
#define A5_GPU_STUBS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <list>
using namespace std;

bool A5GpuInit(int max_rounds, int condition, unsigned int gpu_mask, int pipemul);
int A5GpuKeySearch(uint64_t job_id, uint64_t start_value, uint64_t target, int32_t start_round, int32_t stop_round, uint32_t advance, void* context);
int A5GpuSubmit(uint64_t job_id, uint64_t start_value, uint32_t start_round, uint32_t advance, void* context);
bool A5GpuIsIdle();
void A5GpuClear();
void A5GpuCancel(uint64_t job_id);
void A5GpuSpinLock(bool state);
bool A5GpuPopResult(uint64_t& job_id, uint64_t& start_value, uint64_t& stop_value, void** context);
void A5GpuShutdown();
char *A5GpuGetDeviceStats();
bool A5GpuPipelineInfo(int &length);
int  A5GpuSubmitPartial(uint64_t job_id, uint64_t start_value, uint32_t end_round, uint32_t advance, void* context);



#endif
