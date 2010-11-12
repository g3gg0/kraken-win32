#include "ServerCore.h"


#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>


#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h> 
#else
#include <winsock.h>
#define close closesocket
#endif

#define MAX_CLIENTS 25

ServerCore::ServerCore(int port,dispatch cb) :
    mDispatch(cb),
    mRunning(false),
    mClientCount(1)
{
#ifdef WIN32
	WSADATA data;
    if ( WSAStartup ( MAKEWORD ( 1, 1 ), &data ) )
	{
        printf("Can't init winsock.\n");
        return;
    }
#endif

	struct sockaddr_in stSockAddr;
    int reuse_addr = 1;

    mListener = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(mListener==-1) {
        printf("Can't create socket.\n");
        return;
    }
#ifndef WIN32
	setsockopt(mListener, SOL_SOCKET, SO_REUSEADDR, &reuse_addr,
		sizeof(reuse_addr));
#endif

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(port);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
 
    if(-1 == bind(mListener,(const struct sockaddr *)&stSockAddr,
                  sizeof(struct sockaddr_in)))
    {
        printf("error: bind failed\n");
        close(mListener);
        mListener = -1;
        return;
    }

    if(-1 == listen(mListener, 10))
    {
        printf("error: listen failed\n");
        close(mListener);
        mListener = -1;
        return;
    }

    /* Init semaphore */
    sem_init( &mMutex, 0, 1 );
    sem_init( &mQueueMutex, 0, 1 );

    mRunning = true;
    pthread_create(&mThread, NULL, thread_stub, (void*)this);
}

ServerCore::~ServerCore()
{
    mRunning = false;
    pthread_join(mThread, NULL);
    map<unsigned int, ClientConnection*>::iterator it = mClientMap.begin();
    while(it != mClientMap.end())
    {
        delete (*it).second;
        it++;
    }
    if(mListener!=-1) {
        close(mListener);
    }
    sem_destroy(&mMutex);
    sem_destroy(&mQueueMutex);	
}


void* ServerCore::thread_stub(void* arg)
{
    if (arg) {
        ServerCore* s = (ServerCore*)arg;
        s->Serve();
    }
	return NULL;
}


void ServerCore::ProcessMessageQueue()
{
	sem_wait(&mQueueMutex);

    while (mMessageQueue.size()>0) 
	{
        string msg = mMessageQueue.front();
        int clientId = mClientQueue.front();
        mMessageQueue.pop();
        mClientQueue.pop();

		map<unsigned int, ClientConnection*>::iterator it = mClientMap.find(clientId);
		if (it!=mClientMap.end()) 
		{
			(*it).second->Write(msg);
		}
	}

    sem_post(&mQueueMutex);
}


void ServerCore::Serve()
{
    fd_set sockets;
    struct timeval timeout;
    int max_desc;
    int readsocks;

    while(mRunning) {
        /* Build select list */
        FD_ZERO(&sockets);
        FD_SET(mListener, &sockets);
        max_desc = mListener;
        sem_wait(&mMutex);

		ProcessMessageQueue();

        map<unsigned int, ClientConnection*>::iterator it = mClientMap.begin();
        while(it != mClientMap.end())
        {
            int fd = (*it).second->getFd();
            FD_SET(fd, &sockets);
            if (fd>max_desc) {
                max_desc = fd;
            }
            it++;
        }
        sem_post(&mMutex);

		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
        readsocks = select(max_desc+1, &sockets, (fd_set*)0, (fd_set*)0,
                           &timeout);

		if (readsocks < 0) {
			printf("ERROR: select failed");
			break;
		}

        if(readsocks) {
            if(FD_ISSET(mListener, &sockets)) {
                /* New connection */
                int conn = accept(mListener, NULL, NULL);
                if (mClientMap.size()<MAX_CLIENTS) {
                    ClientConnection* client = new ClientConnection(conn);
                    mClientMap[mClientCount++] = client;
                } else {
                    close(conn);
                }
            }

            sem_wait(&mMutex);
            it = mClientMap.begin();
            while(it != mClientMap.end())
            {
                ClientConnection* client = (*it).second;
                if (FD_ISSET(client->getFd(), &sockets)) {
                    string data;
                    int status = client->Read(data);
                    if (status<0) {
                        /* Error , close */
                        map<unsigned int, ClientConnection*>::iterator it2 = it;
                        it2++;
                        delete client;
                        mClientMap.erase(it);
                        it = it2;
                        continue;
                    }
                    if (status>0) {
                        if (mDispatch) {
                            mDispatch((*it).first,data);
                        }
                    }
                }
                it++;
            }
            sem_post(&mMutex);
        } 
    }
}


void ServerCore::Write(int clientId, string data)
{
    sem_wait(&mQueueMutex);
    mMessageQueue.push(data);
    mClientQueue.push(clientId);
    sem_post(&mQueueMutex);
}

void ServerCore::Broadcast(string data)
{
    sem_wait(&mMutex);
    map<unsigned int, ClientConnection*>::iterator it = mClientMap.begin();
    while(it != mClientMap.end()) {
        (*it).second->Write(data);
        it++;
    }
    sem_post(&mMutex);
}


ClientConnection::ClientConnection(int fd) :
    mFd(fd)
{
#ifndef WIN32
	int opts;
    /* Set non blocking */
	opts = fcntl(mFd,F_GETFL);
	if (opts < 0) {
		printf("Error while: fcntl(F_GETFL)\n");
        return;
	}
	opts = (opts | O_NONBLOCK);
	if (fcntl(mFd,F_SETFL,opts) < 0) {
		printf("Error while: fcntl(F_SETFL)\n");
        return;
	}
    // sock_mode( mFd, TCP_MODE_ASCII );
#else
	u_long val = 1;
	ioctlsocket( mFd, FIONBIO, &val );
#endif
}

ClientConnection::~ClientConnection()
{
    close(mFd);
}

int ClientConnection::Write(string dat)
{
    size_t remain = dat.size();
    while(remain) {
#ifndef WIN32
        size_t r = write(mFd, dat.c_str(), dat.size());
#else
        size_t r = send(mFd, dat.c_str(), dat.size(), 0);
#endif
        if (r<0) break;
        remain-=r;
    }
    return 0;
}

#define BUFFER_SIZE 512

int ClientConnection::Read(string &data)
{
    char buffer[BUFFER_SIZE];
    int bytes_read;
    int total_count = 0;
    char *ch;
    char last_read = 0;
    bool newline = false;

    ch = buffer;
    while (total_count < BUFFER_SIZE - 1) {
#ifndef WIN32
        bytes_read = read(mFd, &last_read, 1);
        if (bytes_read < 0) {
            /* The other side may have closed unexpectedly */
            return -1;
        }
		if(bytes_read == 0)
		{
			return 0;
		}
#else
        bytes_read = recv(mFd, &last_read, 1, 0);

		if(bytes_read < 0)
		{
			int err = WSAGetLastError();
			if(err == WSAEWOULDBLOCK)
			{
				return 0;
			}
			else
			{
				return -1;
			}
		}
#endif

        if ((last_read=='\r')||(last_read=='\n')) {
            newline = true;
            break;
        }

        *ch++ = last_read;
        total_count++;
    }

    *ch = '\0';

    mBuffer = mBuffer + string(buffer);

    if (newline) {
        /* newline read */
        data = mBuffer;
        mBuffer = string("");
		return 1;
    }

    return 0;
}

#if 0
void dump(int c, string d)
{
    printf("%i %s\n",c,d.c_str());
}


int main(int argc, char* argv[])
{
    printf("Starting\n");
    ServerCore* core = new ServerCore(4545,dump);
    sleep(10);
    core->Broadcast("Hello clients\n");
    sleep(10);
    delete core;
}
#endif
