#include "dlg.h"
#include "sys.h"

#include <cjson/cJSON.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int x, y, relative_x, relative_y, width, height;
} click_info_t;

int parse_click_info_json(const char *json, click_info_t *out) {
  if (!json || !out)
    return -1;

  // default: "do nothing" for missing fields
  *out = (click_info_t){0};

  cJSON *root = cJSON_Parse(json);
  if (!root)
    return -1;

// Helper macro: fetch optional number as int
#define GET_INT(KEY, VALUE)                                                    \
  do {                                                                         \
    cJSON *it = cJSON_GetObjectItemCaseSensitive(root, KEY);                   \
    if (cJSON_IsNumber(it)) {                                                  \
      out->VALUE = (int)(it->valueint);                                        \
    }                                                                          \
  } while (0)

  GET_INT("x", x);
  GET_INT("y", y);
  GET_INT("relative_x", relative_x);
  GET_INT("relative_y", relative_y);
  GET_INT("width", width);
  GET_INT("height", height);

#undef GET_OPT_INT

  cJSON_Delete(root);
  return 0;
}

typedef struct snd_block {
  sigset_t sigset;
  int poll_fd;   /* epoll/kqueue fd */
  int signal_fd; /* signalfd (Linux) or -1 (FreeBSD) */
} snd_block_t;

static void die(const char *msg) {
  perror(msg);
  exit(1);
}
//////////////////////////////////////////////////////////////

static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return -1;
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    return -1;
  return 0;
}
//////////////////////////////////////////////////////////////

static int line_getc(int fd, char *c) { return sys_read(fd, c, 1, NULL); }

static ssize_t line_gets(int fd, char *buf, size_t size) {
  int err;

  // don't want infinte loop here
  for (size_t len = 0; len < size; ++len) {
    err = line_getc(fd, buf + len);
    if (err)
      return err;
    if (buf[len] == '\n')
      return len;
  }
  return -ENOSPC;
}
//////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  dlg_init_t di = {.width = 300, .heigth = 25, .pos_x = 1450, .pos_y = 25};
  dlg_sound_raylib(22, di);
  return 0;

  printf("{\"full_text\": \"snd_block\"}\n");
  fflush(stdout);

  int rc;
  struct sys_event event = {0};
  struct sys_event_queue_vptr eq_vptr = sys_event_queue_vptr();
  snd_block_t snd_block = {0};
  const char *card = "default";

  if (set_nonblocking(STDIN_FILENO) < 0) {
    die("fcntl(stdin)");
  }

  if (sigemptyset(&snd_block.sigset)) {
    die("sigemptyset");
  }

#define SIGADDSET(SIG)                                                         \
  do {                                                                         \
    if (sigaddset(&snd_block.sigset, SIG)) {                                   \
      die("sigaddset " #SIG);                                                  \
    }                                                                          \
  } while (0)

  SIGADDSET(SIGINT);
  SIGADDSET(SIGTERM);
  SIGADDSET(SIGHUP);
  SIGADDSET(SIGIO);
#undef SIGADDSET

  if (sys_cloexec(STDIN_FILENO)) {
    die("sys_cloexec");
  }

  // Block these signals so they only arrive via signalfd
  if (sigprocmask(SIG_BLOCK, &snd_block.sigset, NULL) < 0) {
    die("sigprocmask");
  }

  rc = eq_vptr.create(&snd_block.poll_fd, &snd_block.signal_fd,
                      &snd_block.sigset);
  if (rc) {
    die("eq_vptr.create");
  }
  rc = eq_vptr.add_fd(snd_block.poll_fd, STDIN_FILENO);
  if (rc) {
    die("eq_vptr.add_fd STDIN_FILENO");
  }

  // poll events
  while (2 + 2 != 5) {
    rc = eq_vptr.wait(snd_block.poll_fd, snd_block.signal_fd, &event, -1);
    if (rc) {
      if (rc == -EINTR || rc == -EAGAIN)
        continue;
      break;
    }

    if (event.type == SYS_EVENT_SIGNAL) {
      // if the event is some signal:
      int sig = event.sig;
      if (sig == SIGTERM || sig == SIGINT) {
        break;
      }
      printf("unhandled signal %d", sig);
      continue;
    }

    if (event.type == SYS_EVENT_FD) {
      // if the event is some data from fd
      int fd = event.fd;
      if (fd == STDIN_FILENO) {
        char buff[2048] = {0};
        rc = line_gets(STDIN_FILENO, buff, sizeof(buff) - 1);
        if (rc == -EAGAIN) {
          break; // got EOF
        }

        click_info_t ci = {0};
        if (parse_click_info_json(buff, &ci)) {
          fprintf(stderr, "[stdin] INVALID JSON: %zd bytes: %s\n", strlen(buff),
                  buff);
          continue;
        }
        // todo show dialog

        fprintf(stderr, "[stdin] click_info:\n");
        fprintf(stderr, "\tx: %d\n", ci.x);
        fprintf(stderr, "\ty: %d\n", ci.y);
        fprintf(stderr, "\trelative_x: %d\n", ci.relative_x);
        fprintf(stderr, "\trelative_y: %d\n", ci.relative_y);
        fprintf(stderr, "\twidth: %d\n", ci.width);
        fprintf(stderr, "\theight: %d\n", ci.height);
        continue;
      }

      // something related to sound system happened
      // TODO HANDLE SOMEHOW
    } // if (event.type == SYS_EVENT_FD)
  } // while 1

  // release resources
  if (eq_vptr.destroy(snd_block.poll_fd)) {
    die("eq_vptr.destroy");
  }

  return 0;
}
