#!/usr/bin/env bash

g++ -c main.cpp -o main.o
readelf -h main.o
file main.o

g++ -static main.cpp -o main_static
readelf -h main_static
file main_static

g++ -shared -fPIC lib.cpp -o libfoo.so
readelf -h libfoo.so
file libfoo.so

g++ main.cpp -o main
readelf -h main
file main