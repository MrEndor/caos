#!/usr/bin/env bash

g++ -static main.cpp -o main_static

g++ main1.cpp -L/opt/mylibs -lstdc++ -Wl,-rpath,/opt/mylibs -o main

g++ -fPIC -shared lib.cpp -o libmy.so
g++ main1.cpp -L. -lmy -o main
LD_LIBRARY_PATH=. ./main

g++ main1.cpp -L. -lmy -Wl,-rpath,. -o main
./main

g++ main1.cpp -L/opt/mylibs -lmy -Wl,-rpath,/opt/mylibs -o main

ltrace ./prog
