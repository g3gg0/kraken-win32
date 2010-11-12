#include <stdio.h>
#include <stdlib.h>
#include <linux/fs.h>
#include <stropts.h>
#include <fcntl.h>
#include <iostream>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    if (argc<2) {
        printf("usage: %s device\n",argv[0]);
        exit(0);
    }

    int fd = open(argv[1],O_RDONLY);
    if (fd==0) {
        printf("Cant open %s\n",argv[1]);
        exit(0);
    }

    unsigned long long physical_device_size=0;
    unsigned long long logical_sector_size=0ULL;

    ioctl (fd, BLKBSZGET, &logical_sector_size);
    std::cout << "Logical sector size: " << logical_sector_size << "\n";

    ioctl (fd, BLKGETSIZE64, &physical_device_size);
    std::cout << "Phy device size: " << physical_device_size << "\n";

    unsigned long long blocks = physical_device_size / logical_sector_size;

    srand(time(NULL));

    char* buffer = new char[logical_sector_size];

    for (int i=0; i<10000 ;i++) {
        unsigned int b = rand() % (int)blocks;
        unsigned long long pos = b * logical_sector_size;
        lseek(fd,pos,SEEK_SET);
        read(fd,buffer,16);
    }

    delete [] buffer;


    close(fd);
}
