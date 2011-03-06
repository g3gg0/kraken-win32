
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

/**
 *  Create a singleton like instance of Kraken
 *  Loads all table indexes into memory
 */
Kraken::Kraken(const char* config, int server_port) :
    mNumDevices(0)
{
    assert (mInstance==NULL);
	mRunning = false;
    mInstance = this;
	mJobParallel = false;
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
				/* Add to TableInfo list */
				if (mTableInfo.size()) {
					snprintf(num,16,",%d",advance);
					mTableInfo = mTableInfo+string(num);
				} else {
					snprintf(num,16,"%d",advance);
					mTableInfo = string(num);
				}
                /* Make an entry in the active map */
                mActiveMap[advance]=1;
            }
            pos += len;
        }
        pos++;
    }
    mTableInfo = string("Tables: ")+mTableInfo+string("\n");

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
    mServer = NULL;
    if (server_port) {
        mServer = new ServerCore(server_port, serverCmd);
    }

	mRunning = true;
	/* an extra thread for console */
    pthread_create(&mConsoleThread, NULL, Kraken::consoleThread, (void*)this);
}

/**
 *  Cleanup, and free everything.
 */
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

/**
 *  Recieve a crack command and insert into mutex protected queue.
 */
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

/**
 *  Main woker thread loop
 */
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

	MEMCHECK();

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
	if ((mJobParallel || (mFragments.size()==0)) && mSubmittedStartValue.size() == 0)
	{
		char msg[256];

		/* did we process a job before? */
		if (mBusy)
		{
			/* yeah, inform about its result */
/*
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
*/
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

			int jobId = mWorkIds.front();
            int client = mWorkClients.front();
            string work = mWorkOrders.front();

            mWorkIds.pop();
            mWorkClients.pop();
            mWorkOrders.pop();

            char *plaintext = strdup(work.c_str());

			sprintf(msg, "102 %i Processing your request now\r\n", jobId);
			sendMessage(msg, client);

			uint32_t count = -1;
			uint32_t countRef = -1;
			char *bitsRef = NULL;
			char *count_pos = strchr((char*)plaintext, ' ');

			/* Set the active map to indicate which tables shall be used */
			char *tablist = strchr( plaintext, '[' );
			if (tablist) 
			{
				/* Start with no active */
				map<unsigned int,int>::iterator it = mActiveMap.begin();
				while (it!=mActiveMap.end()) {
					(*it).second = 0;
					it++;
				}

				/* set the end of plaintext at '[' */
				*tablist = '\000';
				tablist++;
				unsigned int num;
				while(sscanf(tablist,"%d",&num)==1) {
					it = mActiveMap.find(num);
					if (it!=mActiveMap.end()) {
						(*it).second = 1;
					}
					while ((*tablist>='0')&&(*tablist<='9')) {
						tablist++;
					}
					if (*tablist==',') {
						tablist++;
					} else break;
				}
			} else {
				/* All active */
				map<unsigned int,int>::iterator it = mActiveMap.begin();
				while (it!=mActiveMap.end()) {
					(*it).second = 1;
					it++;
				}
			}


			/* check if there was a space after the first bits - thats count */
			if(count_pos)
			{
				*count_pos = '\000';
				count_pos++;
				sscanf(count_pos, "%i", &count);

				/* check if there was a space after count - that are the ref bits */
				count_pos = strchr((char*)count_pos, ' ');
				if(count_pos)
				{
					*count_pos = '\000';
					count_pos++;
					bitsRef = strdup(count_pos);
					
					/* check if there was a space after ref bits - that are the ref bits count */
					/* intentionally work on bitsRef to terminate this string before count value */
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
			int submitted = 0;
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
					/* Skip if not active */
					if (mActiveMap[(*it).first]==0) {
						it++;
						continue;
					}
					/* Create fragments for sample */ 
                    for (int k=0; k<8; k++)
					{
                        Fragment* fr = new Fragment(plainrev,k,(*it).second,(*it).first);
                        fr->setBitPos(i);
						fr->setRef(count, countRef, bitsRef, client, jobId);
                        mFragments[fr] = 0;

						mSubmittedStartValue.push(plain);
						mSubmittedStartRound.push(k);
						mSubmittedAdvance.push((*it).first);
						mSubmittedContext.push(fr);

						submitted++;
                    }
                    it++;
                }
            }

			struct timeval start_time;
			gettimeofday(&start_time, NULL);
			mTimingMap[jobId] = start_time;
			mJobMap[jobId] = submitted;
			mJobMapMax[jobId] = submitted;

			free(plaintext);
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
			if(atiFree || cpuFree || mJobParallel)
			{
				uint64_t start_value = mSubmittedStartValue.front();
				unsigned int start_round = mSubmittedStartRound.front();
				uint32_t advance = mSubmittedAdvance.front();
				void* context = mSubmittedContext.front();

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
	A5AtiSpinLock(true);
	A5CpuSpinLock(true);
	deviceSpinLock(true);

	/* now clear all fragments that are somewhere being processed */
    sem_wait(&mMutex);

	mFragments.clear();
	mJobMap.clear();
	mJobMapMax.clear();
	mTimingMap.clear();
	
	A5CpuClear();
	A5AtiClear();

	/* now cancel all disk transfers */
	for (unsigned int i=0; i<mDevices.size(); i++) {
		mDevices[i]->Clear();
    }
	
	/* and let the wheel spin again */
	A5AtiSpinLock(false);
	A5CpuSpinLock(false);
	deviceSpinLock(false);

    sem_post(&mMutex);
}

/**
 *  Remove and delete a fragment from the work list maps
 */
void Kraken::removeFragment(Fragment* frag)
{
    sem_wait(&mMutex);
	
	if(mJobMap.find(frag->getJobNum()) != mJobMap.end())
	{
		mJobMap[frag->getJobNum()]--;
		if(mJobMap[frag->getJobNum()] == 0)
		{
			/* Kc not found, cleanup. reporting. */
			char msg[128];
			char details[128];
			char builder[128];

			struct timeval tv;
			gettimeofday(&tv, NULL);
			unsigned long diff = 1000000 * (tv.tv_sec - mTimingMap[frag->getJobNum()].tv_sec);
			diff += tv.tv_usec - mTimingMap[frag->getJobNum()].tv_usec;

			mFailedKc++;
			snprintf(details,128,"search took %i sec",(int)(diff/1000000));
			sprintf(msg, "404 %i Key not found (%s)\r\n", frag->getJobNum(), details);

			sendMessage(msg, frag->getClientId());

			/* delete job from list */
			map<unsigned int,int>::iterator it = mJobMap.find(frag->getJobNum());
			mJobMap.erase(it);

			/* delete job from list */
			map<unsigned int,int>::iterator it2 = mJobMapMax.find(frag->getJobNum());
			mJobMapMax.erase(it2);

			/* delete timing info */
			map<unsigned int,struct timeval>::iterator it3 = mTimingMap.find(frag->getJobNum());
			mTimingMap.erase(it3);
		}


		map<Fragment*,int>::iterator it = mFragments.find(frag);
		if (it!=mFragments.end()) {
			mFragments.erase(it);
			delete frag;
		}
	}

    sem_post(&mMutex);
}

void Kraken::deviceSpinLock(bool state)
{
	for (unsigned int i=0; i<mDevices.size(); i++) 
	{
		mDevices[i]->SpinLock(state);
	}
}

/**
 *  Remove and delete a fragment from the work list maps
 */
void Kraken::cancelJobFragments(int jobId)
{
	A5AtiSpinLock(true);
	A5CpuSpinLock(true);
	deviceSpinLock(true);

    sem_wait(&mMutex);

    map<Fragment*,int>::iterator it = mFragments.begin();
	list<void*> fragments;

	/* first cancel all fragment operations */
    while (it!=mFragments.end()) 
	{
		if(it->first->getJobNum() == jobId)
		{
			fragments.push_back(it->first);
		}
		it++;
    }

	/* now cancel all disk transfers */
	for (unsigned int i=0; i<mDevices.size(); i++) {
		mDevices[i]->Clear();
    }
	
	A5AtiCancel(fragments);
	A5CpuCancel(fragments);

	/* remove tagged fragments */
	while(fragments.size())
	{
		Fragment *frag = (Fragment*)fragments.front();
		fragments.pop_front();	
		removeFragment(frag);
	}	

	/* restart all disk pending transfers again */
    map<Fragment*,int>::iterator it2 = mFragments.begin();
    while (it2!=mFragments.end()) 
	{
		it2->first->requeueTransfer();
		it2++;
    }
    sem_post(&mMutex);	
	
	A5AtiSpinLock(false);
	A5CpuSpinLock(false);
	deviceSpinLock(false);

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


/**
 * Report a found key back to the issuing client
 */
void Kraken::reportFind(uint64_t result, Fragment *frag)
{
	unsigned char keyData[8];
	char msg[256];

	/* output to client */
	sprintf(msg, "103 %i %016llX %i (found a table hit in table %i)\r\n", frag->getJobNum(), result, frag->getBitPos(), frag->getAdvance());
	sendMessage(msg, frag->getClientId());

	/* was this a Kc cracking command with reference bits? */
	if(frag->getBitsRef() != NULL)
	{
		if(find_kc(result, frag->getBitPos(), frag->getCount(), frag->getCountRef(), frag->getBitsRef(), keyData))
		{
			/* Kc found, cleanup. reporting. */
			char details[128];
			char builder[128];

			struct timeval tv;
			gettimeofday(&tv, NULL);
			unsigned long diff = 1000000 * (tv.tv_sec - mTimingMap[frag->getJobNum()].tv_sec);
			diff += tv.tv_usec - mTimingMap[frag->getJobNum()].tv_usec;

			snprintf(details,128,"search took %i sec",(int)(diff/1000000));
			mFoundKc++;
			sprintf(msg, "200 %i ", frag->getJobNum());
			for(int pos = 0; pos < 8; pos++)
			{
				sprintf(builder, "%02X", keyData[pos]);
				strcat(msg, builder);
			}

			strcat(msg," Key found (");
			strcat(msg, details);
			if(mJobParallel)
			{
				strcat(msg,", parallel mode enabled - please wait for 404");
			}
			else
			{
				clearFragments();
			}
			strcat(msg,")\r\n");

			sendMessage(msg, frag->getClientId());

			//cancelJobFragments(frag->getJobNum());

			mFoundKeys++;
			memcpy(mKeyResult, keyData, 8);
		}
	}

	MEMCHECK();
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
 *   220 - response to "jobs"-command
 *   221 - response to "progress"-command
 *   
 */

/**
 *  Recieve and parse commands from clients
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
        sprintf(msg,"219 %s",mInstance->mTableInfo.c_str());
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
			sprintf(msg, "400 Releasing memory not possible, kraken is busy.\r\n");
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
    else if (!strncmp(command,"cancel",6))
	{
		const char *parm = command + 6;
		int id = -1;

		if(strlen(parm) > 0)
		{
			sscanf(parm, "%i", &id);
		}

		if(id >= 0)
		{
			if(!kraken->mJobParallel)
			{
				sprintf(msg, "212 Cancelling request #%i\r\n", id);
				kraken->clearFragments();
				//kraken->cancelJobFragments(id);
			}
			else
			{
				sprintf(msg, "400 Selective cancel not possible, running in parallel mode\r\n");
			}
		}
		else
		{
			sprintf(msg, "212 Cancelling current request (if any)\r\n");

			kraken->clearFragments();
		}
    }
    else if (!strncmp(command,"fake",4))
	{
		sprintf(msg, "213 Faking result of currently running request (if any) and will report key EEEE[...]EE\r\n");

		memset(kraken->mKeyResult, 0xEE, 8);
		kraken->mFoundKeys = 1;
        kraken->clearFragments();
    }
    else if (!strncmp(command,"jobs",4))
	{    
		int size = 512;
		char *buffer = (char*)malloc(size);
		
		sem_wait(&kraken->mMutex);
		map<unsigned int,int>::iterator it = kraken->mJobMap.begin();
		sprintf(msg, "220 Active jobs:\r\n");
		kraken->sendMessage(msg, clientID);

		while (it!=kraken->mJobMap.end()) 
		{
			double progress = 100.0f - ((100.0f * kraken->mJobMap[(*it).first]) / kraken->mJobMapMax[(*it).first]);
			struct timeval tv;
			gettimeofday(&tv, NULL);
			unsigned long diff = 1000000 * (tv.tv_sec - kraken->mTimingMap[(*it).first].tv_sec);
			diff += tv.tv_usec - kraken->mTimingMap[(*it).first].tv_usec;

			sprintf(msg, "220    %4d: active %i sec, progress %3.2f %%\r\n", (*it).first, (int)(diff/1000000), progress );
			kraken->sendMessage(msg, clientID);
			it++;
		}
		unsigned int histogram[4];
		histogram[0] = 0;
		histogram[1] = 0;
		histogram[2] = 0;
		histogram[3] = 0;
		int total = 0;
		map<Fragment*,int>::iterator it2 = kraken->mFragments.begin();
		while (it2!=kraken->mFragments.end()) {
			int state = (*it2).first->getState();
			if ((state>=0)&&(state<4)) {
				histogram[state]++;
				total++;
			}
			it2++;
		}
		sem_post(&kraken->mMutex);

		sprintf(msg, "220 State counts (%d): %d %d %d %d\r\n", total, histogram[0],histogram[1],histogram[2],histogram[3]);
		free(buffer);
    }
    else if (!strncmp(command,"progress",8))
	{		
		const char *parm = command + 8;
		int id = -1;

		if(strlen(parm) > 0)
		{
			sscanf(parm, "%i", &id);
		}

		if(id >= 0 && kraken->mJobMap.find(id) != kraken->mJobMap.end())
		{
			double progress = 100.0f - ((100.0f * kraken->mJobMap[id]) / kraken->mJobMapMax[id]);

			sprintf(msg, "221 Progress of job %i is %2.2f %%\r\n", id, progress );
		}
		else
		{
			sprintf(msg, "400 No such job\r\n" );
		}
    }
    else if (!strncmp(command,"parallel",8))
	{
		if(kraken->mBusy)
		{
			sprintf(msg, "400 Mode switch not possible, kraken is busy.\r\n");
		}
		else
		{
			kraken->mJobParallel = true;
			sprintf(msg, "221 Job processing now parallel\r\n" );
		}
    }
    else if (!strncmp(command,"serial",6))
	{
		if(kraken->mBusy)
		{
			sprintf(msg, "400 Mode switch not possible, kraken is busy.\r\n");
		}
		else
		{
			kraken->mJobParallel = false;
			sprintf(msg, "221 Job processing now serial\r\n" );
		}
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
    char command[1025];
	Kraken* kraken = (Kraken*)arg;

    printf("\n");
    printf("Started '"KRAKEN_VERSION"'\n");
    printf("Commands are: crack test status stats fake cancel quit\n");
    printf("\n");
	printf("Kraken> ");

	while(kraken->mRunning) {
		char* ch = fgets(command, 1024, stdin);

		/* make sure the console writer does not corrupt our output */
		sem_wait(&kraken->mConsoleMutex);

		command[1024]='\0';
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

/**
 * Program entry point
 */
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
