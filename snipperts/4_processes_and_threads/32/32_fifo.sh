#!/bin/bash

echo "1. Создание FIFO из терминала:"
echo "   mkfifo /tmp/test_fifo"
mkfifo /tmp/test_fifo
ls -l /tmp/test_fifo
echo

echo "2. Использование FIFO из терминала:"
echo "   В одном терминале: echo 'Hello' > /tmp/test_fifo"
echo "   В другом терминале: cat /tmp/test_fifo"
rm /tmp/test_fifo
echo

gcc fifo_demo.c -o fifo_demo

echo "3. Простой FIFO между процессами:"
./fifo_demo simple
echo

echo "4. Несколько писателей в один FIFO:"
./fifo_demo writers
echo "   Результат: все сообщения доставлены"
echo

echo "5. Несколько читателей из одного FIFO:"
./fifo_demo readers
echo "   Результат: только ОДИН читатель получил данные"
echo "   Данные из FIFO читаются только один раз!"

# rm -f fifo_demo
