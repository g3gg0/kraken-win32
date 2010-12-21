#include <string.h>
#include <stdio.h>
#include "Bidirectional.h"
#include "TheMatrix.h"

#include "Globals.h"

int find_kc(uint64_t stop, uint32_t pos, uint32_t framecount, uint32_t framecount2, char* testbits, unsigned char *keydata )
{
    Bidirectional back;
    TheMatrix tm;
    back.doPrintCand(false);

    uint64_t stop_val = Bidirectional::ReverseBits(stop);

    stop_val = back.Forwards(stop_val, 100, NULL);
    back.ClockBack( stop_val, 101+pos );
    uint64_t tst;
    unsigned char bytes[16];
    char out[115];
    out[114]='\0';
    int x = 0;

    while (back.PopCandidate(tst)) {
        uint64_t orig = tm.CountUnmix(tst, framecount);
        orig = tm.KeyUnmix(orig);
		bool valid = true;

		
#ifndef EMBED_FINDKC
        printf("KC(%2i): ", x);
        for(int i=7; i>=0; i--) {
            printf("%02x ",(unsigned)(orig>>(8*i))&0xff);
        }
#endif

        if (framecount2>=0 && testbits && strlen(testbits) > 0) {
            uint64_t mix = tm.KeyMix(orig);
            mix = tm.CountMix(mix,framecount2);
            mix = back.Forwards(mix, 101, NULL);
            back.Forwards(mix, 114, bytes);

            int ok = 0;
            for (int bit=0;bit<114;bit++) {
                int byte = bit / 8;
                int b = bit & 0x7;
                int v = bytes[byte] & (1<<(7-b));
                char check = v ? '1' : '0';

				if (check==testbits[bit]) {
					ok++;
				}
            }

			/* TODO: why 104? shouldn't that be 114? */
            if (ok>104) {
				valid = true;
#ifndef EMBED_FINDKC
                printf(" *** MATCHED ***");
#endif
            } else {
				valid = false;
#ifndef EMBED_FINDKC
                printf(" mismatch");
#endif
            }
        }

#ifndef EMBED_FINDKC
        printf("\n");
#endif

		if(valid && keydata)
		{
			for(int i=7; i>=0; i--) 
			{
				unsigned char keyByte = (orig>>(8*i)) & 0xff;

				keydata[7-i] = keyByte;
			}
			return 1;
		}
        x++;
    }
	return 0;
}


#ifndef EMBED_FINDKC

int print_usage()
{
	printf("usage: find_kc <foundkey> <bitpos> <framecount> [<framecount2> <burst2>]\n");
	return -1;
}


int main(int argc, char* argv[])
{
	char *testbits = NULL;
    uint32_t framecount1 = 0;
    uint32_t framecount2 = 0;
	uint32_t pos = 0;
	uint64_t stop = 0;
	int ret = 0;

    if (argc!=4 && argc!=6) {
		return print_usage();
    }

    if(sscanf(argv[1],"%llx",&stop) != 1) {
		return print_usage();
	}
    if(sscanf(argv[2],"%i",&pos) != 1) {
		return print_usage();
	}
    if(sscanf(argv[3],"%i",&framecount1) != 1) {
		return print_usage();
	}
	
    if (argc==6) {
		testbits = argv[5];
        if(sscanf(argv[4],"%i",&framecount2) != 1 || (strlen(testbits) != 114)) {
			return print_usage();
		}
    }

	return find_kc(stop, pos, framecount1, framecount2, testbits, NULL);
}
#endif
