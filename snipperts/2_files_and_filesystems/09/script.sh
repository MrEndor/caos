#!/usr/bin/env bash

g++ cp.cpp -o cp

echo "Cacos xddddd" > a.txt
./cp a.txt b.txt
cat b.txt

gcc lseek_create_hole.c -o lseek_create_hole
./lseek_create_hole

ls -lh a.txt
du -h a.txt