#include "dlg.h"
#include "sys.h"

#include <cjson/cJSON.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include <pulse/pulseaudio.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct click_info {
  int x, y, relative_x, relative_y, width, height;
} click_info_t;

static int parse_click_info_json(const char *json, click_info_t *out) {
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

static void pa_exit_signal_cb(pa_mainloop_api *api, pa_signal_event *e, int sig,
                              void *userdata) {
  fprintf(stderr, "Got exit signal %d\n", sig);
  exit(0);
}
//////////////////////////////////////////////////////////////

static void pa_io_event_cb(pa_mainloop_api *ea, pa_io_event *e, int fd,
                           pa_io_event_flags_t events, void *userdata) {
  int rc;
  char buff[1024] = {0};
  rc = line_gets(STDIN_FILENO, buff, sizeof(buff) - 1);
  if (rc < 0) {
    // todo log
    return; // got EOF so probably need to exit
  }

  click_info_t ci = {0};
  if (parse_click_info_json(buff, &ci)) {
    fprintf(stderr, "[stdin] INVALID JSON: %zd bytes: %s\n", strlen(buff),
            buff);
    return;
  }
  // todo show dialog

  dlg_init_t di = dlg_default_di();
  dlg_sound_raylib(22, &di);

  fprintf(stderr, "[stdin] click_info:\n");
  fprintf(stderr, "\tx: %d\n", ci.x);
  fprintf(stderr, "\ty: %d\n", ci.y);
  fprintf(stderr, "\trelative_x: %d\n", ci.relative_x);
  fprintf(stderr, "\trelative_y: %d\n", ci.relative_y);
  fprintf(stderr, "\twidth: %d\n", ci.width);
  fprintf(stderr, "\theight: %d\n", ci.height);
}
//////////////////////////////////////////////////////////////

static void subscribe_success_cb(pa_context *c, int success, void *userdata) {
  fprintf(stderr, "pa_context_success_cb. success: %d\n", success);
}
//////////////////////////////////////////////////////////////

static void pa_sink_info_cb(pa_context *c, const pa_sink_info *i, int eol,
                            void *userdata) {
  if (i == NULL) {
    printf("sink info is NULL\n");
    return;
  }

  if (!PA_SINK_IS_OPENED(i->state)) {
    printf("name: %s NOT OPENED. state: %x\n", i->name, i->state);
    return;
  }

  printf("pa_sink_info_cb: eol=%d\n", eol);
  printf("\tname: %s\n", i->name);
  printf("\tdescription: %s\n", i->description);
  printf("\tindex: %d\n", i->index);
  printf("\tmute: %d\n", i->mute);
  char buff[1024] = {0};
  pa_cvolume_snprint(buff, 1023, &i->volume);
  printf("\t%s\n", buff);
}
//////////////////////////////////////////////////////////////

static void ctx_subscribe_cb(pa_context *c, pa_subscription_event_type_t t,
                             uint32_t idx, void *userdata) {
  if (t & PA_SUBSCRIPTION_EVENT_CHANGE) {
    pa_context_get_sink_info_list(c, pa_sink_info_cb, NULL);
  }
  fprintf(stderr, "pa_context_subscribe_cb: et = %x, idx = %d\n", t, idx);
}

static void ctx_state_changed_cb(pa_context *pa_ctx, void *userdata) {
  pa_context_state_t state = pa_context_get_state(pa_ctx);
  fprintf(stderr, "pa_context_notify_cb: state = %x\n", state);

  if (!PA_CONTEXT_IS_GOOD(state)) {
    printf("!PA_CONTEXT_IS_GOOD\n");
    // todo restore it somehow, restart everything
    // etc., instead of die
    die("!PA_CONTEXT_IS_GOOD\n");
  }

  if (state & PA_CONTEXT_READY) {
    pa_subscription_mask_t ctx_sub_msk =
        PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SINK_INPUT;
    pa_operation *pa_op =
        pa_context_subscribe(pa_ctx, ctx_sub_msk, subscribe_success_cb, NULL);
    fprintf(stderr, "pa_op: %p\n", pa_op);
    pa_context_set_subscribe_callback(pa_ctx, ctx_subscribe_cb, NULL);
  }
}

// AUDIO_HIGH_SYMBOL=${AUDIO_HIGH_SYMBOL:-'🔊'}
// AUDIO_MED_THRESH=${AUDIO_MED_THRESH:-60}
// AUDIO_MED_SYMBOL=${AUDIO_MED_SYMBOL:-'🔉'}
// AUDIO_LOW_THRESH=${AUDIO_LOW_THRESH:-20}
// AUDIO_LOW_SYMBOL=${AUDIO_LOW_SYMBOL:-'🔈'}
// AUDIO_MUTED_SYMBOL=${AUDIO_MUTED_SYMBOL:-'🔇'}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  int rc;

  // not sure if I need this
  if (sys_cloexec(STDIN_FILENO)) {
    die("sys_cloexec");
  }

  pa_mainloop *pa_ml = pa_mainloop_new();
  if (!pa_ml) {
    die("pa_main_loop_new\n");
  }

  pa_mainloop_api *pa_api = pa_mainloop_get_api(pa_ml);
  if (!pa_api) {
    die("pa_mainloop_get_api\n");
  }

  if (pa_signal_init(pa_api)) {
    die("pa_signal_init\n");
  }

  int exit_signals[] = {SIGINT, SIGHUP, SIGTERM, -1};
  for (int *es = exit_signals; *es != -1; ++es) {
    if (!pa_signal_new(*es, pa_exit_signal_cb, NULL)) {
      die("pa_signal_new\n");
    }
  }

  pa_context *pa_ctx = pa_context_new(pa_api, "volumectl");
  rc = pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
  pa_context_set_state_callback(pa_ctx, ctx_state_changed_cb, NULL);

  pa_io_event *pa_ioev = pa_api->io_new(pa_api, STDIN_FILENO, PA_IO_EVENT_INPUT,
                                        pa_io_event_cb, NULL);

  pa_mainloop_run(pa_ml, &rc);
  pa_mainloop_free(pa_ml);

  return 0;
}
