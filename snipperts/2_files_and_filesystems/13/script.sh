#!/usr/bin/env bash

echo "test1" > file1.txt
echo "test2" > file2.txt

gcc mv.c -o mv
gcc rm.c -o rm

ls -l file*.txt

./mv file1.txt renamed.txt
ls -l *.txt

mkdir testdir
./mv renamed.txt testdir/moved.txt
ls -lR

./rm file2.txt
ls -lR