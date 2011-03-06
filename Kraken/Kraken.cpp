
#include "Kraken.h"

#include "Fragment.h"
#include <stdio.h>
#include <string>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "../a5_cpu/A5CpuStubs.h"
#include "../a5_ati/A5AtiStubs.h"

#include "Globals.h"


/* g3gg0: hardcore-link to find_kc. someone should clean this up. */
int find_kc(uint64_t stop, uint32_t pos, uint32_t framecount, uint32_t framecount2, char* testbits, unsigned char *keydata );

Kraken* Kraken::mInstance = NULL;

Kraken::Kraken(const char* config, int server_port) :
    mNumDevices(0)
{
    assert (mInstance==NULL);
	mRunning = false;
    mInstance = this;
	mRequestId = 0;

	mRequests = 0;
	mFoundKc = 0;
	mFailedKc = 0;

	printf("\r\n");
	printf(" Starting Kraken\r\n");
	printf("-----------------\r\n");
	printf("\r\n");

    string configFile = string(config)+string("tables.conf");
    FILE* fd = fopen(configFile.c_str(),"rb");
    if (fd==NULL) {
        printf(" [E] Could not find %s\n", configFile.c_str());
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
    size_t pos = 0;
    while (pos<(size_t)size) {
        if (pFile[pos]) {
            size_t len = strlen(&pFile[pos]);
            if (strncmp(&pFile[pos],"Device:",7)==0) {
                const char* ch1 = strchr(&pFile[pos],' ');
                if (ch1) {
                    ch1++;
                    const char* ch2 = strchr(ch1,' ');
                    string devname(ch1,ch2-ch1);

					NcqDevice *dev = new NcqDevice(devname.c_str());
					if(!dev->isRunning())
					{
						printf(" [E] Failed to initialize device. Aborting.\r\n");
						return;
					}

                    mNumDevices++;
                    mDevices.reserve(mNumDevices);
                    mDevices.push_back(dev);
                }
            }
            else if (strncmp(&pFile[pos],"Table:",6)==0) {
                unsigned int devno;
                unsigned int advance;
                unsigned long long offset;

				printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
				printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
				printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
				printf("\r");
				printf(" [x] Loaded tables: %i    ", mTables.size() + 1);
				fflush(stdout);

                sscanf(&(pFile[pos+7]),"%u %u %llu",&devno,&advance,&offset);

                if(devno>=(unsigned int)mNumDevices)
				{
					printf(" [E] Invalild drive number. dev: %u, adv: %u, off: %llu\r\n", devno, advance, offset );
					printf(" [E] Failed to initialize table. Aborting.\r\n");
					return;
				}

                char num[32];
                sprintf( num,"/%u.idx", advance );
                string indexFile = string(config)+string(num);

                DeltaLookup* dl = new DeltaLookup(mDevices[devno],indexFile);
                dl->setBlockOffset(offset);
                mTables.push_back( pair<unsigned int, DeltaLookup*>(advance, dl) );                
            }
            pos += len;
        }
        pos++;
    }
    delete [] pFile;

	printf("\r\n");

    /* Init semaphore */
    sem_init( &mMutex, 0, 1 );
    sem_init( &mSpinlock, 0, 1 );
	sem_init( &mConsoleMutex, 0, 1 );

    A5CpuInit(8, 12, 4);
    mUsingAti = A5AtiInit(8,12,0xffffffff,1);


	gettimeofday(&mLastJobTime, NULL);
	mBusy = false;
	mTablesLoaded = true;
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
	Shutdown();
}

void Kraken::UnloadTables()
{
	if(mTablesLoaded)
	{
		tableListIt it = mTables.begin();
		while (it!=mTables.end()) {
			(*it).second->UnloadTable();
			it++;
		}
	}
	mTablesLoaded = false;
}

void Kraken::LoadTables()
{
	if(!mTablesLoaded)
	{
		tableListIt it = mTables.begin();
		while (it!=mTables.end()) {
			(*it).second->LoadTable();
			it++;
		}
	}
	mTablesLoaded = true;
}

void Kraken::Shutdown()
{
	if(mRunning)
	{
		mRunning = false;
		pthread_join(mConsoleThread, NULL);

		mServer->Shutdown();
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
}

int Kraken::Crack(int client, const char* plaintext)
{
	int ret = -1;
	
    sem_wait(&mMutex);
	mRequests++;
	mRequestId++;
	ret = mRequestId;
    mWorkIds.push(mRequestId);
    mWorkOrders.push(string(plaintext));
    mWorkClients.push(client);
    sem_post(&mMutex);

	return ret;
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

	MEMCHECK();
    sem_wait(&mMutex);

	/* when busy, update the last job timestamp */
	if(mBusy)
	{
		gettimeofday(&mLastJobTime, NULL);
	}

	/* when being idle for too long time, unload tables to save memory */
	if(mTablesLoaded)
	{
		struct timeval tv;
		gettimeofday(&tv, NULL);
		unsigned long diff = tv.tv_sec - mLastJobTime.tv_sec;

		/* idle for more than n seconds? */
		if(diff > 60 * 30)
		{
			UnloadTables();
		}
	}

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
				mFailedKc++;
				sprintf(msg, "404 %i Key not found (%s)\r\n", mCurrentId, details);
			}
			else
			{
				mFoundKc++;
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
			if(!mTablesLoaded)
			{
				LoadTables();
			}

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
            size_t samples = len - 63;
            for (size_t i=0; i<samples; i++)
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

			/* at least one of both devices are free */
			if(atiFree || cpuFree)
			{
				uint64_t start_value = mSubmittedStartValue.front();
				unsigned int start_round= mSubmittedStartRound.front();
				uint32_t advance= mSubmittedAdvance.front();
				void* context= mSubmittedContext.front();

				mSubmittedStartValue.pop();
				mSubmittedStartRound.pop();
				mSubmittedAdvance.pop();
				mSubmittedContext.pop();

#ifdef BALANCE_DEVICES
				/* ati has priority */
				if(atiFree) 
				{
					A5AtiSubmit(start_value, start_round, advance, context);
				}
				else
				{
					A5CpuSubmit(start_value, start_round, advance, context);
				}
#else
				if(mUsingAti)
				{
					A5AtiSubmit(start_value, start_round, advance, context);
				}
				else
				{
					A5CpuSubmit(start_value, start_round, advance, context);
				}
#endif

				assigned = true;
			}
			else
			{
				assigned = false;
			}

		} while(assigned && mSubmittedStartValue.size()>0);
	}

	MEMCHECK();
    sem_post(&mMutex);

    return mBusy;
}

void Kraken::clearFragments()
{
	/* first stop somewhere in the processing loop */
	for (unsigned int i=0; i<mDevices.size(); i++) {
		mDevices[i]->Pause();
    }

	/* now clear all fragments that are somewhere being processed */
    sem_wait(&mMutex);
	mFragments.clear();
	A5CpuClear();
	A5AtiClear();
    sem_post(&mMutex);

	/* now cancel all disk transfers */
	for (unsigned int i=0; i<mDevices.size(); i++) {
		mDevices[i]->Clear();
    }
	
	/* and let the wheel spin again */
	for (unsigned int i=0; i<mDevices.size(); i++) {
		mDevices[i]->Unpause();
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
	/* make sure the console input interface does not corrupt our output */
	sem_wait(&mConsoleMutex);

	/* clear the last output. usually just the Kraken> prompt but maybe also some user input */
	printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
	printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
	/* make sure we are printing at the first column */
	printf("\r");

	if(client != -1)
	{
		if(mServer)
		{
			mServer->Write(client, msg);
		}
		printf(" [i] [client:%i] %s", client, msg);
	}
	else
	{
		printf(" [i] %s", msg);
	}

	printf("\rKraken> ");
	fflush(stdout);

	/* we're done */
	sem_post(&mConsoleMutex);
}

void Kraken::reportFind(uint64_t result, int bitPos, int count, int countRef, char *bitsRef)
{
	unsigned char keyData[8];
	char msg[256];

	/* output to client */
	sprintf(msg, "103 %i %016llX %i (found a table hit)\r\n", mCurrentId, result, bitPos);
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

	MEMCHECK();
	//printf(found.c_str());
}


class KrakenPerfDisk : NcqRequestor {

private:
	uint64_t mRequestsRunning;
	vector<NcqDevice*> mDevices;
	Kraken *mKraken;
    sem_t mMutex;
	int mClient;

public:
	KrakenPerfDisk(Kraken *kraken, int clientID, vector<NcqDevice*> devices)
	{
		sem_init( &mMutex, 0, 1 );
		mKraken = kraken;
		mClient = clientID;
		mDevices = devices;
		mRequestsRunning = 0;   
		
		srand( (unsigned)time( NULL ) );
	}

    void Start(uint64_t requests)
	{
		sem_wait(&mMutex);
		for (uint64_t req=0; req<requests; req++) {
			for (int i=0; i<mDevices.size(); i++) {
				uint64_t blockNo = (uint64_t)((double)rand() / (RAND_MAX + 1) * (mDevices[i]->getMaxBlockNum()));

				mDevices[i]->Request(this, blockNo);
				mRequestsRunning++;
			}
		}
		sem_post(&mMutex);
	}

	void KrakenPerfDisk::processBlock(const void* pDataBlock)
	{
		sem_wait(&mMutex);
		mRequestsRunning--;

		if(!mRequestsRunning)
		{
			mKraken->sendMessage("216 Performance test finished. Retrieve results with 'stats'\r\n", mClient);
		}
		sem_post(&mMutex);
	}
};



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
 *   405 - not allowed
 *   404 [id] - no key found for this request
 *   
 *   210 - response to "status"-command 
 *   211 - response to "idle"-command
 *   212 - response to "cancel"-command
 *   213 - response to "fake"-command
 *   214 - response to "stats"-command
 *   215 - response to "perf disk"-command
 *   216 - finished "perf disk"-command
 *   217 - response to "suspend"-command
 *   218 - response to "wnd_hide/show"-command
 *   219 - response to "list"-command
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
		size_t queued = kraken->mWorkOrders.size();

		/* already processing one request? */
		if(kraken->mBusy)
		{
			queued++;
		}

        // Test frame 998
        int id = kraken->Crack(clientID, "001101110011000000001000001100011000100110110110011011010011110001101010100100101111111010111100000110101001101011");

		sprintf(msg, "101 %i Request queued (%i already in queue)\r\n", id, queued );
	}    
	else if (!strncmp(command,"list",4)) 
	{
        /* Return a printed list of loaded tables */
        tableListIt it = mInstance->mTables.begin();
        string table_list;
        while (it!=mInstance->mTables.end()) {
            char num[16];
            if (table_list.size()) {
                snprintf(num,16,",%d",(*it).first);
                table_list = table_list+string(num);
            } else {
                snprintf(num,16,"%d",(*it).first);
                table_list = string(num);
            }
            it++;
        }
        table_list = string("Tables: ")+table_list+string("\n");
        sprintf(msg,"219 %s",table_list.c_str());
	}
    else if (!strncmp(command,"wnd_show",8))
	{
#ifdef WIN32
		HWND hWnd = GetConsoleWindow();
		ShowWindow( hWnd, SW_SHOW );
		sprintf(msg, "218 Ok.\r\n");
#endif
	}    
	else if (!strncmp(command,"wnd_hide",8))
	{	
#ifdef WIN32
		HWND hWnd = GetConsoleWindow();
		ShowWindow( hWnd, SW_HIDE );
		sprintf(msg, "218 Ok.\r\n");
#endif
	}
    else if (!strncmp(command,"suspend",7))
	{
		/* already processing one request? */
		if(kraken->mBusy)
		{
			sprintf(msg, "405 Releasing memory not possible, kraken is busy.\r\n");
		}
		else
		{
			kraken->UnloadTables();
			sprintf(msg, "217 Released memory. Will refill when next statement gets processed.\r\n");
		}
	}
    else if (!strncmp(command,"status",6))
	{
		size_t queued = kraken->mWorkOrders.size();

		/* already processing one request? */
		if(kraken->mBusy)
		{
			queued++;
		}

		sprintf(msg, "210 Kraken server (%i jobs in queue, %i processed, %i keys found, %i not found, tables currently %s)\r\n", queued, kraken->mRequests, kraken->mFoundKc, kraken->mFailedKc, (kraken->mTablesLoaded?"in RAM":"not loaded"));
	}
    else if (!strncmp(command,"stats",5))
	{
		int size = 512;
		char *buffer = (char*)malloc(size);
		
		strcpy(buffer, "214 ");

		for (int i=0; i<kraken->mNumDevices; i++) {
			strcat(buffer,kraken->mDevices[i]->GetDeviceStats());
			strcat(buffer," - ");

			size += strlen(buffer) + 1;
			buffer = (char*)realloc(buffer, size);
		}
		strcat(buffer,"\r\n");

		size = strlen(buffer) + 1;

		strncpy(msg, buffer, (size >= sizeof(msg))?(sizeof(msg)-1):(size));
		free(buffer);
	}
    else if (!strncmp(command,"crack",5))
	{
        const char* ch = command + 5;
        while (*ch && (*ch!='0') && (*ch!='1')) ch++;
        size_t len = strlen(ch);
        if (len>63)
		{
			size_t queued = kraken->mWorkOrders.size();

			/* already processing one request? */
			if(kraken->mBusy)
			{
				queued++;
			}

            int id = kraken->Crack(clientID, ch);
			sprintf(msg, "101 %i Request queued (%i already in queue)\r\n", id, queued  );
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
	        sprintf(msg, "400 Sorry, you may not shutdown the beast remotely for obvious reason\r\n" );
		}
    }
    else if (!strncmp(command,"perf",4))
	{
        const char* ch = command + 4;
        while (*ch && (*ch==' ')) ch++;

		if(!strncmp(ch,"disk",4))
		{
			unsigned long seeks = 2000;
			ch = ch + 4;
			while (*ch && (*ch==' ')) ch++;

			if(*ch && sscanf(ch, "%lu", &seeks) != 1)
			{
	            sprintf(msg, "400 Bad request\r\n" );
			}
			else
			{
				sprintf(msg, "215 Starting disk performance test. (queueing %lu random reads)\r\n", seeks );
				/* TODO: we should free this later.... */
				KrakenPerfDisk *perf = new KrakenPerfDisk(kraken, clientID, kraken->mDevices);
				perf->Start(seeks);
			}
		}
		else
		{
            sprintf(msg, "400 Bad request\r\n" );
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
    printf("Started '"KRAKEN_VERSION"'\n");
    printf("Commands are: crack test status stats fake cancel quit\n");
    printf("\n");
	printf("Kraken> ");

	while(kraken->mRunning) {
		char* ch = fgets(command, 256, stdin);

		/* make sure the console writer does not corrupt our output */
		sem_wait(&kraken->mConsoleMutex);

		command[255]='\0';
		if (!ch) break;

		size_t len = strlen(command);
		if (command[len-1]=='\n') {
			len--;
			command[len]='\0';
		}

		printf("\rKraken> ");
		fflush(stdout);

		/* we're finished with printing stuff */
		sem_post(&kraken->mConsoleMutex);

		/* process command */
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

	kr.Shutdown();

	return 0;
}
