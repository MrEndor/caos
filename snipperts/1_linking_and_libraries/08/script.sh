#!/usr/bin/env bash

strace ./prog
strace -p <pid>

man 2 read
man 2 write
man 2 open
man 2 errno