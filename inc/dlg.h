#ifndef DLG_H
#define DLG_H

#include "alsa_sound_ctx.h"
#include <stdint.h>

int dlg_sound(int64_t vol, const snd_ctx_alsa_t *snd_ctx);

#endif
