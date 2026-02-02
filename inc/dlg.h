#ifndef DLG_H
#define DLG_H

#include <stdint.h>

typedef struct dlg_init {
  int32_t width;
  int32_t heigth;
  int32_t pos_x;
  int32_t pos_y;
} dlg_init_t;

int dlg_sound_raylib(int64_t vol, dlg_init_t di);
dlg_init_t default_di(void);

#endif
