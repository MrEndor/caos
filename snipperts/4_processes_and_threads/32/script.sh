man fifo

man 1 mkfifo

mkfifo /tmp/my_fifo         # создать FIFO файл
ls -la /tmp/my_fifo         # покажет: prw-rw-rw-
rm /tmp/my_fifo             # удалить

man 3 mkfifo

./reader &

# Запустить писатель (заблокируется, пока нет читателя)
./writer