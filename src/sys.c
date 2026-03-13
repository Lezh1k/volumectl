#include <errno.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sys.h"

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
