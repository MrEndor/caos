#!/usr/bin/env bash

ldd ./prog

vim $(which ldd)

LD_TRACE_LOADED_OBJECTS=1 ./main