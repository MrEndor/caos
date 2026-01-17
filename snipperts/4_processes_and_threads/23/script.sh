#!/usr/bin/env bash

#top, htop

ps aux

# ps auxf
# ps -ejH
# echo

pstree
pstree -p
pstree -p -s $$
pstree <user>

ps aux --forest
ps -ejH

pstree -G

echo "PID: $$"
echo "PPID: $PPID"
cat /proc/$$/status | grep -E "Pid|PPid|Uid|Gid"


ps aux
ps aux --sort=-%mem
ps aux --sort=-%cpu
ps -eo pid,user,vsz,rss,comm


top
top -p <pid>
htop


readlink /proc/<pid>/cwd