
#include "Globals.h"
#include "Kraken.h"
#include "Fragment.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#include "../a5_cpu/A5CpuStubs.h"
#include "../a5_ati/A5GpuStubs.h"

#include "Memdebug.h"


/* g3gg0: hardcore-link to find_kc. someone should clean this up. */
int find_kc(uint64_t stop, uint32_t pos, uint32_t framecount, uint32_t framecount2, char* testbits, unsigned char *keydata );

Kraken* Kraken::mInstance = NULL;


const char *AboutMessages[] =
{
		"----------------------------------------------------",
		" Kraken server:         " KRAKEN_VERSION "",
#if (defined __TIME__) && (defined __DATE__)
		" Compiled at:           " __DATE__ " " __TIME__ "",
#endif
		" Compiler used:         " COMPILER_VERSION "",
		"",
		" Forked off from 'Kraken' by 'Frank A. Stevenson'",
		" and highly customized by 'g3gg0.de' (a5@g3gg0.de)",
		"",
		" Thanks & Credits to:   | Frank A. Stevenson |",
		"                        | Sascha Krissler    |",
		"                        | Sylvain Munaut     |",
		"----------------------------------------------------"
};

const char *HelpMessages[] =
{
	" Kraken-win32 Help:",
	"",
	" (<parm> are required parameters, [parm] are optional)",
	"",
	" - crack <bits1> [<COUNT1> <bits2> <COUNT2>]",
	"    will try to find the Kc if bitsx and COUNTx are given.",
	"    if just the bits are given and no COUNT etc, kraken",
	"    will report the intermediate result as usual, but",
	"    with some other formatting.",
	"    e.g. 'crack 10101101...1101 12332 111001010...1101 12351'",
	"         'crack 10101010...0001010101' (original kraken style)",
	"",
	" - cancel [id]",
	"    cancel request given by id.",
	"",
	" - test",
	"    queue the standard kraken testbits to run a crack.",
	"",
	" - perf {disk,gpu,cpu} [requests]",
	"    queue requests to either HDD, GPU or CPU to test its speed.",
	"    without parameter [reads] it will queue 2000/32000/1000 requests.",
	"",
	" - progress [id]",
	"    show current progress of given job.",
	"",
	" - tables",
	"    dump table information.",
	"",
	" - status",
	"    short status message how many jobs are queued or running and how many",
	"    keys (Kc) were found during this session.",
	"",
	" - jobs",
	"    show details about all jobs currently running.",
	"",
	" - stats",
	"    dump performance relevant stats.",
	"",
	" - about",
	"    some details about this build.",
	"",
	" - suspend",
	"    unload tables from RAM. they will be reloaded with the next request.",
	"",
	" - help",
	"    you are reading this.",
	"",
	" - idle",
	"    guess what!",
	"",
	" - quit (console only)",
	"    will quit the server",
	""
};

/* we want to decouple plugin and Kraken threads. here is all stuff to receive messages asynchronously */
t_mutex AsyncTransmitMutex;
char AsyncMessage[MAX_MSG_LENGTH + 1];
int AsyncClientId = -1;

/**
 *  Receive and parse commands from clients
 */
void serverCmd(int clientID, char *cmd)
{
	mutex_lock(&AsyncTransmitMutex);

	/* wait until last message was processed if any. */
	while(AsyncClientId >= 0)
	{
		Sleep(50);
	}

	strncpy(AsyncMessage, cmd, MAX_MSG_LENGTH);
	AsyncClientId = clientID;

	mutex_unlock(&AsyncTransmitMutex);
}

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
    mHalted = false;
	mRequestId = 0;

	mRequests = 0;
	mFoundKc = 0;
	mFailedKc = 0;

	mJobIncrementSet = false;
	mJobIncrement = 180;

	mWriteClient = NULL;
	mWriteClientCtx = NULL;
	mServerCmd = serverCmd;

	printf("\r\n");
	printf(" Starting Kraken\r\n");
	printf("-----------------\r\n");
	printf("\r\n");

    mServer = NULL;
    if (server_port) {
        mServer = new ServerCore(server_port, serverCmd);
    }

    string configFile = string(config)+DIR_SEP+string("tables.conf");
    FILE* fd = fopen(configFile.c_str(),"rb");
    if (fd==NULL) {
        printf(" [E] Could not find %s\n", configFile.c_str());
        exit(0);
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
				printf(" [x] Loaded tables: %i    ", (int)(mTables.size() + 1));
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
            else if (strncmp(&pFile[pos],"Extension:",10)==0) {
				char extension[64];

                sscanf(&(pFile[pos+11]),"%s", extension);

				LoadExtension(extension, &(pFile[pos+11 + strlen(extension) + 1]));

            }
            pos += len;
        }
        pos++;
    }
    mTableInfo = string("Tables: ")+mTableInfo+string("\n");

    free(pFile);

	printf("\r\n");

    /* Init semaphore */
    mutex_init( &mMutex );
    mutex_init( &mWasteMutex );
	mutex_init( &mConsoleMutex );

    A5CpuInit(8, 12, 4);
    mUsingGpu = A5GpuInit(8,12,0xffffffff,1);

	gettimeofday(&mLastJobTime, NULL);
	mBusy = false;
	mTablesLoaded = true;
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

bool Kraken::LoadExtension(char *extension, char *parms)
{	
	char file[64];
	bool (*fInit)(Kraken *, char *) = NULL;
    void* lHandle = NULL;

	sprintf(file, "%s" DL_EXT, extension);
	lHandle = DL_OPEN(file);

#ifndef WIN32
    char* lError = dlerror();
    if (lError) {
        fprintf(stderr, " [E] Error when opening A5Cpu"DL_EXT": %s\n", lError);
        return false;
    }
#else
    if (lHandle == NULL) {
        fprintf(stderr, " [E] Error when opening %s: 0x%08X\n", file, GetLastError());
        return false;
    }
#endif

	fInit = (bool (*)(Kraken *, char *))DL_SYM(lHandle, "ext_init");
#ifndef WIN32
    lError = dlerror();
    if (lError) {
        fprintf(stderr, " [E] Error when loading symbol 'ext_init' from %s: %s%s\n", file, file, lError);
        return false;
    }
#else
    if (*fInit == NULL) {
        fprintf(stderr, " [E] Error when loading symbol 'ext_init' from %s: 0x%08X\n", file, GetLastError());
        return false;
    }
#endif

	return fInit(this, parms);
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
		A5GpuShutdown();

		tableListIt it = mTables.begin();
		while (it!=mTables.end()) {
			delete (*it).second;
			it++;
		}

		for (int i=0; i<mNumDevices; i++) {
			delete mDevices[i];
		}

		mutex_destroy(&mMutex);
	}
}

/**
 *  Recieve a crack command and insert into mutex protected queue.
 */
uint64_t Kraken::Crack(int client, const char* plaintext)
{
	t_job job;

	/* prepare the job structure */
	job.client_id = client;
	job.order = string(plaintext);
	job.max_fragments = 0;
	job.key_found = false;
	gettimeofday(&job.start_time, NULL);

	/* lock and submit */
    mutex_lock(&mMutex);

	job.job_id = mRequestId;
	mJobs[job.job_id] = job;
    mNewJobs.push_back(job.job_id);
	mRequests++;
	mRequestId++;

    mutex_unlock(&mMutex);

	return job.job_id;
}

/**
 *  Main woker thread loop
 */
bool Kraken::Tick()
{
    mutex_lock(&mMutex);

	/* was there a message to process? */
	if(AsyncClientId >= 0)
	{
		handleServerCmd(AsyncClientId, AsyncMessage);
		AsyncClientId = -1;
	}

	/* using a code block to keep variables in smaller scope */
	{	
		Fragment* frag;
		uint64_t start_val;
		uint64_t stop_val;
		int32_t start_rnd;
		uint64_t job_id;

		/* we dont need the job id anyway */
		while (A5CpuPopResult(job_id, start_val, stop_val, start_rnd, (void**)&frag)) 
		{
			frag->handleSearchResult(stop_val, start_rnd);
		}

		if (mUsingGpu) 
		{
			while (A5GpuPopResult(job_id, start_val, stop_val, (void**)&frag)) 
			{
				frag->handleSearchResult(stop_val, 0);
			}
		}
	}

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

	/* any new jobs? */
	if (mNewJobs.size() > 0)
	{
		char msg[256];

        /* Start a new job if there is some order */
        if (mNewJobs.size() > 0)
		{
            mBusy = true;

			if(!mTablesLoaded)
			{
				LoadTables();
			}

			mFoundKeys = 0;

            gettimeofday(&mStartTime, NULL);

			uint64_t jobId = mNewJobs.front();
			int client = mJobs[jobId].client_id;
            string work = mJobs[jobId].order;

            mNewJobs.pop_front();

            char *plaintext = strdup(work.c_str());

			snprintf(msg, 256, "102 %i Processing your request now\r\n", (int)jobId);
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
			size_t samples = len - 63;
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

				/* go through all loaded tables */
                tableListIt it = mTables.begin();
                while (it!=mTables.end())
				{
					/* Skip if not marked active */
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

						mJobs[jobId].fragments[fr] = 0;

						/* submit that job to be processed */
						if(mUsingGpu)
						{
							A5GpuSubmit(jobId, plain, k, (*it).first, fr);
						}
						else
						{
							A5CpuSubmit(jobId, plain, k, (*it).first, fr);
						}

						submitted++;
                    }
                    it++;
                }
            }

			mJobs[jobId].max_fragments = submitted;

			/* no tables active for this job */
			if(!submitted)
			{
				cancelJobFragments(jobId, (char*)"400 No tables match your criteria.\r\n");
			}

			free(plaintext);
        }
    }
	else
	{
		mBusy = false;
	}

	/* free the fragments that are queued for deletion */
    mutex_lock(&mWasteMutex);

	map<Fragment*,uint64_t>::iterator fi = mWastedFragments.begin();
	while(fi != mWastedFragments.end())
	{
		removeFragment(fi->first);
		fi++;
	}
	mWastedFragments.clear();

    mutex_unlock(&mWasteMutex);
    mutex_unlock(&mMutex);

    return mBusy;
}
  
/**
 * Report a found key back to the issuing client
 */
void Kraken::queueFragmentRemoval(Fragment *frag, bool table_hit, uint64_t result)
{
	bool deleted = false;
	int client_id = frag->getClientId();
	uint64_t job_id = frag->getJobNum();

	if(table_hit)
	{
		unsigned char keyData[8];
		char msg[256];

		/* output to client */
		snprintf(msg, 256, "103 %i %016llX %i (found a table hit in table %i)\r\n", (int)job_id, result, (int)frag->getBitPos(), frag->getAdvance());
		sendMessage(msg, client_id);

		/* was this a Kc cracking command with reference bits? */
		if(frag->getBitsRef() != NULL)
		{
			if(find_kc(result, (int)frag->getBitPos(), frag->getCount(), frag->getCountRef(), frag->getBitsRef(), keyData))
			{
				/* Kc found, cleanup. reporting. */
				memcpy(mJobs[job_id].key_data, keyData, 8);
				mJobs[job_id].key_found = true;

				sendJobResult(job_id);
				cancelJobFragments(job_id, NULL);
				deleted = true;
			}
		}
	}

	/* no hit or no key found, only remove this fragment */
	if(!deleted)
	{
		mutex_lock(&mWasteMutex);
		mWastedFragments[frag] = frag->getJobNum();
		mutex_unlock(&mWasteMutex);
	}

	MEMCHECK();
}


void Kraken::sendJobResult(uint64_t job_id)
{
	char msg[128];
	char builder[128];
	char details[128];

	struct timeval tv;
	gettimeofday(&tv, NULL);
	unsigned long diff = 1000000 * (tv.tv_sec - mJobs[job_id].start_time.tv_sec);
	diff += tv.tv_usec - mJobs[job_id].start_time.tv_usec;

	snprintf(details, 128, "search took %i sec", (int)(diff/1000000));

	if(!mJobs[job_id].key_found)
	{
		mFailedKc++;
		snprintf(msg, 128, "404 %i Key not found (%s)\r\n", (int)job_id, details);

		/* only update on full table scans */
		if(!mJobIncrementSet)
		{
			mJobIncrementSet = true;
			mJobIncrement = (int)(diff/1000000);
		}
	}
	else
	{
		mFoundKc++;
		sprintf(msg, "200 %i ", (int)job_id);
		for(int pos = 0; pos < 8; pos++)
		{
			sprintf(builder, "%02X", mJobs[job_id].key_data[pos]);
			strcat(msg, builder);
		}

		strcat(msg," Key found (");
		strcat(msg, details);
		strcat(msg,")\r\n");

	}
	sendMessage(msg, mJobs[job_id].client_id);
}

/**
 *  Remove and delete a fragment from the work list maps
 */
void Kraken::removeFragment(Fragment* frag)
{
    mutex_lock(&mMutex);
	uint64_t job_id = frag->getJobNum();
	
	if(mJobs.find(job_id) != mJobs.end())
	{
		/* delete fragment */
		map<Fragment *, int>::iterator it = mJobs[job_id].fragments.find(frag);
		if (it!=mJobs[job_id].fragments.end()) 
		{
			mJobs[job_id].fragments.erase(it);
			delete frag;
		}
		
		/* no fragments anymore? */
		if(mJobs[job_id].fragments.size() == 0)
		{
			/* Kc not found, cleanup. reporting. */
			sendJobResult(job_id);
			mJobs.erase(job_id);
		}
	}

    mutex_unlock(&mMutex);
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
void Kraken::cancelJobFragments(uint64_t jobId, char *message)
{
	A5GpuSpinLock(true);
	A5CpuSpinLock(true);
	deviceSpinLock(true);

    mutex_lock(&mMutex);
    mutex_lock(&mWasteMutex);

	/* now cancel all disk transfers */
	for (unsigned int i=0; i<mDevices.size(); i++) 
	{
		mDevices[i]->Cancel(jobId);
    }	

	/* then cancel all fragment operations */
    map<Fragment*,int>::iterator it = mJobs[jobId].fragments.begin();
    while (it!=mJobs[jobId].fragments.end()) 
	{
		/* already in queue to be wasted? */
		if(mWastedFragments.find(it->first) != mWastedFragments.end())
		{
			/* remove! */
			mWastedFragments.erase(it->first);
		}
		delete (it->first);
		it++;
    }
	mJobs[jobId].fragments.clear();

	A5GpuCancel(jobId);
	A5CpuCancel(jobId);

	if(message)
	{
		sendMessage(message, mJobs[jobId].client_id);
	}

	
	/* this job never existed...... job? which job? */
	mJobs.erase(jobId);
	
	mutex_unlock(&mWasteMutex);
    mutex_unlock(&mMutex);
	
	A5GpuSpinLock(false);
	A5CpuSpinLock(false);
	deviceSpinLock(false);
}

void Kraken::sendMessage(char *msg, int client)
{	
	/* make sure the console input interface does not corrupt our output */
	mutex_lock(&mConsoleMutex);

	/* clear the last output. usually just the Kraken> prompt but maybe also some user input */
	printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
	printf("\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8\x8");
	/* make sure we are printing at the first column */
	printf("\r");

	if(client != -1)
	{
		bool handled = false;

		if(!handled && mWriteClient)
		{
			handled = mWriteClient(mWriteClientCtx, client, msg);
		}
		
		if(!handled && mServer)
		{
			handled = mServer->Write(client, msg);
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
	mutex_unlock(&mConsoleMutex);
}


double Kraken::GetJobProgress(uint64_t jobId)
{
	double progress = -1.0f;

    mutex_lock(&mMutex);

	/* is there a job? */
	if(mJobs.find(jobId) != mJobs.end())
	{
		/* already fragments queued? */
		if(mJobs[jobId].max_fragments > 0)
		{
			progress = 100.0f - ((100.0f * mJobs[jobId].fragments.size()) / mJobs[jobId].max_fragments);
		}
		else
		{
			progress = 0.0f;
		}
	}

    mutex_unlock(&mMutex);

	return progress;
}


class KrakenPerfDisk : NcqRequestor {

private:
	uint64_t mRequestsRunning;
	vector<NcqDevice*> mDevices;
	Kraken *mKraken;
    t_mutex mMutex;
	int mClient;

public:
	KrakenPerfDisk(Kraken *kraken, int clientID, vector<NcqDevice*> devices)
	{
		mutex_init( &mMutex );
		mKraken = kraken;
		mClient = clientID;
		mDevices = devices;
		mRequestsRunning = 0;   
		
		srand( (unsigned)time( NULL ) );
	}

    void Start(uint64_t requests)
	{
		mutex_lock(&mMutex);
		for (uint64_t req=0; req<requests; req++) {
			for (int i=0; i<(int)mDevices.size(); i++) {
				uint64_t blockNo = (uint64_t)(((double)rand() / RAND_MAX) * (mDevices[i]->getMaxBlockNum()));

				mDevices[i]->Request(UINT64_MAX, this, blockNo);
				mRequestsRunning++;
			}
		}
		mutex_unlock(&mMutex);
	}

	bool processBlock(const void* pDataBlock)
	{
		mutex_lock(&mMutex);
		mRequestsRunning--;
		mutex_unlock(&mMutex);

		if(!mRequestsRunning)
		{
			mKraken->sendMessage((char*)"216 Performance test finished. Retrieve results with 'stats'\r\n", mClient);
			delete this;
			return false;
		}

		return true;
	}
};


class KrakenPerfA5 : Fragment {

private:
	uint64_t mRequestsRunning;
	uint64_t mTotalRequests;
	Kraken *mKraken;
    t_mutex mMutex;
	int mClient;
	struct timeval mStartTime;
	bool mGpu;

public:
	KrakenPerfA5(Kraken *kraken, int clientID, bool gpu)
	{
		mutex_init( &mMutex );
		mKraken = kraken;
		mClient = clientID;
		mRequestsRunning = 0;
		mGpu = gpu;
		
		srand( (unsigned int)time( NULL ) );
	}

    void Start(uint64_t requests)
	{
		uint64_t start_value = 0;

		mTotalRequests = requests;
		gettimeofday(&mStartTime, NULL);

		mutex_lock(&mMutex);
		for (uint64_t req=0; req<requests; req++) 
		{
			if(mGpu)
			{
				A5GpuSubmit(UINT64_MAX, start_value, 0, 0, this);
			}
			else
			{
				A5CpuSubmit(UINT64_MAX, start_value, 0, 0, this);
			}
			mRequestsRunning++;
		}
		mutex_unlock(&mMutex);
	}

	bool handleSearchResult(uint64_t result, int start_round)
	{
		mutex_lock(&mMutex);
		mRequestsRunning--;
		mutex_unlock(&mMutex);

		/* thats the result of start_value=0, start_round=0 */
		if(result != 0xd5258184902f7000ULL || start_round != 0)
		{
			char msg[256];
			sprintf(msg, "216 A5 Performance test failed. Algo returned invalid result value.\r\n" );
			mKraken->sendMessage(msg, mClient);

			delete this;
			return false;
		}

		if(!mRequestsRunning)
		{
			struct timeval stopTime;

			gettimeofday(&stopTime, NULL);
			uint64_t diff = 1000000 * (stopTime.tv_sec - mStartTime.tv_sec);
			diff += stopTime.tv_usec - mStartTime.tv_usec;

			double calcs = ((double)mTotalRequests / (double)diff) * 1000000.0f;

			char msg[256];
			sprintf(msg, "216 A5 Performance test finished. Average %.3f calcs/s \r\n", calcs );
			mKraken->sendMessage(msg, mClient);

			delete this;
			return false;
		}

		return true;
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
 *   401 - not authorized
 *   405 - not allowed
 *   404 [id] - no key found for this request
 *   405 [id] - job was cancelled
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
 *   222 - response to "about"-command
 *   224 - response to "help"-command
 *   
 */

void Kraken::handleServerCmd(int clientID, char * command)
{
	char msg[512];

	if(strlen(command) <= 2)
	{
		return;
	}

    if (strncmp(command,"test",4)==0)
	{
		size_t queued = mJobs.size();

		/* already processing one request? */
		if(mBusy)
		{
			queued++;
		}

        // Test frame 998
        uint64_t id = Crack(clientID, "001101110011000000001000001100011000100110110110011011010011110001101010100100101111111010111100000110101001101011");

		sprintf(msg, "101 %d Request queued (%d already in queue).\r\n", (int)id, (int)queued );
	}
	else if (!strncmp(command,"tables",6)) 
	{
        /* Return a printed list of loaded tables */
        sprintf(msg,"219 %s",mInstance->mTableInfo.c_str());
	}
	else if (!strncmp(command,"halt",4)) 
	{
		mHalted = true;
        sprintf(msg,"225 Server halted\r\n");
	}
	else if (!strncmp(command,"continue",4)) 
	{
		mHalted = false;
        sprintf(msg,"225 Server running again\r\n");
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
		if(mBusy)
		{
			sprintf(msg, "400 Releasing memory not possible, kraken is busy.\r\n");
		}
		else
		{
			UnloadTables();
			sprintf(msg, "217 Released memory. Will refill when next statement gets processed.\r\n");
		}
	}
    else if (!strncmp(command,"help",4))
	{
		int entries = sizeof(HelpMessages) / sizeof(const char*);

		for(int pos = 0; pos < entries; pos++)
		{
			sprintf(msg, "223 %s\r\n", HelpMessages[pos] );
			sendMessage(msg, clientID);
		}

		sprintf(msg, "223\r\n" );
    }
    else if (!strncmp(command,"about",5))
	{
		int entries = sizeof(AboutMessages) / sizeof(const char*);

		for(int pos = 0; pos < entries; pos++)
		{
			sprintf(msg, "222 %s\r\n", AboutMessages[pos] );
			sendMessage(msg, clientID);
		}

		sprintf(msg, "222\r\n" );
	}
    else if (!strncmp(command,"status",6))
	{
		size_t queued = mJobs.size();

		/* already processing one request? */
		if(mBusy)
		{
			queued++;
		}

		sprintf(msg, "210 Kraken server (%i jobs in queue, %i processed, %i keys found, %i not found, tables currently %s)'\r\n", (int)queued, mRequests, mFoundKc, mFailedKc, (mTablesLoaded?"in RAM":"not loaded"));
	}
    else if (!strncmp(command,"stats",5))
	{
		for (int i=0; i<mNumDevices; i++) 
		{
			sprintf(msg, "214 HDD %s\r\n", mDevices[i]->GetDeviceStats());
			sendMessage(msg, clientID);
		}

		char *cpuStats = A5CpuGetDeviceStats();
		if(cpuStats != NULL && strlen(cpuStats) > 0)
		{
			sprintf(msg, "214 CPU %s\r\n", cpuStats);
			sendMessage(msg, clientID);
		}

		char *gpuStats = A5GpuGetDeviceStats();
		if(gpuStats != NULL && strlen(gpuStats) > 0)
		{
			/* split comma-separated list of cores into seperate lines */
			char *end = NULL;

			do
			{
				end = strchr(gpuStats, ',');
				if(end != NULL)
				{
					*end = '\000';
				}

				if(strlen(gpuStats) > 0)
				{
					sprintf(msg, "214 GPU %s\r\n", gpuStats);
					sendMessage(msg, clientID);
				}

				if(end != NULL)
				{
					gpuStats = end + 1;
				}
			} while(end != NULL);
		}

		sprintf(msg, "214\r\n" );
	}
    else if (!strncmp(command,"crack",5))
	{
		if(mHalted)
		{
			sprintf(msg, "403 Forbidden. Server is currently halted.\r\n" );
		}
		else
		{
			const char* ch = command + 5;
			while (*ch && (*ch!='0') && (*ch!='1')) ch++;
			size_t len = strlen(ch);

			if (len>63)
			{
				size_t queued = mJobs.size();

				/* already processing one request? */
				if(mBusy)
				{
					queued++;
				}

				uint64_t id = Crack(clientID, ch);
				sprintf(msg, "101 %d Request queued (%i already in queue).\r\n", (int)id, (int)queued );
			}
			else
			{
				sprintf(msg, "400 Bad request\r\n" );
			}
		}
    }
    else if (!strncmp(command,"idle",4))
	{
		sprintf(msg, "211 Idle you are?\r\n");
    }
    else if (!strncmp(command,"cancel",6))
	{
		const char *parm = command + 6;
		int job_id = -1;

		/* cancel all jobs */
			/* not working :(
		if(!strncmp(parm, " *", 2))
		{
			mutex_lock(&mMutex);
			map<uint64_t,t_job>::iterator it = mJobs.begin();
			
			while (it!=mJobs.end()) 
			{
				char clientMsg[256];

				sprintf(clientMsg, "405 %i Your request was cancelled by client %i.\r\n", (int)(*it).first, clientID);
				cancelJobFragments((*it).first, clientMsg);
				it++;
			}

			mutex_unlock(&mMutex);
			sprintf(msg, "212 Cancelled all jobs.\r\n");
		}
		else
		*/
		{
			if(strlen(parm) > 0)
			{
				sscanf(parm, "%i", &job_id);
			}

			if(job_id >= 0)
			{
				char clientMsg[256];

				sprintf(msg, "212 Cancelled job %i.\r\n", job_id);
				sprintf(clientMsg, "405 %i Your request was cancelled by client %i.\r\n", job_id, clientID);
				cancelJobFragments(job_id, clientMsg);
			}
			else
			{
				sprintf(msg, "400 You have to specify a job to cancel.\r\n");
			}
		}
    }
    else if (!strncmp(command,"fake",4))
	{
		sprintf(msg, "400 Not working in this version due to restructured job handling. May come again.\r\n");
		/*
		sprintf(msg, "213 Faking result of currently running request (if any) and will report key EEEE[...]EE\r\n");

		memset(mKeyResult, 0xEE, 8);
		mFoundKeys = 1;
        clearFragments();
		*/
    }
    else if (!strncmp(command,"jobs",4))
	{
		mutex_lock(&mMutex);
		map<uint64_t,t_job>::iterator it = mJobs.begin();
		sprintf(msg, "220 Active jobs:\r\n");
		sendMessage(msg, clientID);

		unsigned int histogram[6] = { 0, 0, 0, 0, 0, 0 };
		int total = 0;

		while (it!=mJobs.end()) 
		{
			uint64_t job_id = (*it).first;
			float progress = 100.0f - ((100.0f * mJobs[job_id].fragments.size()) / mJobs[job_id].max_fragments);
			struct timeval tv;
			gettimeofday(&tv, NULL);
			unsigned long diff = 1000000 * (tv.tv_sec - mJobs[job_id].start_time.tv_sec);
			diff += tv.tv_usec - mJobs[job_id].start_time.tv_usec;

			sprintf(msg, "220    %4i: active %i sec, progress %3.2f %%\r\n", (int)(*it).first, (int)(diff/1000000), progress );
			sendMessage(msg, clientID);

			map<Fragment*,int>::iterator it2 = mJobs[job_id].fragments.begin();
			while (it2!=mJobs[job_id].fragments.end()) {
				int state = (*it2).first->getState();
				if ((state>=0)&&(state<6)) {
					histogram[state]++;
					total++;
				}
				it2++;
			}

			it++;
		}

		mutex_unlock(&mMutex);

		sprintf(msg, "220\r\n");
		sendMessage(msg, clientID);
		sprintf(msg, "220 Work units:  %6d\r\n", total);
		sendMessage(msg, clientID);
		sprintf(msg, "220       init:  %6d\r\n", histogram[0]);
		sendMessage(msg, clientID);
		sprintf(msg, "220        HDD:  %6d\r\n", histogram[1]);
		sendMessage(msg, clientID);
		sprintf(msg, "220        CPU:  %6d\r\n", histogram[2]);
		sendMessage(msg, clientID);
		sprintf(msg, "220        GPU:  %6d\r\n", histogram[3]);
		sendMessage(msg, clientID);
		sprintf(msg, "220       done:  %6d\r\n", histogram[4]);
		sendMessage(msg, clientID);
		sprintf(msg, "220       fail:  %6d\r\n", histogram[5]);
		sendMessage(msg, clientID);
		
		sprintf(msg, "220\r\n");
    }
    else if (!strncmp(command,"progress",8))
	{		
		const char *parm = command + 8;
		int job_id = -1;

		if(strlen(parm) > 0)
		{
			sscanf(parm, "%i", &job_id);
		}

		double progress = GetJobProgress(job_id);

		if(progress >= 0)
		{
			sprintf(msg, "221 %i Progress of job is %2.2f %%\r\n", job_id, progress );
		}
		else
		{
			sprintf(msg, "221 %i No such job\r\n", job_id );
		}
    }
    else if (!strncmp(command,"quit",4))
	{
		if(clientID == -1)
		{
			sprintf(msg, "666 says goodbye\r\n");
			mRunning = false;
		}
		else
		{
	        sprintf(msg, "400 Sorry, you may not shutdown the beast remotely for obvious reason\r\n" );
		}
    }
    else if (!strncmp(command,"perf",4))
	{
		bool requests_disabled = false;
        const char* ch = command + 4;
        while (*ch && (*ch==' ')) ch++;

		if(!strncmp(ch,"disk",4))
		{
			unsigned long seeks = 2000;
			ch = ch + 4;
			while (*ch && (*ch==' ')) ch++;

			if(*ch && !requests_disabled && (sscanf(ch, "%lu", &seeks) != 1))
			{
	            sprintf(msg, "400 Bad request\r\n" );
			}
			else
			{
				sprintf(msg, "215 Starting disk performance test. (queueing %lu random reads)\r\n", seeks );
				KrakenPerfDisk *perf = new KrakenPerfDisk(this, clientID, mDevices);
				perf->Start(seeks);
			}
		}
		else if(!strncmp(ch,"gpu",3))
		{
			unsigned long calcs = 32000;
			ch = ch + 3;
			while (*ch && (*ch==' ')) ch++;

			if(*ch && !requests_disabled && sscanf(ch, "%lu", &calcs) != 1)
			{
	            sprintf(msg, "400 Bad request\r\n" );
			}
			else
			{
				sprintf(msg, "215 Starting GPU performance test. (queueing %lu nullvalue-calcs)\r\n", calcs );
				KrakenPerfA5 *perf = new KrakenPerfA5(this, clientID, true);
				perf->Start(calcs);
			}
		}
		else if(!strncmp(ch,"cpu",3))
		{
			unsigned long calcs = 1000;
			ch = ch + 3;
			while (*ch && (*ch==' ')) ch++;

			if(*ch && !requests_disabled && sscanf(ch, "%lu", &calcs) != 1)
			{
	            sprintf(msg, "400 Bad request\r\n" );
			}
			else
			{
				sprintf(msg, "215 Starting CPU performance test. (queueing %lu nullvalue-calcs)\r\n", calcs );
				KrakenPerfA5 *perf = new KrakenPerfA5(this, clientID, false);
				perf->Start(calcs);
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

	sendMessage(msg, clientID);
}

void *Kraken::consoleThread(void *arg)
{
    char command[1025];
	Kraken* kraken = (Kraken*)arg;

    printf("\n");
    printf("Started '" KRAKEN_VERSION "'\n");
    printf("Commands are: crack test status stats fake cancel about help quit\n");
    printf("\n");
	printf("Kraken> ");

	while(kraken->mRunning) {
		char* ch = fgets(command, 1024, stdin);

		/* make sure the console writer does not corrupt our output */
		mutex_lock(&kraken->mConsoleMutex);

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
		mutex_unlock(&kraken->mConsoleMutex);

		/* process command */
		kraken->handleServerCmd(-1, command);
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

	mutex_init(&AsyncTransmitMutex);

    while (kr.mRunning) {
		if(!kr.mHalted)
		{
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
		else
		{
			usleep(1000);        
		}
    }

	kr.Shutdown();

	return 0;
}
