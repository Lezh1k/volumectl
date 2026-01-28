#include <errno.h>
#include <fcntl.h>

#ifdef __linux__
#define _GNU_SOURCE /* for signalfd */
#include <sys/epoll.h>
#include <sys/signalfd.h>
#endif

#ifdef __FreeBSD__
#include <sys/event.h>
#endif

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sys.h"

#ifdef __linux__
static int sys_poll_create(int *poll_fd, int *signal_fd,
                           const sigset_t *sigset);
static int sys_poll_add_fd(int poll_fd, int fd);
static int sys_poll_del_fd(int poll_fd, int fd);
static int sys_poll_wait(int poll_fd, int signal_fd, struct sys_event *event,
                         int timeout_ms);
static int sys_poll_destroy(int poll_fd);
#endif

#ifdef __FreeBSD__
static int sys_kqueue_create(int *kqueue_fd, int *signal_fd,
                             const sigset_t *sigset);
static int sys_kqueue_add_fd(int kqueue_fd, int fd);
static int sys_kqueue_del_fd(int kqueue_fd, int fd);
static int sys_kqueue_wait(int kqueue_fd, int signal_fd,
                           struct sys_event *event, int timeout_ms);
static int sys_kqueue_destroy(int kqueue_fd);
#endif

#ifdef __FreeBSD__
/* Portable polling API using epoll + signalfd on Linux */
int sys_kqueue_create(int *kqueue_fd, int *signal_fd, const sigset_t *sigset) {
  (void)signal_fd; // we don't use it, added as arg for linux API compatibility
  struct kevent ev;
  int kq;
  int rc;

  kq = kqueue1(O_CLOEXEC);
  if (kq == -1) {
    rc = -errno;
    return rc;
  }

  // block signals so they are only delivered via kqueue
  sigset_t oldmask;
  rc = sigprocmask(SIG_BLOCK, sigset, &oldmask);

  if (rc == -1) {
    rc = -errno;
    return rc;
  }

  for (int signo = 1; signo < NSIG; ++signo) {
    if (!sigismember(sigset, signo))
      continue;

    // EVFILT_SIGNAL (probably something else too)
    EV_SET(&ev, signo, EVFILT_SIGNAL, EV_ADD | EV_ENABLE, 0, 0, NULL);
    rc = kevent(kq, &ev, 1, NULL, 0, NULL);
    if (rc == -1) {
      rc = -errno;
      close(kq);
      return rc;
    }
  }

  *kqueue_fd = kq;
  return 0;
}

int sys_kqueue_add_fd(int kqueue_fd, int fd) {
  struct kevent ev;
  EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
  if (kevent(kqueue_fd, &ev, 1, NULL, 0, NULL) == -1) {
    return -errno;
  }
  return 0;
}

int sys_kqueue_del_fd(int kqueue_fd, int fd) {
  int rc;
  struct kevent ev;
  EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  rc = kevent(kqueue_fd, &ev, 1, NULL, 0, NULL);
  if (rc == -1) {
    if (errno == ENOENT) {
      return 0;
    }
    rc = -errno;
    return rc;
  }
  return 0;
}

int sys_kqueue_wait(int kqueue_fd, int signal_fd, struct sys_event *event,
                    int timeout_ms) {
  (void)signal_fd; // don't have it in FreeBSD
  struct kevent ev;
  struct timespec ts;
  struct timespec *pts = NULL;
  int rc;

  if (timeout_ms >= 0) {
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = 0; // I don't care because
    pts = &ts;
  }

  rc = kevent(kqueue_fd, NULL, 0, &ev, 1, pts);
  if (rc == -1) {
    return -errno;
  }

  if (rc == 0) {
    // timeout
    return -EAGAIN;
  }

  if (ev.filter == EVFILT_SIGNAL) {
    event->type = SYS_EVENT_SIGNAL;
    event->sig = (int)ev.ident; // signal num
    event->fd = -1;
    return 0;
  }

  if (ev.filter == EVFILT_READ) {
    event->type = SYS_EVENT_FD;
    event->sig = 0;
    event->fd = (int)ev.ident; // read fd
    return 0;
  }

  return -EINVAL;
}

int sys_kqueue_destroy(int kqueue_fd) { return close(kqueue_fd); }
#endif

#ifdef __linux__
int sys_poll_create(int *poll_fd, int *signal_fd, const sigset_t *sigset) {
  struct epoll_event ev;
  int epoll_fd, sig_fd;
  int rc;

  /* Create epoll instance */
  epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd == -1) {
    rc = -errno;
    return rc;
  }

  /* Create signalfd from the signal mask */
  sig_fd = signalfd(-1, sigset, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sig_fd == -1) {
    close(epoll_fd);
    rc = -errno;
    return rc;
  }

  /* Add signalfd to epoll */
  ev.events = EPOLLIN;
  ev.data.fd = sig_fd;
  rc = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sig_fd, &ev);
  if (rc == -1) {
    close(sig_fd);
    close(epoll_fd);
    rc = -errno;
    return rc;
  }

  *poll_fd = epoll_fd;
  *signal_fd = sig_fd;

  return 0;
}

int sys_poll_add_fd(int poll_fd, int fd) {
  struct epoll_event ev;
  int rc;

  ev.events = EPOLLIN;
  ev.data.fd = fd;

  rc = epoll_ctl(poll_fd, EPOLL_CTL_ADD, fd, &ev);
  if (rc == -1) {
    rc = -errno;
    return rc;
  }

  return 0;
}

int sys_poll_del_fd(int poll_fd, int fd) {
  int rc;

  rc = epoll_ctl(poll_fd, EPOLL_CTL_DEL, fd, NULL);
  if (rc == -1) {
    rc = -errno;
    return rc;
  }

  return 0;
}

int sys_poll_wait(int poll_fd, int signal_fd, struct sys_event *event,
                  int timeout_ms) {
  struct epoll_event ev;
  struct signalfd_siginfo si;
  ssize_t n;
  int rc;

  rc = epoll_wait(poll_fd, &ev, 1, timeout_ms);
  if (rc == -1) {
    rc = -errno;
    return rc;
  }

  if (rc == 0) {
    /* Timeout */
    return -EAGAIN;
  }

  /* Check if this is a signal or a regular fd */
  if (ev.data.fd == signal_fd) {
    /* Read signal info from signalfd */
    n = read(signal_fd, &si, sizeof(si));
    if (n != sizeof(si)) {
      return -errno;
    }

    event->type = SYS_EVENT_SIGNAL;
    event->sig = si.ssi_signo;
    event->fd = si.ssi_fd; /* May be useful for some signals */
  } else {
    /* Regular fd is readable */
    event->type = SYS_EVENT_FD;
    event->fd = ev.data.fd;
    event->sig = 0;
  }

  return 0;
}

int sys_poll_destroy(int poll_fd) { return close(poll_fd); }
#endif

struct sys_event_queue_vptr sys_event_queue_vptr(void) {
  static struct sys_event_queue_vptr res;
#ifdef __linux__
  res.create = sys_poll_create;
  res.add_fd = sys_poll_add_fd;
  res.del_fd = sys_poll_del_fd;
  res.wait = sys_poll_wait;
  res.destroy = sys_poll_destroy;
#endif

#ifdef __FreeBSD__
  res.create = sys_kqueue_create;
  res.add_fd = sys_kqueue_add_fd;
  res.del_fd = sys_kqueue_del_fd;
  res.wait = sys_kqueue_wait;
  res.destroy = sys_kqueue_destroy;
#endif
  return res;
}
//////////////////////////////////////////////////////////////
///

int sys_read(int fd, void *buf, size_t size, size_t *count) {
  ssize_t rc;

  rc = read(fd, buf, size);
  if (rc == -1) {
    rc = -errno;
    if (rc == -EWOULDBLOCK)
      rc = -EAGAIN;
    return rc;
  }

  /* End of file or pipe */
  if (rc == 0)
    return -EAGAIN;

  if (count)
    *count = rc;

  return 0;
}
//////////////////////////////////////////////////////////////

static int sys_getfd(int fd, int *flags) {
  int rc;

  rc = fcntl(fd, F_GETFD);
  if (rc == -1) {
    rc = -errno;
    return rc;
  }

  *flags = rc;

  return 0;
}

static int sys_setfd(int fd, int flags) {
  int rc;

  rc = fcntl(fd, F_SETFD, flags);
  if (rc == -1) {
    rc = -errno;
    return rc;
  }

  return 0;
}

int sys_cloexec(int fd) {
  int flags;
  int err;

  err = sys_getfd(fd, &flags);
  if (err)
    return err;

  return sys_setfd(fd, flags | FD_CLOEXEC);
}
//////////////////////////////////////////////////////////////
