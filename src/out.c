#include "out.h"
#include <stdio.h>

const int32_t AUDIO_MED_THRESH = 60;
const int32_t AUDIO_LOW_THRESH = 25;

static const char *HIGH_SYMBOL = "🔊";
static const char *MED_SYMBOL = "🔉";
static const char *LOW_SYMBOL = "🔈";
static const char *MUT_SYMBOL = "🔇";

void volume_to_stdout(int32_t vol) {
  const char *prefix = MUT_SYMBOL;
  if (vol > AUDIO_MED_THRESH) {
    prefix = HIGH_SYMBOL;
  } else if (vol > AUDIO_LOW_THRESH) {
    prefix = MED_SYMBOL;
  } else if (vol > 0) {
    prefix = LOW_SYMBOL;
  } else {
    prefix = MUT_SYMBOL;
  }
  printf("%s: %d%%\n", prefix, vol);
}
