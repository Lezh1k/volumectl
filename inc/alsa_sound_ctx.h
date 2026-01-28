#ifndef ALSA_SOUND_CTX_H
#define ALSA_SOUND_CTX_H

#include <alsa/asoundlib.h>
#include <stdint.h>

typedef struct snd_ctx_alsa {
  snd_mixer_t *mixer;
  snd_mixer_selem_id_t *sid;

  snd_mixer_elem_t *elem;
  int64_t min_val;
  int64_t max_val;

} snd_ctx_alsa_t;

int snd_ctx_alsa_create(snd_ctx_alsa_t *ctx);

int snd_ctx_alsa_get_volume(const snd_ctx_alsa_t *ctx, int64_t *vol);
int snd_ctx_alsa_set_volume(const snd_ctx_alsa_t *ctx, int vol);

#endif
