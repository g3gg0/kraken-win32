#include <stdio.h>
#include <iostream>

/*****
 *
 * Written by Frank A. Stevenson
 *
 * Illustrates that the A5 keymixing stage is linear since the
 * majority clocking rule has been disabled. This allows a set of
 * linear equations to be set up, and an invertible matrix can be
 * created.
 *
 * The inverted matrix can the be used to undo the key mixing stage
 * very quickly.
 *
 * (C) 2009
 *
 * BSD Lincence applies
 *
 *****/


typedef unsigned long long uin64_t;

template < typename T >
inline T highbit(T& t)
{
    return t = (((T)(-1)) >> 1) + 1;
}


template < typename T >
std::ostream& bin(T& value, std::ostream &o)
{
    for ( T bit = highbit(bit); bit; bit >>= 1 )
    {
        o << ( ( value & bit ) ? '1' : '0' );
    }
    return o;
}

uint64_t reverseBits(uint64_t r)
{
  uint64_t r1 = r;
  uint64_t r2 = 0;
  for (int j = 0; j < 64 ; j++ ) {
    r2 = (r2<<1) | (r1 & 0x01);
    r1 = r1 >> 1;
  }
  return r2;
}

uint64_t parity(uint64_t r)
{
  uint64_t r1 = r;

  r = (r>>32)^r;
  r = (r>>16)^r;
  r = (r>>8)^r;
  r = (r>>4)^r;
  r = (r>>2)^r;
  r = (r>>1)^r;

  return r & 0x1;
}

class TheMatrix {
public:
    TheMatrix();

    uint64_t KeyMix(uint64_t);
    uint64_t KeyUnmix(uint64_t);
    uint64_t KeyMixSlow(uint64_t);


    uint64_t mMat1[64];
    uint64_t mMat2[64];
    uint64_t mMat3[64];
private:
    void Invert();
};

TheMatrix::TheMatrix()
{
  uint64_t lfsr1[19];
  uint64_t lfsr2[22];
  uint64_t lfsr3[23];

  for (int i=0; i<19; i++) lfsr1[i]=0ULL;
  for (int i=0; i<22; i++) lfsr2[i]=0ULL;
  for (int i=0; i<23; i++) lfsr3[i]=0ULL;

  uint64_t clock_in = 1;

  for (int i=0; i<64; i++) {
    lfsr1[0] = lfsr1[0] ^ clock_in;
    lfsr2[0] = lfsr2[0] ^ clock_in;
    lfsr3[0] = lfsr3[0] ^ clock_in;
    mMat1[i] = clock_in;  /* identity */
    clock_in = clock_in + clock_in; /* shift up*/

    uint64_t feedback1 = lfsr1[13]^lfsr1[16]^lfsr1[17]^lfsr1[18];
    uint64_t feedback2 = lfsr2[20]^lfsr2[21];
    uint64_t feedback3 = lfsr3[7]^lfsr3[20]^lfsr3[21]^lfsr3[22];

    for (int i=18; i>0; i--) lfsr1[i]=lfsr1[i-1];
    for (int i=21; i>0; i--) lfsr2[i]=lfsr2[i-1];
    for (int i=22; i>0; i--) lfsr3[i]=lfsr3[i-1];
    
    lfsr1[0] = feedback1;
    lfsr2[0] = feedback2;
    lfsr3[0] = feedback3;
  }

  for (int i=0; i<19; i++) mMat2[i]    = lfsr1[i];
  for (int i=0; i<22; i++) mMat2[i+19] = lfsr2[i];
  for (int i=0; i<23; i++) mMat2[i+41] = lfsr3[i];

  for (int i=0; i<64; i++) mMat3[i] = mMat2[i];  /* copy for inversion */

  Invert();

}

uint64_t TheMatrix::KeyMix(uint64_t key)
{
    uint64_t out = 0;

    for (int i=0; i< 64; i++) {
        out = (out<<1) | parity(key & mMat2[i]);
    }

    return reverseBits(out);
    // return out;
}

uint64_t TheMatrix::KeyUnmix(uint64_t mix)
{
    uint64_t out = 0;

    uint64_t b = 1;
    for (int i=0; i< 64; i++) {
        out = (out<<1) | parity(mix & mMat1[i]);
    }

    return reverseBits(out);
}


uint64_t TheMatrix::KeyMixSlow(uint64_t key)
{
    uint64_t out = 0;
    int lfsr1 = 0x0;
    int lfsr2 = 0x0;
    int lfsr3 = 0x0;

    for (int i=0; i< 64; i++) {
        int bit = key & 0x01;
        key = key >> 1;
        lfsr1 = lfsr1 ^ bit;
        lfsr2 = lfsr2 ^ bit;
        lfsr3 = lfsr3 ^ bit;

        /* Clock the different lfsr */
        unsigned int val = (lfsr1&0x52000)*0x4a000;
        val ^= lfsr1<<(31-17);
        lfsr1 = 2*lfsr1 | (val>>31);

        val = (lfsr2&0x300000)*0xc00;
        lfsr2 = 2*lfsr2 | (val>>31);


        val = (lfsr3&0x500080)*0x1000a00;
        val ^= lfsr3<<(31-21);
        lfsr3 = 2*lfsr3 | (val>>31);
    }

    lfsr1 = lfsr1 & 0x7ffff;
    lfsr2 = lfsr2 & 0x3fffff;
    lfsr3 = lfsr3 & 0x7fffff;

    out = (uint64_t)lfsr1 | ((uint64_t)lfsr2<<19) | ((uint64_t)lfsr3<<41);
    return out;
}
    
void TheMatrix::Invert()
{
    int moved[64];
    for (int i=0; i<64; i++) moved[i] = 0;

    int swaps = 1;

    /* elimination */
    uint64_t b = 1ULL;
    for (int i=0; i<64; i++) {
        for (int j=i; j<64; j++) {
            if (i==j) {
                if((mMat3[j]&b)==0) {
                    bool found = false;
                    for(int k=j; k<64; k++) {
                        if (mMat3[k]&b) {
                            mMat3[j] = mMat3[j] ^ mMat3[k];
                            mMat1[j] = mMat1[j] ^ mMat1[k];
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        /* The code doesn't handle all matrices */
                        /* abort in uncompleted state */
                        printf("Trøbbel i tårnet!\n");
                        return;
                    }
                }
            } else {
                if (mMat3[j]&b) {
                    // printf("Eliminate %i -> %i\n", i, j);
                    mMat3[j] = mMat3[j] ^ mMat3[i];
                    mMat1[j] = mMat1[j] ^ mMat1[i];
                }
            }
        }
        b = b << 1;
    }

    /* elimination */
    b = 1ULL;
    for (int i=0; i<64; i++) {
        for (int j=(i-1); j>=0; j--) {
            if (mMat3[j]&b) {
                // printf("Eliminate(2) %i -> %i\n", i, j);
                mMat3[j] = mMat3[j] ^ mMat3[i];
                mMat1[j] = mMat1[j] ^ mMat1[i];
            }
        }
        b = b << 1;
    }

}

 
int main(int argc, char* argv[])
{
  
  TheMatrix tm;

#if 0
  for (int i=0; i<64; i++) {
    bin(tm.mMat1[i], std::cout);
    std::cout << "\n";
    // std::cout << std::hex << matrix2[i] << "\n";
  }
#endif

  uint64_t key = 0x123456789abcdef0ULL;
  
  std::cout << "\nKey: " << std::hex << key << "\n";

  std::cout << "\nSlow: " << std::hex << tm.KeyMixSlow(key) << "\n";

  std::cout << "\nQuick:" << std::hex << tm.KeyMix(key) << "\n";

  uint64_t mix = tm.KeyMixSlow(key);

  uint64_t umix = tm.KeyUnmix(mix);

  std::cout << "\nUnmixed key:" << std::hex << umix << "\n\n\n";

  
  return 0;
}
