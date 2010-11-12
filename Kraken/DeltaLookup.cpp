#define _FILE_OFFSET_BITS 64

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
    /* Load index - compress to ~41MB of alloced memory */
    FILE* fd = fopen(index.c_str(),"rb");

	/* throw proper error messages */
    if (fd == NULL) {
        printf("(%s:%i) Could not open %s for reading.\n", __FILE__, __LINE__, index.c_str());
		return;
    }

	/* get index file size first */
    fseek(fd ,0 ,SEEK_END );
    long size = ftell(fd);
    unsigned int num = (size / sizeof(uint64_t))-1;
    fseek(fd ,0 ,SEEK_SET );

	/* then allocate structures according to that size */
    mBlockIndex = (int*)malloc((num+1)*sizeof(int));
    mPrimaryIndex = (uint64_t*)malloc(((num/256)+1)*sizeof(int64_t));
	size_t alloced = (num+1)*sizeof(int)+((num/256)+1)*sizeof(int64_t);

	/* throw proper error messages */
	if (mBlockIndex==NULL || mPrimaryIndex==NULL) {
		printf("(%s:%i) Failed allocating memory. (errno=%i)\r\n", __FILE__, __LINE__, errno);
		return;
	}

	/* try to load cache file */
	bool cached = false;
	string cachefile = index + ".cache";
    FILE* cachefd = fopen(cachefile.c_str(),"rb");

	/* was there one? */
    if (cachefd != 0) {
		uint32_t magic = 0;
		long cachedSize = 0;

		fread(&magic, sizeof(uint32_t), 1, cachefd);
		fread(&cachedSize, sizeof(long), 1, cachefd);

		/* check if stored magic and index file size match */
		if(magic == mCacheMagic && size == cachedSize)
		{
			/* they match, so load the already prepared data */
			fread(&mNumBlocks, sizeof(int), 1, cachefd);
			fread(&mStepSize, sizeof(int64_t), 1, cachefd);
			fread(&mLowEndpoint, sizeof(uint64_t), 1, cachefd);
			fread(&mHighEndpoint, sizeof(uint64_t), 1, cachefd);
			fread(mBlockIndex, (num+1)*sizeof(int), 1, cachefd);
			fread(mPrimaryIndex, (num/256+1)*sizeof(int64_t), 1, cachefd);
			cached = true;
		}
		fclose(cachefd);
    }
	
	/* either no cache file available or loading failed */
	if(!cached) {
		uint64_t *buffer = (uint64_t*)malloc(size);
		mNumBlocks = num;

		if (buffer == NULL) {
			printf("(%s:%i) Failed allocating memory\r\n", __FILE__, __LINE__);
			return;
		}

		uint64_t end;
		mStepSize = 0xfffffffffffffLL/(num+1);
		int64_t min = 0;
		int64_t max = 0;
		int64_t last = 0;

		/* read the whole file at once to speed up loading */
		size_t read_blocks = fread(buffer,size,1,fd);

		if(read_blocks != 1) {
			printf("(%s:%i) Failed reading index\r\n", __FILE__, __LINE__);
			return;
		}

		for(int bl=0; bl<num; bl++) {
			end = buffer[bl];
			int64_t offset = (end>>12)-last-mStepSize;
			last = end>>12;
			if (offset>max) max = offset;
			if (offset<min) min = offset;

			if(offset>=0x7fffffff || offset<=-0x7fffffff) {
				printf("(%s:%i) Invalid index file\r\n", __FILE__, __LINE__);
				return;
			}

			mBlockIndex[bl]=offset;
			if ((bl&0xff)==0) {
				mPrimaryIndex[bl>>8]=end;
			}
		}
		mBlockIndex[num] = 0x7fffffff; /* for detecting last index */
		printf(" \\_min: %llx, max: %llx, index: %llx, alloc:%i\r\n", min,max,mPrimaryIndex[1],alloced);

		mLowEndpoint = mPrimaryIndex[0];
		mHighEndpoint = buffer[num];
		free(buffer);

		/* save to cache if possible */
		cachefd = fopen(cachefile.c_str(),"wb");
		if (cachefd != 0) {
			fwrite(&mCacheMagic, sizeof(uint32_t), 1, cachefd);
			fwrite(&size, sizeof(long), 1, cachefd);
			fwrite(&mNumBlocks, sizeof(int), 1, cachefd);
			fwrite(&mStepSize, sizeof(int64_t), 1, cachefd);
			fwrite(&mLowEndpoint, sizeof(uint64_t), 1, cachefd);
			fwrite(&mHighEndpoint, sizeof(uint64_t), 1, cachefd);
			fwrite(mBlockIndex, (num+1)*sizeof(int), 1, cachefd);
			fwrite(mPrimaryIndex, (num/256+1)*sizeof(int64_t), 1, cachefd);
			fclose(cachefd);
		}
	}

    fclose(fd);
    mBlockOffset=0ULL;

    mDevice = dev;

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
}

DeltaLookup::~DeltaLookup()
{
    delete [] mBlockIndex;
    delete [] mPrimaryIndex;
}

#define DEBUG_PRINT 0

uint64_t DeltaLookup::StartEndpointSearch(NcqRequestor* req, uint64_t end, uint64_t& blockstart)
{
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
    bl = bl *256;
    uint64_t delta = (mStepSize + mBlockIndex[bl+1])<<12;
    while((here+delta)<=end) {
        here+=delta;
        bl++;
        count++;
        delta = (mStepSize + mBlockIndex[bl+1])<<12;
    }

#if DEBUG_PRINT
    printf("%i block (%i)\n", bl, count);
#endif

    blockstart = here; /* set first in case of sync loading */
    mDevice->Request(req, (uint64_t)bl+mBlockOffset );
    return here;
}

int DeltaLookup::CompleteEndpointSearch(const void* pDataBlock, uint64_t here,
                                        uint64_t end, uint64_t& result)
{
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

