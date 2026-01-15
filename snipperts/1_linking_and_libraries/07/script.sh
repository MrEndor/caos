#!/usr/bin/env bash

# /var/lib/systemd/coredump/

g++ gdb_demo.cpp -o demo_no_debug
g++ -g gdb_demo.cpp -o demo_with_debug

ulimit -c unlimited

gdb ./main

gdb -batch -x gdb_commands.txt ./demo_with_debug

gdb -batch -ex "backtrace" ./crash_prog core