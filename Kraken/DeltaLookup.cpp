
#include "DeltaLookup.h"

#include "NcqDevice.h"
#include <iostream>
#include <stdio.h>
#include <list>
#include <sys/time.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stropts.h>

#include "Globals.h"

#define READ8()\
    bits = (mBitBuffer>>(mBitCount-8))&0xff;                 \
    mBitBuffer = (mBitBuffer<<8)|pBuffer[mBufPos++];

#define READ8safe()\
    bits = (mBitBuffer>>(mBitCount-8))&0xff;                 \
    if (mBufPos<4096) {                                      \
        mBitBuffer = (mBitBuffer<<8)|pBuffer[mBufPos++];     \
    }

#define READN(n)\
    bits = mBitBuffer>>(mBitCount-(n));         \
    bits = bits & ((1<<(n))-1);                 \
    mBitCount-=(n);                             \
    if(mBitCount<8) { \
        mBitBuffer = (mBitBuffer<<8)|pBuffer[mBufPos++];    \
        mBitCount+=8; \
    } 


bool DeltaLookup::mInitStatics = false;
unsigned short DeltaLookup::mBase[256];
unsigned char DeltaLookup::mBits[256];

DeltaLookup::DeltaLookup(NcqDevice* dev, std::string index)
{
	mIndexFileName = index;
    mDevice = dev;
    mBlockOffset = 0ULL;

	LoadTable();
}

void DeltaLookup::UnloadTable()
{
    if(mPrimaryIndex)
	{
		free(mPrimaryIndex);
	}
	
    if(mBlockIndex)
	{
		free(mBlockIndex);
	}

    mPrimaryIndex = NULL;
    mBlockIndex = NULL;
}

void DeltaLookup::LoadTable()
{
    /* Load index - compress to ~41MB of alloced memory */
    FILE* indexfd = fopen(mIndexFileName.c_str(),"rb");

	/* throw proper error messages */
    if (indexfd == NULL) {
        printf("(%s:%i) Could not open %s for reading.\n", __FILE__, __LINE__, mIndexFileName.c_str());
		return;
    }

	/* get index file size first */
    fseek(indexfd ,0 ,SEEK_END );
    uint64_t index_file_size = (uint64_t)ftell(indexfd);
    uint64_t index_file_entries = (index_file_size / sizeof(uint64_t))-1;
    fseek(indexfd ,0 ,SEEK_SET );

	/* then allocate structures according to that size */
    mBlockIndexEntries = index_file_entries + 1;
    mBlockIndexSize = mBlockIndexEntries * sizeof(int);
    mBlockIndex = (int*)malloc((size_t) mBlockIndexSize);
	mBlockIndex[mBlockIndexEntries-1] = 0;

	mPrimaryIndexEntries = (index_file_entries/256) + 1;
    mPrimaryIndexSize = mPrimaryIndexEntries * sizeof(int64_t);
    mPrimaryIndex = (uint64_t*)malloc((size_t) mPrimaryIndexSize);
	mPrimaryIndex[mPrimaryIndexEntries-1] = 0;

	size_t alloced = (size_t) (mBlockIndexSize + mPrimaryIndexSize);

	/* throw proper error messages */
	if (mBlockIndex==NULL || mPrimaryIndex==NULL) {
		printf("(%s:%i) Failed allocating memory.\r\n", __FILE__, __LINE__);
		return;
	}

	/* try to load cache file */
	bool cached = false;
	string cachefile = mIndexFileName + ".cache";
    FILE* cachefd = fopen(cachefile.c_str(),"rb");

	/* was there one? */
    if (cachefd != 0) {
		uint32_t magic = 0;
		uint32_t spare = 0;
		uint64_t cachedSize = 0;

		fread(&magic, sizeof(uint32_t), 1, cachefd);
		fread(&cachedSize, sizeof(uint64_t), 1, cachefd);

		/* check if stored magic and index file size match */
		if(magic == CACHE_MAGIC && index_file_size == cachedSize)
		{
			/* they match, so load the already prepared data */
			fread(&spare, sizeof(uint32_t), 1, cachefd);
			fread(&mStepSize, sizeof(int64_t), 1, cachefd);
			fread(&mLowEndpoint, sizeof(uint64_t), 1, cachefd);
			fread(&mHighEndpoint, sizeof(uint64_t), 1, cachefd);
			fread(mBlockIndex, (size_t) mBlockIndexSize, 1, cachefd);
			fread(mPrimaryIndex, (size_t) mPrimaryIndexSize, 1, cachefd);
			cached = true;
		}
		fclose(cachefd);
    }
	
	/* either no cache file available or loading failed */
	if(!cached) {
		printf("(caching)");
		fflush(stdout);

		uint64_t *buffer = (uint64_t*)malloc((size_t) index_file_size);

		if (buffer == NULL) {
			printf("(%s:%i) Failed allocating memory\r\n", __FILE__, __LINE__);
			return;
		}

		mStepSize = 0xfffffffffffffLL/(index_file_entries+1);

		uint64_t end;
		int64_t min = 0;
		int64_t max = 0;
		int64_t last = 0;

		/* read the whole file at once to speed up loading */
		size_t read_blocks = fread(buffer,(size_t) index_file_size,1,indexfd);

		if(read_blocks != 1) {
			printf("(%s:%i) Failed reading index file\r\n", __FILE__, __LINE__);
			return;
		}

		for(unsigned int bl=0; bl<index_file_entries; bl++) {
			end = buffer[bl];
			int64_t offset = (end>>12)-last-mStepSize;
			last = end>>12;
			if (offset>max) max = offset;
			if (offset<min) min = offset;

			if(offset>=0x7fffffff || offset<=-0x7fffffff) {
				printf("(%s:%i) Invalid index file\r\n", __FILE__, __LINE__);
				return;
			}

			mBlockIndex[bl] = offset & 0xFFFFFFFF;
			if ((bl&0xff)==0) {
				mPrimaryIndex[bl>>8]=end;
			}
		}
		mBlockIndex[index_file_entries] = 0x7fffffff; /* for detecting last index */
		//printf(" \\_min: %llx, max: %llx, index: %llx, alloc:%i\r\n", min,max,mPrimaryIndex[1],alloced);

		mLowEndpoint = mPrimaryIndex[0];
		mHighEndpoint = buffer[index_file_entries];
		free(buffer);

		/* save to cache if possible */
		cachefd = fopen(cachefile.c_str(),"wb");
		if (cachefd != 0) {
			uint32_t magic = CACHE_MAGIC;
			uint32_t spare = 0;
			
			fwrite(&magic, sizeof(uint32_t), 1, cachefd);
			fwrite(&index_file_size, sizeof(uint64_t), 1, cachefd);
			fwrite(&spare, sizeof(uint32_t), 1, cachefd);
			fwrite(&mStepSize, sizeof(int64_t), 1, cachefd);
			fwrite(&mLowEndpoint, sizeof(uint64_t), 1, cachefd);
			fwrite(&mHighEndpoint, sizeof(uint64_t), 1, cachefd);
			fwrite(mBlockIndex, (size_t) mBlockIndexSize, 1, cachefd);
			fwrite(mPrimaryIndex, (size_t) mPrimaryIndexSize, 1, cachefd);
			fclose(cachefd);
		}
	}

    fclose(indexfd);


    if (!mInitStatics) {
        /* Fill in decoding tables */
        int groups[] = {0,4,126,62,32,16,8,4,2,1};
        int gsize = 1;
        unsigned short base = 0;
        int group = 0;
        for (int i=0;i<10;i++) {
            for (int j=0; j<groups[i]; j++) {
                mBase[group] = base;
                mBits[group] = i;
                base += gsize;
                group++;
            }
            gsize = 2 * gsize;
        }
        /* The rest should be unused */
        assert(group<256);
        mInitStatics = true;
    }

	mTableLoaded = true;
}

DeltaLookup::~DeltaLookup()
{
	UnloadTable();
}

void DeltaLookup::Cancel(uint64_t job_id)
{
	mDevice->Cancel(job_id);
}

#define DEBUG_PRINT 0

uint64_t DeltaLookup::StartEndpointSearch(uint64_t job_id, NcqRequestor* req, uint64_t end, uint64_t& blockstart)
{
	if(!mTableLoaded)
	{
		return 0;
	}

    if (end<mLowEndpoint) return 0ULL;
    if (end>mHighEndpoint) return 0ULL;

    uint64_t bid = (end>>12) / mStepSize;
    unsigned int bl = ((unsigned int)bid)/256;

    /* Methinks the division has been done by float, and may 
     * have less precision than required
     */
    while (bl && (mPrimaryIndex[bl]>end)) bl--;

    uint64_t here = mPrimaryIndex[bl];
    int count = 0;
    bl <<= 8;

	/* check index variable before entering the loop... */
	if(bl+1 >= mBlockIndexEntries)
	{
		return 0ULL;
	}

    uint64_t delta = (mStepSize + mBlockIndex[bl+1])<<12;
    while((here+delta)<=end) {
        here+=delta;
        bl++;
        count++;

		/* ... and make sure the index does not get larger than the array is */
		if(bl+1 >= mBlockIndexEntries)
		{
			return 0ULL;
		}
        delta = (mStepSize + mBlockIndex[bl+1])<<12;
    }

#if DEBUG_PRINT
    printf("%i block (%i)\n", bl, count);
#endif

    blockstart = here; /* set first in case of sync loading */
    mDevice->Request(job_id, req, (uint64_t)bl+mBlockOffset );
    return here;
}

int DeltaLookup::CompleteEndpointSearch(const void* pDataBlock, uint64_t here,
                                        uint64_t end, uint64_t& result)
{
	if(!mTableLoaded)
	{
		return 0;
	}

    const unsigned char* pBuffer = (const unsigned char*)pDataBlock;
    unsigned int mBufPos = 0;
    unsigned int mBitBuffer = pBuffer[mBufPos++];
    unsigned int mBitCount = 8;
    unsigned char bits;
    uint64_t index;
    uint64_t tmp;
    uint64_t delta;

    /* read generating index for first chain in block */
    READ8();
    tmp = bits;
    READ8();
    tmp = (tmp<<8)|bits;
    READ8();
    tmp = (tmp<<8)|bits;
    READ8();
    tmp = (tmp<<8)|bits;
    READN(2);
    tmp = (tmp<<2)|bits;

#if DEBUG_PRINT
    printf("%llx %llx\n", here, tmp);
#endif

    if (here==end) {
        result = tmp;
        return 1;
    }

    for(;;) {
        int available = (4096-mBufPos)*8 + mBitCount;
        if (available<51) {
#if DEBUG_PRINT
            printf("End of block (%i bits left)\n", available);
#endif
            break;
        }
        READ8();
        if (bits==0xff) {
            if (available<72) {
#if DEBUG_PRINT
                printf("End of block (%i bits left)\n", available);
#endif
                break;
            }
            /* Escape code */
            READ8();
            tmp = bits;
            READ8();
            tmp = (tmp<<8)|bits;
            READ8();
            tmp = (tmp<<8)|bits;
            READ8();
            tmp = (tmp<<8)|bits;
            READ8();
            tmp = (tmp<<8)|bits;
            READ8();
            tmp = (tmp<<8)|bits;
            READ8();
            tmp = (tmp<<8)|bits;
            READ8safe();
            tmp = (tmp<<8)|bits;
            delta = tmp >> 34;
            index = tmp & 0x3ffffffffULL;
        } else {
            unsigned int code = bits;
            unsigned int rb = mBits[code];
            // printf("%02x - %i - %x ",code,rb,mBase[code]);
            delta = mBase[code];
            unsigned int d2 = 0;
            if (rb>=8) {
                READ8();
                d2 = bits;
                rb-=8;
            }
            if (rb) {
                READN(rb);
                d2 = (d2<<rb)|bits;
            }
            // printf("%llx %x\n",delta,d2);
            delta+=d2;
            READ8();
            delta = (delta<<8)|bits;
            READN(2);
            delta = (delta<<2)|bits;

            READN(1);
            uint64_t idx = bits;
            READ8();
            idx = (idx<<8)|bits;
            READ8();
            idx = (idx<<8)|bits;
            READ8();
            idx = (idx<<8)|bits;
            READ8safe();
            index = (idx<<8)|bits;
        }
        here += delta<<12;
#if DEBUG_PRINT
        printf("%llx %llx\n", here, index);
#endif
        if (here==end) {
           result = index;
           return 1; 
        }

        if (here>end) {
#if DEBUG_PRINT
            printf("passed: %llx %llx\n", here, end);
#endif
            break;
        }
    }

    return 0;
}

