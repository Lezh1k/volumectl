#ifndef DLG_H
#define DLG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct dlg_init {
  int32_t width;
  int32_t heigth;
  int32_t pos_x;
  int32_t pos_y;
} dlg_init_t;

int dlg_open(int64_t vol, const dlg_init_t *di);
int dlg_tick(void);
void dlg_close(void);

bool dlg_is_open(void);
int32_t dlg_current_vol(void);


#endif
