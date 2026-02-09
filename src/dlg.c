#include "dlg.h"
#include "log.h"
#include "out.h"
#include <microui.h>
#include <raylib.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define FONT_SIZE 20

static int text_width(mu_Font font, const char *txt, int len) {
  return MeasureText(TextFormat("%.*s", len, txt), FONT_SIZE);
}
static int text_height(mu_Font font) { return FONT_SIZE; }

static volatile bool g_open = false;
static mu_Context g_ctx = {0};
static float g_slider_curr = 0.0f;
static float g_slider_prev = 0.0f;
static dlg_init_t g_di = {0};

int dlg_open(int64_t vol, const dlg_init_t *di) {
  if (g_open || !di) {
    return 0;
  }

  g_di = *di;
  g_slider_curr = (float)vol;
  g_slider_prev = g_slider_curr;

  mu_init(&g_ctx);
  g_ctx.text_height = text_height;
  g_ctx.text_width = text_width;

  // I don't want any logs in STDOUT because use it in i3blocks env
  SetTraceLogLevel(LOG_NONE);
  SetTargetFPS(60);
  SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_VSYNC_HINT);
  InitWindow(g_di.width, g_di.heigth, "volumectl");
  SetWindowPosition(g_di.pos_x, g_di.pos_y);

  g_open = true;
  return 0;
}
//////////////////////////////////////////////////////////////

int dlg_tick(void) {
  if (!g_open) {
    return 0;
  }

  if (WindowShouldClose()) {
    dlg_close();
    return 0;
  }

  Vector2 mp = GetMousePosition();
  BeginDrawing();

  // mu_input functions
  // MOUSE_BUTTON_LEFT = 0 and MU_MOUSE_LEFT = (1 << 0)
  // MOUSE_BUTTON_RIGHT = 1 and MU_MOUSE_RIGHT = (1 << 1)
  // MOUSE_BUTTON_MIDDLE = 2 and MU_MOUSE_MIDDLE = (1 << 2)
  mu_input_mousemove(&g_ctx, mp.x, mp.y);
  for (int btn = MOUSE_BUTTON_LEFT; btn <= MOUSE_BUTTON_MIDDLE; ++btn) {
    if (IsMouseButtonDown(btn)) {
      mu_input_mousedown(&g_ctx, mp.x, mp.y, 1 << btn);
    }
    if (IsMouseButtonUp(btn)) {
      mu_input_mouseup(&g_ctx, mp.x, mp.y, 1 << btn);
    }
  }

  // keys
  const KeyboardKey keys_vol_up[] = {KEY_K, KEY_L, KEY_UP, KEY_RIGHT, KEY_NULL};
  const KeyboardKey keys_vol_down[] = {KEY_J, KEY_H, KEY_DOWN, KEY_LEFT,
                                       KEY_NULL};
  for (const KeyboardKey *pk = keys_vol_up; *pk != KEY_NULL; ++pk) {
    if (IsKeyPressed(*pk)) {
      ++g_slider_curr;
      break;
    }
  }
  for (const KeyboardKey *pk = keys_vol_down; *pk != KEY_NULL; ++pk) {
    if (IsKeyPressed(*pk)) {
      --g_slider_curr;
      break;
    }
  }

  g_slider_curr = MIN(MAX(g_slider_curr, 0),
                      100); // if sc < 0 sc = 0; if sc > 100 sc = 100
  // !mu_input end

  // process ui
  mu_begin(&g_ctx);
  if (!mu_begin_window_ex(&g_ctx, "Volumectl",
                          mu_rect(0, 0, g_di.width, g_di.heigth),
                          MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
    log_error("mu_begin_window failed\n");
    dlg_close();
    return 0;
  }

  mu_layout_row(&g_ctx, 2, (int[]){40, -1}, -1);
  mu_label(&g_ctx, "vol: ");
  mu_slider_ex(&g_ctx, &g_slider_curr, 0.0f, 100.f, 1.0f, "%.1f%%",
               MU_OPT_EXPANDED | MU_OPT_ALIGNCENTER);

  mu_end_window(&g_ctx);
  mu_end(&g_ctx);
  // !process ui end

  // process commands
  mu_Command *cmd = NULL;
  while (mu_next_command(&g_ctx, &cmd)) {
    switch (cmd->type) {
    case MU_COMMAND_RECT: {
      DrawRectangle(cmd->rect.rect.x, cmd->rect.rect.y, cmd->rect.rect.w,
                    cmd->rect.rect.h, *(Color *)&cmd->rect.color);
    } break;
    case MU_COMMAND_TEXT: {
      DrawText(cmd->text.str, cmd->text.pos.x, cmd->text.pos.y, FONT_SIZE,
               *(Color *)&cmd->text.color);
    } break;
    } // switch (cmd->type)
  } // while(mu_next_command(ctx, &cmd)
  // !process commands end

  if (g_slider_prev != g_slider_curr) {
    g_slider_prev = g_slider_curr;
  }

  // actually we don't need it
  // Color rai_background = {.r = 0, .g = 0, .b = 0, .a = 0};
  // ClearBackground(rai_background);

  EndDrawing();
  return 1;
}
//////////////////////////////////////////////////////////////

void dlg_close(void) {
  if (!g_open) {
    return;
  }
  CloseWindow();
  g_open = false;
}
//////////////////////////////////////////////////////////////

bool dlg_is_open(void) { return g_open; }
//////////////////////////////////////////////////////////////
int32_t dlg_current_vol(void) { return (int32_t)g_slider_curr; }
