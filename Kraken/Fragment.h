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
    Fragment(uint64_t plaintext, unsigned int round,
                    DeltaLookup* table, unsigned int advance);
    void processBlock(const void* pDataBlock);

    void setBitPos(int pos) {mBitPos=pos;}
    void setRef(int count, int countRef, char *bitsRef, int clientId, int jobId) 
	{
		mCount=count;
		mCountRef=countRef;
		mBitsRef=bitsRef;
		mClientId=clientId;
		mJobNum=jobId;
	}
	
	unsigned int getAdvance() {return mAdvance;}
    int getBitPos() {return mBitPos;}
    int getState() {return mState;}
    int getCount() {return mCount;}
    int getCountRef() {return mCountRef;}
    char *getBitsRef() {return mBitsRef;}
    int getJobNum() {return mJobNum;}
    int getClientId() {return mClientId;}
	void cancel();

	void requeueTransfer();
    void handleSearchResult(uint64_t result, int start_round);

private:
    uint64_t mKnownPlaintext;
    unsigned int mNumRound;
    unsigned int mAdvance;
    DeltaLookup* mTable;
    int mBitPos;
    int mState;
	int mCount;
	int mCountRef;
	char* mBitsRef;
    int mJobNum;
    int mClientId;
	bool mCancelled;

    uint64_t mEndpoint;
    uint64_t mBlockStart;
    uint64_t mStartIndex;
};

#endif
