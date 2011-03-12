#!/bin/bash
cd ../a5_cpu/;./build.sh;cd ../Kraken/;cp ../a5_cpu/A5Cpu.so .

g++ -O2 -DEMBED_FINDKC -o kraken Kraken.cpp NcqDevice.cpp DeltaLookup.cpp Fragment.cpp ServerCore.cpp ../a5_cpu/A5CpuStubs.cpp ../a5_ati/A5AtiStubs.cpp ../A5Util/Bidirectional.cpp ../A5Util/TheMatrix.cpp ../A5Util/find_kc.cpp -I. -lpthread -ldl -D__STDC_LIMIT_MACROS
