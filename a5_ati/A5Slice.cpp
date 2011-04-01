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

#include "A5Slice.h"

#include <assert.h>
#include <sys/time.h>
#include <iostream>
#include <string.h>
#include <stdio.h>

#include "kernelLib.h"

#include <Globals.h>

#define DISASSEMBLE 0

#if DISASSEMBLE
static FILE* gDisFile;

static void logger(const char*msg)
{
    if (gDisFile) {
        fwrite(msg,strlen(msg),1,gDisFile);
    }
}
#endif


PFNCALCTXCREATECOUNTER Ext_calCtxCreateCounter = 0;
PFNCALCTXBEGINCOUNTER Ext_calCtxBeginCounter = 0;
PFNCALCTXENDCOUNTER Ext_calCtxEndCounter = 0;
PFNCALCTXDESTROYCOUNTER Ext_calCtxDestroyCounter = 0;
PFNCALCTXGETCOUNTER Ext_calCtxGetCounter = 0;

A5Slice::A5Slice(AtiA5* cont, int dev, int dp, int rounds, int pipe_mult) :
    mNumRounds(rounds),
    mController(cont),
    mState(0),
    mDp(dp),
    mWaitState(ePopulate),
    mTicks(0)
{
	mUsable = false;
    mDevNo = dev;
    mMaxCycles = (1<<mDp)*rounds*10;
	mAvgExecTime = 0.0f;
	mAvgProcessTime = 0.0f;

    // CAL setup
    assert((dev>=0)&&(dev<CalDevice::getNumDevices()));
    mDev = CalDevice::createDevice(dev);
    assert(mDev);
    mNum = mDev->getDeviceAttribs()->wavefrontSize * mDev->getDeviceAttribs()->numberOfSIMD * pipe_mult;

    unsigned int dim = mNum;
    unsigned int dim32 = 32*mNum;

    mConstCount = mDev->resAllocLocal1D(1,CAL_FORMAT_UINT_1,0);
    mResControl = mDev->resAllocLocal1D(dim,CAL_FORMAT_UINT_1,0);
    mResStateLocal = mDev->resAllocLocal1D(dim32,CAL_FORMAT_UINT_4, CAL_RESALLOC_GLOBAL_BUFFER);
    mResStateRemote = mDev->resAllocRemote1D(dim32,CAL_FORMAT_UINT_4, CAL_RESALLOC_CACHEABLE|CAL_RESALLOC_GLOBAL_BUFFER);

    /* Lazy check to ensure that memory has been allocated */
    assert(mConstCount);
    assert(mResControl);
    assert(mResStateLocal);
    assert(mResStateRemote);

    unsigned char* myKernel = getKernel(dp);

    if (myKernel == NULL) {
		printf ( " [E] A5Ati:   [%i] Could not load optimized kernel\r\n", mDevNo);
		return;
    }

    if (calclCompile(&mObject, CAL_LANGUAGE_IL, (const CALchar*)myKernel, mDev->getDeviceInfo()->target) != CAL_RESULT_OK) {
		freeKernel(myKernel);
		myKernel = getFallbackKernel(dp);

		if (myKernel == NULL) {
			printf ( " [E] A5Ati:   [%i] Could not load fallback kernel\r\n", mDevNo);
			freeKernel(myKernel);
			return;
		}

		/* retry with more generic kernel */
	    if (calclCompile(&mObject, CAL_LANGUAGE_IL, (const CALchar*)myKernel, mDev->getDeviceInfo()->target) != CAL_RESULT_OK) {
			printf ( " [E] A5Ati:   [%i] Compilation failed.\r\n", mDevNo);
			printf ( " [E] A5Ati:   [%i]   Reason: %s\r\n", mDevNo, calclGetErrorString());
			printf ( " [E] A5Ati:   [%i]   Your card might be too old for this code.\r\n", mDevNo);
			return;
		}
		printf ( " [x] A5Ati:   [%i] Warning: Using unoptimized kernel for your device.\r\n", mDevNo);
    }

    freeKernel(myKernel);
    myKernel = NULL;

    if (calclLink (&mImage, &mObject, 1) != CAL_RESULT_OK) {
		printf ( " [E] A5Ati:   [%i] Link failed.\r\n", mDevNo);
		printf ( " [E] A5Ati:   [%i]   Reason: %s\r\n", mDevNo, calclGetErrorString());
		return;
    }

#if DISASSEMBLE
    gDisFile = fopen("disassembly.txt","w");
    calclDisassembleImage(mImage, logger);
    fclose(gDisFile);
#endif

    mCtx = mDev->getContext();
    mModule = new CalModule(mCtx);
    if (mModule==0) {
		printf ( " [E] A5Ati:   [%i] Could not create module.\r\n", mDevNo);
		printf ( " [E] A5Ati:   [%i]   Reason: %s\r\n", mDevNo, calclGetErrorString());
		return;
    }

    if (!mModule->Load(mImage)) {
		printf ( " [E] A5Ati:   [%i] Could not load module image.\r\n", mDevNo);
		printf ( " [E] A5Ati:   [%i]   Reason: %s\r\n", mDevNo, calclGetErrorString());
		return;
    }

    unsigned int *dataPtr = NULL;
    CALuint pitch = 0;
    mIterCount = (1<<dp)/10;
    if (mIterCount>128) mIterCount = 128;
    mIterCount = 256;
    if (calResMap((CALvoid**)&dataPtr, &pitch, *mConstCount, 0) != CAL_RESULT_OK) {
		printf ( " [E] A5Ati:   [%i] Could not map mConstCount resource.\r\n", mDevNo);
		printf ( " [E] A5Ati:   [%i]   Reason: %s\r\n", mDevNo, calclGetErrorString());
		return;
    }
    *dataPtr = mIterCount;   /* Number of rounds */
    printf(" [x] A5Ati:   [%i] Running %i rounds per kernel invocation.\n", mDevNo, mIterCount);
    calResUnmap(*mConstCount);

    mModule->Bind( "cb0", mConstCount );
    mModule->Bind( "i0", mResControl );
    mMemLocal = mModule->Bind( "g[]", mResStateLocal );

    if (calCtxGetMem(&mMemRemote, *mCtx, *mResStateRemote) != CAL_RESULT_OK) {
		printf ( " [E] A5Ati:   [%i] Could not add memory to context.\r\n", mDevNo);
		printf ( " [E] A5Ati:   [%i]   Reason: %s\r\n", mDevNo, calclGetErrorString());
		return;
    };

    if (!Ext_calCtxCreateCounter) {
        calExtGetProc( (CALvoid**)&Ext_calCtxCreateCounter, CAL_EXT_COUNTERS,
                       "calCtxCreateCounter");
    }
    Ext_calCtxCreateCounter( &mCounter, *mCtx, CAL_COUNTER_IDLE );
    if (!Ext_calCtxBeginCounter) {
        calExtGetProc( (CALvoid**)&Ext_calCtxBeginCounter, CAL_EXT_COUNTERS,
                       "calCtxBeginCounter");
    }
    Ext_calCtxBeginCounter( *mCtx, mCounter );
    if (!Ext_calCtxEndCounter) {
        calExtGetProc( (CALvoid**)&Ext_calCtxEndCounter, CAL_EXT_COUNTERS,
                       "calCtxEndCounter");
    }
    if (!Ext_calCtxDestroyCounter) {
        calExtGetProc( (CALvoid**)&Ext_calCtxDestroyCounter, CAL_EXT_COUNTERS,
                       "calCtxDestroyCounter");
    }
    if (!Ext_calCtxGetCounter) {
        calExtGetProc( (CALvoid**)&Ext_calCtxGetCounter, CAL_EXT_COUNTERS,
                       "calCtxGetCounter");
    }

    mControl = new unsigned int[mNum];

    /* Init free list */
    int jobs = mNum * 32;
    mJobs = new AtiA5::JobPiece_s[jobs];
    for(int i=0; i<jobs; i++) {
        mJobs[i].next_free = i-1;
        mJobs[i].idle = true;
    }
    mFree = jobs - 1;
    mNumJobs = 0;

    memset( mControl, 0, mNum*sizeof(unsigned int) );

	printf(" [x] A5Ati:   [%i] Using %i threads\n", mDevNo, mNum );

	mUsable = true;
}

A5Slice::~A5Slice() {
    CalDevice::unrefResource(mConstCount);
    CalDevice::unrefResource(mResControl);
    CalDevice::unrefResource(mResStateLocal);
    CalDevice::unrefResource(mResStateRemote);

	/* if the device was not usable, there is perhaps nothing more to free */
	if(!mUsable)
	{
		return;
	}

    Ext_calCtxEndCounter(*mCtx,mCounter);
    Ext_calCtxDestroyCounter( *mCtx, mCounter );

    calCtxReleaseMem(*mCtx, mMemRemote);

    delete [] mControl;
    delete [] mJobs;

    delete mModule;
    calclFreeImage(mImage);
    calclFreeObject(mObject);
    delete mDev;
}

void A5Slice::Clear() 
{
    mNumJobs = 0;

    int jobs = mNum * 32;
    mJobs = new AtiA5::JobPiece_s[jobs];
    for(int i=0; i<jobs; i++) {
        mJobs[i].next_free = i-1;
        mJobs[i].idle = true;
    }
    mFree = jobs - 1;

}

void A5Slice::Cancel(uint64_t job_id) 
{
    for(int pos = 0; pos < mNum * 32; pos++)
	{
        AtiA5::JobPiece_s* job = &mJobs[pos];
		if (!job->idle && (job->job_id == job_id))
		{
			/* Add to free list */
			job->next_free = mFree;
			job->idle = true;
			mFree = pos;
			mNumJobs--;
        }
    }
}

/* Get the state of a chain across the slices */
uint64_t A5Slice::getState( int block, int bit ) {
    uint64_t res = 0;
    unsigned int mask = 1 << bit;

    for(int i=0; i<16; i++) {
        unsigned int val = mState[32*block+i].x & mask;
        res = (res << 1) | (val >> bit);
        val = mState[32*block+i].y & mask;
        res = (res << 1) | (val>> bit);
        val = mState[32*block+i].z & mask;
        res = (res << 1) | (val>> bit);
        val = mState[32*block+i].w & mask;
        res = (res << 1) | (val>> bit);
    }

    return res;
}

/* Get the state of a chain across the slices reversed (sane) */
uint64_t A5Slice::getStateRev( int block, int bit ) {
    uint64_t res = 0;
    unsigned int mask = 1 << bit;

    for(int i=0; i<16; i++) {
        uint64_t val = mState[32*block+i].x & mask;
        res = (res >> 1) | ((val >> bit)<<63);
        val = mState[32*block+i].y & mask;
        res = (res >> 1) | ((val >> bit)<<63);
        val = mState[32*block+i].z & mask;
        res = (res >> 1) | ((val >> bit)<<63);
        val = mState[32*block+i].w & mask;
        res = (res >> 1) | ((val >> bit)<<63);
    }

    return res;
}

/* Set the state of a chain across the slices */
void A5Slice::setState( uint64_t v, int block, int bit ) {
    unsigned int mask= 1 << bit;
    unsigned int invmask = ~mask;
    unsigned int b;

    for(int i=0; i<16; i++) {
        unsigned int val = mState[32*block+i].x & invmask;
        b = (v>>63) & 1;
        val = val | (b<<bit);
        v = v << 1;
        mState[32*block+i].x = val;

        val = mState[32*block+i].y & invmask;
        b = (v>>63) & 1;
        val = val | (b<<bit);
        v = v << 1;
        mState[32*block+i].y = val;

        val = mState[32*block+i].z & invmask;
        b = (v>>63) & 1;
        val = val | (b<<bit);
        v = v << 1;
        mState[32*block+i].z = val;

        val = mState[32*block+i].w & invmask;
        b = (v>>63) & 1;
        val = val | (b<<bit);
        v = v << 1;
        mState[32*block+i].w = val;
    }
}

/* Get the state of a chain across the slices, reversed (sane) */
void A5Slice::setStateRev( uint64_t v, int block, int bit ) {
    unsigned int mask= 1 << bit;
    unsigned int invmask = ~mask;
    unsigned int b;

    for(int i=0; i<16; i++) {
        unsigned int val = mState[32*block+i].x & invmask;
        b = v & 1;
        val = val | (b<<bit);
        v = v >> 1;
        mState[32*block+i].x = val;

        val = mState[32*block+i].y & invmask;
        b = v & 1;
        val = val | (b<<bit);
        v = v >> 1;
        mState[32*block+i].y = val;

        val = mState[32*block+i].z & invmask;
        b = v & 1;
        val = val | (b<<bit);
        v = v >> 1;
        mState[32*block+i].z = val;

        val = mState[32*block+i].w & invmask;
        b = v & 1;
        val = val | (b<<bit);
        v = v >> 1;
        mState[32*block+i].w = val;
    }
}

/* Set the round index of a particular chain slot */
void A5Slice::setRound( int block, int bit, uint64_t v) {
    unsigned int mask= 1 << bit;
    unsigned int invmask = ~mask;
    unsigned int b;

    for(int i=0; i<16; i++) {
        uint4 rvec = mState[block*32+16+i];

        unsigned int val = rvec.x & invmask;
        b = v & 1;
        val = val | (b<<bit);
        v = v >> 1;
        rvec.x = val;

        val = rvec.y & invmask;
        b = v & 1;
        val = val | (b<<bit);
        v = v >> 1;
        rvec.y = val;

        val = rvec.z & invmask;
        b = v & 1;
        val = val | (b<<bit);
        v = v >> 1;
        rvec.z = val;

        val = rvec.w & invmask;
        b = v & 1;
        val = val | (b<<bit);
        v = v >> 1;
        rvec.w = val;

        mState[block*32+16+i] = rvec;
    }
}

/* CPU processes the tricky bits (switching rounds) after the
 * GPU has done its part
 */
void A5Slice::process()
{
    for (int i=0; i<mNum; i++) {
        unsigned int control = 0;
        unsigned int todo = 0;
        unsigned int* dp_bits = (unsigned int*)&mState[32*i];
        unsigned int* rf_bits = (unsigned int*)&mState[32*i+16];
        for (int j=0; j<mDp; j++) {
            todo |= dp_bits[j]^rf_bits[j];
        }
        todo = ~todo;

        for( int j=0; j<32; j++) {
            AtiA5::JobPiece_s* job = &mJobs[32*i+j];
            if (!job->idle) {
                job->cycles += mIterCount;
                bool intMerge = job->cycles > (unsigned int)mMaxCycles;
                if (intMerge || (1<<j)&todo) {
                    /* Time to change round or evict stuck chain */
                    int round = job->current_round;
                    if ((!intMerge)&&(round<job->end_round)) {
                        uint64_t res = getStateRev(i,j) ^ job->round_func[round];
                        round++;
                        job->current_round = round;
                        uint64_t rfunc = job->round_func[round];
                        setRound(i,j,rfunc);
                        /* redo previous round function */
                        setStateRev(res^rfunc, i,j);
                        /* Compute this round even if at DP */
                        control |= (1<<j);
                    } else {
                        /* Chain completed */
                        uint64_t res = getStateRev(i,j);
                        if (job->end_round==(mNumRounds-1)) {
                            /* Allow partial searches to complete */
                            res = res ^ job->round_func[round];
                        }
                        if (intMerge) {
                            /* Internal merges are reported as an invalid end point */
                            res = 0xffffffffffffffffULL;
                        }
                        job->end_value = res;
                        mController->PushResult(job);
                        /* This item is idle, try to populate with new job */
                        if( mAvailable && mController->PopRequest(job) ) {
                            uint64_t rfunc = job->round_func[job->current_round];
                            setStateRev( job->start_value, i, j );
                            setRound(i,j, rfunc);
                            mAvailable--;
                        } else {
                            /* Add to free list */
                            setStateRev( 0ULL, i, j );
                            setRound(i,j, 0ULL);
                            job->next_free = mFree;
                            job->idle = true;
                            mFree = 32*i+j;
                            mNumJobs--;
                        }
                    }
                }
            }
        }

        mControl[i] = control;
    }

    /* Populate to idle queue */
    while( mAvailable && (mFree>=0)) {
        int i,j;
        i = mFree >> 5;
        j = mFree & 0x1f;
        AtiA5::JobPiece_s* job = &mJobs[mFree];
        if (mController->PopRequest(job) ) {
            mFree = job->next_free;
            uint64_t rfunc = job->round_func[job->current_round];
            setStateRev( job->start_value, i, j );
            setRound(i,j, rfunc);
            mAvailable--;
            mNumJobs++;
        } else {
            /* Something not right */
            break;
        }
    }
}

/* Initialize the pipeline (set to 0) */
void A5Slice::populate() {
    CALuint pitch = 0;
    if (calResMap((CALvoid**)&mState, &pitch, *mResStateRemote, 0) != CAL_RESULT_OK) 
    {
		printf ( " [E] A5Ati:   [%i] Can't map mResStateRemote resource: %s\r\n", mDevNo, calGetErrorString());
		return;
    }
    for (int i=0; i < mNum ; i++ ) {
        for(int j=0; j < 32; j++) {
            setStateRev( 0ULL, i, j );
            setRound(i,j, 0ULL);
        }
        mControl[i] = 0;
    }
    calResUnmap(*mResStateRemote);
    mState = 0;
}


bool A5Slice::tick()
{
    CALresult res;

	if(!mUsable)
	{
		return false;
	}

    switch(mWaitState) {
    case ePopulate:
        populate();
        res = calMemCopy(&mEvent, *mCtx, mMemRemote, *mMemLocal, 0);
        if (res!=CAL_RESULT_OK)
        {
            printf(" [E] A5Ati:   [%i] Error while calMemCopy: %s\n", mDevNo, calGetErrorString());
            return false;
        }
        calCtxIsEventDone(*mCtx, mEvent);
        mWaitState = eDMAto;
        break;
    case eDMAto:
        if (calCtxIsEventDone(*mCtx, mEvent)==CAL_RESULT_PENDING) 
		{
			return false;
		}

        {
            unsigned int *dataPtr = NULL;
            CALuint pitch = 0;
            if (calResMap((CALvoid**)&dataPtr, &pitch, *mResControl, 0) != CAL_RESULT_OK) 
            {
				printf(" [E] A5Ati:   [%i] Can't map mResControl resource: %s\n", mDevNo, calGetErrorString());
				return false;
            }
            memcpy( dataPtr, mControl, mNum*sizeof(unsigned int));
            calResUnmap(*mResControl);

            CALdomain domain = {0, 0, mNum, 1};
            gettimeofday(&mTvStarted, NULL);
            if (!mModule->Exec(domain,64)) 
			{
				printf(" [E] A5Ati:   [%i] Could not execute module: %s\n", mDevNo, calGetErrorString());
				return false;
            }
        }
        mModule->Finished();
        calCtxFlush(*mCtx);
        mWaitState = eKernel;
        break;

    case eKernel:
        if (!mModule->Finished()) 
		{
			return false;
		}

		if(true)
        {
            struct timeval tv2;
            gettimeofday(&tv2, NULL);
            unsigned long diff = 1000000*(tv2.tv_sec-mTvStarted.tv_sec);
            diff += tv2.tv_usec-mTvStarted.tv_usec;

			mAvgExecTime = (mAvgExecTime + ((double)diff) / 1000000.0f) / 2;

            // printf("Exec() took %i usec\n",(unsigned int)diff);
        }
        if (calMemCopy(&mEvent, *mCtx, *mMemLocal, mMemRemote, 0)!=CAL_RESULT_OK)
        {
            printf(" [E] A5Ati:   [%i] Error while calMemCopy: %s\n", mDevNo, calGetErrorString());
            return false;
        }
        calCtxIsEventDone(*mCtx, mEvent);
        mWaitState = eDMAfrom;
        break;

    case eDMAfrom:
        if (calCtxIsEventDone(*mCtx, mEvent)==CAL_RESULT_PENDING) 
		{
			return false;
		}

        {
            struct timeval tv1;
            struct timeval tv2;
            gettimeofday(&tv1, NULL);

            CALuint pitch = 0;
            if (calResMap((CALvoid**)&mState, &pitch, *mResStateRemote, 0) != CAL_RESULT_OK) 
            {
                assert(!" [E] A5Ati: Can't map mConstCount resource");
            }

            // Do the actual CPU processing
            process();

            calResUnmap(*mResStateRemote);
            mState = 0;
            gettimeofday(&tv2, NULL);
            unsigned long diff = 1000000*(tv2.tv_sec-tv1.tv_sec);
            diff += tv2.tv_usec-tv1.tv_usec;

			mAvgProcessTime = (mAvgProcessTime + ((double)diff) / 1000000.0f) / 2;
            // printf("process() took %i usec\n",diff);
        }
        if (calMemCopy(&mEvent, *mCtx, mMemRemote, *mMemLocal, 0)!=CAL_RESULT_OK)
        {
            printf(" [E] A5Ati:   [%i] Error while calMemCopy: %s\n", mDevNo, calGetErrorString());
            return false;
        }
        calCtxIsEventDone(*mCtx, mEvent);
        mWaitState = eDMAto;
        break;

    default:
        printf(" [E] A5Ati: State error");
		return false;
    };

	MEMCHECK();

#if 0
    /* Ticks that has performed any action make it to here */
    mTicks++;
    if (mTicks%100 == 0) {
        CALfloat perf;
        // Measure performance
        Ext_calCtxEndCounter( *mCtx, mCounter );
        Ext_calCtxGetCounter( &perf, *mCtx, mCounter );
        Ext_calCtxBeginCounter( *mCtx, mCounter );
        printf("Perf %i: %f\n", mDevNo, perf);
    }
#endif
    /* report that something was actually done */
    return true;
}

void A5Slice::flush()
{
    calCtxFlush(*mCtx);
}
