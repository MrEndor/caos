#!/bin/bash

echo "file1" > file1.txt
echo "file2" > file2.txt
echo "file3" > file3.txt

gcc hold_files.c -o hold_files
gcc lsfd.c -o lsfd

./hold_files &
PID=$!

fuser ./file2.txt

ls -l /proc/$PID/fd

./lsfd $PID

lsof -p $PID
lsof file1.txt

kill $PID 2>/dev/null
wait $PID 2>/dev/null
