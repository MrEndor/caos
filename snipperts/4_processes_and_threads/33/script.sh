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