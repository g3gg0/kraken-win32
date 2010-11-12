#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <string.h>

// which bits of a 32bit variable actually play a role
#define R1MASK	0x07FFFF /* 19 bits, numbered 0..18 */
#define R2MASK	0x3FFFFF /* 22 bits, numbered 0..21 */
#define R3MASK	0x7FFFFF /* 23 bits, numbered 0..22 */

struct simple {
  // reverse the order of bits in a register.
  // the MSB becomes the new LSB and vice versa
  template <int N, typename T>
  static T d_register_reverse_bits(T r) {
    T res = 0;
    for (int i = 0; i < N; i++) {
      if (r & 1ULL << i) {
	res |= 1ULL << N - 1 - i;
      }
    }
    return res;
  }

  // majority bit
  /* Middle bit of each of the three shift registers, for clock control */
  static const uint32_t R1MID =	0x000100; /* bit 8 */
  static const uint32_t R2MID =	0x000400; /* bit 10 */
  static const uint32_t R3MID =	0x000400; /* bit 10 */

  /* Feedback taps, for clocking the shift registers. */
  static const uint32_t R1TAPS =	0x072000; /* bits 18,17,16,13 */
  static const uint32_t R2TAPS =	0x300000; /* bits 21,20 */
  static const uint32_t R3TAPS =	0x700080; /* bits 22,21,20,7 */

  /* Output taps, for output generation */
  static const uint32_t R1OUT =	0x040000; /* bit 18 (the high bit) */
  static const uint32_t R2OUT =	0x200000; /* bit 21 (the high bit) */
  static const uint32_t R3OUT =	0x400000; /* bit 22 (the high bit) */

  static int parity32(uint32_t x) {
    x ^= x>>16;
    x ^= x>>8;
    x ^= x>>4;
    x ^= x>>2;
    x ^= x>>1;
    return x&1;
  }
  static int majority(uint32_t R1, uint32_t R2, uint32_t R3) {
    int sum;
    sum = ((R1&R1MID) >> 8) + ((R2&R2MID) >> 10) + ((R3&R3MID) >> 10);
    if (sum >= 2)
      return 1;
    else
      return 0;
  }

  static int getbit(uint32_t R1, uint32_t R2, uint32_t R3) {
    return ((R1&R1OUT) >> 18) ^ ((R2&R2OUT) >> 21) ^ ((R3&R3OUT) >> 22);
  }

  static uint32_t clockone(uint32_t reg, uint32_t mask, uint32_t taps) {
    uint32_t t = reg & taps;
    reg = (reg << 1) & mask;
    reg |= parity32(t);
    return reg;
  }

  // calculate the new round function value
  // based on a 64bit xilinx PRNG
  // output of the shift operations is discarded.
  static uint64_t advance_rf_lfsr_buggy(uint64_t v) {
    for (int i = 0; i < 64; i++) {
      uint64_t fb =  (
		      ~(
			 ((v >> 63) & 1) ^
			 ((v >> 62) & 1) ^
			 ((v >> 60) & 1) ^
			 ((v >> 59) & 1)
			)
		      ) & 1;
      v >>= 1;
      v |= fb << 63;
    }
    return v;
  }

  // calculate the new round function value
  // based on a 64bit xilinx PRNG
  // output of the shift operations is discarded.
  static uint64_t advance_rf_lfsr(uint64_t v) {
    for (int i = 0; i < 64; i++) {
      uint64_t fb =  (
		      ~(
			 ((v >> 63) & 1) ^
			 ((v >> 62) & 1) ^
			 ((v >> 60) & 1) ^
			 ((v >> 59) & 1)
			)
		      ) & 1;
      v <<= 1;
      v |= fb;
    }
    return v;
  }

  // advance the state for the simple increment round function generator
  static uint64_t advance_rf_inc(uint64_t v) {
    return v + 1;
  }

  struct implementation {
      // input: 64bits of start value
      // index: ignore
      // nruns: max chain length
      // dpbits: number of bits of a distringuished point between round functions
      // rounds: number of round functions
      static uint64_t run(uint64_t input, uint32_t index, int nruns, int dpbits,
                          int rounds, int rfgen_advance, bool buggy, uint64_t comp,
                          bool last, bool first, int extra) {
          uint64_t result = input;
          uint64_t rfstate = 0;
          int i;
          int rfapplycount = 0;

          for (int i = 0; i < rfgen_advance; i++) {
              if (buggy) {
                  rfstate = advance_rf_lfsr_buggy(rfstate);
              } else {
                  rfstate = advance_rf_lfsr(rfstate);
              }
              //std::cerr << "0x" << std::hex << std::setfill('0') << std::setw(16) << rfstate << "\n";
          }
          // std::cerr << std::hex << rfstate << "\n";

          // main loop: for each chain column

          int rflength = 0;
          int rflength_max = 0;
          int rfmaxapp = 0;

          if (first) {
              result ^= rfstate;
          }

          for (int run = nruns; run > 0; --run) {
              // apply the round function
              result ^= rfstate;

              // std::cerr << std::hex << std::setw(16) << result << " " << std::setw(16) << (result & (1ULL << dpbits) - 1) << "\n";
              // std::cerr << std::dec;
              // test round function condition
              if ((result & (1ULL << dpbits) - 1) == 0ULL) {
                  // advance round function
                  if (buggy) {
                      rfstate = advance_rf_lfsr_buggy(rfstate);
                  } else {
                      if (0) {
                          int sum = 0;
                          for (int l = 0; l < 64; ++l) {
                              sum += rfstate >> l & 1;
                          }
                          std::cerr << std::hex << rfstate << std::dec << " " << sum << "\n";
                          }
                      rfstate = advance_rf_lfsr(rfstate);
                  }
                  rfapplycount++;
                  if (rflength > rflength_max) { rflength_max = rflength; rfmaxapp = rfapplycount; }
                  rflength = 0;
              } else {
                  rflength++;
              }

              // check chain end condition
              if (rfapplycount == rounds) {
                  std::cerr << '[' << rfapplycount << " " << nruns - run <<
                      (comp == result ? " OK" : (comp ? " BAD" : "")) << "] ";
                  return result;
              }

              // apply A5/1 to result, store in result
              // goes on till the end of the main loop

              // fill the registers with the keystream from the last round
              uint32_t R3 = result            & R3MASK;
              uint32_t R2 = result >> 23      & R2MASK;
              uint32_t R1 = result >> 23 + 22 & R1MASK;

              // reverse the bit order to account for
              // the fpga and cuda optimizations
              R1 = d_register_reverse_bits<19>(R1);
              R2 = d_register_reverse_bits<22>(R2);
              R3 = d_register_reverse_bits<23>(R3);

#if 0
              for (int ii = 0; ii < 19; ii++) {
                  std::cerr << ii << " " << ((R1 >> ii) & 1) << "\n";
              }
#endif

              // produce result from R1,R2,R3
              //	std::cerr << ((R1&R1OUT) >> 18) << " "
              //		  << ((R2&R2OUT) >> 21) << " "
              //		  << ((R3&R3OUT) >> 22) << " ";
              //	std::cerr << getbit(R1, R2, R3) << "\n";
              result = uint64_t(getbit(R1, R2, R3)) << 63;
              for(i=1; i < 64 + extra; i++) {
                  int maj = majority(R1, R2, R3);
                  uint32_t T1 = clockone(R1, R1MASK, R1TAPS);
                  uint32_t T2 = clockone(R2, R2MASK, R2TAPS);
                  uint32_t T3 = clockone(R3, R3MASK, R3TAPS);

                  bool cr1 = false, cr2 = false, cr3 = false;
                  if (((R1&R1MID)!=0) == maj) {
                      cr1 = true;
                      R1 = T1;
                  }
                  if (((R2&R2MID)!=0) == maj) {
                      cr2 = true;
                      R2 = T2;
                  }
                  if (((R3&R3MID)!=0) == maj) {
                      cr3 = true;
                      R3 = T3;
                  }
                  // std::cerr << "clock: " << cr1 << " " << cr2 << " " << cr3 << "\n";
                  // std::cerr << "key: " << getbit(R1, R2, R3) << "\n";
                  result = result >> 1 | uint64_t(getbit(R1, R2, R3)) << 63;
              }
              // std::cout << std::hex << result << "\n";
          }
          if (last) { result ^= rfstate; }
          std::cerr << "[" << rfapplycount << (comp == result ? " OK" : (comp ? " BAD" : "")) << "] ";
          return result;
      }
  };
};

int main(int argc, char ** argv) {
  uint64_t r1 = 123, r2 = 123, r3 = 123;
  uint64_t input;
  uint64_t comp = 0;
  
  bool last = false;
  bool first = false;
  bool buggy = false;

  int maxrun = 2000000;

  int ch;
  int extra = 0;
  bool printrf = false;

  if (argc<4) {
      std::cout << "Usage: " << argv[0] <<
          " 0xStartingPoint DP rounds advance (options)\n";
      std::cout << "  options: -e extra_clockings\n";
      return 0;
  }

  while ((ch = getopt(argc, argv, "flc:e:rbo:1")) != EOF) {
    switch (ch) {
    case 'f' : first = true; break;
    case 'l' : last  = true; break;
    case 'c' : sscanf(optarg, "%llX", &comp); break;
    case 'e' : extra = atoi(optarg);
    case 'r' : printrf = true; break;
    case '1' : buggy = true; break;
    case 'o' : maxrun = atoi(optarg); break;
    }
  }
  argc -= optind;
  argv += optind;
  sscanf(argv[0], "%llX", &input);

  uint64_t startval = input;
  std::cout << "0x" << std::hex
	    << std::setfill('0') << std::setw(16)
	    << startval
	    << " -> 0x"
	    << std::setfill('0') << std::setw(16)
	    << simple::implementation::run(startval,
					   0,
					   maxrun,
					   atoi(argv[1]),
					   atoi(argv[2]),
					   atoi(argv[3]),
					   buggy,
					   comp,
					   last,
					   first, extra) << "\n";
}
