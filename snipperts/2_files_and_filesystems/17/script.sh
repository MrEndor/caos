#!/usr/bin/env bash

ls -l /dev/ 2>/dev/null | grep "^b" | head -3 || echo "There are no block devices in docker-container"

ls -l /dev/null /dev/zero /dev/random

gcc devices_demo.c -o devices_demo
./devices_demo

tty

gcc redirect_to_tty.c -o redirect_to_tty

echo "CPU:"
cat /proc/cpuinfo | head -10

echo "Memory:"
cat /proc/meminfo | head -5

echo "Disks:"
ls /sys/block/

ls /sys/block/*/device/model 2>/dev/null | head -1 | xargs cat 2>/dev/null || echo "Недоступно в контейнере"

