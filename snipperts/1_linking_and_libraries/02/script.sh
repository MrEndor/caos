#!/usr/bin/env bash

g++ -c main.cpp -o main.o

ld main.o \
  /usr/lib/libstdc++.so \
  /usr/lib/libc.so \
  /usr/lib/gcc/x86_64-pc-linux-gnu/13.3.1/crtbegin.o \
  /usr/lib/gcc/x86_64-pc-linux-gnu/13.3.1/crtend.o \
  -o main

ld main.o \
  /usr/lib/libstdc++.so \
  /usr/lib/libc.so \
  /usr/lib/gcc/x86_64-pc-linux-gnu/15.1.1/crtbegin.o \
  /usr/lib/gcc/x86_64-pc-linux-gnu/15.1.1/crtend.o \
  /usr/lib/crti.o \
  /usr/lib/crtn.o \
  /usr/lib/crt1.o \
  -dynamic-linker /lib64/ld-linux-x86-64.so.2 \
  -o main