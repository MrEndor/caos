#!/usr/bin/env bash

echo "Original" > original.txt

strace ln original.txt hardlink.txt

gcc ln.c -o ln

./ln original.txt hardlink.txt
ls -li original.txt hardlink.txt

./ln -s original.txt symlink.txt
ls -li original.txt symlink.txt

cat original.txt hardlink.txt symlink.txt

echo "Modified via hardlink" > hardlink.txt
cat original.txt

rm original.txt
ls -l hardlink.txt symlink.txt

cat hardlink.txt

cat symlink.txt 2>&1 || echo "Файл недоступен"
