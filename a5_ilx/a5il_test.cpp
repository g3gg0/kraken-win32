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

#include "A5IlStubs.h"
#include <stdio.h>
#include <iostream>


int main(int argc, char* argv[])
{
    uint64_t rstart;
    uint64_t rfinish;
    int      start_round;
    bool status = A5IlInit(8,12,0xff);
    if (!status) {
        std::cout << "Failed to initialize CPU A5 engine.\n";
        return -1;
    }

    uint64_t ctr = 0;
    int ncount = 0;
    for (int i=0; i < 100000 ; i++ ) {
        // A5IlSubmit(0xde001bc0006f0000ULL+ctr,0,100,NULL);
        A5IlSubmit(0x0123456789abcdefULL, 0,100,NULL);
        ctr = ctr + 0x8000;
        ncount++;
    }
    while(ncount>0) {
        sleep(1);
        bool found = true;
        int found_now = 0;
        while (found) {
            found = A5IlPopResult(rstart,rfinish,start_round,NULL);
            if (found) {
                if (rfinish!=0x29f6aa7054988000ULL) {
                    printf("IL Result: %016llx %016llx (%i)\n", rstart, rfinish, start_round);
                }
                // ncount--;
                found_now++;
                A5IlSubmit(0x0123456789abcdefULL, 0,100,NULL);
                // A5IlSubmit(0xde001bc0006f0000ULL+ctr,0,100,NULL);
                // ctr = ctr + 0x8000;
            }
        }
        printf("Completed %i chains\n", found_now );
    }

    A5IlShutdown();

}

