man 7 shm_overview

gcc -o writer writer.c -lrt
gcc -o reader reader.c -lrt

./writer &
./reader


ls -la /dev/shm/

ipcs -m
ipcs -m -p <pid>


cat /proc/<pid>/maps
pmap <pid>

ipcs -m


gcc shm_demo.c -o shm_demo -lrt

ftok
echo
ipcs -m
echo
cat /proc/sysvipc/shm
echo


./shm_demo sysv &
PID=$!
sleep 0.5
echo

ipcs -m
cat /proc/sysvipc/shm
echo

cat /proc/$PID/maps | grep shm
wait

ls -la /dev/shm/
echo

./shm_demo posix &
PID=$!
sleep 0.5
echo

ls -la /dev/shm/
echo

cat /proc/$PID/maps | grep shm
wait

rm -f shm_demo