##### Права доступа к файлам. Как посмотреть, как изменить права доступа?

**Права доступа (permissions)** в Linux определяют, кто может что делать с файлом. Каждый файл имеет три набора прав:

- **Owner (владелец)** - U (user) - права самого владельца файла
- **Group (группа)** - G (group) - права группы владельца
- **Others (остальные)** - O (other) - права для всех остальных

Каждый набор содержит три бита:

- **r (read)** = 4 - чтение файла
- **w (write)** = 2 - запись/изменение файла
- **x (execute)** = 1 - выполнение файла (или вход в директорию)

**Как посмотреть права доступа:**

```bash
# Просмотр прав в формате ls
ls -l file.txt
# Output: -rw-r--r-- 1 user group 1024 Jan 14 12:00 file.txt
#         ^ owner=rw- group=r-- other=r--

# Просмотр в восьмеричном формате
stat -c "%a %n" file.txt
# Output: 644 file.txt
```

**Расшифровка формата ls -l:**

```
-rw-r--r-- 1 user group 1024 Jan 14 12:00 file.txt
^          ^
|          +-- количество ссылок
+-- тип файла (- = обычный файл, d = директория, l = symlink, и т.д.)

-rw-r--r--
 ^^^ ^^^ ^^^
 uuu ggg ooo (user, group, other)

 uuu = rw- = 110 = 6
 ggg = r-- = 100 = 4
 ooo = r-- = 100 = 4
 
Итого: 644
```

**Как изменить права доступа:**

**Используя символьный формат:**

```bash
# Добавить право выполнения для владельца
chmod u+x file.txt

# Убрать право записи для группы
chmod g-w file.txt

# Установить права для всех категорий
chmod u=rwx,g=rx,o= file.txt

# Рекурсивно для директории
chmod -R 755 mydir/

# Символы: + добавить, - убрать, = установить
# Категории: u (user), g (group), o (other), a (all)
# Права: r (read), w (write), x (execute)
```

**Используя восьмеричный формат:**

```bash
# 644 = rw- r-- r--
chmod 644 file.txt

# 755 = rwx r-x r-x (для программ)
chmod 755 script.sh

# 700 = rwx --- --- (только владелец)
chmod 700 private.key
```

**Таблица распространённых комбинаций:**

| Число | Символы   | Назначение                                   |
|-------|-----------|----------------------------------------------|
| 444   | r--r--r-- | Только чтение для всех                       |
| 644   | rw-r--r-- | Обычный файл (читаемый всем, пишет владелец) |
| 755   | rwxr-xr-x | Исполняемый файл (программа, скрипт)         |
| 666   | rw-rw-rw- | Читаемый/пишемый для всех (опасно!)          |
| 777   | rwxrwxrwx | Всё для всех (очень опасно!)                 |
| 700   | rwx------ | Только для владельца                         |
| 600   | rw------- | Только чтение/запись владельцем              |
| 777   | rwxrwxrwx | Всё разрешено для всех                       |

---

##### Почему r и x - это разные права? Как понимать эти права доступа для директорий?

**Для обычных файлов:**

- **r (read)** - возможность читать содержимое файла
- **x (execute)** - возможность запустить файл как программу

Вы можете разрешить читать код программы (r), но не позволить её выполнять (x).

**Для директорий смысл меняется:**

| Право | Для файла               | Для директории                                          |
|-------|-------------------------|---------------------------------------------------------|
| **r** | Читать содержимое файла | Просматривать список файлов в директории (ls)           |
| **w** | Модифицировать файл     | Создавать/удалять файлы в директории                    |
| **x** | Выполнить файл          | **Входить в директорию** (cd), получать доступ к файлам |

**Примеры для директорий:**

```bash
# 755 = rwx r-x r-x (типичная директория)
# - владелец может всё
# - группа может входить и просматривать список
# - остальные могут входить и просматривать список
drwxr-xr-x

# 700 = rwx --- --- (приватная директория)
# - только владелец может входить и управлять содержимым
drwx------

# 744 = rwx r-- r-- (опасно!)
# - группа и остальные могут видеть файлы БЕЗ права входить
# - это обычно ошибка конфигурации
drwxr--r--
```

**Важная точка:** Чтобы получить доступ к файлу в поддиректории, нужно право **x** на все родительские директории:

```bash
ls -ld /home/user/projects/myproject
# Если у /home нет x - не получится заходить в /home/user
# Если у /home/user нет x - не получится заходить дальше
# И так далее...
```

**Практический пример - права на директорию только для чтения без x:**

```bash
mkdir testdir
cd testdir
touch hahaha
echo "Content" > hahaha
chmod u-x testdir   # Убираем право входить
ls testdir/hahaha   # Ошибка: не можем войти в директорию!
```

---

##### Как поменять владельца файла, как поменять группу владельца?

**Изменение владельца файла:**

```bash
# Изменить владельца на alice
chown alice file.txt

# Изменить владельца и группу одновременно
chown alice:developers file.txt

# Используя UID вместо имени
chown 1001 file.txt

# Рекурсивно для директории
chown -R alice mydir/

# Изменить владельца с сохранением группы
chown alice: file.txt
```

**Изменение группы файла:**

```bash
# Изменить группу на developers
chgrp developers file.txt

# Рекурсивно
chgrp -R developers mydir/
```

**Просмотр владельца и группы:**

```bash
# Показывает владельца и группу
ls -l file.txt
# Output: -rw-r--r-- 1 alice developers 1024 Jan 14 12:00 file.txt

# Более подробный вывод
stat file.txt | grep -E "^Access:|Uid:|Gid:"
# Output:
# Access: (0644/-rw-r--r--)  Uid: ( 1001/   alice)   Gid: ( 1002/developers)
```

**Ограничения:**

- Только **root** может изменять владельца файла (`chown`)
- **Владелец файла** может изменить его группу на одну из групп, в которых он состоит
- Группа может быть изменена только владельцем или root'ом

---

##### Какие сисколлы используются для всего вышеперечисленного?

**Для изменения прав доступа:**

```c
#include <sys/stat.h>

// Изменить права доступа файла
int chmod(const char *pathname, mode_t mode);

// Изменить права доступа дескриптора файла
int fchmod(int fd, mode_t mode);

// Изменить права доступа на файл, на который ссылается symlink
int lchmod(const char *pathname, mode_t mode);
```

**Для изменения владельца и группы:**

```c
#include <unistd.h>

// Изменить владельца и/или группу
int chown(const char *pathname, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
int lchown(const char *pathname, uid_t owner, gid_t group);

// Примечание: -1 означает "не менять"
chown("/path/to/file", 1001, -1);  // Изменить владельца, оставить группу
chown("/path/to/file", -1, 1002);  // Изменить группу, оставить владельца
```

**Для получения информации о файле:**

```c
#include <sys/stat.h>

// Получить информацию о файле
int stat(const char *pathname, struct stat *statbuf);
int fstat(int fd, struct stat *statbuf);
int lstat(const char *pathname, struct stat *statbuf);

// struct stat содержит:
struct stat {
    mode_t st_mode;   // права доступа и тип файла
    uid_t  st_uid;    // владелец (UID)
    gid_t  st_gid;    // группа (GID)
    ino_t  st_ino;    // номер inode
    nlink_t st_nlink; // количество жёстких ссылок
    off_t  st_size;   // размер файла в байтах
    // ... и много других полей
};

// Примеры использования:
int owner = statbuf.st_uid;
mode_t permissions = statbuf.st_mode & 0777;
int is_directory = S_ISDIR(statbuf.st_mode);
```

**Пример: проверка и изменение прав**

```c
#include <sys/stat.h>
#include <stdio.h>

int main() {
    struct stat sb;
    
    // Получить информацию о файле
    if (stat("myfile.txt", &sb) == -1) {
        perror("stat");
        return 1;
    }
    
    // Проверить права
    printf("Current permissions: %o\n", sb.st_mode & 0777);
    printf("Owner UID: %d\n", sb.st_uid);
    
    // Изменить права
    if (chmod("myfile.txt", 0644) == -1) {
        perror("chmod");
        return 1;
    }
    
    // Изменить владельца (требует root)
    if (chown("myfile.txt", 1001, 1002) == -1) {
        perror("chown");
        return 1;
    }
    
    return 0;
}
```

**Константы для режимов доступа:**

```c
#include <sys/stat.h>

// Доступ для чтения, записи, выполнения
#define S_IRUSR 0400   // read by owner
#define S_IWUSR 0200   // write by owner
#define S_IXUSR 0100   // execute by owner

#define S_IRGRP 0040   // read by group
#define S_IWGRP 0020   // write by group
#define S_IXGRP 0010   // execute by group

#define S_IROTH 0004   // read by others
#define S_IWOTH 0002   // write by others
#define S_IXOTH 0001   // execute by others

// Комбинированные константы
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)  // 0700
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)  // 0070
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)  // 0007

// Макросы для проверки типа файла
#define S_ISREG(mode)  ((mode & S_IFMT) == S_IFREG)  // обычный файл
#define S_ISDIR(mode)  ((mode & S_IFMT) == S_IFDIR)  // директория
#define S_ISLNK(mode)  ((mode & S_IFMT) == S_IFLNK)  // символическая ссылка
```

---

##### Что такое sticky bit? Что такое suid-бит, что означают права доступа s и S у файла? Покажите примеры файлов, которые ими обладают.

**SUID бит (Set User ID)** - выполняется с правами владельца файла

```bash
# Просмотр SUID бита
ls -l /usr/bin/passwd
# Output: -rwsr-xr-x 1 root root 59976 Jan 14 12:00 /usr/bin/passwd
#            ^
#            s = SUID бит установлен, владелец может выполнить     

# В восьмеричном формате
stat -c "%a %n" /usr/bin/passwd
# Output: 4755 /usr/bin/passwd
#         ^--- 4 = SUID бит

# Установить SUID бит
chmod u+s /usr/bin/myprogram
chmod 4755 /usr/bin/myprogram  # rwsr-xr-x
```

**Как работает SUID:**

- Обычно программа выполняется с правами запустившего её пользователя
- С SUID битом программа выполняется с правами **владельца файла**
- Пример: `/usr/bin/passwd` принадлежит root и имеет SUID, поэтому обычный пользователь может менять свой пароль (хотя
  пароли хранят root)

**SGID бит (Set Group ID)** - выполняется с правами группы владельца

```bash
# SGID на файле (редко)
chmod g+s file.txt

# SGID на директории (часто) - файлы созданы с группой директории
chmod g+s mydir/
ls -ld mydir/
# Output: drwxr-sr-x  (s на месте группы x)

# В восьмеричном формате
chmod 2755 mydir/  # rwxr-sr-x

# Практический пример: проектная директория с автоматическим наследованием группы
mkdir /project
chown :developers /project
chmod g+s /project
# Теперь все файлы, созданные в /project, будут принадлежать группе developers
```

**Sticky bit** - только владелец может удалять файлы в директории

```bash
# Просмотр sticky бита
ls -ld /tmp
# Output: drwxrwxrwt  (t на месте других x)
#                  ^
#                sticky бит установлен

# Установить sticky бит
chmod o+t mydir/
chmod 1777 mydir/  # rwxrwxrwt

# Удалить sticky бит
chmod o-t mydir/
```

**Таблица спец битов:**

| Бит    | Восьмер | Символ | На файле               | На директории                 |
|--------|---------|--------|------------------------|-------------------------------|
| SUID   | 4xxx    | s/S    | Выполнить как владелец | (не применим)                 |
| SGID   | 2xxx    | s/S    | Выполнить как группа   | Новые файлы наследуют группу  |
| Sticky | 1xxx    | t/T    | (нет эффекта)          | Только владелец может удалять |

-rwsr-xr-x   # s (строчная) = SUID + исполняемый для владельца
-rwSr-xr-x   # S (заглавная) = SUID, НО НЕ исполняемый для владельца

**Примеры системных файлов с битами:**

```bash
# SUID примеры
ls -l /usr/bin/passwd /usr/bin/sudo /bin/su
# -rwsr-xr-x  (все имеют SUID)

# /usr/bin/newgrp имеет SGID
ls -l /usr/bin/newgrp
# -rwxr-sr-x

# /tmp имеет sticky бит
ls -ld /tmp /var/tmp
# drwxrwxrwt

# Примеры для собственных файлов
# Программа, которая нужно запускать от root'а, но находится в пути пользователя
chmod 4755 /usr/local/bin/backup_script
```

**Осторожно с SUID:**

```bash
# Опасно: SUID скрипт - уязвимость!
chmod 4755 script.sh  # ❌ НЕ ДЕЛАЙТЕ ТАК!

# Причина: shell может содержать переменные окружения
# которые позволяют выполнить произвольный код

# Безопасно: SUID бинарный файл на C
gcc -o my_program program.c
chmod 4755 my_program  # Если вы знаете что делаете
```

---

##### Что такое атрибуты файлов, какие они бывают, как посмотреть и как поменять атрибуты файлов?

**Атрибуты файлов (файловые атрибуты)** - это дополнительные флаги, хранящиеся в inode, которые влияют на поведение
файла.

Важно: атрибуты отличаются от **прав доступа**. Права контролируют кто может что делать, атрибуты контролируют как ОС и
ФС работают с файлом.

**Просмотр атрибутов:**

```bash
# Просмотр атрибутов на ext4 (использует chattr/lsattr)
lsattr file.txt
# Output: ------i------- file.txt
#         ^ атрибуты

# Более подробный вывод
lsattr -v file.txt
# Output: 16 -------i------- file.txt (версия)
```

**Список основных атрибутов:**

| Символ | Флаг                  | Описание                                                                                |
|--------|-----------------------|-----------------------------------------------------------------------------------------|
| **a**  | append only           | Файл может только дополняться, не может быть усечён или перезаписан. Полезно для логов. |
| **c**  | compressed            | Файл автоматически сжимается при записи на диск                                         |
| **d**  | no dump               | Файл пропускается при резервном копировании (dump)                                      |
| **e**  | extent format         | Файл использует extent'ы (для больших файлов)                                           |
| **i**  | immutable             | Файл неизменяемый: не может быть изменён, удалён, переименован даже root'ом             |
| **j**  | data journaling       | Все данные журналируются перед записью на диск                                          |
| **s**  | secure deletion       | При удалении данные затираются нулями                                                   |
| **t**  | no tail-merging       | Не сливать маленькие блоки в конце файла                                                |
| **u**  | undeletable           | Данные сохраняются для восстановления после удаления                                    |
| **A**  | no atime update       | Время доступа (atime) не обновляется                                                    |
| **D**  | synchronous directory | Изменения директории записываются синхронно                                             |
| **S**  | synchronous updates   | Все изменения записываются синхронно                                                    |

**Изменение атрибутов:**

```bash
# Установить атрибут immutable (неизменяемый файл)
chattr +i /etc/passwd
ls -l /etc/passwd  # Права видны
lsattr /etc/passwd  # Атрибуты видны: ----i--------

# Попытка удалить/изменить файл с +i
rm /etc/passwd
# Output: Operation not permitted

# Даже root не сможет без remove (даже sudo)
sudo rm /etc/passwd
# Output: Operation not permitted (требует chattr -i сначала)

# Убрать атрибут
chattr -i /etc/passwd

# Установить append-only для лога
chattr +a /var/log/myapp.log
# Теперь можно добавлять в лог, но не удалять строки

# Установить no-atime (улучшение производительности)
chattr +A large_file.bin

# Несколько атрибутов одновременно
chattr +Ai myfile
# Добавить immutable и no-atime

# Убрать все атрибуты
chattr = myfile

# Рекурсивно для директории
chattr -R +A mydir/
```

**Получение атрибутов через системные вызовы:**

```c
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <fcntl.h>

int get_file_attrs(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("open");
        return -1;
    }
    
    int attrs;
    if (ioctl(fd, FS_IOC_GETFLAGS, &attrs) == -1) {
        perror("ioctl");
        close(fd);
        return -1;
    }
    
    close(fd);
    return attrs;
}

// Проверка конкретного атрибута
int has_immutable(int attrs) {
    return (attrs & FS_IMMUTABLE_FL) != 0;
}

// Установка атрибутов
int set_file_attrs(const char *filename, int attrs) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) return -1;
    
    if (ioctl(fd, FS_IOC_SETFLAGS, &attrs) == -1) {
        perror("ioctl");
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

// Пример: добавить immutable флаг
int add_immutable(const char *filename) {
    int attrs = get_file_attrs(filename);
    if (attrs == -1) return -1;
    
    attrs |= FS_IMMUTABLE_FL;  // Установить флаг
    return set_file_attrs(filename, attrs);
}
```

**Важные замечания:**

1. **Атрибуты зависят от ФС**: ext4 поддерживает `lsattr`/`chattr`, но NTFS/FAT не поддерживают

2. **Права и атрибуты независимы**: даже если файл имеет права 777, с +i он не может быть изменён

3. **+i для критических файлов**: часто используется для `/etc/passwd`, `/etc/shadow`, конфигов

4. **+a для логов**: гарантирует что логи только дополняются, не перезаписываются

5. **Требует root**: только root может устанавливать большинство атрибутов

**Пример практического использования:**

```bash
# Защитить важный конфиг
chattr +i /etc/myapp/config.conf

# Логирование только на добавление
chattr +a /var/log/myapp.log

# Автоматически стирать при удалении (для чувствительных данных)
chattr +s /home/user/secrets.txt

# Отключить обновление времени доступа (производительность)
chattr +A /mnt/storage/largefile.iso

# Проверить результат
lsattr /etc/myapp/config.conf /var/log/myapp.log
```
