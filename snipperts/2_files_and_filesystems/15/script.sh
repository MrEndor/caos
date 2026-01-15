#!/usr/bin/env bash

ls -l file.txt
stat -c "%a %n" file.txt

chmod u+x file.txt

chmod g-w file.txt

chmod u=rwx,g=rx,o= file.txt

chmod -R 755 mydir/


mkdir testdir
cd testdir
touch hahaha
echo "Content" > hahaha
chmod u-x testdir
ls testdir/hahaha


chown alice file.txt

chown alice:developers file.txt

chown 1001 file.txt

chown -R alice mydir/

chown alice: file.txt


chgrp developers file.txt

chgrp -R developers mydir/


man 2 chmod
man 2 fchmod
man 2 lchmod


man 2 chown
man 2 fchown
man 2 lchown


ls -l file.txt
stat file.txt | grep -E "^Access:|Uid:|Gid:"


echo "test" > file.txt
mkdir testdir

ls -l file.txt testdir

chmod 777 file.txt
chmod 755 testdir
ls -l file.txt testdir

mkdir no_x_dir
chmod 644 no_x_dir
ls no_x_dir 2>&1 | head -1
chmod 755 no_x_dir

mkdir sticky_dir
chmod o+t sticky_dir/
chmod 1777 sticky_dir
ls -ld sticky_dir
ls -ld /tmp

ls -l /usr/bin/passwd /usr/bin/sudo

stat -c "%a %n" /usr/bin/passwd

cp /bin/cat suid_demo
chmod u+s /usr/bin/myprogram
chmod 4755 suid_demo
ls -l suid_demo
chmod u+s /usr/bin/myprogram
chmod 4644 suid_demo
ls -l suid_demo

mkdir /project
chown :developers /project
chmod g+s /project

touch attr_test
lsattr attr_test

chattr +i attr_test
