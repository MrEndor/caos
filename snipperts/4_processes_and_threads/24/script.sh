#!/usr/bin/env bash

sleep 100 &
PID=$!

echo "PID=$PID"
ps -o pid,ni,cmd -p $PID

grep "^Uid" /proc/$PID/status
grep "^Priority" /proc/$PID/status

renice -n -5 -p $PID 2>&1 || echo "root"

renice -n 10 -p $PID
ps -o pid,ni,cmd -p $PID

kill $PID

# ============================================

sleep 100 &
PID=$!

echo "PID=$PID"
taskset -p $PID

taskset -cp 0,1  $PID
taskset -p $PID

kill $PID

# ============================================

grep "^Cap" /proc/self/status

sleep 100 &
PID=$!
grep "^Cap" /proc/$PID/status
kill $PID

setcap cap_net_admin+ep /path/to/binary
getcap /path/to/binary
getpcaps <pid>

# Процесс root имеет все capabilities в Permitted
sudo -i
cat /proc/self/status | grep Cap
# CapPrm: 000001ffffffffff (все способности разрешены)

# Обычный пользователь не имеет никаких
whoami  # user
cat /proc/self/status | grep Cap
# CapPrm: 0000000000000000 (ничего не разрешено)


gcc proc_cap.c -lcap