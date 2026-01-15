#!/usr/bin/env bash

ls -l /etc/passwd

ls -ld /home

ls -l /dev/null

ls -l /dev/nvme0n1

mkfifo pipe
ls -l mypipe

ls -l /run/systemd/journal/socket


ls -i
stat file.txt

ls -l /proc/
ls -l /sys/
ls -l /dev/


swapon --show 2>/dev/null || echo "No swap enabled"
free -h 2>/dev/null

dd if=/dev/zero of=swapfile bs=1M count=1024
mkswap swapfile
swapon swapfile