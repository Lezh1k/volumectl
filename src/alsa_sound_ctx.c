#include "alsa_sound_ctx.h"
#include <alsa/asoundlib.h>
#include <math.h>
#include <stdio.h>

int snd_ctx_alsa_create(snd_ctx_alsa_t *ctx) {
  int rc = 0;
  // probably better move to envs
  const char *card = "default";
  const char *selem_name = "Master";

  if ((rc = snd_mixer_open(&ctx->mixer, 0)) < 0) {
    fprintf(stderr, "snd_mixer_open failed. rc = %d\n", rc);
    return rc;
  }

  if ((rc = snd_mixer_attach(ctx->mixer, card)) < 0) {
    fprintf(stderr, "snd_mixer_attach failed. rc = %d\n", rc);
    snd_mixer_close(ctx->mixer);
    return rc;
  }

  if ((rc = snd_mixer_selem_register(ctx->mixer, NULL, NULL)) < 0) {
    fprintf(stderr, "snd_mixer_selem_register failed. rc = %d\n", rc);
    snd_mixer_close(ctx->mixer);
    return rc;
  }

  if ((rc = snd_mixer_load(ctx->mixer)) < 0) {
    fprintf(stderr, "snd_mixer_load failed. rc = %d\n", rc);
    snd_mixer_close(ctx->mixer);
    return rc;
  }

  // TODO remove if sid is not necessary in next calls and use
  // snd_mixer_selem_id_alloca
  if ((rc = snd_mixer_selem_id_malloc(&ctx->sid)) < 0) {
    fprintf(stderr, "snd_mixer_selem_id_malloc failed. rc = %d\n", rc);
    snd_mixer_close(ctx->mixer);
    return rc;
  }

  // snd_mixer_selem_id_alloca(&ctx->sid);
  snd_mixer_selem_id_set_index(ctx->sid, 0);
  snd_mixer_selem_id_set_name(ctx->sid, selem_name);

  ctx->elem = snd_mixer_find_selem(ctx->mixer, ctx->sid);
  if (!ctx->elem) {
    rc = 404; // not found
    fprintf(stderr, "snd_mixer_find_selem failed. rc = %d\n", rc);
    snd_mixer_close(ctx->mixer);
    return rc;
  }
  snd_mixer_selem_get_playback_volume_range(ctx->elem, &ctx->min_val,
                                            &ctx->max_val);

  return 0;
}
//////////////////////////////////////////////////////////////

int snd_ctx_alsa_get_volume(const snd_ctx_alsa_t *ctx, int64_t *vol) {
  int rc = snd_mixer_selem_get_playback_volume(ctx->elem,
                                               SND_MIXER_SCHN_FRONT_LEFT, vol);
  if (rc) {
    fprintf(stderr, "snd_mixer_selem_get_playback_volume failed. rc: %d\n", rc);
    return rc;
  }
  double range = ctx->max_val - ctx->min_val;
  *vol = lround((*vol * 100.0) / range);
  return 0;
}
//////////////////////////////////////////////////////////////

int snd_ctx_alsa_set_volume(const snd_ctx_alsa_t *ctx, int vol) {
  long vv = (long)((double)vol / 100.0 * (ctx->max_val - ctx->min_val) +
                   ctx->min_val);
  int rc = snd_mixer_selem_set_playback_volume_all(ctx->elem, vv);
  return rc;
}
//////////////////////////////////////////////////////////////
