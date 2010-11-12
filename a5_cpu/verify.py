#!/usr/bin/python

from ctypes import *
import time
import random
import sys
import struct

print "Verify table"

id = int(sys.argv[1])
f1 = open( sys.argv[1]+".start.tbl" )
f2 = open( sys.argv[1]+".end.tbl" )

wdict = {}

while True:
    d1 = f1.read(8)
    d2 = f2.read(8)
    if len(d1)+len(d2)<16:
        break
    sp = struct.unpack("<Q",d1)[0]
    ep = struct.unpack("<Q",d2)[0]
    wdict[sp] = ep

a5br = cdll.LoadLibrary("./A5Cpu.so")

if not a5br.A5CpuInit(c_int(id), c_int(8), c_int(12), c_int(2)):
    print "Could not initialize Streams engine. Quitting."
    sys.exit(-1)


#Fill up the request buffer with initial data
for s in wdict.keys():
    a5br.A5CpuSubmit(c_ulonglong(s),c_int(0));

begin = c_ulonglong(0);
result = c_ulonglong(0);
dummy = c_int()


print "Entering the grinder"
num = len(wdict.keys())
errors = 0
while True:
    while a5br.A5CpuPopResult(pointer(begin),
                                pointer(result),
                                pointer(dummy) ):
        status = "OK"
        if wdict[begin.value] !=  result.value:
            status = "Error"
            errors = errors + 1
        print "%016x -> %016x" % (begin.value,  result.value) , status
        num = num - 1
    if num == 0:
        break
    time.sleep(1)


a5br.A5CpuShutdown()

print errors, "Errors"
