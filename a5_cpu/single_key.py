#!/usr/bin/python

from ctypes import *
import time
import random
import sys

print "Brook ATI placeholder chain generator."


a5br = cdll.LoadLibrary("./A5Cpu.so")

#if not a5br.A5CpuInit(c_int(146944), c_int(32), c_int(15)):
if not a5br.A5CpuInit(c_int(311488), c_int(32), c_int(15)):
    print "Could not initialize Streams engine. Quitting."
    sys.exit(-1)

#Initialize random generator
#f = open("/dev/random","rb")
#bits = f.read(20)
#f.close()
random.seed("not so random")

#Fill up the request buffer with initial data
#for i in range(10000):
#    a5br.A5BrookSubmit(c_ulonglong(random.getrandbits(64)),0);

#a5br.A5CpuSubmit(c_ulonglong(0x13d0880e50538000), c_int(0))
a5br.A5CpuSubmit(c_ulonglong(int(sys.argv[1],16)), c_int(0))

begin = c_ulonglong(0);
result = c_ulonglong(0);
dummy = c_int()


print "Entering the grinder"
while True:
    if a5br.A5CpuPopResult(pointer(begin),
                                pointer(result),
                                pointer(dummy) ):
        print "%016x -> %016x" % (begin.value,  result.value)
        break
        #a5br.A5BrookSubmit(c_ulonglong(random.getrandbits(64)),c_int(0));
        #count = count + 1
    print "wating"
    time.sleep(1)


a5br.A5CpuShutdown()


