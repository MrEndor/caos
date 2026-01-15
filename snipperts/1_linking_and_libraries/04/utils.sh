#!/usr/bin/env bash

readelf -h a.out
readelf -S a.out
readelf -s a.out
readelf -d a.out

objdump -d a.out
objdump -x a.out

nm a.out
nm -D a.out

file a.out

objcopy -O binary a.out a.out.bin
objcopy --only-section=.text -O binary a.out text.bin