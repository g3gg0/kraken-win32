#!/usr/bin/python

def Whiten(key):
    white = 0xe5e5e5e5 ^ key;
    for i in range(4):
        white = (1103*white) ^ key ^ (white>>6);
        white = white & 0xffffffffffffffff;
    return white

def MergeBits(key):
    white = Whiten(key)
    r = 0
    for i in range(32):
        r = r | ((key&(1<<i))<<i) | ((white&(1<<i))<<(i+1))

    return r

for i in range(100):
    print "%08x->%08x (start: %016x)" % (i,Whiten(i),MergeBits(i))
