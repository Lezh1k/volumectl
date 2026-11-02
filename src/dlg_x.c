#include "dlg.h"
#include "log.h"
#include "sys.h"
#include <microui.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define IDLE_TIMEOUT 4.0f
#define DEFAULT_FONT_NAME "fixed"
#define DEFAULT_FONT_SIZE 20
#define FALLBACK_FONT_NAME "fixed" /* always available on any X server */

static bool g_open = false;
static struct timespec g_last_tick_ts = {0};

static Display *g_disp = NULL;
static Window g_win = None;
static GC g_gc = NULL;
static XFontStruct *g_font = NULL;

/* Window that had X input focus right before we stole it, so dlg_close()
   can hand focus back explicitly. Needed because g_win is override_redirect
   (invisible to i3), so the WM never re-focuses the previous window itself
   -- and XDestroyWindow's default RevertToParent just leaves focus on the
   root window instead of the terminal. */
static Window g_prev_focus = None;
static int g_prev_revert_to = RevertToParent;

static mu_Context g_ctx = {0};
static float g_slider_curr = 0.0f;
static float g_slider_prev = 0.0f;

static float g_idle = 0.0f;
static dlg_geometry_t g_di = {0};

static const int EVENT_MASK = ExposureMask | KeyPressMask | ButtonPressMask |
                              ButtonReleaseMask | PointerMotionMask |
                              StructureNotifyMask;

/* Loads the font named by VOLUMECTL_FONT/VOLUMECTL_FONT_SIZE (defaulting to
   DEFAULT_FONT_NAME/DEFAULT_FONT_SIZE), falling back to FALLBACK_FONT_NAME
   if the requested family/size combination doesn't exist on this server.
   These are X core bitmap fonts, not scalable TrueType -- a family only
   exists in whatever discrete pixel sizes are installed (see README.md's
   Configuration section for how to list them with xlsfonts). */
static XFontStruct *load_configured_font(Display *disp) {
  const char *name = getenv("VOLUMECTL_FONT");
  if (!name || !*name) {
    name = DEFAULT_FONT_NAME;
  }

  int size = DEFAULT_FONT_SIZE;
  const char *size_str = getenv("VOLUMECTL_FONT_SIZE");
  if (size_str && *size_str) {
    int parsed = atoi(size_str);
    if (parsed > 0) {
      size = parsed;
    } else {
      log_error("invalid VOLUMECTL_FONT_SIZE: %s, using default %d\n", size_str,
                size);
    }
  }

  char xlfd[256];
  snprintf(xlfd, sizeof(xlfd), "-*-%s-*-*-*-*-%d-*-*-*-*-*-*-*", name, size);

  XFontStruct *font = XLoadQueryFont(disp, xlfd);
  if (!font) {
    log_error("failed to load font '%s' size %d (pattern: %s), "
              "falling back to '%s'\n",
              name, size, xlfd, FALLBACK_FONT_NAME);
    font = XLoadQueryFont(disp, FALLBACK_FONT_NAME);
  }
  return font;
}
//////////////////////////////////////////////////////////////

static int text_width(mu_Font font, const char *txt, int len) {
  UNUSED(font);
  if (len < 0) {
    len = (int)strlen(txt);
  }
  return XTextWidth(g_font, txt, len);
}
//////////////////////////////////////////////////////////////

static int text_height(mu_Font font) {
  UNUSED(font);
  return g_font->ascent + g_font->descent;
}
//////////////////////////////////////////////////////////////

static int xmousebtn_to_mu(int xbtn) {
  /*
  1	Left button
  2	Middle button (scroll wheel)
  3	Right button
  4	Scroll wheel up
  5	Scroll wheel down
  6	Push scroll wheel left
  7	Push scroll wheel right
  8	Fourth button (Browser Back)
  9	Fifth button (Browser Forward)
  */
  switch (xbtn) {
  case 1:
    return MU_MOUSE_LEFT;
  case 2:
    return MU_MOUSE_MIDDLE;
  case 3:
    return MU_MOUSE_RIGHT;
  }
  return 0;
}

int dlg_open(int64_t vol, const dlg_geometry_t *di) {
  if (g_open || !di) {
    return 0;
  }

  g_di = *di;
  g_idle = 0.0f;
  g_slider_curr = (float)vol;
  g_slider_prev = g_slider_curr;

  g_disp = XOpenDisplay(NULL);
  if (!g_disp) {
    log_error("XOpenDisplay failed\n");
    return 1;
  }

  g_font = load_configured_font(g_disp);
  if (!g_font) {
    log_error("failed to load fallback font '%s'\n", FALLBACK_FONT_NAME);
    XCloseDisplay(g_disp);
    g_disp = NULL;
    return 1;
  }

  mu_init(&g_ctx);
  g_ctx.text_height = text_height;
  g_ctx.text_width = text_width;

  int screen = DefaultScreen(g_disp);
  const int border_width = 1;
  const int border_color = BlackPixel(g_disp, screen);
  const int background = WhitePixel(g_disp, screen);
  g_win = XCreateSimpleWindow(g_disp, RootWindow(g_disp, screen), g_di.pos_x,
                              g_di.pos_y, g_di.width, g_di.heigth, border_width,
                              border_color, background);

  g_gc = XCreateGC(g_disp, g_win, 0, NULL);
  XSetFont(g_disp, g_gc, g_font->fid);

  // Bypass the window manager: no title bar, no decorations
  XSetWindowAttributes attrs;
  attrs.override_redirect = True;
  XChangeWindowAttributes(g_disp, g_win, CWOverrideRedirect, &attrs);

  // select events of interest
  XSelectInput(g_disp, g_win, EVENT_MASK);

  // i'm not sure if I need next 2 lines.
  // Atom wm_delete = XInternAtom(g_disp, "WM_DELETE_WINDOW", False);
  // XSetWMProtocols(g_disp, g_win, &wm_delete, 1);

  // Remember who had focus before we take it, so dlg_close() can give it
  // back explicitly instead of relying on X's revert-to-parent default.
  XGetInputFocus(g_disp, &g_prev_focus, &g_prev_revert_to);

  XMapWindow(g_disp, g_win);

  clock_gettime(CLOCK_MONOTONIC, &g_last_tick_ts);
  g_open = true;
  return 0;
}
//////////////////////////////////////////////////////////////

static int not_failing_x_error_handler(Display *disp, XErrorEvent *ev) {
  UNUSED(disp);
  log_error("req: %d, error: %d", ev->request_code, ev->error_code);
  return 0;
}

int dlg_tick(void) {
  if (!g_open) {
    return 0;
  }

  int input_event_happened = 0;
  bool want_close = false;

  XEvent event;
  XErrorHandler old_eh = XSetErrorHandler(not_failing_x_error_handler);
  while (XCheckWindowEvent(g_disp, g_win, EVENT_MASK, &event)) {
    switch (event.type) {
    case MapNotify: {
      /* Window is now on screen and viewable — grab focus.
         Doing this before the window is viewable would raise
         a BadMatch error, which is why we wait for MapNotify. */
      XSetInputFocus(g_disp, g_win, RevertToParent, CurrentTime);
    } break;

    case Expose:
      // not sure if I want this
      XDrawRectangle(g_disp, g_win, g_gc, g_di.pos_x, g_di.pos_y, g_di.width,
                     g_di.heigth);
      break;

    case KeyPress: {
      KeySym key = XLookupKeysym(&event.xkey, 0);
      if (key == XK_Escape) {
        want_close = true;
        break;
      }
      const KeySym keys_vol_up[] = {XK_k, XK_K, XK_l, XK_L, XK_Up, XK_Right, 0};
      const KeySym keys_vol_down[] = {XK_j,    XK_J,    XK_h, XK_H,
                                      XK_Down, XK_Left, 0};

      for (const KeySym *pk = keys_vol_up; *pk != 0; ++pk) {
        if (key == *pk) {
          g_slider_curr += 1.0f;
          input_event_happened = 1;
          break;
        }
      }

      for (const KeySym *pk = keys_vol_down; *pk != 0; ++pk) {
        if (key == *pk) {
          g_slider_curr -= 1.0f;
          input_event_happened = 1;
          break;
        }
      }
    } break;

    case ButtonPress: {
      mu_input_mousedown(&g_ctx, event.xbutton.x, event.xbutton.y,
                         xmousebtn_to_mu(event.xbutton.button));
      input_event_happened = 1;
    } break;

    case ButtonRelease: {
      mu_input_mouseup(&g_ctx, event.xbutton.x, event.xbutton.y,
                       xmousebtn_to_mu(event.xbutton.button));
      input_event_happened = 1;
    } break;

    case MotionNotify: {
      mu_input_mousemove(&g_ctx, event.xmotion.x, event.xmotion.y);
      input_event_happened = 1;
    } break;

    default: {
      log_trace("not handled events: %d\n", event.type);
    } break;
    } // switch (event.type)
  } // while (XCheckWindowEvent(g_disp, g_win, EVENT_MASK, &event))

  XSync(g_disp, false);
  XSetErrorHandler(old_eh);

  g_slider_curr = MIN(MAX(g_slider_curr, 0), 100);

  mu_begin(&g_ctx);
  if (!mu_begin_window_ex(&g_ctx, "Volumectl",
                          mu_rect(0, 0, g_di.width, g_di.heigth),
                          MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
    log_error("mu_begin_window failed: %d\n", 1);
    dlg_close();
    return 0;
  }

  mu_layout_row(&g_ctx, 2, (int[]){40, -1}, -1);
  mu_label(&g_ctx, "vol: ");
  mu_slider_ex(&g_ctx, &g_slider_curr, 0.0f, 100.f, 1.0f, "%.1f%%",
               MU_OPT_EXPANDED | MU_OPT_ALIGNCENTER);

  mu_end_window(&g_ctx);
  mu_end(&g_ctx);

  // process commands
  mu_Command *cmd = NULL;
  while (mu_next_command(&g_ctx, &cmd)) {
    switch (cmd->type) {
    case MU_COMMAND_RECT: {
      unsigned long pixel = ((cmd->rect.color.r & 0xff) << 16) |
                            ((cmd->rect.color.g & 0xff) << 8) |
                            ((cmd->rect.color.b & 0xff));
      XSetForeground(g_disp, g_gc, pixel);
      XFillRectangle(g_disp, g_win, g_gc, cmd->rect.rect.x, cmd->rect.rect.y,
                     cmd->rect.rect.w, cmd->rect.rect.h);
    } break;
    case MU_COMMAND_TEXT: {
      /*
          memcpy(cmd->text.str, str, len);
          cmd->text.str[len] = '\0';
          that's why I use strlen(cmd->text.str)
      */
      unsigned long pixel = ((cmd->text.color.r & 0xff) << 16) |
                            ((cmd->text.color.g & 0xff) << 8) |
                            ((cmd->text.color.b & 0xff));
      XSetForeground(g_disp, g_gc, pixel);
      /* cmd->text.pos.y is the top of the text box (see text_height()),
         but XDrawString expects a baseline -- offset by the font's ascent. */
      XDrawString(g_disp, g_win, g_gc, cmd->text.pos.x,
                  cmd->text.pos.y + g_font->ascent, cmd->text.str,
                  strlen(cmd->text.str));
    } break;
    } // switch (cmd->type)
  } // while(mu_next_command(ctx, &cmd)
  // // !process commands end

  if (g_slider_prev != g_slider_curr) {
    g_slider_prev = g_slider_curr;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  float dt = (float)(now.tv_sec - g_last_tick_ts.tv_sec) +
             (float)(now.tv_nsec - g_last_tick_ts.tv_nsec) / 1e9f;
  g_last_tick_ts = now;

  g_idle = g_idle * (!input_event_happened) + dt;
  if (want_close || g_idle >= IDLE_TIMEOUT) {
    dlg_close();
  }

  return 1;
}
//////////////////////////////////////////////////////////////

void dlg_close(void) {
  if (!g_open) {
    return;
  }

  g_open = false;
  g_idle = 0.0f;

  /* Restore focus to whatever had it before dlg_open() stole it. Our window
     is override_redirect, so i3 won't do this for us, and the default
     RevertToParent target (root window) leaves nothing focused.

     Sometimes I catch BadMatch in XSetInputFocus. So we don't want whole app
     fail
   */
  XErrorHandler old_eh = XSetErrorHandler(not_failing_x_error_handler);
  XSetInputFocus(g_disp, g_prev_focus, g_prev_revert_to, CurrentTime);
  XSync(g_disp, false);
  XSetErrorHandler(old_eh);

  XFreeGC(g_disp, g_gc);
  XUnmapWindow(g_disp, g_win);
  XDestroyWindow(g_disp, g_win);
  XFreeFont(g_disp, g_font);
  XFlush(g_disp);
  XCloseDisplay(g_disp);

  g_gc = NULL;
  g_font = NULL;
}
//////////////////////////////////////////////////////////////

bool dlg_is_open(void) { return g_open; }
//////////////////////////////////////////////////////////////
int32_t dlg_current_vol(void) { return (int32_t)g_slider_curr; }
