#!/usr/bin/env bash

echo "Hello caos!" > out.txt
cat out.txt
echo "Hello caos!" 2> err.txt

echo "Hello caos!" >> out.txt
cat out.txt
echo "Hello caos!" 2>> err.txt

echo "Hello caos!" > out.txt 2>&1

echo "Hello caos!" > /dev/null
echo "Hello caos!" 2> /dev/null

echo "Hello caos!" | tee out.txt
cat out.txt
echo "Hello caos!" | tee -a out.txt
cat out.txt

gcc tee.c -o tee

echo "Hello caos!" | ./tee out.txt
cat out.txt
