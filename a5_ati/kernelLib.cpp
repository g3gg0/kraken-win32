#include <stdio.h>
#include <malloc.h>
#include <assert.h>
#include <stdlib.h>
#ifdef WIN32
#include <direct.h>
#include <stdlib.h>
#include <windows.h>
#endif

extern "C" {
#ifdef HAVE_ZLIB
#include "zlib.h"
#endif

}

/* w32 version loads kernels from disk only */
#ifdef WIN32

static char* gKernelStarts[4];
static char* gKernelEnds[4];

#else

extern unsigned int _binary_my_kernel_dp11_Z_end;
extern unsigned int _binary_my_kernel_dp11_Z_size;
extern unsigned int _binary_my_kernel_dp11_Z_start;

extern unsigned int _binary_my_kernel_dp12_Z_end;
extern unsigned int _binary_my_kernel_dp12_Z_size;
extern unsigned int _binary_my_kernel_dp12_Z_start;

extern unsigned int _binary_my_kernel_dp13_Z_end;
extern unsigned int _binary_my_kernel_dp13_Z_size;
extern unsigned int _binary_my_kernel_dp13_Z_start;

extern unsigned int _binary_my_kernel_dp14_Z_end;
extern unsigned int _binary_my_kernel_dp14_Z_size;
extern unsigned int _binary_my_kernel_dp14_Z_start;

static char* gKernelStarts[4] = {
    (char*)&_binary_my_kernel_dp11_Z_start,
    (char*)&_binary_my_kernel_dp12_Z_start,
    (char*)&_binary_my_kernel_dp13_Z_start,
    (char*)&_binary_my_kernel_dp14_Z_start
};

static char* gKernelEnds[4] = {
    (char*)&_binary_my_kernel_dp11_Z_end,
    (char*)&_binary_my_kernel_dp12_Z_end,
    (char*)&_binary_my_kernel_dp13_Z_end,
    (char*)&_binary_my_kernel_dp14_Z_end
};
#endif

#define CHUNK 1024


unsigned char* getKernelGeneric(unsigned int dp, char* name)
{
	bool zipped = true;

    dp -= 11;
    if ((dp<0)||(dp>3)) return NULL;

#ifdef WIN32
	char filename[32];
	FILE* kernel;
	
	sprintf_s(filename, "%s%i.il", name, (dp+11));
	if(fopen_s(&kernel, filename, "rb") == 0) 
	{
#ifdef HAVE_ZLIB
		zipped = true;

		sprintf_s(filename, "%s%i.Z", name, (dp+11));
		if(fopen_s(&kernel, filename, "rb") == 0) 
#endif
		{ 
			printf("A5Ati: Failed opening kernel file '%s'.\r\n", __FILE__, __LINE__, filename);
			printf("A5Ati: Make sure the ATI kernels are in the startup directory.\r\n", __FILE__, __LINE__);
			return NULL;
		}
	}
	else
	{
		zipped = false;
	}

	/* get file size */
	fseek(kernel, 0, SEEK_END);
	long size = ftell(kernel);
	fseek(kernel, 0, SEEK_SET);

	gKernelStarts[dp] = (char*)malloc(size);
	size_t read_blocks = fread(gKernelStarts[dp],size,1,kernel);
	fclose(kernel);

	if(read_blocks != 1) {
		printf("(%s:%i) Failed reading kernel file\r\n", __FILE__, __LINE__);
		return NULL;
	}

	gKernelEnds[dp] = gKernelStarts[dp] + size;
	
#endif

	if(zipped)
	{
#ifdef HAVE_ZLIB
		int ret;
		z_stream strm;
		strm.zalloc = Z_NULL;
		strm.zfree = Z_NULL;
		strm.opaque = Z_NULL;
		strm.avail_in = gKernelEnds[dp]-gKernelStarts[dp];
		size_t bsize = CHUNK + 1;
		void *buf = malloc(bsize);
		size_t ind = 0;
		strm.next_out = (Bytef*)buf;
		strm.next_in = (Bytef*)gKernelStarts[dp];
		ret = inflateInit(&strm);
		if (ret != Z_OK) return NULL;

		do {
			if ((ind+CHUNK)>bsize) {
				bsize += CHUNK;
				buf = realloc(buf, bsize);
			}
			strm.avail_out = CHUNK;
			strm.next_out = &((Bytef*)buf)[ind];

			ret = inflate(&strm, Z_NO_FLUSH);    /* no bad return value */
			assert(ret != Z_STREAM_ERROR);       /* state not clobbered */
			switch (ret) {
			case Z_NEED_DICT:
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				printf("Zlib error: %i\n", ret);
				(void)inflateEnd(&strm);
				free(buf);
				return NULL;
			default:
				break;
			}
			size_t have = CHUNK - strm.avail_out;
			ind += have;

			// printf("Have %i bytes: in %i\n", have, strm.avail_in);
	    
		} while (strm.avail_out == 0);

		inflateEnd(&strm);
		((char*)buf)[ind]=0;

		return (unsigned char*)buf;
#else
		printf("Zlib not available. Failed uncomressing kernel.\n");
		return NULL;
#endif
	}
	else
	{
		int length = gKernelEnds[dp]-gKernelStarts[dp];
		void *buf = malloc(length + 1);

		memcpy(buf, gKernelStarts[dp], length);
		((char*)buf)[length] = 0;

		return (unsigned char*)buf;
	}

	return NULL;
}

void freeKernel(unsigned char* k)
{
    free(k);
}

unsigned char* getFallbackKernel(unsigned int dp)
{
	return getKernelGeneric(dp, "fallback_kernel_dp");
}

unsigned char* getKernel(unsigned int dp)
{
	return getKernelGeneric(dp, "optimized_kernel_dp");
}
