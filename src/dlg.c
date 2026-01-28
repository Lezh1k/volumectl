#include "dlg.h"
#include <microui.h>
#include <raylib.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define FONT_SIZE 20
#define WINDOW_WIDTH 300
#define WINDOW_HEIGHT 50
#define WINDOW_MARGIN 0

static int text_width(mu_Font font, const char *txt, int len) {
  return MeasureText(TextFormat("%.*s", len, txt), FONT_SIZE);
}
static int text_height(mu_Font font) { return FONT_SIZE; }

int dlg_sound(int64_t vol, const snd_ctx_alsa_t *snd_ctx) {
  int rc = 0;
  mu_Context __ctx = {0};
  mu_Context *ctx = &__ctx;
  mu_init(ctx);

  ctx->text_height = text_height;
  ctx->text_width = text_width;

  float slider_curr = (float)vol;
  float slider_prev = slider_curr;
  bool dragging = false;

  Color rai_background = {.r = 0x00, .g = 0x55, .b = 0xaa, .a = 0x00};
  SetTargetFPS(60);
  SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_VSYNC_HINT);
  InitWindow(WINDOW_WIDTH + WINDOW_MARGIN, WINDOW_HEIGHT + WINDOW_MARGIN,
             "volumectl");

  SetWindowPosition(1420, 25); // TODO get these from input
  while (!WindowShouldClose()) {
    Vector2 wp = GetWindowPosition();
    Vector2 mp = GetMousePosition();

    BeginDrawing();

    // mu_input functions
    // MOUSE_BUTTON_LEFT = 0 and MU_MOUSE_LEFT = (1 << 0)
    // MOUSE_BUTTON_RIGHT = 1 and MU_MOUSE_RIGHT = (1 << 1)
    // MOUSE_BUTTON_MIDDLE = 2 and MU_MOUSE_MIDDLE = (1 << 2)
    mu_input_mousemove(ctx, mp.x, mp.y);
    for (int btn = MOUSE_BUTTON_LEFT; btn <= MOUSE_BUTTON_MIDDLE; ++btn) {
      if (IsMouseButtonDown(btn)) {
        mu_input_mousedown(ctx, mp.x, mp.y, 1 << btn);
      }
      if (IsMouseButtonUp(btn))
        mu_input_mouseup(ctx, mp.x, mp.y, 1 << btn);
    }
    const KeyboardKey keys_vol_up[] = {KEY_K, KEY_L, KEY_UP, KEY_RIGHT,
                                       KEY_NULL};
    const KeyboardKey keys_vol_down[] = {KEY_J, KEY_H, KEY_DOWN, KEY_LEFT,
                                         KEY_NULL};
    for (const KeyboardKey *pk = keys_vol_up; *pk != KEY_NULL; ++pk) {
      if (IsKeyPressed(*pk)) {
        ++slider_curr;
        break;
      }
    }
    for (const KeyboardKey *pk = keys_vol_down; *pk != KEY_NULL; ++pk) {
      if (IsKeyPressed(*pk)) {
        --slider_curr;
        break;
      }
    }

    slider_curr =
        MIN(MAX(slider_curr, 0), 100); // if sc < 0 sc = 0; if sc > 100 sc = 100
    // !mu_input end

    // process ui
    mu_begin(ctx);
    if (!mu_begin_window_ex(
            ctx, "Volumectl",
            mu_rect(WINDOW_MARGIN, WINDOW_MARGIN, WINDOW_WIDTH, WINDOW_HEIGHT),
            MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
      perror("mu_begin_window failed\n");
      return 1;
    }

    mu_layout_row(ctx, 2, (int[]){40, -1}, -1);
    mu_label(ctx, "vol: ");
    mu_slider_ex(ctx, &slider_curr, 0.0f, 100.f, 1.0f, "%.1f%%",
                 MU_OPT_EXPANDED | MU_OPT_ALIGNCENTER);
    mu_end_window(ctx);
    mu_end(ctx);
    // !process ui end

    // process commands
    mu_Command *cmd = NULL;
    while (mu_next_command(ctx, &cmd)) {
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

    if (slider_prev != slider_curr) {
      if ((rc = snd_ctx_alsa_set_volume(snd_ctx, (int)slider_curr))) {
        fprintf(stderr, "snd_ctx_alsa_get_volume failed. err: %d\n", rc);
      }
      slider_prev = slider_curr;
    }

    ClearBackground(rai_background);
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
//////////////////////////////////////////////////////////////
