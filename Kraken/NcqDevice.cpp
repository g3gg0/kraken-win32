
#include "NcqDevice.h"

#include "Kraken.h"
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
	mWait = false;
	mWaiting = false;
	mStartSector = 0;
	mRequestCount = 0;

	mDeviceName = strdup(pzDevNode);
	char *offsetstr = strchr(mDeviceName, '+');

    /* if the device name contains <device>+<number> like \.\\PhysicalDevice+63
     * then use this number as sector offset on the disk.
     */
	if(offsetstr)
	{
		sscanf(offsetstr+1, "%Lu", &mStartSector);
		*offsetstr = '\000';
	}

	printf(" [x] Opening device '%s', start sector %Lu\r\n", mDeviceName, mStartSector);
	sprintf(mDeviceStats, "%s: (no data yet)", mDeviceName);

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
        mMappings[i].cancelled = false;
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
 
void NcqDevice::Request(uint64_t job_id, class NcqRequestor* req, uint64_t blockno)
{
#if 0
    lseek(mDevice, blockno*4096, SEEK_SET );
    size_t r = read(mDevice,mBuffer,4096);
    assert(r==4096);
    req->processBlock(mBuffer);
#else
    sem_wait(&mMutex);
    request_t qr;
	qr.job_id = job_id;
    qr.req = req;
    qr.blockno = blockno;
    mRequests[job_id].push_back(qr);
	mRequestCount++;
    sem_post(&mMutex);
#endif
}

void NcqDevice::Cancel(uint64_t job_id)
{
	sem_wait(&mMutex);

	/* remove all requests for this job id */
	if(mRequests.find(job_id) != mRequests.end())
	{
		mRequestCount -= mRequests[job_id].size(); 
		mRequests[job_id].clear();
		mRequests.erase(job_id);
	}

	/* search for any active transfer with this job id */
	for (int i=0; i<NCQ_REQUESTS; i++) 
	{
        if (mMappings[i].req && mMappings[i].job_id == job_id) 
		{
			mMappings[i].cancelled = true;
		}
	}

	sem_post(&mMutex);
}

void NcqDevice::SpinLock(bool state)
{
	/* lock now */
	if(state)
	{
		mWait = true;

		while(!mWaiting)
		{
			usleep(100);
		}
	}
	else
	{
		mWait = false;
	}
}


char* NcqDevice::GetDeviceStats()
{
	return mDeviceStats;
}


bool NcqDevice::PopRequest(mapRequest_t* job)
{
	sem_wait(&mMutex);

	/* none available */
	if(mRequestCount == 0)
	{
		sem_post(&mMutex);
		return false;
	}

	/* find any pending job */
	map<uint64_t,deque<request_t>>::iterator it = mRequests.begin();

	/* and queue the first request */
	while(it != mRequests.end())
	{
		if(it->second.size() > 0)
		{
			request_t req = it->second.front();
			it->second.pop_front();

			uint64_t offset = mStartSector * 512 + req.blockno * 4096;

			job->job_id = req.job_id;
			job->req = req.req;
            job->blockno = req.blockno;
			job->cancelled = false;
			job->overlapped.Offset = offset & 0xFFFFFFFF;
			job->overlapped.OffsetHigh = (offset >> 32);

			mRequestCount--;

			sem_post(&mMutex);

			return true;
		}
		it++;
	}

    sem_post(&mMutex);
	return false;
}


void NcqDevice::WorkerThread()
{
	bool idle = false;
	struct timeval stopTime;

    while (mRunning)
	{
		bool queued = false;

		usleep(500);

		while(mWait)
		{
			mWaiting = true;
			usleep(0);
		}
		mWaiting = false;

        sem_wait(&mMutex);

        /* schedule requests */
        while ((mFreeMap>=0)&&(mRequestCount>0))
        {
			queued = true;

            int free = mFreeMap;
            mFreeMap = mMappings[free].next_free;

			if(!PopRequest(&mMappings[free]))
			{
				printf("FAIL");
			}

			bool failed = false;
			int errorcode = 0;
#ifdef WIN32
			/* use readfile with overlapped to fire a queued background read */
            BOOL ret = ReadFile(mDevice, mMappings[free].buffer, 4096, NULL, &(mMappings[free].overlapped));
			int err = GetLastError();

			/* ReadFile will fail and the error status is "io pending" */
			if(ret == FALSE && (err != ERROR_IO_PENDING))
			{
				failed = true;
				errorcode = err;
			}
#else
			/* map the block into memory */
            mMappings[free].addr = mmap64(NULL, 4096, PROT_READ, MAP_PRIVATE|MAP_FILE, mDevice, qr.blockno*4096);

			/* any problems? */
			if(mMappings[free].addr == MAP_FAILED)
			{
				failed = true;
				errorcode = errno;
			}
			else
			{
				/* tell the memory manager that we will need that block soon */
				madvise(mMappings[free].addr, 4096, MADV_WILLNEED);
			}
#endif
			if(failed)
			{
		        sem_post(&mMutex);
				mRunning = false;
				printf (" [E] ReadFile on device '%s' failed with code %i.\r\n", mDeviceName, errorcode);
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
				bool finished = false;
				queued = true;
					
#ifdef WIN32
				/* this block was already read? */
				DWORD bytesRead = 0;
				BOOL ret = GetOverlappedResult(mDevice, &(mMappings[i].overlapped), &bytesRead, FALSE);
				
				/* seems so */
                if (ret == TRUE && bytesRead == 4096) 
				{
					finished = true;
				}
#else				
				unsigned char core[4];

				/* check if the page was already loaded */
                mincore(mMappings[i].addr, 4096, core);
                if (core[0] & 0x01) 
				{
					finished = true;
                }
				else
				{
					/* nag again... */
                    madvise(mMappings[i].addr, 4096, MADV_WILLNEED);
				}
#endif
                if (ret == TRUE && bytesRead == 4096) 
				{
					mBlocksRead++;
					if(!mMappings[i].cancelled)
					{
						mMappings[i].req->processBlock(mMappings[i].buffer);
					}
                    mMappings[i].req = NULL;
                    mMappings[i].cancelled = false;
						
#ifndef WIN32
					/* not needed anymore */
                    munmap(mMappings[i].addr, 4096);
#endif

                    /* Add to free list */
                    mMappings[i].next_free = mFreeMap;
                    mFreeMap = i;
                }
            }
        }


		/* was idle but now we have jobs */
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

		if(idle)
		{
			usleep(500);
		}
    }
}
