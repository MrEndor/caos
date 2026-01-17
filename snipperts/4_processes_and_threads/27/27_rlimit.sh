#!/bin/bash

ulimit -a

ulimit -S -t 10
ulimit -H -t 20
ulimit -t

gcc rlimit.c -o rlimit
./rlimit



ulimit -v 102400
ulimit -t 10