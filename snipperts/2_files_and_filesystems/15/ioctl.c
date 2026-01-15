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

int has_immutable(int attrs) {
    return (attrs & FS_IMMUTABLE_FL) != 0;
}

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
