#include <stdio.h>
#include <iostream>
#include <list>
#include <assert.h>

typedef unsigned long long uint64_t;

#define MAX_STEPS 250

#define STATE2LFSR1(state) ((state)&0x7ffff)
#define STATE2LFSR2(state) (((state)>>19)&0x3fffff)
#define STATE2LFSR3(state) (((state)>>41)&0x7fffff)

#define LFSR12STATE(lfsr) ((lfsr)&0x7ffff)
#define LFSR22STATE(lfsr) (((lfsr)&0x3fffff)<<19)
#define LFSR32STATE(lfsr) (((lfsr)&0x7fffff)<<41)

#define LFSR2STATE(lfsr1,lfsr2,lfsr3) (LFSR12STATE(lfsr1)|LFSR22STATE(lfsr2)|LFSR32STATE(lfsr3))

#define NEXTBIT1(lfsr1) (((lfsr1&(1<<18))?1:0)^((lfsr1&(1<<17))?1:0)^((lfsr1&(1<<16))?1:0)^((lfsr1&(1<<13))?1:0))
// multiplying the to-be xorred bits with the inversed bitmask, gives us the xorred result we need at position 31
#define NEXTBIT2(lfsr2) ((((lfsr2&0x300000)*0xc00)>>31)&1)
#define NEXTBIT3(lfsr3) (((lfsr3&(1<<22))?1:0)^((lfsr3&(1<<21))?1:0)^((lfsr3&(1<<20))?1:0)^((lfsr3&(1<<7))?1:0))

#define UNWINDBIT1(lfsr1) (1&(lfsr1 ^ (lfsr1>>18) ^ (lfsr1>>17) ^ (lfsr1>>14)))
#define UNWINDBIT2(lfsr2) (1&(lfsr2 ^ (lfsr2>>21)))
#define UNWINDBIT3(lfsr3) (1&(lfsr3 ^ (lfsr3>>22) ^ (lfsr3>>21) ^ (lfsr3>>8)))

#define CB1(state) ((STATE2LFSR1(state)&(1<<8))?1:0)
#define CB2(state) ((STATE2LFSR2(state)&(1<<10))?1:0)
#define CB3(state) ((STATE2LFSR3(state)&(1<<10))?1:0)

class A5Backwards {
public:
	A5Backwards();

	void ClockBack(uint64_t final, int steps, int offset1=0, int offset2=0, int offset3=0);

	uint64_t Forwards(uint64_t start, int steps);

private:
	void FillBack(uint64_t final);

	std::list<uint64_t> mCandidates;
	uint64_t mBack1[MAX_STEPS];
	uint64_t mBack2[MAX_STEPS];
	uint64_t mBack3[MAX_STEPS];
};


A5Backwards::A5Backwards() {
}

void A5Backwards::FillBack(uint64_t final) {
		uint64_t lfsr1 = STATE2LFSR1(final);
		uint64_t lfsr2 = STATE2LFSR2(final);
		uint64_t lfsr3 = STATE2LFSR3(final);

		/* precalculate MAX_STEPS backwards clockings of all lfsrs */
		for (int i=0; i<MAX_STEPS; i++) {
			mBack1[i] = LFSR12STATE(lfsr1);
			lfsr1 = (lfsr1>>1) | (UNWINDBIT1(lfsr1)<<18);
		}

		for (int i=0; i<MAX_STEPS; i++) {
			mBack2[i] = LFSR22STATE(lfsr2);
			lfsr2 = (lfsr2>>1) | (UNWINDBIT2(lfsr2)<<21);
		}

		for (int i=0; i<MAX_STEPS; i++) {
			mBack3[i] = LFSR32STATE(lfsr3);
			lfsr3 = (lfsr3>>1) | (UNWINDBIT3(lfsr3)<<22);
		}

		/***
		 * Verify with MAX_STEPS steps forwards
		 *
		 * (Only used to verify code correctness)
		 **/
		uint64_t wlfsr1 = lfsr1;
		uint64_t wlfsr2 = lfsr2;
		uint64_t wlfsr3 = lfsr3;
		for (int i=0; i<MAX_STEPS; i++) {
			/* Clock the different lfsr */
			wlfsr1 = (wlfsr1<<1) | NEXTBIT1(wlfsr1);
			wlfsr2 = (wlfsr2<<1) | NEXTBIT2(wlfsr2);
			wlfsr3 = (wlfsr3<<1) | NEXTBIT3(wlfsr3);
		}

		uint64_t cmp = LFSR2STATE(wlfsr1,wlfsr2,wlfsr3);
		// std::cout << std::hex << final << " -> " << cmp << "\n";

		assert(cmp==final);
}


uint64_t A5Backwards::Forwards(uint64_t state, int steps)
{
	uint64_t lfsr1 = STATE2LFSR1(state);
	uint64_t lfsr2 = STATE2LFSR2(state);
	uint64_t lfsr3 = STATE2LFSR3(state);

	for (int i=0; i<steps; i++) {
		/* Majority count */
		int count = (CB1(state)+CB2(state)+CB3(state)) >= 2 ? 1:0;
		
		/* Clock the different lfsr */
		if ( CB1(state)==count) lfsr1 = (lfsr1<<1) | NEXTBIT1(lfsr1);
		if ( CB2(state)==count) lfsr2 = (lfsr2<<1) | NEXTBIT2(lfsr2);
		if ( CB3(state)==count) lfsr3 = (lfsr3<<1) | NEXTBIT3(lfsr3);

		state = LFSR2STATE(lfsr1,lfsr2,lfsr3);
	}
	
	return state;
}

bool showCandidate = false;
int siblingsFound = false;

void A5Backwards::ClockBack(uint64_t final, int steps, int offset1, int offset2, int offset3)
{
	if ( final ) FillBack( final );

	if ( steps == 0 )
	{
        if (showCandidate) {
            uint64_t test = mBack1[offset1] | mBack2[offset2] | mBack3[offset3];
            std::cout << "Candidate: " << std::hex << test << "\n";
        }
        siblingsFound++;
		return;
	}

// since at least 2 lfsr's are shifted at every clock, there are 4 previous states possible. We check here if such previous states would have resulted in a certain shift.

// states in which - clocked back - we have 2 equal and 1 unequal clock bits:
	if ( CB1(mBack1[offset1+1]) == CB2(mBack2[offset2+1]) && CB1(mBack1[offset1+1]) != CB3(mBack3[offset3]) ) ClockBack(0,steps-1,offset1+1,offset2+1,offset3  );
	if ( CB1(mBack1[offset1+1]) == CB3(mBack3[offset3+1]) && CB1(mBack1[offset1+1]) != CB2(mBack2[offset2]) ) ClockBack(0,steps-1,offset1+1,offset2  ,offset3+1);
	if ( CB2(mBack2[offset2+1]) == CB3(mBack3[offset3+1]) && CB2(mBack2[offset2+1]) != CB1(mBack1[offset1]) ) ClockBack(0,steps-1,offset1  ,offset2+1,offset3+1);

// states in which - clocked back - we have 3 equal clock bits:
	if ( CB1(mBack1[offset1+1]) == CB2(mBack2[offset2+1]) && CB1(mBack1[offset1+1]) == CB3(mBack3[offset3+1]) ) ClockBack(0,steps-1,offset1+1,offset2+1,offset3+1);
}

int main(int argc, char* argv[])
{
	long long out = 0;
	int lfsr1 = 0x1234;
	int lfsr2 = 0x5678;
	int lfsr3 = 0x9abc;

	A5Backwards back;

    FILE* fd = fopen("/dev/urandom","rb");

    if(fd==0) {
        printf("Could not open /dev/urandom\n");
        return -1;
    }

    int samples = 10000;

    for (int i=0; i < 120; i++) {
        uint64_t startVal;
        int found = 0;
        int siblings = 0;
        for (int j=0; j<samples; j++) {
            size_t r = fread(&startVal,sizeof(startVal),1,fd);
            uint64_t tst = back.Forwards(startVal,i);
            siblingsFound = 0;
            back.ClockBack( tst , 100 );
            if (siblingsFound) {
                found++;
                siblings+=siblingsFound;
            }
        }
        float cost = 1.0f + (float)i/64.0;
        float perc = (float)found/(float)samples;
        float sperc = (float)siblings/(float)samples;
        float gain = perc/0.15; /* 0.15 is baseline percantage */
        printf("%i extra clockings %f can be reversed 100 eff: %f (%f:%f)\n",i,perc,gain/cost,sperc,sperc/cost);
    }

    fclose(fd);





#if 0

	printf("\nFinal state: %x %x %x\n", lfsr1, lfsr2, lfsr3); 
	std::cout << std::hex << out << "\n";

#endif

}
