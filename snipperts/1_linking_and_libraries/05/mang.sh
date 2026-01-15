#!/usr/bin/env bash

echo _Z3fooi | c++filt
echo _ZN2Ns3Bar3bazEd | c++filt

nm a.out | c++filt
objdump -C -t a.out

nm a.out
nm -D a.out
readelf -s a.out
readelf -sD a.out
objdump -t a.out