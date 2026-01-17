#!/bin/bash

gcc seccomp_demo.c -o seccomp_demo -lseccomp

./seccomp_demo

rm -f seccomp_demo core
