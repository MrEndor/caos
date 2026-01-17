#!/usr/bin/env bash

gcc -fstack-protector-all stack_smashing_error.c
gcc -fno-stack-protector stack_smashing_error.c            
gcc -fstack-protector-strong stack_samshing_error.c

cat /proc/sys/kernel/randomize_va_space

sudo sysctl -w kernel.randomize_va_space=0

sudo sysctl -w kernel.randomize_va_space=2

