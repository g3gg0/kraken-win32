#ifndef A5_CPU_STUBS
#define A5_CPU_STUBS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <list>
using namespace std;

bool A5CpuInit(int max_rounds, int condition, int threads);
int A5CpuKeySearch(uint64_t job_id, uint64_t start_value, uint64_t target, int32_t start_round, int32_t stop_round, uint32_t advance, void* context);
int A5CpuSubmit(uint64_t job_id, uint64_t start_value, int32_t start_round, uint32_t advance, void* context);
bool A5CpuIsIdle();
void A5CpuClear();
void A5CpuCancel(uint64_t job_id);
void A5CpuSpinLock(bool state);
bool A5CpuPopResult(uint64_t& job_id, uint64_t& start_value, uint64_t& stop_value, int32_t& start_round, void** context);
void A5CpuShutdown();
char *A5CpuGetDeviceStats();
bool A5CpuPipelineInfo(int &length);

#endif
