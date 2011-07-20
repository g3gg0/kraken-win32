#ifndef NCQ_DEVICE_H
#define NCQ_DEVICE_H

#include <stdint.h>
#include <queue>

#include <pthread.h>
#include <semaphore.h>
#include <list>
#include <sys/time.h>
#ifdef WIN32
#include <windows.h>
#endif
#include <map>
#include <queue>
#include <deque>

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
    virtual bool processBlock(const void* pDataBlock) = 0;
};

typedef struct {
    uint64_t            job_id;
    class NcqRequestor* req;
    uint64_t            blockno;
} request_t;

typedef struct {
    uint64_t            job_id;
    class NcqRequestor* req;
    uint64_t            blockno;
    int                 next_free;
    void*               addr;
	bool				cancelled;
#ifdef WIN32
	OVERLAPPED			overlapped;
	char				buffer[4096];
#endif
} mapRequest_t;


class NcqDevice {
public:
    NcqDevice(const char* pzDevNode);
    ~NcqDevice();

	bool isRunning();
	void SpinLock(bool state);
	uint64_t getMaxBlockNum() { return mMaxBlockNum; }
	char* GetDeviceStats();
	bool PopRequest(mapRequest_t* req);
    void Request(uint64_t job_id, class NcqRequestor*, uint64_t blockno);
    void Cancel(uint64_t job_id);
	void Clear();

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
	
	uint64_t mRequestCount;
	map<uint64_t, deque<request_t> > mRequests;
    int mFreeMap;
    t_mutex mMutex;
	t_mutex mSpinlock;
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
