/***************************************************************
 * A5/1 Chain generator.
 *
 * Copyright 2009. Frank A. Stevenson. All rights reserved.
 *
 * Permission to distribute, modify and copy is granted to the
 * TMTO project, currently hosted at:
 * 
 * http://reflextor.com/trac/a51
 *
 * Code may be modifed and used, but not distributed by anyone.
 *
 * Request for alternative licencing may be directed to the author.
 *
 * All (modified) copies of this source must retain this copyright notice.
 *
 *******************************************************************/
#ifndef A5_CPU
#define A5_CPU

/* DLL export incatantion */
#if defined _WIN32 || defined __CYGWIN__
#  ifdef BUILDING_DLL
#    ifdef __GNUC__
#      define DLL_PUBLIC __attribute__((dllexport))
#    else
#      define DLL_PUBLIC __declspec(dllexport)
#    endif
#  else
#    ifdef __GNUC__
#      define DLL_PUBLIC __attribute__((dllimport))
#    else
#      define DLL_PUBLIC __declspec(dllimport)
#    endif
#  endif
#  define DLL_LOCAL
#else
#  if __GNUC__ >= 4
#    define DLL_PUBLIC __attribute__ ((visibility("default")))
#    define DLL_LOCAL  __attribute__ ((visibility("hidden")))
#  else
#    define DLL_PUBLIC
#    define DLL_LOCAL
#  endif
#endif



#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include "Advance.h"
#include "Globals.h"
#include <map>
#include <queue>
#include <deque>
#include <list>

using namespace std;

typedef struct
{
	uint64_t job_id;
	uint64_t start_value;
	uint32_t start_round;
	uint32_t end_round;
	uint32_t advance;
	uint64_t target;
	void* context;
} t_a5_request;

typedef struct
{
	uint64_t job_id;
	uint64_t start_value;
	uint64_t end_value;
	uint32_t start_round;
	void* context;
} t_a5_result;

class DLL_LOCAL A5Cpu {
public:
  A5Cpu(int max_rounds, int condition, int threads);
  ~A5Cpu();

  void Shutdown();
  int  Submit(uint64_t job_id, uint64_t start_value, uint64_t target_value, int32_t start_round, int32_t stop_round, uint32_t advance, void* context);
  bool PopResult(uint64_t& job_id, uint64_t& start_value, uint64_t& stop_value, int32_t& start_round, void** context);
  bool IsIdle();
  void SpinLock(bool state);
  void Clear();
  void Cancel(uint64_t job_id);
  static uint64_t ReverseBits(uint64_t r);
  static int PopcountNibble(int x);

private:
  void CalcTables(void);
  void Process(void);

  int mNumThreads;
  pthread_t* mThreads;
  static void* thread_stub(void* arg);
  static A5Cpu* mSpawner;

  unsigned int mCondition;
  unsigned int mMaxRound;

  bool mIsUsingTables;
  uint16_t mClockMask[16*16*16];
  unsigned char mTable6bit[1024];
  unsigned char mTable5bit[512];
  unsigned char mTable4bit[256];

  bool mRunning; /* false stops worker thread */
  bool mWait;
  int mWaiting;

  /* Mutex semaphore to protect the queues */
  t_mutex mMutex;
  uint64_t mRequestCount;
  map<uint64_t, deque<t_a5_request> > mRequests;
  map<uint64_t, deque<t_a5_result> > mResults;

  map< uint32_t, class Advance* > mAdvances;
};

#endif
