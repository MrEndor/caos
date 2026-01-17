#!/bin/bash

ps aux
echo

sleep 10 &
PID=$!
echo "PID=$PID"
ps -o pid,stat,cmd -p $PID
echo

#fg, bg, jobs
#ctrl+z stop
g++ stop.cpp -o stop

kill $PID 2>/dev/null
wait $PID 2>/dev/null

gcc zombie_demo.c -o zombie_demo
./zombie_demo &
DEMO_PID=$!
echo "PID=$DEMO_PID"
sleep 2
ps aux | grep -E "$DEMO_PID|defunct|<defunct>"
wait $DEMO_PID 2>/dev/null
echo

rm -f zombie_demo
