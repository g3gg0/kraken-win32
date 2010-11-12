#!/usr/bin/python

import sys
import os
import shutil

if len(sys.argv) < 3:
    print "Usage: %s inpath outpath" % (sys.argv[0],)
    sys.exit(0)

if os.path.isdir(sys.argv[2]):
    print "Output path exists. Will do nothing."
    sys.exit(0)
else:
    os.makedirs(sys.argv[2])

os.system("./SSDwriter %s %s"%(sys.argv[1],sys.argv[2]+"/blockdevicedata.bin"))
shutil.copy(sys.argv[1]+"/index.dat",sys.argv[2])


