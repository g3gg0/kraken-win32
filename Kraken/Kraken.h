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
    int Crack(int client, const char* plaintext);
    bool Tick();

    static Kraken* getInstance() { return mInstance; } 
    void removeFragment(Fragment* frag);
	void clearFragments();

    bool isUsingAti() {return mUsingAti;}

    void reportFind(uint64_t result, int bitPos, int count, int countRef, char *bitsRef);
	void sendMessage(char *msg, int client);
    static void serverCmd(int clientID, string cmd);
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
    int mNumDevices;
    vector<NcqDevice*> mDevices;
    list< pair<unsigned int, DeltaLookup*> > mTables;
    typedef list< pair<unsigned int, DeltaLookup*> >::iterator tableListIt;
    map<Fragment*,int> mFragments;
    static Kraken* mInstance;
    sem_t mMutex;
    sem_t mConsoleMutex;
	pthread_t mConsoleThread;

    queue<int> mWorkIds;
    queue<string> mWorkOrders;
    queue<int> mWorkClients;
	int mCurrentId;
    int mCurrentClient;

	queue<uint64_t> mSubmittedStartValue;
	queue<unsigned int> mSubmittedStartRound;
    queue<uint32_t> mSubmittedAdvance;
	queue<void*> mSubmittedContext;

    bool mUsingAti;
    bool mBusy;
    struct timeval mStartTime;
    ServerCore* mServer;
	int mFoundKeys;
	unsigned char mKeyResult[8];

	unsigned int mRequestId;

};


#endif
