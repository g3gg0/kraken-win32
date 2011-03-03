#ifndef DELTA_LOOKUP_H
#define DELTA_LOOKUP_H
#include <queue>
#include <string>
#include <stdint.h>
#include "NcqDevice.h"

#define CACHE_MAGIC 0x63661CAC

class DeltaLookup {
public:
    DeltaLookup(NcqDevice *dev, std::string index);
    ~DeltaLookup();

    void setBlockOffset(uint64_t bo) {mBlockOffset=bo;}

    uint64_t StartEndpointSearch(NcqRequestor* req, uint64_t end, uint64_t& blockstart);

    int CompleteEndpointSearch(const void* pDataBlock, uint64_t blockstart,
                               uint64_t endpoint, uint64_t& result);

	void LoadTable();
	void UnloadTable();

private:
    NcqDevice* mDevice;	
	string mIndexFileName;

	bool mTableLoaded;
    int* mBlockIndex;
	uint64_t mBlockIndexSize;
	uint64_t mBlockIndexEntries;

    uint64_t* mPrimaryIndex;
	uint64_t mPrimaryIndexSize;
	uint64_t mPrimaryIndexEntries;


    int64_t mStepSize; // avarage step pr block

    uint64_t mLowEndpoint;
    uint64_t mHighEndpoint;
    uint64_t mBlockOffset;

    static bool mInitStatics;
    static unsigned short mBase[256];
    static unsigned char mBits[256];
};

#endif
