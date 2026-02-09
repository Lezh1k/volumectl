#include "dlg.h"
#include "log.h"
#include "out.h"
#include "sys.h"

#include <cjson/cJSON.h>

#include <errno.h>
#include <signal.h>

#include <pulse/pulseaudio.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

vlog_level_t log_level = VLOG_DEBUG;

typedef struct click_info {
  int x, y, rel_x, rel_y, blk_w, blk_h;
} click_info_t;

static int parse_click_info_json(const char *json, click_info_t *out);
static int line_getc(int fd, char *c);
static ssize_t line_gets(int fd, char *buf, size_t size);
static void die(const char *msg);
static void pa_exit_signal_cb(pa_mainloop_api *api, pa_signal_event *e, int sig,
                              void *userdata);
static void pa_io_event_cb(pa_mainloop_api *ea, pa_io_event *e, int fd,
                           pa_io_event_flags_t events, void *userdata);
static void pa_sink_info_cb(pa_context *c, const pa_sink_info *i, int eol,
                            void *userdata);
static void ctx_on_change_cb(pa_context *c, pa_subscription_event_type_t t,
                             uint32_t idx, void *userdata);
static void subscribe_success_cb(pa_context *c, int success, void *userdata);
static void ctx_state_changed_cb(pa_context *pa_ctx, void *userdata);

int parse_click_info_json(const char *json, click_info_t *out) {
  if (!json || !out)
    return -1;
  *out = (click_info_t){0};
  cJSON *root = cJSON_Parse(json);
  if (!root)
    return -1;

  const char *keys[] = {"x",     "y",      "relative_x", "relative_y",
                        "width", "height", NULL};
  int *values[] = {&out->x,     &out->y,     &out->rel_x, &out->rel_y,
                   &out->blk_w, &out->blk_h, NULL};
  for (int i = 0; keys[i]; ++i) {
    cJSON *it = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
    if (!cJSON_IsNumber(it))
      continue;
    *values[i] = (int)it->valueint;
  }

  cJSON_Delete(root);
  return 0;
}
//////////////////////////////////////////////////////////////

int line_getc(int fd, char *c) {
  // probably safe get_line version
  return sys_read(fd, c, 1, NULL);
}
//////////////////////////////////////////////////////////////

ssize_t line_gets(int fd, char *buf, size_t size) {
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

void die(const char *msg) {
  log_fatal("%s\n", msg);
  exit(1);
}
//////////////////////////////////////////////////////////////

void pa_exit_signal_cb(pa_mainloop_api *api, pa_signal_event *e, int sig,
                       void *userdata) {
  log_trace("Got exit signal %d\n", sig);
  exit(0);
}
//////////////////////////////////////////////////////////////

void pa_io_event_cb(pa_mainloop_api *ea, pa_io_event *e, int fd,
                    pa_io_event_flags_t events, void *userdata) {
  if (fd != STDIN_FILENO) {
    log_debug("unexpected input fd: %d\n", fd);
    return; // we expect input only from stdin
  }

  int rc;
  char buff[1024] = {0};
  rc = line_gets(STDIN_FILENO, buff, sizeof(buff) - 1);
  if (rc < 0) {
    // todo log
    return; // got EOF so probably need to exit
  }

  click_info_t ci = {0};
  if (parse_click_info_json(buff, &ci)) {
    log_error("[stdin] INVALID JSON: %zd bytes: %s\n", strlen(buff), buff);
    return;
  }

  // if panel on top - positive offset. else - negative offset (offset =
  // block.height)
  int32_t coeff = ci.y - ci.rel_y == 0 ? 1 : -1;
  dlg_init_t di = {.width = ci.blk_w + 150,
                   .heigth = ci.blk_h, // (same as block)
                   .pos_x = ci.x - ci.rel_x - ci.blk_w / 2,
                   .pos_y = ci.y - ci.rel_y + ci.blk_h * coeff};
  dlg_sound_raylib(22, &di);

  log_trace("[stdin] click_info:\n");
  log_trace("\tx: %d\n", ci.x);
  log_trace("\ty: %d\n", ci.y);
  log_trace("\trelative_x: %d\n", ci.rel_x);
  log_trace("\trelative_y: %d\n", ci.rel_y);
  log_trace("\twidth: %d\n", ci.blk_w);
  log_trace("\theight: %d\n", ci.blk_h);
}
//////////////////////////////////////////////////////////////

void pa_sink_info_cb(pa_context *c, const pa_sink_info *i, int eol,
                     void *userdata) {
  if (i == NULL) {
    return; // probably impossible
  }

  // we want just first channel actually, but let's do in a "right" way
  uint64_t v = 0;
  for (uint8_t ci = 0; ci < i->volume.channels; ++ci) {
    v += (i->volume.values[ci] * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM;
  }
  v /= i->volume.channels;
  volume_to_stdout(v, !!i->mute);

  log_trace("Sink #%u\n", i->index);
  log_trace("\tName: %s\n", i->name);
  log_trace("\tDescription: %s\n", i->description);
  log_trace("\tState: %s (%x)\n",
            (i->state == PA_SINK_RUNNING ? "RUNNING"
             : i->state == PA_SINK_IDLE  ? "IDLE"
                                         : "SUSPENDED"),
            i->state);
  log_trace("\tVolume: %lu%%\n", v);
  log_trace("\tMute: %s\n", i->mute ? "yes" : "no");
  log_trace("\tChannels: %d\n", i->sample_spec.channels);
  log_trace("\tSample Rate: %d Hz\n", i->sample_spec.rate);
  log_trace("\tMonitor Source: %s (#%u)\n", i->monitor_source_name,
            i->monitor_source);
  log_trace("\tDriver: %s\n", i->driver);
  log_trace("\tModule: %u\n", i->owner_module);
  log_trace("----------------------------------------\n");
}
//////////////////////////////////////////////////////////////

void ctx_on_change_cb(pa_context *c, pa_subscription_event_type_t t,
                      uint32_t idx, void *userdata) {
  pa_subscription_event_type_t facility =
      t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
  pa_subscription_event_type_t op = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
  log_trace("ctx_on_change_cb: et = %x, idx = %d, facility = %x, op = %x\n", t,
            idx, facility, op);

  if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
    if (op == PA_SUBSCRIPTION_EVENT_CHANGE) {
      pa_context_get_sink_info_by_index(c, idx, pa_sink_info_cb, userdata);
    }
    if (op == PA_SUBSCRIPTION_EVENT_NEW) {
      pa_context_get_sink_info_by_index(c, idx, pa_sink_info_cb, userdata);
    }
  } // if (facility == PA_SUBSCRIPTION_EVENT_SINK)
}
//////////////////////////////////////////////////////////////

void subscribe_success_cb(pa_context *c, int success, void *userdata) {
  log_trace("subscribe_success_cb succes = %d\n", success);
  if (!success) {
    die("subscribe_ctx_cb");
  }
  pa_context_set_subscribe_callback(c, ctx_on_change_cb, NULL);
}
//////////////////////////////////////////////////////////////

void ctx_state_changed_cb(pa_context *pa_ctx, void *userdata) {
  pa_context_state_t state = pa_context_get_state(pa_ctx);
  log_trace("pa_context_notify_cb: state = %x\n", state);

  if (!PA_CONTEXT_IS_GOOD(state)) {
    // todo restore it somehow, restart everything
    // etc., instead of die
    die("PA_CONTEXT IS NOT GOOD\n");
  }

  if (state == PA_CONTEXT_READY) {
    pa_context_get_sink_info_by_name(pa_ctx, NULL, pa_sink_info_cb, NULL);

    pa_subscription_mask_t ctx_sub_msk = PA_SUBSCRIPTION_MASK_SINK;
    pa_operation *pa_op =
        pa_context_subscribe(pa_ctx, ctx_sub_msk, subscribe_success_cb, NULL);
    log_debug("pa_op: %p\n", pa_op);
  }
}
//////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  int rc;
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

  // todo check pa_ioev
  pa_io_event *pa_ioev = pa_api->io_new(pa_api, STDIN_FILENO, PA_IO_EVENT_INPUT,
                                        pa_io_event_cb, NULL);
  pa_mainloop_run(pa_ml, &rc);
  pa_mainloop_free(pa_ml);

  return 0;
}
//////////////////////////////////////////////////////////////
