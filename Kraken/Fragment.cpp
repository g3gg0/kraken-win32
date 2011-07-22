
#include "Globals.h"
#include "Fragment.h"

#include "Kraken.h"
#include <stdio.h>

#include "../a5_cpu/A5CpuStubs.h"
#include "../a5_ati/A5GpuStubs.h"

#include "Memdebug.h"


static uint64_t kr02_whitening(uint64_t key)
{
    uint64_t white = 0;
    uint64_t bits = 0x93cbc4077efddc15ULL;
    uint64_t b = 0x1;
    while (b) {
        if (b & key) {
            white ^= bits;
        }
        bits = (bits<<1)|(bits>>63);
        b = b << 1;
    }
    return white;
}

static uint64_t kr02_mergebits(uint64_t key)
{
    uint64_t r = 0ULL;
    uint64_t b = 1ULL;
    unsigned int i;

    for(i=0;i<64;i++) {
        if (key&b) {
            r |= 1ULL << (((i<<1)&0x3e)|(i>>5));
        }
        b = b << 1;
    }
    return r;
}

void ApplyIndexFunc(uint64_t& start_index, int bits)
{
    uint64_t w = kr02_whitening(start_index);
    start_index = kr02_mergebits((w<<bits)|start_index);
}

Fragment::Fragment() :
    mKnownPlaintext(0),
    mNumRound(0),
    mAdvance(0),
    mTable(NULL),
    mState(0),
    mEndpoint(0),
    mBlockStart(0),
    mStartIndex(0),
	/* these get set by setRef */	
	mCount(0),
	mCountRef(0),
	mBitsRef(0),
	mClientId(0),
	mJobId(0)
{
	mutex_init(&mMutex);
}

Fragment::Fragment(uint64_t plaintext, unsigned int round, DeltaLookup* table, unsigned int advance) :
    mKnownPlaintext(plaintext),
    mNumRound(round),
    mAdvance(advance),
    mTable(table),
    mState(0),
    mEndpoint(0),
    mBlockStart(0),
    mStartIndex(0),
	/* these get set by setRef */	
	mCount(0),
	mCountRef(0),
	mBitsRef(0),
	mClientId(0),
	mJobId(0)
{
	mutex_init(&mMutex);
}

Fragment::~Fragment()
{
	mutex_destroy(&mMutex);
}

bool Fragment::processBlock(const void* pDataBlock)
{
    int res = 0;

	mutex_lock(&mMutex);
	
	if(pDataBlock != NULL)
	{
		res = mTable->CompleteEndpointSearch(pDataBlock, mBlockStart, mEndpoint, mStartIndex);
	}

	/* no data? */
    if (!res || mState != 1) 
	{
		/* smth failed */
        mState = 5;
		mutex_unlock(&mMutex);
		Kraken::getInstance()->queueFragmentRemoval(this, false, 0);
		return false;
    }

    /* Found endpoint */
    uint64_t search_rev = mStartIndex;
	bool queued = false;

    ApplyIndexFunc(search_rev, 34);

	/* undo rounds? */
    if (mNumRound) 
	{
		if(A5GpuSubmitPartial(mJobId, search_rev, mNumRound, mAdvance, this) >= 0)
		{
			queued = true;
			mState = 3;
		}
    } 

	/* seems there is no GPU support or mNumRound is zero. try GPU key search. */
	if(!queued)
	{
		if(A5GpuKeySearch(mJobId, search_rev, mKnownPlaintext, 0, mNumRound+1, mAdvance, this) >= 0)
		{
			queued = true;
			mState = 2;
		}
	}

	/* GPU key search not available, use CPU */
	if(!queued)
	{
		if(A5CpuKeySearch(mJobId, search_rev, mKnownPlaintext, 0, mNumRound+1, mAdvance, this) >= 0)
		{
			queued = true;
			mState = 2;
		}
	}

    if (!queued) 
	{
		printf(" [E] Failed to queue job.\n");
        mState = 5;
		mutex_unlock(&mMutex);
		Kraken::getInstance()->queueFragmentRemoval(this, false, 0);
		return false;
	}

	mutex_unlock(&mMutex);
	return true;
}


bool Fragment::handleSearchResult(uint64_t result, int start_round)
{
	bool fail = false;
	bool queued = false;

	mutex_lock(&mMutex);

    switch (mState) 
	{
		/* init state */
		case 0:
			mEndpoint = result;
			mState = 1;
			mTable->StartEndpointSearch(mJobId, this, mEndpoint, mBlockStart);

			if (mBlockStart==0ULL) 
			{
				mState = 5;

				/* Endpoint out of range */
				mutex_unlock(&mMutex);
				Kraken::getInstance()->queueFragmentRemoval(this, false, 0);
				return false;
			}

			break;

		/* seek in tables state */
		case 1:
			/* this should not be reached here */
			fail = true;
			break;

		/* handle result state */
		case 2:
			mState = 4;

			mutex_unlock(&mMutex);

			if (start_round<0) 
			{
				/* Found */
				Kraken::getInstance()->queueFragmentRemoval(this, true, result);
				return false;
			}
			else
			{
				Kraken::getInstance()->queueFragmentRemoval(this, false, 0);
				return false;
			}
			break;

		/* GPU partial process state */
		case 3:

			/* We are here because of a partial GPU search */			
			if(!queued)
			{
				if(A5GpuKeySearch(mJobId, result, mKnownPlaintext, mNumRound-1, mNumRound+1, mAdvance, this) >= 0)
				{
					queued = true;
					mState = 2;
				}
			}

			/* search final round with CPU if GPU doesnt support */
			if(!queued)
			{
				if(A5CpuKeySearch(mJobId, result, mKnownPlaintext, mNumRound-1, mNumRound+1, mAdvance, this) >= 0)
				{
					queued = true;
					mState = 2;
				}
			}

			if(!queued) 
			{
				printf(" [E] Failed to queue job.\n");
				mState = 5;
				Kraken::getInstance()->queueFragmentRemoval(this, false, 0);
				mutex_unlock(&mMutex);
				return false;
			}
			break;

		/* finished state */
		case 4:
			/* this should not be reached here */
			fail = true;
			break;

		/* failed state */
		case 5:
			/* this should not be reached here */
			fail = true;
			break;
    } 

	/* reached an invalid state? */
	if(fail)
	{
		mState = 5;
		Kraken::getInstance()->queueFragmentRemoval(this, false, 0);
		mutex_unlock(&mMutex);
		return false;
	}


	mutex_unlock(&mMutex);
	return true;
}
