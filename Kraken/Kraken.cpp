#include "Kraken.h"
#include "Fragment.h"
#include <stdio.h>
#include <string>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "../a5_cpu/A5CpuStubs.h"
#include "../a5_ati/A5AtiStubs.h"


#ifdef WIN32
#define KRAKEN_VERSION "(rev. 197, g3gg0.de, win32)"
#define DL_EXT ".dll"
#else
#define KRAKEN_VERSION ""
#define DL_EXT ".so"
#endif

/* g3gg0: hardcore-link to find_kc. someone should clean this up. */
int find_kc(uint64_t stop, uint32_t pos, uint32_t framecount, uint32_t framecount2, char* testbits, unsigned char *keydata );

Kraken* Kraken::mInstance = NULL;

Kraken::Kraken(const char* config, int server_port) :
    mNumDevices(0)
{
    assert (mInstance==NULL);
    mInstance = this;
	mRequestId = 0;

    string configFile = string(config)+string("tables.conf");
    FILE* fd = fopen(configFile.c_str(),"rb");
    if (fd==NULL) {
        printf("Could not find %s\n", configFile.c_str());
        assert(0);
    }
    fseek(fd ,0 ,SEEK_END );
    int size = ftell(fd);
    fseek(fd ,0 ,SEEK_SET );
    char* pFile = (char*)malloc(size+1);
    size_t r = fread(pFile,1,size,fd);
    pFile[size]='\0';
    assert(r==size);
    fclose(fd);
    for (int i=0; i < size; i++) {
        if (pFile[i]=='\r') {
            pFile[i] ='\0';
            continue;
        }
        if (pFile[i]=='\n') pFile[i] ='\0';
    }
    int pos = 0;
    while (pos<size) {
        if (pFile[pos]) {
            int len = strlen(&pFile[pos]);
            if (strncmp(&pFile[pos],"Device:",7)==0) {
                //printf("%s\n", &pFile[pos]);
                const char* ch1 = strchr(&pFile[pos],' ');
                if (ch1) {
                    ch1++;
                    const char* ch2 = strchr(ch1,' ');
                    string devname(ch1,ch2-ch1);
                    //printf("%s\n", devname.c_str());
                    mNumDevices++;
                    mDevices.reserve(mNumDevices);
                    mDevices.push_back(new NcqDevice(devname.c_str()));
                }
            }
            else if (strncmp(&pFile[pos],"Table:",6)==0) {
                unsigned int devno;
                unsigned int advance;
                unsigned long long offset;

                sscanf(&(pFile[pos+7]),"%u %u %llu",&devno,&advance,&offset);
				printf("dev: %u, adv: %u, offset: %llu\n", devno, advance, offset );
                if(devno>=mNumDevices)
				{
					printf(" \\_INVALID DRIVE NUMBER!\r\n");
					return;
				}
                char num[32];
                sprintf( num,"/%u.idx", advance );
                string indexFile = string(config)+string(num);
                // if (advance==340) {
                {
                    DeltaLookup* dl = new DeltaLookup(mDevices[devno],indexFile);
                    dl->setBlockOffset(offset);
                    mTables.push_back( pair<unsigned int, DeltaLookup*>(advance, dl) );
                }
            }
            pos += len;
        }
        pos++;
    }
    delete [] pFile;

    /* Init semaphore */
    sem_init( &mMutex, 0, 1 );

    A5CpuInit(8, 12, 4);

    mUsingAti = A5AtiInit(8,12,0xffffffff,1);

    mBusy = false;
	mCurrentId = 0;
    mCurrentClient = 0;
    mServer = NULL;
    if (server_port) {
        mServer = new ServerCore(server_port, serverCmd);
    }

	mRunning = true;
	/* an extra thread for console */
    pthread_create(&mConsoleThread, NULL, Kraken::consoleThread, (void*)this);
}

Kraken::~Kraken()
{
	mRunning = false;
    pthread_join(mConsoleThread, NULL);

	delete mServer;
	mServer = NULL;

    A5CpuShutdown();
    A5AtiShutdown();

    tableListIt it = mTables.begin();
    while (it!=mTables.end()) {
        delete (*it).second;
        it++;
    }

    for (int i=0; i<mNumDevices; i++) {
        delete mDevices[i];
    }

    sem_destroy(&mMutex);
}

void Kraken::Crack(int client, const char* plaintext, char *response )
{
	int id;
	char msg[256];
	
    sem_wait(&mMutex);
	mRequestId++;
    sprintf(msg, "101 %i Request queued\r\n", mRequestId );
    mWorkIds.push(mRequestId);
    mWorkOrders.push(string(plaintext));
    mWorkClients.push(client);
    sem_post(&mMutex);

	if(response != NULL)
	{
		strcat(response, msg);
	}
}

bool Kraken::Tick()
{
    uint64_t start_val;
    uint64_t stop_val;
    Fragment* frag;
    int32_t start_rnd;

    while (A5CpuPopResult(start_val, stop_val, start_rnd, (void**)&frag)) {
        frag->handleSearchResult(stop_val, start_rnd);
    }

    if (mUsingAti) {
        while (A5AtiPopResult(start_val, stop_val, (void**)&frag)) {
            frag->handleSearchResult(stop_val, 0);
        }
    }

    sem_wait(&mMutex);

	/* finished current job and have no more work packages to assign? */
    if (mFragments.size()==0 && mSubmittedStartValue.size() == 0)
	{
		char msg[256];
		char details[128];
		char builder[128];

		/* did we process a job before? */
		if (mBusy)
		{
			/* yeah, inform about its result */
			struct timeval tv;
			gettimeofday(&tv, NULL);
			unsigned long diff = 1000000 * (tv.tv_sec - mStartTime.tv_sec);
			diff += tv.tv_usec - mStartTime.tv_usec;

			snprintf(details,128,"search took %i sec",(int)(diff/1000000));

			if (mFoundKeys == 0)
			{
				sprintf(msg, "404 %i Key not found (%s)\r\n", mCurrentId, details);
			}
			else
			{
				sprintf(msg, "200 %i ", mCurrentId);
				for(int pos = 0; pos < 8; pos++)
				{
					sprintf(builder, "%02X", mKeyResult[pos]);
					strcat(msg, builder);
				}

				strcat(msg," Key found (");
				strcat(msg, details);
				strcat(msg,")\r\n");
			}

			sendMessage(msg, mCurrentClient);

			mFoundKeys = 0;
			mBusy = false;
		}

        /* Start a new job if there is some order */
        if (mWorkOrders.size()>0)
		{
            mBusy = true;
			mFoundKeys = 0;

            gettimeofday(&mStartTime, NULL);

			mCurrentId = mWorkIds.front();
            mCurrentClient = mWorkClients.front();
            string work = mWorkOrders.front();

            mWorkIds.pop();
            mWorkClients.pop();
            mWorkOrders.pop();

            const char* plaintext = work.c_str();

			sprintf(msg, "102 %i Processing your request now\r\n", mCurrentId);
			sendMessage(msg, mCurrentClient);

			uint32_t count = -1;
			uint32_t countRef = -1;
			char *bitsRef = NULL;
			char *count_pos = strchr((char*)plaintext, ' ');

			if(count_pos)
			{
				*count_pos = '\000';
				count_pos++;
				sscanf(count_pos, "%i", &count);

				count_pos = strchr((char*)count_pos, ' ');
				if(count_pos)
				{
					*count_pos = '\000';
					count_pos++;
					bitsRef = strdup(count_pos);

					count_pos = strchr((char*)bitsRef, ' ');
					if(count_pos)
					{
						*count_pos = '\000';
						count_pos++;
						sscanf(count_pos, "%i", &countRef);
					}
				}
			}

            size_t len = strlen(plaintext);
            int samples = len - 63;
            for (int i=0; i<samples; i++)
			{
                uint64_t plain = 0;
                uint64_t plainrev = 0;
                for (int j=0;j<64;j++) {
                    if (plaintext[i+j]=='1')
					{
                        plain = plain | (1ULL<<j);
                        plainrev = plainrev | (1ULL<<(63-j));
                    }
                }
                tableListIt it = mTables.begin();
                while (it!=mTables.end())
				{
                    for (int k=0; k<8; k++)
					{
                        Fragment* fr = new Fragment(plainrev,k,(*it).second,(*it).first);
                        fr->setBitPos(i);
						fr->setRef(count, countRef, bitsRef);
                        mFragments[fr] = 0;

						mSubmittedStartValue.push(plain);
						mSubmittedStartRound.push(k);
						mSubmittedAdvance.push((*it).first);
						mSubmittedContext.push(fr);
                    }
                    it++;
                }
            }
        }
    }

    /* assign submitted orders to free devices */
    if (mSubmittedStartValue.size()>0)
	{
		bool assigned = false;

		do
		{
			bool atiFree = false;
			bool cpuFree = false;

			if(mUsingAti)
			{
				if(A5AtiIsIdle())
				{
					atiFree = true;
				}
			}

			if(A5CpuIsIdle())
			{
				cpuFree = true;
			}
			/* one of both devices are free */
			if(atiFree ||cpuFree)
			{
				uint64_t start_value = mSubmittedStartValue.front();
				unsigned int start_round= mSubmittedStartRound.front();
				uint32_t advance= mSubmittedAdvance.front();
				void* context= mSubmittedContext.front();

				mSubmittedStartValue.pop();
				mSubmittedStartRound.pop();
				mSubmittedAdvance.pop();
				mSubmittedContext.pop();

#if 1
				/* ati has priority */
				if (atiFree) 
				{
					//printf("job -> ATI\r\n");
					A5AtiSubmit(start_value, start_round, advance, context);
				}
				else
				{
					//printf("job -> CPU\r\n");
					A5CpuSubmit(start_value, start_round, advance, context);
				}
#else
				A5AtiSubmit(start_value, start_round, advance, context);
#endif

				assigned = true;
			}
			else
			{
				assigned = false;
			}

		} while(assigned && mSubmittedStartValue.size()>0);
	}

    sem_post(&mMutex);
    return mBusy;
}

void Kraken::clearFragments()
{
    sem_wait(&mMutex);
	mFragments.clear();
	A5CpuClear();
    sem_post(&mMutex);

	for (int i=0; i<mDevices.size(); i++) {
		mDevices[i]->Clear();
    }
}

void Kraken::removeFragment(Fragment* frag)
{
    sem_wait(&mMutex);
    map<Fragment*,int>::iterator it = mFragments.find(frag);
    if (it!=mFragments.end()) {
        mFragments.erase(it);
        delete frag;
    }
    sem_post(&mMutex);
}

void Kraken::sendMessage(char *msg, int client)
{
	if(client != -1)
	{
		if(mServer)
		{
			mServer->Write(client, msg);
		}
		printf("[%i] %s", client, msg);
	}
	else
	{
		printf("%s", msg);
	}
}

void Kraken::reportFind(string found, uint64_t result, int bitPos, int count, int countRef, char *bitsRef)
{
	unsigned char keyData[8];
	char msg[256];
	char resString[256];
	char builder[256];

	/* output to client */
	sprintf(msg, "103 %i %016llx %i (found a table hit)\r\n", mCurrentId, result, bitPos);
	sendMessage(msg, mCurrentClient);

	/* was this a Kc cracking command with reference bits? */
	if(bitsRef != NULL)
	{
		if(find_kc(result, bitPos, count, countRef, bitsRef, keyData))
		{
			/* Kc found, cleanup. reporting happens in processing thread */
			clearFragments();
			A5CpuClear();

			mFoundKeys++;
			memcpy(mKeyResult, keyData, 8);
		}
	}

	//printf(found.c_str());
}
/* 
 * Kraken Status codes:
 * 
 *   100 - your request will get queued
 *   101 [id] - your request has been queued with ID [id]
 *   102 [id] - your request [id] is getting processed now
 *   103 [id] [result] [bitpos] - intermediate result found
 *
 *   200 [id] [key] - Kc for [id] was found
 *
 *   400 - invalid request
 *   404 [id] - no key found for this request
 *   
 *   210 - status response
 *   211 - idle response
 *   212 - cancelling positive response
 *   213 - faking response
 *   
 */

void Kraken::serverCmd(int clientID, string cmd)
{
	Kraken* kraken = Kraken::getInstance();
    const char* command = cmd.c_str();
	char msg[512];

	if(cmd.length() <= 2)
	{
		return;
	}

    if (strncmp(command,"test",4)==0)
	{
		int queued = kraken->mWorkOrders.size();

		/* already processing one request? */
		if(kraken->mBusy)
		{
			queued++;
		}

        sprintf(msg, "100 Queuing request (%i already in queue)\r\n", queued );
		kraken->sendMessage(msg, clientID);

        // Test frame 998
		strcpy(msg, "");
        kraken->Crack(clientID, "001101110011000000001000001100011000100110110110011011010011110001101010100100101111111010111100000110101001101011", msg);
	}
    else if (!strncmp(command,"status",6))
	{
		int queued = kraken->mWorkOrders.size();

		/* already processing one request? */
		if(kraken->mBusy)
		{
			queued++;
		}

		sprintf(msg, "210 Kraken server (%i jobs currently in queue)\r\n", queued);
	}
    else if (!strncmp(command,"crack",5))
	{
        const char* ch = command + 5;
        while (*ch && (*ch!='0') && (*ch!='1')) ch++;
        int len = strlen(ch);
        if (len>63)
		{
			int queued = kraken->mWorkOrders.size();

			/* already processing one request? */
			if(kraken->mBusy)
			{
				queued++;
			}

            sprintf(msg, "100 Queuing request (%i already in queue)\r\n", queued );
			kraken->sendMessage(msg, clientID);

			strcpy(msg, "");
            kraken->Crack(clientID, ch, msg);
        }
		else
		{
            sprintf(msg, "400 Bad request\r\n" );
		}
    }
    else if (!strncmp(command,"idle",4))
	{
		sprintf(msg, "211 Idle you are?\r\n");
    }
    else if (!strncmp(command,"cancel",5))
	{
		sprintf(msg, "212 Cancelling current request (if any)\r\n");

        kraken->clearFragments();
    }
    else if (!strncmp(command,"fake",4))
	{
		sprintf(msg, "213 Faking result of currently running request (if any) and will report key EEEE[...]EE\r\n");

		memset(kraken->mKeyResult, 0xEE, 8);
		kraken->mFoundKeys = 1;
        kraken->clearFragments();
    }
    else if (!strncmp(command,"quit",4))
	{
		if(clientID == -1)
		{
			sprintf(msg, "666 says goodbye\r\n");
			kraken->mRunning = false;
		}
		else
		{
	        sprintf(msg, "400 Sorry, you may not shutdown the beast remotely for obvious reasons\r\n" );
		}
    }
	else
	{
        sprintf(msg, "400 Unknown request\r\n" );
	}

	kraken->sendMessage(msg, clientID);
}

void *Kraken::consoleThread(void *arg)
{
    char command[256];
	Kraken* kraken = (Kraken*)arg;

    printf("\n");
    printf("Kraken Server "KRAKEN_VERSION" running\n");
    printf("Commands are: crack test status fake cancel quit\n");
    printf("\n");

	while(kraken->mRunning) {
		printf("\nKraken> ");
		char* ch = fgets(command, 256 , stdin);
		command[255]='\0';
		if (!ch) break;
		size_t len = strlen(command);
		if (command[len-1]=='\n') {
			len--;
			command[len]='\0';
		}
		printf("\n");
		
		kraken->serverCmd(-1, command);

		usleep(1000);
	}

	return NULL;
}

int main(int argc, char* argv[])
{
    if (argc<2) {
        printf("usage: %s <index_path> [server port]\n", argv[0] );
        return -1;
    }

    int server_port = 8866;
    if (argc>2) server_port=atoi(argv[2]);
    Kraken kr(argv[1], server_port);

    while (kr.mRunning) {
        bool busy = kr.Tick();

		if(!busy)
		{
			usleep(1000);        
		}
		else
		{
			usleep(0);
		}
    }

	return 0;
}
