
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

#include "Globals.h"


NcqDevice::NcqDevice(const char* pzDevNode)
{
    mRunning = false;
	mPaused = false;
	mStartSector = 0;

	mDeviceName = strdup(pzDevNode);
	char *offsetstr = strchr(mDeviceName, '+');

	strcpy(mDeviceStats, "(idle)");

    /* if the device name contains <device>+<number> like \.\\PhysicalDevice+63
     * then use this number as sector offset on the disk.
     */
	if(offsetstr)
	{
		sscanf(offsetstr+1, "%Lu", &mStartSector);
		*offsetstr = '\000';
	}

	printf(" [x] Opening device '%s', start sector %Lu\r\n", mDeviceName, mStartSector);

	uint64_t diskSize = 0;

#ifdef WIN32
	mDevice = CreateFileA(mDeviceName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_RANDOM_ACCESS, NULL);

	if(mDevice == NULL)
	{
		printf(" [E] Failed to open data disk '%s'\r\n", pzDevNode);
		return;
	}

	/* check if user has read access. 
	   open a new handle here since the mDevice handle allows asynchronous access only.
	 */
	char tstBuf[4096];
	DWORD tstRead = 0;
	HANDLE tstHandle = CreateFileA(mDeviceName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
	BOOL tstRet = ReadFile(tstHandle, tstBuf, 4096, &tstRead, NULL);
	DISK_GEOMETRY pdg;
	DWORD junk;

	/* get disk size */
 	if(!DeviceIoControl(tstHandle,IOCTL_DISK_GET_DRIVE_GEOMETRY,NULL, 0, &pdg, sizeof(pdg), &junk, (LPOVERLAPPED) NULL))
	{
		CloseHandle(mDevice);
		printf(" [E] Failed to size of data disk '%s'.\r\n", pzDevNode);
		printf(" [E] Maybe you are not Administrator or have no administrative rights?\r\n");
		return;
	}
	diskSize = pdg.Cylinders.QuadPart * (ULONG)pdg.TracksPerCylinder * (ULONG)pdg.SectorsPerTrack * (ULONG)pdg.BytesPerSector;

	CloseHandle(tstHandle);

	if(tstRet == FALSE || tstRead != 4096)
	{
		CloseHandle(mDevice);
		printf(" [E] Failed to read from data disk '%s'.\r\n", pzDevNode);
		printf(" [E] Maybe you are not Administrator or have no administrative rights?\r\n");
		return;
	}

	/* calc max block number */
	mMaxBlockNum = (diskSize - (mStartSector * 512)) / 4096;
#else
    mDevice = open(mDeviceName,O_RDONLY/*|O_BINARY*/);

	if(mDevice<0)
	{
		printf(" [E] Failed to open data disk '%s'\r\n", pzDevNode);
		return;
	}
	
	/* todo */
	mMaxBlockNum = diskSize / 4096;
#endif

    /* Set up free list */
    for (int i=0; i<NCQ_REQUESTS; i++) {
        mMappings[i].req = NULL;
        mMappings[i].addr = NULL;  /* idle */
        mMappings[i].cancel = false;
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
    sem_init( &mSpinlock, 0, 1 );

    /* Start worker thread */
	mBlocksRead = 0;
	mReadTime = 0;
    mRunning = true;
    mDevC = pzDevNode[strlen(pzDevNode)-2];

	pthread_create(&mWorker, NULL, thread_stub, (void*)this);
}


bool NcqDevice::isRunning()
{
	return mRunning;
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

void NcqDevice::Pause()
{
	sem_wait(&mMutex);
	mPaused = true;
	sem_post(&mMutex);
}

void NcqDevice::Unpause()
{
	mPaused = false;
}

/* clear all pending requests */
void NcqDevice::Clear()
{
    sem_wait(&mMutex);
	while(mRequests.size() > 0)
	{
		mRequests.pop();
	}
	
    for (int i=0; i<NCQ_REQUESTS; i++) 
	{
		mMappings[i].cancel = true;
	}
    sem_post(&mMutex);
}


char* NcqDevice::GetDeviceStats()
{
	return mDeviceStats;
}

void NcqDevice::WorkerThread()
{
#ifdef WIN32
	bool idle = false;
	struct timeval stopTime;

    while (mRunning)
	{
		bool queued = false;
		do
		{
			usleep(500);
		} while(mPaused);

        sem_wait(&mMutex);

        /* schedule requests */
        while ((mFreeMap>=0)&&(mRequests.size()>0))
        {
			queued = true;

            int free = mFreeMap;
            mFreeMap = mMappings[free].next_free;
            request_t qr = mRequests.front();
            mRequests.pop();

			uint64_t offset = mStartSector * 512 + qr.blockno * 4096;

            mMappings[free].req = qr.req;
            mMappings[free].blockno = qr.blockno;
			mMappings[free].cancel = false;
			mMappings[free].overlapped.Offset = offset & 0xFFFFFFFF;
			mMappings[free].overlapped.OffsetHigh = (offset >> 32);

            BOOL ret = ReadFile(mDevice, mMappings[free].buffer, 4096, NULL, &(mMappings[free].overlapped));

			int err = GetLastError();
			if(ret == FALSE && (err != ERROR_IO_PENDING))
			{
		        sem_post(&mMutex);
				mRunning = false;
				printf (" [E] ReadFile on device '%s' failed with code %i.\r\n", mDeviceName, err);
				printf (" [E] Aborting reader thread. No further processing possible.\r\n");
				return;
			}
        }

        sem_post(&mMutex);

        /* Check requests */
        for (int i=0; i<NCQ_REQUESTS; i++) 
		{
            if (mMappings[i].req) 
			{
				queued = true;
				DWORD bytesRead = 0;
				BOOL ret = GetOverlappedResult(mDevice, &(mMappings[i].overlapped), &bytesRead, FALSE);

                if (ret == TRUE && bytesRead == 4096) 
				{
					mBlocksRead++;
					if(!mMappings[i].cancel)
					{
						mMappings[i].req->processBlock(mMappings[i].buffer);
					}
                    mMappings[i].req = NULL;
                    mMappings[i].cancel = false;

                    /* Add to free list */
                    //sem_wait(&mMutex);
                    mMappings[i].next_free = mFreeMap;
                    mFreeMap = i;
                    //sem_post(&mMutex);
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
				float avgAccessTime = ((float)seconds / (float)mBlocksRead) * 1000.0f;

				sprintf(mDeviceStats, "%s: %iblks, %iKiB, %is, %iKiB/s, %0.2fms", mDeviceName, (int)mBlocksRead, (int)kBytes, (int)seconds, (int)rate, avgAccessTime);			
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
