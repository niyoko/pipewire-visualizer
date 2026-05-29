#include "binning.h"

#include <glib.h>
#include <math.h>

#define SPECTRUM_MIN_BIN 0
#define SPECTRUM_MAX_HZ 16000
static int assign_bin_count(int not_assigned, double divisor) {
  int count = (int)(not_assigned - not_assigned / divisor + 0.5);
  return count <= 0 ? 1 : count;
}

static void init_log_bar_table(PwvizBinner *binner, int bar_count) {
  if (binner->initialized && binner->bar_count == bar_count)
    return;

  int max_bin =
      (int)((double)SPECTRUM_MAX_HZ * PWVIZ_FFT_SIZE / PWVIZ_SAMPLE_RATE);
  max_bin = CLAMP(max_bin, bar_count, PWVIZ_FFT_SIZE / 2);

  for (int i = 0; i < PWVIZ_BAR_COUNT; i++)
    binner->bin_counts[i] = 1;

  int not_assigned = max_bin - bar_count;
  double divisor = pow((double)not_assigned, 1.0 / bar_count);

  while (not_assigned > 0) {
    int count = assign_bin_count(not_assigned, divisor);

    for (int bar = bar_count - 1; bar >= 0 && not_assigned > 0; bar--) {
      if (count > not_assigned)
        count = not_assigned;

      binner->bin_counts[bar] += (unsigned int)count;
      not_assigned -= count;
      count = assign_bin_count(not_assigned, divisor);
    }
  }

  binner->bar_count = bar_count;
  binner->initialized = 1;
}

void pwviz_binner_init(PwvizBinner *binner) {
  binner->initialized = 0;
  binner->bar_count = 0;
}

void pwviz_binner_calculate(PwvizBinner *binner, const float *magnitudes,
                            float *levels, const PwvizAppConfig *config) {
  int bar_count = CLAMP(config->bar_count, 1, PWVIZ_BAR_COUNT);
  float scale = MAX(config->fft_scale, 0.1f);

  init_log_bar_table(binner, bar_count);

  int bin = SPECTRUM_MIN_BIN;

  for (int b = 0; b < bar_count; b++) {
    int start_bin = bin;
    int end_bin = start_bin + (int)binner->bin_counts[b];
    if (end_bin > PWVIZ_FFT_SIZE / 2 + 1)
      end_bin = PWVIZ_FFT_SIZE / 2 + 1;

    float peak = 0.0f;
    float sum_sq = 0.0f;
    int count = 0;

    for (int i = start_bin; i < end_bin; i++) {
      float magnitude = CLAMP(magnitudes[i] / scale, 0.0f, 255.0f);

      if (magnitude > peak)
        peak = magnitude;

      sum_sq += magnitude * magnitude;
      count++;
    }

    bin = end_bin;

    float rms = count > 0 ? sqrtf(sum_sq / count) : 0.0f;
    float value =
        config->level_mode == PWVIZ_LEVEL_AVERAGE ? rms / 255.0f
                                                  : peak / 255.0f;

    levels[b] = CLAMP(value, 0.0f, 1.0f);
  }

  for (int b = bar_count; b < PWVIZ_BAR_COUNT; b++)
    levels[b] = 0.0f;
}
