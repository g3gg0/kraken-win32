#include <stdio.h>
#include "Bidirectional.h"
#include "TheMatrix.h"

int find_kc(uint64_t stop, uint32_t pos, uint32_t framecount, uint32_t framecount2, char* testbits, unsigned char *keydata )
{
/*
    if (argc<4) {
        printf("usage: %s foundkey bitpos framecount (framecount2 burst2)\n", argv[0]);
        return -1;
    }

    unsigned framecount = 0;
    uint64_t stop;
    sscanf(argv[1],"%llx",&stop);
    int pos;
    sscanf(argv[2],"%i",&pos);
    sscanf(argv[3],"%i",&framecount);
*/
    Bidirectional back;
    TheMatrix tm;
    back.doPrintCand(false);

    uint64_t stop_val = Bidirectional::ReverseBits(stop);
    //printf("#### Found potential key (bits: %i)####\n", pos);
    stop_val = back.Forwards(stop_val, 100, NULL);
    back.ClockBack( stop_val, 101+pos );
    uint64_t tst;
    unsigned char bytes[16];
    char out[115];
    out[114]='\0';
    int x = 0;
    //printf("Framecount is %i\n", framecount);
/*
    unsigned framecount2 = -1;
    if (argc>=6) {
        sscanf(argv[4],"%i",&framecount2);
    }
*/
    while (back.PopCandidate(tst)) {
        uint64_t orig = tm.CountUnmix(tst, framecount);
        orig = tm.KeyUnmix(orig);
		bool valid = true;

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
                if (check==testbits[bit]) ok++;
            }
            if (ok>104) {
				valid = true;
                //printf(" *** MATCHED ***");
            } else {
				valid = false;
                //printf(" mismatch");
            }
        }

		if(valid)
		{
			printf("KC(%i): ", x);
			for(int i=7; i>=0; i--) 
			{
				unsigned char keyByte = (orig>>(8*i)) & 0xff;

				if(keydata)
				{
					keydata[7-i] = keyByte;
				}
				printf("%02x", keyByte);
			}
	        printf("\n");

			return 1;
		}
        x++;
    }
	return 0;
}
