#ifndef A5_ATI_STUBS
#define A5_ATI_STUBS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <list>
using namespace std;

bool A5AtiPipelineInfo(int &length);
bool A5AtiInit(int max_rounds, int condition, unsigned int gpu_mask,
               int pipeline_mul);
int  A5AtiSubmit(uint64_t start_value, unsigned int start_round,
                 uint32_t advance, void* context);
int  A5AtiSubmitPartial(uint64_t start_value, unsigned int stop_round,
                        uint32_t advance, void* context);
bool A5AtiPopResult(uint64_t& start_value, uint64_t& stop_value,
                    void** context);  
bool A5AtiIsIdle();
void A5AtiClear();
void A5AtiCancel(list<void*> ctxList);
void A5AtiSpinLock(bool state);
void A5AtiShutdown();

void ApplyIndexFunc(uint64_t& start_index, int bits);
int ExtractIndex(uint64_t& start_value, int bits);

#endif
