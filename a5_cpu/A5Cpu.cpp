/***************************************************************
 * A5/1 Chain generator.
 *
 * Copyright 2009. Frank A. Stevenson. All rights reserved.
 *
 * Permission to distribute, modify and copy is granted to the
 * TMTO project, currently hosted at:
 * 
 * http://reflextor.com/trac/a51
 *
 * Code may be modifed and used, but not distributed by anyone.
 *
 * Request for alternative licencing may be directed to the author.
 *
 * All (modified) copies of this source must retain this copyright notice.
 *
 *******************************************************************/

#include <Globals.h>
#include "A5Cpu.h"

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <sys/time.h>


using namespace std;
#include <deque>

#include "Memdebug.h"

/**
 * Construct an instance of A5 Cpu searcher
 * create tables and streams
 */
A5Cpu::A5Cpu(int max_rounds, int condition, int threads)
{
  mCondition = 32 - condition;
  mMaxRound = max_rounds;
  mWait = false;
  mWaiting = false;
  mRequestCount = 0;
  mAvgProcessTime = -1.0f;

  /* Set up lookup tables */
  CalcTables();

  /* Init semaphore */
  mutex_init( &mMutex );

  /* Start worker thread */
  mRunning = true;
  mNumThreads = threads>16?16:threads;
  if (mNumThreads<1) mNumThreads=1;
  mThreads = new pthread_t[mNumThreads];
  for (int i=0; i<mNumThreads; i++) {
    pthread_create(&mThreads[i], NULL, thread_stub, (void*)this);
  }
}

void* A5Cpu::thread_stub(void* arg)
{
  if (arg) {
    A5Cpu* a5 = (A5Cpu*)arg;
    a5->Process();
  }
  return NULL;
}

/**
 * Destroy an instance of A5 Cpu searcher
 * delete tables and streams
 */
A5Cpu::~A5Cpu()
{
	Shutdown();
}


void A5Cpu::Shutdown()
{
	if(mRunning)
	{
		/* stop worker threads */
		mRunning = false;
		for (int i=0; i<mNumThreads; i++) {
			pthread_join(mThreads[i], NULL);
		}

		delete [] mThreads;

		mutex_destroy(&mMutex);
	}
}

int  A5Cpu::Submit(uint64_t job_id, uint64_t start_value, uint64_t target,
                   int32_t start_round, int32_t stop_round,
                   uint32_t advance, void* context)
{
    if (start_round>=mMaxRound) return -1;
	if (stop_round<0) stop_round = mMaxRound;

    mutex_lock(&mMutex);

	int ret = 0;
	t_a5_request req;

	req.job_id = job_id;
	req.start_value = start_value;
	req.start_round = start_round;
	req.end_round = stop_round;
	req.advance = advance;
	req.target = target;
	req.context = context;

	if(target)
	{
		mRequests[job_id].push_front(req);
	}
	else
	{
		mRequests[job_id].push_back(req);
	}

	mRequestCount++;
	ret = (mRequestCount>INT_MAX)?(INT_MAX):((int)mRequestCount);

    mutex_unlock(&mMutex);

	return ret;
}  

bool A5Cpu::IsIdle()
{
	bool idle = false;

	mutex_lock(&mMutex);
	if (mRequestCount < mNumThreads)
	{
		idle = true;
	}
	mutex_unlock(&mMutex);

	return idle;
}
  
void A5Cpu::Clear()
{
	mutex_lock(&mMutex);
/*
	mInputStart.clear();
	mInputTarget.clear();
	mInputRound.clear();
	mInputRoundStop.clear();
	mInputAdvance.clear();
	mInputContext.clear();

	mOutput.clear();
	mOutputStartRound.clear();
	mOutputContext.clear();
*/
	mutex_unlock(&mMutex);
}
  
void A5Cpu::SpinLock(bool state)
{
	/* lock now */
	if(state)
	{
		mWait = true;

		while(mWaiting != mNumThreads)
		{
			usleep(100);
		}
	}
	else
	{
		mWait = false;
	}
}

void A5Cpu::Cancel(uint64_t job_id)
{
	mutex_lock(&mMutex);

	if(mRequests.find(job_id) != mRequests.end())
	{
		mRequestCount -= mRequests[job_id].size(); 
		mRequests[job_id].clear();
		mRequests.erase(job_id);
	}

	if(mResults.find(job_id) != mResults.end())
	{
		mResults[job_id].clear();
		mResults.erase(job_id);
	}
	
	mutex_unlock(&mMutex);
}
  
bool A5Cpu::PopResult(uint64_t& job_id, uint64_t& start_value, uint64_t& stop_value,
                      int32_t& start_round, void** context)
{
    mutex_lock(&mMutex);

	/* find any pending job */
	map<uint64_t, deque<t_a5_result> >::iterator it = mResults.begin();

	/* go through all jobs */
	while(it != mResults.end())
	{
		/* does this job have ready results? */
		if(it->second.size() > 0)
		{
			t_a5_result res = it->second.front();
			it->second.pop_front();

			job_id = res.job_id;
			start_value = res.start_value;
			stop_value = res.end_value;
			start_round = res.start_round;

			if(context)
			{
				*context = res.context;
			}

			/* clear job entry if this was the last result */
			if(it->second.size() == 0)
			{
				mResults.erase(job_id);
			}

			mutex_unlock(&mMutex);
			return true;
		}

		it++;
	}

    mutex_unlock(&mMutex);
	return false;
}

void A5Cpu::Process(void)
{
  bool active = false;
  struct timeval tStart;
  struct timeval tEnd;

  uint64_t job_id;
  uint64_t start_point;
  uint64_t target;
  uint64_t start_point_r;
  int32_t  start_round;
  int32_t  stop_round;
  uint32_t advance;
  const uint32_t* RFtable;
  void* context;

  for(;;) {
    if (!mRunning) break;

	if(mWait)
	{
		mutex_lock(&mMutex);
		mWaiting++;
	    mutex_unlock(&mMutex);
		while(mWait)
		{
			usleep(0);
		}
		mutex_lock(&mMutex);
		mWaiting--;
	    mutex_unlock(&mMutex);
	}

    /* Get input */
    mutex_lock(&mMutex);

	/* find any pending job */
	map<uint64_t, deque<t_a5_request> >::iterator it = mRequests.begin();

	/* and queue the first request */
	while((!active) && (it != mRequests.end()))
	{
		/* look for a job with work packets */
		if(it->second.size() > 0)
		{
			t_a5_request req = it->second.front();
			it->second.pop_front();
			mRequestCount--;

			job_id = req.job_id;
			start_point = req.start_value;
			target = req.target;
			start_round = req.start_round;
			stop_round = req.end_round;
			advance = req.advance;
			context = req.context;

			start_point_r = ReverseBits(start_point);

			map< uint32_t, class Advance* >::iterator it2 = mAdvances.find(advance);
			if (it2==mAdvances.end()) 
			{
				class Advance* adv = new Advance(advance, mMaxRound);
				mAdvances[advance] = adv;
				RFtable = adv->getRFtable();
			} 
			else 
			{
				RFtable = (*it2).second->getRFtable();
			}
			active = true;
			
			/* clear job entry if it has no requests anymore */
			if(it->second.size() == 0)
			{
				/* we may not use the iterator anymore now! */
				mRequests.erase(job_id);
			}
		}
		else
		{
			it++;
		}
	}

    mutex_unlock(&mMutex);

    if (!active) {
      /* Don't use CPU while idle */
      usleep(50);
      continue;
    }

    gettimeofday( &tStart, NULL );
    /* Do something */
    unsigned int out_hi = ((start_point_r>>32) & 0xFFFFFFFF);
    unsigned int out_lo = (start_point_r & 0xFFFFFFFF);

    unsigned int target_lo = (target & 0xFFFFFFFF);
    unsigned int target_hi = ((target >> 32) & 0xFFFFFFFF);

    unsigned int last_key_lo;
    unsigned int last_key_hi;

    bool keysearch = (target != 0ULL);

    for (int round=start_round; round < stop_round; ) {
        out_lo = out_lo ^ RFtable[2*round];
        out_hi = out_hi ^ RFtable[2*round+1];

        if ((out_hi>>mCondition)==0) {
            // uint64_t res = (((uint64_t)out_hi)<<32)|out_lo;
            // res = ReverseBits(res);
            // printf("New round %i %016llx %08x:%08x\n", round, res, out_hi, out_lo);
            round++;
            if (round>=stop_round) break;
        }

        unsigned int lfsr1 = out_lo;
        unsigned int lfsr2 = (out_hi << 13) | (out_lo >> 19);
        unsigned int lfsr3 = out_hi >> 9;

        last_key_hi = out_hi;
        last_key_lo = out_lo;

        for (int i=0; i<25 ; i++) {
            int clocks = ((lfsr1<<3)&0xf00) | ((lfsr2>>3)&0xf0) | ((lfsr3>>7)&0xf);
            int masks = mClockMask[clocks];

            /* lfsr1 */
            unsigned int tmask = (masks>>8)&0x0f;
            unsigned int tval = mTable6bit[((lfsr1>>9)&0x3f0)|tmask];
            unsigned int tval2 = mTable4bit[((lfsr1>>6)&0xf0)|tmask];
            lfsr1 = (lfsr1<<(tval2>>4))^(tval>>4)^(tval2&0x0f);

            /* lfsr2 */
            tmask = (masks>>4)&0x0f;
            tval = mTable5bit[((lfsr2>>13)&0x1f0)|tmask];
            out_hi = out_hi ^ (tval&0x0f);
            lfsr2 = (lfsr2<<(masks>>12))^(tval>>4);

            /* lfsr3 */
            tmask = masks & 0x0f;
            tval = mTable6bit[((lfsr3>>13)&0x3f0)|tmask];
            tval2 = mTable4bit[(lfsr3&0xf0)|tmask];
            lfsr3 = (lfsr3<<(tval2>>4))^(tval>>4)^(tval2&0x0f);
        }
        for (int i=0; i<8 ; i++) {
            int clocks = ((lfsr1<<3)&0xf00) | ((lfsr2>>3)&0xf0) | ((lfsr3>>7)&0xf);
            int masks = mClockMask[clocks];
            
            /* lfsr1 */
            unsigned int tmask = (masks>>8)&0x0f;
            unsigned int tval = mTable6bit[((lfsr1>>9)&0x3f0)|tmask];
            out_hi = (out_hi << 4) | (tval&0x0f);
            unsigned int tval2 = mTable4bit[((lfsr1>>6)&0xf0)|tmask];
            lfsr1 = (lfsr1<<(tval2>>4))^(tval>>4)^(tval2&0x0f);

            /* lfsr2 */
            tmask = (masks>>4)&0x0f;
            tval = mTable5bit[((lfsr2>>13)&0x1f0)|tmask];
            out_hi = out_hi ^ (tval&0x0f);
            lfsr2 = (lfsr2<<(masks>>12))^(tval>>4);        

            /* lfsr3 */
            tmask = masks & 0x0f;
            tval = mTable6bit[((lfsr3>>13)&0x3f0)|tmask];
            out_hi =  out_hi ^ (tval&0x0f);
            tval2 = mTable4bit[(lfsr3&0xf0)|tmask];
            lfsr3 = (lfsr3<<(tval2>>4))^(tval>>4)^(tval2&0x0f);
        }
        for (int i=0; i<8 ; i++) {
            int clocks = ((lfsr1<<3)&0xf00) | ((lfsr2>>3)&0xf0) | ((lfsr3>>7)&0xf);
            int masks = mClockMask[clocks];

            /* lfsr1 */
            unsigned int tmask = (masks>>8)&0x0f;
            unsigned int tval = mTable6bit[((lfsr1>>9)&0x3f0)|tmask];
            out_lo = (out_lo << 4) | (tval&0x0f);
            unsigned int tval2 = mTable4bit[((lfsr1>>6)&0xf0)|tmask];
            lfsr1 = (lfsr1<<(tval2>>4))^(tval>>4)^(tval2&0x0f);

            /* lfsr2 */
            tmask = (masks>>4)&0x0f;
            tval = mTable5bit[((lfsr2>>13)&0x1f0)|tmask];
            out_lo = out_lo ^ (tval&0x0f);
            lfsr2 = (lfsr2<<(masks>>12))^(tval>>4);        

            /* lfsr3 */
            tmask = masks & 0x0f;
            tval = mTable6bit[((lfsr3>>13)&0x3f0)|tmask];
            out_lo =  out_lo ^ (tval&0x0f);
            tval2 = mTable4bit[(lfsr3&0xf0)|tmask];
            lfsr3 = (lfsr3<<(tval2>>4))^(tval>>4)^(tval2&0x0f);
        }
        if (keysearch&&(target_hi==out_hi)&&(target_lo==out_lo)) {
            /* report key as finishing state */
            out_hi = last_key_hi;
            out_lo = last_key_lo;
            start_round = -1;
            break;
        }
    }

    uint64_t res = (((uint64_t)out_hi)<<32)|out_lo;
    res = ReverseBits(res);

	t_a5_result result;

	result.job_id = job_id;
	result.start_value = start_point;
	result.end_value = res;
	result.start_round = start_round;
	result.context = context;

	active = false;

	/* calc average processing time */
    gettimeofday( &tEnd, NULL );
    uint64_t diff = 1000000ULL * (tEnd.tv_sec - tStart.tv_sec);
    diff += (tEnd.tv_usec - tStart.tv_usec);
    double diffSeconds = ((double)diff) / 1000000.0f;

    /* Report completed chains */
    mutex_lock(&mMutex);
	mResults[job_id].push_back(result);

	/* update average processing time */
	if(mAvgProcessTime >= 0)
	{
		mAvgProcessTime = (mAvgProcessTime + diffSeconds) / 2.0f;
	}
	else
	{
		mAvgProcessTime = diffSeconds;
	}

    mutex_unlock(&mMutex);
  }
}

/* Reverse bit order of an unsigned 64 bits int */
uint64_t A5Cpu::ReverseBits(uint64_t r)
{
  uint64_t r1 = r;
  uint64_t r2 = 0;
  for (int j = 0; j < 64 ; j++ ) {
    r2 = (r2<<1) | (r1 & 0x01);
    r1 = r1 >> 1;
  }
  return r2;
}

int A5Cpu::PopcountNibble(int x) {
    int res = 0;
    for (int i=0; i<4; i++) {
        res += x & 0x01;
        x = x >> 1;
    }
    return res;
}


void A5Cpu::CalcTables(void)
{
   /* Calculate clocking table */
    for(int i=0; i< 16 ; i++) {
        for(int j=0; j< 16 ; j++) {
            for(int k=0; k< 16 ; k++) {
                /* Copy input */
                int m1 = i;
                int m2 = j;
                int m3 = k;
                /* Generate masks */
                int cm1 = 0;
                int cm2 = 0;
                int cm3 = 0;
		/* Counter R2 */
		int r2count = 0;
                for (int l = 0; l < 4 ; l++ ) {
                    cm1 = cm1 << 1;
                    cm2 = cm2 << 1;
                    cm3 = cm3 << 1;
                    int maj = ((m1>>3)+(m2>>3)+(m3>>3))>>1;
                    if ((m1>>3)==maj) {
                        m1 = (m1<<1)&0x0f;
                        cm1 |= 0x01;
                    }
                    if ((m2>>3)==maj) {
                        m2 = (m2<<1)&0x0f;
                        cm2 |= 0x01;
			r2count++;
                    }
                    if ((m3>>3)==maj) {
                        m3 = (m3<<1)&0x0f;
                        cm3 |= 0x01;
                    }
                }
                // printf( "%x %x %x -> %x:%x:%x\n", i,j,k, cm1, cm2, cm3);
                int index = i*16*16+j*16+k;
                mClockMask[index] = (r2count<<12) | (cm1<<8) | (cm2<<4) | cm3;
            }
        }
    }

    /* Calculate 111000 + clock mask table */
    for (int i=0; i < 64 ; i++ ) {
        for(int j=0; j<16; j++) {
            int count = PopcountNibble(j);
            int feedback = 0;
            int data = i;
            for (int k=0; k<count; k++) {
                feedback = feedback << 1;
                int v = (data>>5) ^ (data>>4) ^ (data>>3);
                data = data << 1;
                feedback ^= (v&0x01);
            }
            data = i;
            int mask = j;
            int output = 0;
            for (int k=0; k<4; k++) {
                output = (output<<1) ^ ((data>>5)&0x01);
                if (mask&0x08) {
                    data = data << 1;
                }
                mask = mask << 1;
            }
            int index = i * 16 + j; 
            mTable6bit[index] = (feedback<<4) | output;
            // printf("%02x:%x -> %x %x\n", i,j,feedback, output);
        }
    }

    /* Calculate 11000 + clock mask table */
    for (int i=0; i < 32 ; i++ ) {
        for(int j=0; j<16; j++) {
            int count = PopcountNibble(j);
            int feedback = 0;
            int data = i;
            for (int k=0; k<count; k++) {
                feedback = feedback << 1;
                int v = (data>>4) ^ (data>>3);
                data = data << 1;
                feedback ^= (v&0x01);
            }
            data = i;
            int mask = j;
            int output = 0;
            for (int k=0; k<4; k++) {
                output = (output<<1) ^ ((data>>4)&0x01);
                if (mask&0x08) {
                    data = data << 1;
                }
                mask = mask << 1;
            }
            int index = i * 16 + j; 
            mTable5bit[index] = (feedback<<4) | output;
            // printf("%02x:%x -> %x %x\n", i,j,feedback, output);
        }
    }

    /* Calculate 1000 + clock mask table */
    for (int i=0; i < 16 ; i++ ) {
        for(int j=0; j<16; j++) {
            int count = PopcountNibble(j);
            int feedback = 0;
            int data = i;
            for (int k=0; k<count; k++) {
                feedback = feedback << 1;
                int v = (data>>3);
                data = data << 1;
                feedback ^= (v&0x01);
            }
            int index = i * 16 + j;
            mTable4bit[index] = (count<<4)|feedback;
            // printf("%02x:%x -> %x\n", i,j,feedback );
        }
    }
}

char* A5Cpu::GetDeviceStats() 
{ 
	if(mAvgProcessTime > 0)
	{
		sprintf(mDeviceStats, "%i Cores: Average speed is %.2f calcs/s per core (total %.2f calcs/s)", mNumThreads, (1.0f/mAvgProcessTime), (mNumThreads/mAvgProcessTime) );
	}
	else
	{
		sprintf(mDeviceStats, "%i Cores: Average speed is <untested>", mNumThreads );
	}
	return mDeviceStats; 
}

/* Stubs for shared library - exported without name mangling */

extern "C" {

static class A5Cpu* a5Instance = 0;

bool DLL_PUBLIC A5Init(int max_rounds, int condition, int threads)
{
  if (a5Instance) return false;
  a5Instance = new A5Cpu(max_rounds, condition, threads );
  return true;
}

int  DLL_PUBLIC A5Submit(uint64_t job_id, uint64_t start_value, int32_t start_round, uint32_t advance, void* context)
{
  if (a5Instance) {
      return a5Instance->Submit(job_id, start_value, 0ULL, start_round, -1, advance, context);
  }
  return -1; /* Error */
}

int  DLL_PUBLIC A5KeySearch(uint64_t job_id, uint64_t start_value, uint64_t target, int32_t start_round, int32_t stop_round, uint32_t advance, void* context)
{
  if (a5Instance) {
      return a5Instance->Submit(job_id, start_value, target, start_round, stop_round, advance, context);
  }
  return -1; /* Error */
}


bool DLL_PUBLIC A5PopResult(uint64_t& job_id, uint64_t& start_value, uint64_t& stop_value, int32_t& start_round, void** context)
{
  if (a5Instance) {
      return a5Instance->PopResult(job_id, start_value, stop_value, start_round, context);
  }
  return false; /* Nothing popped */ 
}

void DLL_PUBLIC A5Shutdown()
{
    if (a5Instance) {
		a5Instance->Shutdown();
		delete a5Instance;
    }
	a5Instance = NULL;
}

bool DLL_PUBLIC A5IsIdle()
{
    if (a5Instance) {
        return a5Instance->IsIdle();
    }
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

char DLL_PUBLIC * A5GetDeviceStats()
{
	if (a5Instance)
    {
		return a5Instance->GetDeviceStats();
	}
}

bool DLL_PUBLIC A5PipelineInfo(int &length)
{
    return false;
}

}
