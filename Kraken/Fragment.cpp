
#include "Fragment.h"

#include "Kraken.h"
#include <stdio.h>

#include "../a5_cpu/A5CpuStubs.h"
#include "../a5_ati/A5AtiStubs.h"

#include "Globals.h"

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

Fragment::Fragment(uint64_t plaintext, unsigned int round,
                   DeltaLookup* table, unsigned int advance) :
    mKnownPlaintext(plaintext),
    mNumRound(round),
    mAdvance(advance),
    mTable(table),
    mState(0),
    mEndpoint(0),
    mBlockStart(0),
    mStartIndex(0),
	mCancelled(0)
{
}

void Fragment::cancel()
{
	if(mState == 1)
	{
		mTable->Cancel(this);
	}
	else
	{
		mState = 0;
	}
	mCancelled = true;
}

void Fragment::processBlock(const void* pDataBlock)
{
    int res = mTable->CompleteEndpointSearch(pDataBlock, mBlockStart, mEndpoint, mStartIndex);

    if (res) {
        /* Found endpoint */
        uint64_t search_rev = mStartIndex;
        ApplyIndexFunc(search_rev, 34);
		if (Kraken::getInstance()->isUsingAti()) {
            if (mNumRound) {
                int res = A5AtiSubmitPartial(search_rev, mNumRound, mAdvance, this);
                if (res<0) printf(" [E] Failed to queue A5Ati job.\n");
                mState = 3;
            } else {
                A5CpuKeySearch(search_rev, mKnownPlaintext, 0, mNumRound+1, mAdvance, this);
                mState = 2;
            }
        } else {
            A5CpuKeySearch(search_rev, mKnownPlaintext, 0, mNumRound+1, mAdvance, this);
            mState = 2;
        }
    } else {
        /* not found */
        return Kraken::getInstance()->removeFragment(this);
    }
}

void Fragment::requeueTransfer()
{
	if (mState==1 && !mCancelled) {
        mTable->StartEndpointSearch(this, mEndpoint, mBlockStart);
	}
}

void Fragment::handleSearchResult(uint64_t result, int start_round)
{
    if (mState==0) {
        mEndpoint = result;
        mTable->StartEndpointSearch(this, mEndpoint, mBlockStart);
        mState = 1;
        if (mBlockStart==0ULL) {
            /* Endpoint out of range */
            return Kraken::getInstance()->removeFragment(this);
        }
    } else if (mState==2) {
        if (start_round<0) {
            /* Found */
            Kraken::getInstance()->reportFind(result, this);
        }
        return Kraken::getInstance()->removeFragment(this);
    } else {
        /* We are here because of a partial GPU search */
        /* search final round with CPU */
        A5CpuKeySearch(result, mKnownPlaintext, mNumRound-1, mNumRound+1, mAdvance, this);
        mState = 2;
    }
}
