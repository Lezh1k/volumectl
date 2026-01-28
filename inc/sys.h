
#ifndef SYS_H
#define SYS_H

#include <signal.h>
#include <unistd.h>

/* Portable polling API (epoll/kqueue) */
#define SYS_EVENT_FD 1
#define SYS_EVENT_SIGNAL 2

struct sys_event {
  int type; /* SYS_EVENT_FD or SYS_EVENT_SIGNAL */
  int fd;   /* valid when type == SYS_EVENT_FD */
  int sig;  /* valid when type == SYS_EVENT_SIGNAL */
};

struct sys_event_queue_vptr {
  int (*create)(int *eq_fd, int *sig_fd, const sigset_t *sigset);
  int (*add_fd)(int eq_fd, int fd);
  int (*del_fd)(int eq_fd, int fd);
  int (*wait)(int eq_fd, int sig_fd, struct sys_event *event, int timeout_ms);
  int (*destroy)(int eq_fd);
};

struct sys_event_queue_vptr sys_event_queue_vptr(void);

int sys_read(int fd, void *buf, size_t size, size_t *count);
int sys_cloexec(int fd);

#endif /* SYS_H */
