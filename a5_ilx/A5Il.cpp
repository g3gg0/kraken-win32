/***************************************************************
 * A5/1 Chain generator.
 *
 * Copyright 2010. Frank A. Stevenson. All rights reserved.
 *
 *
 *******************************************************************/

#include "A5Il.h"
#include <stdio.h>
#include <sys/time.h>
#include <assert.h>
#include "A5IlPair.h"

using namespace std;

/**
 * Construct an instance of A5 Il searcher
 * create tables and streams
 */
A5Il::A5Il(int max_rounds, int condition, uint32_t mask) :
    mCondition(condition),
    mMaxRound(max_rounds),
    mGpuMask(mask),
    mSlices(NULL),
    mReady(false)
{
	printf ( " [x] A5Il: Compiled for '"KRAKEN_VERSION"'\r\n");
	mUsable = false;
	mWaiting = false;
	mWait = false;
    assert(mCondition==12);

    /* Init semaphore */
    mutex_init( &mMutex );

    /* Make job queue */
    mJobQueue = new A5JobQueue(8);

	if(!Init())
	{
		return;
	}

    /* Start worker thread */
    mRunning = true;
    mNumThreads = 1;
    mThreads = new pthread_t[mNumThreads];
    for (int i=0; i<mNumThreads; i++) {
        pthread_create(&mThreads[i], NULL, thread_stub, (void*)this);
    }

    while(!mReady) {
        usleep(1000);
    }
    mUsable = true;
}

void* A5Il::thread_stub(void* arg)
{
    if (arg) {
        A5Il* a5 = (A5Il*)arg;
        a5->Process();
    }
    return NULL;
}

/**
 * Destroy an instance of A5 Il searcher
 * delete tables and streams
 */
A5Il::~A5Il()
{
    /* stop worker thread */
    mRunning = false;
    for (int i=0; i<mNumThreads; i++) {
        pthread_join(mThreads[i], NULL);
    }

    delete [] mThreads;

    delete mJobQueue;

    mutex_destroy(&mMutex);
}
  
int  A5Il::Submit(uint64_t start_value, uint64_t target,
                   int32_t start_round, int32_t stop_round,
                   uint32_t advance, void* context)
{
    if (start_round>=mMaxRound) return -1;
    if (stop_round<0) stop_round = mMaxRound;


    return mJobQueue->Submit( start_value, target,
                              start_round, stop_round,
                              advance, context );

}
  
bool A5Il::PopResult(uint64_t& start_value, uint64_t& stop_value,
                      int32_t& start_round, void** context)
{
    bool res = false;
    mutex_lock(&mMutex);
    if (mOutput.size()>0) {
        res = true;
        pair<uint64_t,uint64_t> res = mOutput.front();
        mOutput.pop();
        start_value = res.first;
        stop_value = res.second;
        start_round = mOutputStartRound.front();
        mOutputStartRound.pop();
        void* ctx = mOutputContext.front();
        mOutputContext.pop();
        if (context) *context = ctx;
    }
    mutex_unlock(&mMutex);
    return res;
}


bool A5Il::Init(void)
{
    int numCores = A5IlPair::getNumDevices();
	int usedSlices = 0;
    int pipes = 0;

    mNumSlices = 1;
	printf(" [x] A5Il: Setting up %i GPUs with %i slices each...\n", numCores, mNumSlices);    

    mSlices = new A5IlPair*[mNumSlices];
    
    for( int core=0; core<numCores; core++ ) {
        if ((1<<core)&mGpuMask) {
            mSlices[usedSlices] = new A5IlPair( this, core, mCondition, mMaxRound, mJobQueue);
			if(mSlices[usedSlices]->IsUsable())
			{
				usedSlices++;
			}
			else
			{
				delete mSlices[usedSlices];
			}
        }
	}

	/* none of the cores did set up properly */
	if(usedSlices == 0)
	{
		printf(" [x] A5Il: None of the GPUs did set up properly\n");
		return false;
	}

	return true;
}


void A5Il::Process(void)
{
    for(;;) {
        bool newCmd = false;

		if(mWait)
		{
			mWaiting = true;
			usleep(0);
		}
		else
		{
			int total = mJobQueue->getNumWaiting();
	        
			for( int i=0; i<mNumSlices ; i++ ) {
				total += mSlices[i]->getNumJobs();
			}        

			// printf("*");

			if (total) {
				/* do load balancing */
				int even = total / mNumSlices;
				// printf("Doing an even %i\n", even);
				for( int i=0; i<mNumSlices ; i++ ) {
					mSlices[i]->setLimit(even);
					newCmd |= mSlices[i]->tick();
				}
			} else {
				/* Empty pipeline */
				usleep(1000);
			}

			if (!mRunning) break;
		}
	}
    
    for( int i=0; i<mNumSlices ; i++ ) {
        delete mSlices[i];
    }
    delete[] mSlices;
    mSlices = NULL;

}

void A5Il::PushResult(JobPiece_s* job)
{
    /* Report completed chains */
    mutex_lock(&mMutex);

    uint64_t res = job->key_found ? job->key_found : job->end_value;
    res = ReverseBits(res);
    mOutput.push( pair<uint64_t,uint64_t>(job->start_value,res) );
    mOutputStartRound.push( job->start_round );
    mOutputContext.push( job->context );

    mutex_unlock(&mMutex);
}


bool A5Il::IsIdle()
{
	return mJobQueue->getNumWaiting() > 0;
}

void A5Il::SpinLock(bool state)
{
	/* lock now */
	if(state)
	{
		mWait = true;

		while(!mWaiting)
		{
			usleep(100);
		}
	}
	else
	{
		mWait = false;
	}
}

void A5Il::Cancel(uint64_t job_id)
{
}

void A5Il::Clear()
{
}
  

/* Reverse bit order of an unsigned 64 bits int */
uint64_t A5Il::ReverseBits(uint64_t r)
{
    uint64_t r1 = r;
    uint64_t r2 = 0;
    for (int j = 0; j < 64 ; j++ ) {
        r2 = (r2<<1) | (r1 & 0x01);
        r1 = r1 >> 1;
    }
    return r2;
}


/* Stubs for shared library - exported without name mangling */

extern "C" {

static class A5Il* a5Instance = 0;

bool DLL_PUBLIC A5Init(int max_rounds, int condition, uint32_t mask)
{
    if (a5Instance) return false;
    a5Instance = new A5Il(max_rounds, condition, mask);
    return a5Instance->IsUsable();
}

int  DLL_PUBLIC A5Submit(uint64_t start_value, int32_t start_round,
                           uint32_t advance, void* context)
{
    if (a5Instance) {
        return a5Instance->Submit(start_value, 0ULL, start_round, -1,
                                  advance, context);
    }
    return -1; /* Error */
}

int  DLL_PUBLIC A5KeySearch(uint64_t start_value, uint64_t target,
                              int32_t start_round, int32_t stop_round,
                              uint32_t advance, void* context)
{
    if (a5Instance) {
        return a5Instance->Submit(start_value, target, start_round,
                                  stop_round, advance, context);
    }
    return -1; /* Error */
}


bool DLL_PUBLIC A5PopResult(uint64_t& start_value, uint64_t& stop_value,
                               int32_t& start_round, void** context)
{
    if (a5Instance) {
        return a5Instance->PopResult(start_value, stop_value, start_round,
                                     context);
    }
    return false; /* Nothing popped */ 
}

void DLL_PUBLIC A5Shutdown()
{
    delete a5Instance;
    a5Instance = NULL;
}

bool DLL_PUBLIC A5IsIdle()
{
    return false;
}

int  DLL_PUBLIC A5SubmitPartial(uint64_t job_id, uint64_t start_value, unsigned int stop_round, uint32_t advance, void* context)
{
    return -1; /* Error */
}

bool DLL_PUBLIC A5PipelineInfo(int &length)
{
    return false;
}



void DLL_PUBLIC A5Clear()
{  
	if (a5Instance) {
		a5Instance->Clear();
	}
}

void DLL_PUBLIC A5Cancel(uint64_t job_id)
{  
	if (a5Instance) {
		a5Instance->Cancel(job_id);
	}
}

void DLL_PUBLIC A5SpinLock(bool state)
{  
	if (a5Instance) {
		a5Instance->SpinLock(state);
	}
}

}
