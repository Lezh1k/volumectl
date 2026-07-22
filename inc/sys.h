#ifndef SYS_H
#define SYS_H

#include <unistd.h>

#define UNUSED(x) ((void)x)

int sys_read(int fd, void *buf, size_t size, size_t *count);
int sys_cloexec(int fd);

#endif /* SYS_H */
