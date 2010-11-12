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
unsigned long long whitening(unsigned int key)
{
    int i;
    unsigned long long white = 0xe5e5e5e5 ^ key;
    for(i=0; i < 4 ; i++) {
        white = (1103ULL*white) ^ (unsigned long long)key ^ (white>>6);
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
unsigned long long mergebits(unsigned int key)
{
    int i;
    unsigned long long r = 0ULL;
    unsigned long long white = whitening(key);
    unsigned long long k = key;

    for(i=0;i<32;i++) {
        r = r | ((k & (1ULL<<i))<<i)
              | ((white&(1ULL<<i))<<(i+1));
    }
    return r;
}

/******************************
 *
 * Debug output
 *
 *****************************/
int main(int argc, char* argv[]) {
    unsigned int i;
    unsigned int start = 0;
    unsigned int stop = 100;
    
    if (argc>1) {
        start = atoi(argv[1]);
        stop = start+1;
    }

    for (i=start; i<stop; i++) {
        unsigned long long r = mergebits(i);
        unsigned long long w = whitening(i);
        printf("%08x->%08x%08x (start: %08x%08x)\n", i, (unsigned)(w>>32),
               (unsigned)w,(unsigned)(r>>32),(unsigned)r);
    }
}
