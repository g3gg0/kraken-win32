#define _FILE_OFFSET_BITS 64

#include "NcqDevice.h"
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#ifdef WIN32
#include <compat-win32.h>
#else
#include <sys/mman.h>
#endif
#include <stdio.h>
#include <string.h>

NcqDevice::NcqDevice(const char* pzDevNode)
{
	mStartSector = 0;

	mDeviceName = strdup(pzDevNode);
	char *offsetstr = strchr(mDeviceName, '+');

    /* if the device name contains <device>+<number> like \.\\PhysicalDevice+63
     * then use this number as sector offset on the disk.
     */
	if(offsetstr)
	{
		sscanf(offsetstr+1, "%lu", &mStartSector);
		*offsetstr = '\000';
	}

	printf("Opening device '%s', start sector %i\r\n", mDeviceName, mStartSector);

#ifdef WIN32
	mDevice = CreateFileA(mDeviceName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_RANDOM_ACCESS, NULL);

	if(mDevice == NULL)
	{
		printf("(%s:%i) Failed to open data disk '%s'\r\n", __FILE__, __LINE__, pzDevNode);
		return;
	}
#else
    mDevice = open(mDeviceName,O_RDONLY|O_BINARY);

	if(mDevice<0)
	{
		printf("(%s:%i) Failed to open data disk '%s'\r\n", __FILE__, __LINE__, pzDevNode);
		return;
	}
#endif

    /* Set up free list */
    for (int i=0; i<NCQ_REQUESTS; i++) {
        mMappings[i].req = NULL;
        mMappings[i].addr = NULL;  /* idle */
        mMappings[i].next_free = i - 1;
#ifdef WIN32
		mMappings[i].overlapped.Offset = 0; 
		mMappings[i].overlapped.OffsetHigh = 0; 
		mMappings[i].overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
#endif
    }
    mFreeMap = 31;

    /* Init semaphore */
    sem_init( &mMutex, 0, 1 );

    /* Start worker thread */
	mBlocksRead = 0;
	mReadTime = 0;
    mRunning = true;
    mDevC = pzDevNode[strlen(pzDevNode)-2];

	pthread_create(&mWorker, NULL, thread_stub, (void*)this);
}

    
NcqDevice::~NcqDevice()
{
    mRunning = false;
	pthread_join(mWorker, NULL);
    
#ifdef WIN32
	CloseHandle(mDevice);
#else
    close(mDevice);
#endif
    sem_destroy(&mMutex);
}

void* NcqDevice::thread_stub(void* arg)
{
    if (arg) {
        NcqDevice* nd = (NcqDevice*)arg;
        nd->WorkerThread();
    }
    return NULL;
}
 
void NcqDevice::Request(class NcqRequestor* req, uint64_t blockno)
{
#if 0
    lseek(mDevice, blockno*4096, SEEK_SET );
    size_t r = read(mDevice,mBuffer,4096);
    assert(r==4096);
    req->processBlock(mBuffer);
#else
    sem_wait(&mMutex);
    request_t qr;
    qr.req = req;
    qr.blockno = blockno;
    mRequests.push(qr);
    sem_post(&mMutex);
#endif
}

void NcqDevice::Cancel(class NcqRequestor*)
{
}

/* clear all pending requests */
void NcqDevice::Clear()
{
    sem_wait(&mMutex);
	while(mRequests.size() > 0)
	{
		mRequests.pop();
	}
    sem_post(&mMutex);
}

void NcqDevice::WorkerThread()
{
#ifdef WIN32
	bool idle = false;
	char msg[128];
	struct timeval stopTime;

    while (mRunning)
	{
		bool queued = false;
        usleep(500);

        sem_wait(&mMutex);

        /* schedule requests */
        while ((mFreeMap>=0)&&(mRequests.size()>0))
        {
			queued = true;

            int free = mFreeMap;
            mFreeMap = mMappings[free].next_free;
            request_t qr = mRequests.front();
            mRequests.pop();
            mMappings[free].req = qr.req;
            mMappings[free].blockno = qr.blockno;

			uint64_t offset = mStartSector * 512 + qr.blockno * 4096;
			mMappings[free].overlapped.Offset = offset & 0xFFFFFFFF;
			mMappings[free].overlapped.OffsetHigh = (offset >> 32);

            bool ret = ReadFile(mDevice, mMappings[free].buffer, 4096, NULL, &(mMappings[free].overlapped));

			int err = GetLastError();
			if(!ret && (err != ERROR_IO_PENDING))
			{
				printf ("ReadFile failed with code %i. Aborting reader thread.\r\n", err);
				return;
			}
        }
        sem_post(&mMutex);

        /* Check request */
        for (int i=0; i<NCQ_REQUESTS; i++) 
		{
            if (mMappings[i].req) 
			{
				queued = true;
				DWORD bytesRead = 0;
				bool ret = GetOverlappedResult(mDevice, &(mMappings[i].overlapped), &bytesRead, FALSE);

                if (ret == true && bytesRead == 4096) 
				{
					mBlocksRead++;
                    mMappings[i].req->processBlock(mMappings[i].buffer);
                    mMappings[i].req = NULL;

                    /* Add to free list */
                    sem_wait(&mMutex);
                    mMappings[i].next_free = mFreeMap;
                    mFreeMap = i;
                    sem_post(&mMutex);
                }
            }
        }

		/* was idle but now have jobs */
		if(idle && queued)
		{
			gettimeofday(&mStartTime, NULL);
			idle = false;
		}

		/* no jobs anymore. dump and reset block statistics */
		if(!idle && !queued)
		{
			gettimeofday(&stopTime, NULL);
			uint64_t diff = 1000000 * (stopTime.tv_sec - mStartTime.tv_sec);
			diff += stopTime.tv_usec - mStartTime.tv_usec;

			/* dont flood with small reads */
			if(diff > 0 && mBlocksRead > 10)
			{
				/* one block has 4KiB. scale to seconds */
				float kBytes = mBlocksRead * 4.0f;
				float seconds = diff / 1000000.0f;
				float rate = kBytes / seconds;
				float avgDelayMs = (mReadTime / mBlocksRead) * 1000.0f;

				sprintf(mDeviceStats, "(%s: %i Kib in %i seconds, thats %i KiB/s, served %i reads)\r\n", mDeviceName, (int)kBytes, (int)seconds, (int)rate, (int)mBlocksRead);
				printf(mDeviceStats);
			}

			mBlocksRead = 0;
			idle = true;
		}
    }
#else
    unsigned char core[4];

    while (mRunning) {
        usleep(500);
        sem_wait(&mMutex);
        /* schedule requests */
        while ((mFreeMap>=0)&&(mRequests.size()>0))
        {
            int free = mFreeMap;
            mFreeMap = mMappings[free].next_free;
            request_t qr = mRequests.front();
            mRequests.pop();
            mMappings[free].req = qr.req;
            mMappings[free].blockno = qr.blockno;
            mMappings[free].addr = mmap64(NULL, 4096, PROT_READ,
                                          MAP_PRIVATE|MAP_FILE, mDevice,
                                          qr.blockno*4096);
            // printf("Mapped %p %lli\n", mMappings[free].addr, qr.blockno);
            madvise(mMappings[free].addr,4096,MADV_WILLNEED);
        }
        sem_post(&mMutex);

        /* Check request */
        for (int i=0; i<NCQ_REQUESTS; i++) {
            if (mMappings[i].addr) {
                mincore(mMappings[i].addr,4096,core);
                if (core[0]&0x01) {
                    // Debug disk access
                    // printf("%c",mDevC);
                    // fflush(stdout);
                    /* mapped & ready for use */
                    mMappings[i].req->processBlock(mMappings[i].addr);
                    munmap(mMappings[i].addr,4096);
                    mMappings[i].addr = NULL;
                    /* Add to free list */
                    sem_wait(&mMutex);
                    mMappings[i].next_free = mFreeMap;
                    mFreeMap = i;
                    sem_post(&mMutex);
                } else {
                    madvise(mMappings[i].addr,4096,MADV_WILLNEED);
                }
            }
        }
    }
#endif
}



