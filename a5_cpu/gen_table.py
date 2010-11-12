#!/usr/bin/python

from ctypes import *
import time
import random
import sys
import struct

print "Cpu ATI placeholder chain generator."

#Prepare files for writing
f = open("a51id.cgi","r")
id = f.readline().strip()
id = id[id.find(":")+2:]
f.close()
print "Table ID is:", id

fstart = open( id+".start.tbl", "a" )
fend   = open( id+".end.tbl", "a" )

if (fstart.tell()!=fend.tell()) or (fstart.tell()%8)!=0:
    print "Error in output file sizes."
    sys.exit(-1)


def GetNext():
    return c_ulonglong(random.getrandbits(64-15)<<15)

a5br = cdll.LoadLibrary("./A5Cpu.so")

###
#
# The last argument (e.x 3) the the number of threads that should be spawned
#
###
if not a5br.A5CpuInit(c_int(int(id)), c_int(8), c_int(12),c_int(1)):
    print "Could not initialize Streams engine. Quitting."
    sys.exit(-1)

#Initialize random generator
f = open("/dev/random","rb")
bits = f.read(20)
f.close()
random.seed(bits)


for i in range(10000):
    a5br.A5CpuSubmit( GetNext(),c_int(0));

begin = c_ulonglong(0);
result = c_ulonglong(0);
dummy = c_int()




print "Entering the grinder"
total = fstart.tell() / 8
while True:
    time.sleep(1)
    count = 0
    while a5br.A5CpuPopResult(pointer(begin),
                                pointer(result),
                                pointer(dummy) ):
        print "%016x -> %016x" % (begin.value,  result.value)
        fstart.write( struct.pack("<Q", begin.value ))
        fend.write( struct.pack("<Q", result.value ))
        a5br.A5CpuSubmit(GetNext(),c_int(0));
        count = count + 1
        total = total + 1
    print count, total, "chains found."


a5br.A5CpuShutdown()

fstart.close()
fend.write()

