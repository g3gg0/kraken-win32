/***********************************************
 *
 * Simple single threaded servercore
 *
 **********************************************/

#include <string>
#include <pthread.h>
#include <map>
#include <semaphore.h>
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
    ServerCore(int,dispatch);
    ~ServerCore();

    void Write(int, string);
    void Broadcast(string);

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

    sem_t mQueueMutex;
    sem_t mMutex;
};

