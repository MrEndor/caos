#!/usr/bin/env bash

gcc -c libmath.c -o libmath.o

gcc load_so.c -o load
