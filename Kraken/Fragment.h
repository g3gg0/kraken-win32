#ifndef FRAGMENT_H
#define FRAGMENT_H

/**
 * A class that encapsulates a search fragment
 * a frgment is a test for:
 * 1 known plaintext
 * against:
 * 1 table (id)
 * 1 colour (round)
 **/

#include "NcqDevice.h"
#include "DeltaLookup.h"
#include <stdint.h>

class Fragment : public NcqRequestor {
public:
    Fragment();
    Fragment(uint64_t plaintext, unsigned int round, DeltaLookup* table, unsigned int advance);
    bool processBlock(const void* pDataBlock);

    void setBitPos(size_t pos) {mBitPos=pos;}
    void setRef(int count, int countRef, char *bitsRef, int clientId, uint64_t jobId) 
	{
		mCount=count;
		mCountRef=countRef;
		mBitsRef=bitsRef;
		mClientId=clientId;
		mJobId=jobId;
	}
	
	unsigned int getAdvance() {return mAdvance;}
    size_t getBitPos() {return mBitPos;}
    int getState() {return mState;}
    int getCount() {return mCount;}
    int getCountRef() {return mCountRef;}
    char *getBitsRef() {return mBitsRef;}
    uint64_t getJobNum() {return mJobId;}
    int getClientId() {return mClientId;}
	void cancel();

    virtual bool handleSearchResult(uint64_t result, int start_round);

private:
	uint64_t mJobId;
    uint64_t mKnownPlaintext;
    unsigned int mNumRound;
    unsigned int mAdvance;
    DeltaLookup* mTable;
    size_t mBitPos;
    int mState;
	int mCount;
	int mCountRef;
	char* mBitsRef;
    int mClientId;

    uint64_t mEndpoint;
    uint64_t mBlockStart;
    uint64_t mStartIndex;
};

#endif
