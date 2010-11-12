#ifndef _UNISTD_H
#define _UNISTD_H	 1

/* This file intended to serve as a drop-in replacement for 
 *  unistd.h on Windows
 *  Please add functionality as neeeded 
 */

#include <stdlib.h>
#include <stdint.h>
#include <io.h>
#include <getopt.h> /* getopt from: http://www.pwilson.net/sample.html. */

#define srandom srand
#define random rand

const int W_OK = 2;
const int R_OK = 4;

#define access _access
#define ftruncate _chsize

typedef uint16_t ushort;
typedef uint32_t uint;
typedef uint64_t ulong;


#define ssize_t int

#endif /* unistd.h  */
