#include <stdio.h>
#include <stdlib.h>

/*******************************************
 *
 * Reference implementation of
 * starting point expansion function
 *
 ******************************************/

/*******************************************
 *
 * A bit whitening function (mixer)
 *
 * simple, quick, and reasonably 'random'
 *
 ******************************************/
unsigned long long whitening(unsigned long long key)
{
    int i;
    unsigned long long white = 0;
    unsigned long long bits = 0x93cbc4077efddc15ULL;
    unsigned long long b = 0x1;
    while (b) {
        if (b & key) {
            white ^= bits;
        }
        bits = (bits<<1)|(bits>>63);
        b = b << 1;
    }
    return white;
}


/***********************************************
 *
 * Interleave alternate bits from key,white -
 * reverse bit order so enusre that lsb is part 
 * of the visible chain start point.
 *
 *************************************************/
unsigned long long mergebits(unsigned long long key)
{
    unsigned long long r = 0ULL;
    unsigned long long b = 1ULL;
    unsigned int i;

    for(i=0;i<64;i++) {
        if (key&b) {
            r |= 1ULL << (((i<<1)&0x3e)|(i>>5));
        }
        b = b << 1;
    }
    return r;
}

/******************************
 *
 * Debug output
 *
 *****************************/
int main(int argc, char* argv[]) {
    unsigned long long i;
    unsigned long long start = 0ULL;
    unsigned long long stop = 100ULL;
    unsigned long long r, w;

    if (argc>1) {
        sscanf(argv[1], "%llx", &start);
        stop = start+1;
    }

    for (i=start; i<stop; i++) {
        w = whitening(i);
        r = mergebits( (w<<34)| i );
        printf("%08llx->%08x%08x (start: %08x%08x)\n", i, (unsigned)(w>>32),
               (unsigned)w,(unsigned)(r>>32),(unsigned)r);
    }
}
