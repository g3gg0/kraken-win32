#ifndef KRAKEN_H
#define KRAKEN_H

//#define BALANCE_DEVICES

#include "NcqDevice.h"
#include "DeltaLookup.h"
#include "Fragment.h"

#include <vector>
#include <list>
#include <utility>
#include <map>
#include <semaphore.h>
#include <queue>
#include <string>
#include <sys/time.h>
#ifdef WIN32
#include <compat-win32.h>
#endif
#include "ServerCore.h"

using namespace std;

class Kraken {
public:
    Kraken(const char* config, int server_port=0);
    ~Kraken();

	void Shutdown();
    uint64_t Crack(int client, const char* plaintext);
    bool Tick();
	void UnloadTables();
	void LoadTables();

    static Kraken* getInstance() { return mInstance; } 
	void sendJobResult(uint64_t job_id);
	void queueFragmentRemoval(Fragment *frag, bool table_hit, uint64_t result);
	void cancelJobFragments(uint64_t jobId, char *message);

	void deviceSpinLock(bool state);
    bool isUsingAti() {return mUsingAti;}

    void reportFind(uint64_t result, Fragment *frag);
	void sendMessage(char *msg, int client);
    static void serverCmd(int clientID, string cmd);
    void showFragments(void);
	static void *consoleThread(void *arg);
	bool mRunning;

	/* statistics */
	unsigned int mRequests;
	unsigned int mFoundKc;
	unsigned int mFailedKc;
	/* ToDo: */
	double mTotalSearchTime;
	unsigned long mRuntime;

private:
    void removeFragment(Fragment* frag);

    int mNumDevices;
    vector<NcqDevice*> mDevices;
    list< pair<unsigned int, DeltaLookup*> > mTables;
    typedef list< pair<unsigned int, DeltaLookup*> >::iterator tableListIt;
    static Kraken* mInstance;
    sem_t mMutex;
	sem_t mWasteMutex;
    sem_t mConsoleMutex;
	pthread_t mConsoleThread;
	bool mTablesLoaded;	 


    bool mUsingAti;
    bool mBusy;
	bool mJobParallel;
    struct timeval mStartTime;
    struct timeval mLastJobTime;
    ServerCore* mServer;
	int mFoundKeys;

	unsigned int mRequestId;
    map<unsigned int, int> mActiveMap;
    string mTableInfo;

	typedef struct
	{
		uint64_t job_id;
		int client_id;
		string order;
		int max_fragments;
		struct timeval start_time;
		map<Fragment *, int> fragments;
		bool key_found;
		uint8_t key_data[8];
	} t_job;

	map<uint64_t,t_job> mJobs;
    deque<uint64_t> mNewJobs;
	map<Fragment*,int> mWastedFragments;
};


#endif
