#ifndef NCQ_DEVICE_H
#define NCQ_DEVICE_H

#include <stdint.h>
#include <queue>
#include <semaphore.h>
#include <pthread.h>
#include <list>
#include <sys/time.h>
#ifdef WIN32
#include <windows.h>
#endif

#define KB *(1024ULL)
#define MB *(1024ULL KB)
#define GB *(1024ULL MB)
#define TB *(1024ULL GB)

#define NCQ_REQUESTS      64
using namespace std;

class NcqDevice;

class NcqRequestor {
public:
    NcqRequestor() {};
    virtual ~NcqRequestor() {};

private:
    friend class NcqDevice;
    virtual void processBlock(const void* pDataBlock) = 0;
};

class NcqDevice {
public:
    NcqDevice(const char* pzDevNode);
    ~NcqDevice();

	bool isRunning();
	void SpinLock(bool state);
	uint64_t getMaxBlockNum() { return mMaxBlockNum; }
	char* GetDeviceStats();
    void Request(class NcqRequestor*, uint64_t blockno);
    void Cancel(class NcqRequestor*);
	void Clear();

    typedef struct {
        class NcqRequestor* req;
        uint64_t            blockno;
    } request_t;

    typedef struct {
        class NcqRequestor* req;
        uint64_t            blockno;
        int                 next_free;
        void*               addr;
		bool				cancel;
#ifdef WIN32
		OVERLAPPED			overlapped;
		char				buffer[4096];
#endif
    } mapRequest_t;

    static void* thread_stub(void* arg);
    void WorkerThread();

private:

#ifdef WIN32
	HANDLE mDevice;
#else
    int mDevice;
#endif
	char *mDeviceName;
	char mDeviceStats[512];
	uint64_t mStartSector;
	uint64_t mMaxBlockNum;
    unsigned char mBuffer[4096];
    mapRequest_t mMappings[NCQ_REQUESTS];
    queue< request_t > mRequests;
    queue< request_t > mCancelledRequests;
    int mFreeMap;
    sem_t mMutex;
	sem_t mSpinlock;
    pthread_t mWorker;

	struct timeval mStartTime;
	uint64_t mBlocksRead;
	float mReadTime;

    bool mRunning;
	bool mWait;
	bool mWaiting;
	bool mPaused;
    char mDevC;
};


#endif
