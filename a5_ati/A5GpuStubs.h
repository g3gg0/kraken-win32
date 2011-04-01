#ifndef A5_ATI_STUBS
#define A5_ATI_STUBS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <list>
using namespace std;

bool A5GpuPipelineInfo(int &length);
bool A5GpuInit(int max_rounds, int condition, unsigned int gpu_mask, int pipeline_mul);



int  A5GpuSubmit(uint64_t job_id, uint64_t start_value, uint32_t start_round, uint32_t advance, void* context);
int  A5GpuSubmitPartial(uint64_t job_id, uint64_t start_value, uint32_t stop_round, uint32_t advance, void* context);
bool A5GpuPopResult(uint64_t& job_id, uint64_t& start_value, uint64_t& end_value, void** context);  
int  A5GpuKeySearch(uint64_t job_id, uint64_t start_value, uint64_t target, int32_t start_round, int32_t stop_round, uint32_t advance, void* context);

bool A5GpuIsIdle();
void A5GpuClear();
void A5GpuCancel(uint64_t job_id);
void A5GpuSpinLock(bool state);
void A5GpuShutdown();

void ApplyIndexFunc(uint64_t& start_index, int bits);
int ExtractIndex(uint64_t& start_value, int bits);

#endif
