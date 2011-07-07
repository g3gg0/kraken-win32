/***********************************************
 *
 * Simple single threaded servercore
 *
 **********************************************/

#include <semaphore.h>
#include <string>
#include <pthread.h>
#include <map>
#include "Globals.h"

#include <queue>

using namespace std;

typedef void (*dispatch)(int, std::string);

class ClientConnection {
public:
    ClientConnection(int);
    ~ClientConnection();

    int getFd() {return mFd;}
    int Write(string dat);
    int Read(string& data);

private:
    int mFd;
    string mBuffer;
};


class ServerCore {
public:
    ServerCore();
    ServerCore(int,dispatch);
    ~ServerCore();

    bool Write(int, string);
    void Broadcast(string);
	void Shutdown();

private:
    void Serve();
	void ProcessMessageQueue();

    bool mRunning;
    static void* thread_stub(void* arg);
    pthread_t mThread;
    dispatch mDispatch;

    int mListener;
    unsigned int mClientCount;
    map<unsigned int, ClientConnection*> mClientMap;

    queue<string> mMessageQueue;
    queue<int> mClientQueue;

    t_mutex mQueueMutex;
    t_mutex mMutex;
};

