#!/usr/bin/env bash

mkdir -p testdir/subdir
echo "file1" > testdir/file1.txt
echo "file2" > testdir/file2.txt

ln -s file1.txt testdir/symlink.txt

gcc ls.c -o ls

./ls testdir

ls -l testdir

gcc stat_demo.c -o stat_demo
./stat_demo

gcc fstat_demo.c -o fstat_demo
./fstat_demo