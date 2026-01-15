#!/usr/bin/env bash

g++ -g main.cpp -o main

strip main
nm main