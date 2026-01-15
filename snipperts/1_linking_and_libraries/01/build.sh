#!/usr/bin/env bash

g++ -E main.cpp -o main.i

g++ -S main.cpp -o main.s

g++ -c main.s -o main.o
g++ -c main.cpp -o main.o

g++ main.cpp -o main