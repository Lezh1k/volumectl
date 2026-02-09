#include "out.h"
#include <stdio.h>

const int32_t AUDIO_MED_THRESH = 60;
const int32_t AUDIO_LOW_THRESH = 25;

static const char *LOW_SYMBOL = "🔈";
static const char *MED_SYMBOL = "🔉";
static const char *HIGH_SYMBOL = "🔊";
static const char *MUT_SYMBOL = "🔇";

static const char *COLOR_GREEN = "#00db16";
static const char *COLOR_YELLOW = "#ffff40";
static const char *COLOR_RED = "#c90007";
static const char *MUTED_COLOR = "#8d9196";

void volume_to_stdout(int32_t vol, bool muted) {
  const char *prefix = MUT_SYMBOL;
  const char *color = MUTED_COLOR;
  if (vol > AUDIO_MED_THRESH) {
    prefix = HIGH_SYMBOL;
    color = COLOR_RED;
  } else if (vol > AUDIO_LOW_THRESH) {
    prefix = MED_SYMBOL;
    color = COLOR_YELLOW;
  } else if (vol > 0) {
    prefix = LOW_SYMBOL;
    color = COLOR_GREEN;
  } else {
    prefix = MUT_SYMBOL;
    color = MUTED_COLOR;
  }

  if (muted) {
    prefix = MUT_SYMBOL;
    color = MUTED_COLOR;
  }

  printf("{\"full_text\": \"%2s:%3d%%\", \"color\": \"%s\"}\n", prefix, vol,
         color);
  fflush(stdout);
}
