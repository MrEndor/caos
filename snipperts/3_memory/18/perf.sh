#!/usr/bin/env bash

sudo perf stat -e dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses -p $(pgrep prog)
