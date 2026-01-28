#include "dlg.h"
#include "sys.h"

#include <cjson/cJSON.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

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
  size_t len = 0;
  int err;

  for (;;) {
    if (len >= size)
      return -ENOSPC;

    err = line_getc(fd, buf + len);
    if (err)
      return err;

    if (buf[len++] == '\n')
      break;
  }

  return len;
}
//////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  printf("{\"full_text\": \"snd_block\"}\n");
  fflush(stdout);

  int rc;
  struct sys_event event = {0};
  struct sys_event_queue_vptr eq_vptr = sys_event_queue_vptr();
  snd_block_t snd_block = {0};
  const char *card = "default";
  snd_ctl_t *ctl = NULL;

  rc = snd_ctl_open(&ctl, card, 0);
  if (rc < 0) {
    fprintf(stderr, "snd_ctl_open(%s) failed: %s\n", card, snd_strerror(rc));
    return 2;
  }

  rc = snd_ctl_subscribe_events(ctl, 1);
  if (rc < 0) {
    fprintf(stderr, "snd_ctl_subscribe_events failed: %s\n", snd_strerror(rc));
    snd_ctl_close(ctl);
    return 2;
  }

  // Add ALSA poll fds
  int snd_fds_len = snd_ctl_poll_descriptors_count(ctl);
  if (snd_fds_len <= 0) {
    fprintf(stderr, "snd_ctl_poll_descriptors_count returned %d\n",
            snd_fds_len);
    snd_ctl_close(ctl);
    return 3;
  }

  // sound poll file descriptors
  struct pollfd *snd_pfds = malloc(sizeof(struct pollfd) * snd_fds_len);
  if (!snd_pfds)
    die("calloc pfds");

  rc = snd_ctl_poll_descriptors(ctl, snd_pfds, snd_fds_len);
  if (rc <= 0) {
    fprintf(stderr, "snd_ctl_poll_descriptors returned %d\n", rc);
    free(snd_pfds);
    snd_ctl_close(ctl);
    return 4;
  }

  if (set_nonblocking(STDIN_FILENO) < 0) {
    die("fcntl(stdin)");
  }

  sigemptyset(&snd_block.sigset);
  sigaddset(&snd_block.sigset, SIGINT);
  sigaddset(&snd_block.sigset, SIGTERM);
  sigaddset(&snd_block.sigset, SIGHUP);
  sigaddset(&snd_block.sigset, SIGIO);
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

  for (int i = 0; i < snd_fds_len; i++) {
    eq_vptr.add_fd(snd_block.poll_fd, snd_pfds[i].fd);
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
      snd_ctl_event_t *ev = NULL;
      snd_ctl_event_alloca(&ev);

      // just to avoid infinite loop:
      for (int i = 0; i < 30; ++i) {
        int rc = snd_ctl_read(ctl, ev);
        if (rc == -EAGAIN)
          break; // no more events

        if (rc < 0) {
          fprintf(stderr, "[alsa] snd_ctl_read error: %s\n", snd_strerror(rc));
          break;
        }

        int etype = snd_ctl_event_get_type(ev);
        if (etype != SND_CTL_EVENT_ELEM)
          continue; // actually impossible

        snd_ctl_elem_id_t *id = NULL;
        snd_ctl_elem_id_alloca(&id);
        snd_ctl_event_elem_get_id(ev, id);
        unsigned int mask = snd_ctl_event_elem_get_mask(ev);

        if (!(mask & SND_CTL_EVENT_MASK_VALUE)) {
          fprintf(stderr, "[alsa] ELEM event is not value change. mask = %x\n",
                  mask);
          continue;
        }

        char name[128] = {0};
        snprintf(name, sizeof(name), "%s", snd_ctl_elem_id_get_name(id));
        fprintf(stderr,
                "[alsa] ELEM event: name='%s' numid=%u iface=%d dev=%d "
                "subdev=%d idx=%u mask=0x%x\n",
                name, snd_ctl_elem_id_get_numid(id),
                snd_ctl_elem_id_get_interface(id),
                snd_ctl_elem_id_get_device(id),
                snd_ctl_elem_id_get_subdevice(id),
                snd_ctl_elem_id_get_index(id), mask);
      } // for event in sound events (or until 30 iterations)
    } // if (event.type == SYS_EVENT_FD)
  } // while 1

  // release resources
  free(snd_pfds);
  snd_ctl_close(ctl);
  if (eq_vptr.destroy(snd_block.poll_fd)) {
    die("eq_vptr.destroy");
  }

  return 0;
}

// int rc;
// snd_ctx_alsa_t snd_ctx = {0};
// if ((rc = snd_ctx_alsa_create(&snd_ctx))) {
//   fprintf(stderr, "snd_ctx_alsa_create failed. err: %d\n", rc);
//   return 1;
// }
// printf("snd ctx created\n");
//
// int64_t vol = 0;
// if ((rc = snd_ctx_alsa_get_volume(&snd_ctx, &vol))) {
//   fprintf(stderr, "snd_ctx_alsa_get_volume failed. err: %d\n", rc);
//   return 2;
// }
//
// dlg_sound(vol, &snd_ctx);
