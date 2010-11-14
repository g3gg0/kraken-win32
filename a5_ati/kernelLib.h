#ifndef KERNEL_LIB_H
#define KERNEL_LIB_H

unsigned char* getKernel(unsigned int dp);
unsigned char* getFallbackKernel(unsigned int dp);

void freeKernel(unsigned char* k);

#endif

