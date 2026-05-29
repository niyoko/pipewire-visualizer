#include "binning.h"

#include <glib.h>
#include <math.h>

#define SPECTRUM_MIN_BIN 1
#define SPECTRUM_MAX_HZ 16000
#define SPECTRUM_NOISE_FLOOR_DB -78.0f
#define SPECTRUM_RANGE_DB 62.0f

static int assign_bin_count(int not_assigned, double divisor) {
  int count = (int)(not_assigned - not_assigned / divisor + 0.5);
  return count <= 0 ? 1 : count;
}

static void init_log_bar_table(PwvizBinner *binner) {
  if (binner->initialized)
    return;

  int max_bin =
      (int)((double)SPECTRUM_MAX_HZ * PWVIZ_FFT_SIZE / PWVIZ_SAMPLE_RATE);
  max_bin = CLAMP(max_bin, PWVIZ_BAR_COUNT, PWVIZ_FFT_SIZE / 2);

  for (int i = 0; i < PWVIZ_BAR_COUNT; i++)
    binner->bin_counts[i] = 1;

  int not_assigned = max_bin - PWVIZ_BAR_COUNT;
  double divisor = pow((double)not_assigned, 1.0 / PWVIZ_BAR_COUNT);

  while (not_assigned > 0) {
    int count = assign_bin_count(not_assigned, divisor);

    for (int bar = PWVIZ_BAR_COUNT - 1; bar >= 0 && not_assigned > 0; bar--) {
      if (count > not_assigned)
        count = not_assigned;

      binner->bin_counts[bar] += (unsigned int)count;
      not_assigned -= count;
      count = assign_bin_count(not_assigned, divisor);
    }
  }

  binner->initialized = 1;
}

void pwviz_binner_init(PwvizBinner *binner) {
  binner->initialized = 0;
}

void pwviz_binner_calculate(PwvizBinner *binner, const float *magnitudes,
                            float *levels) {
  init_log_bar_table(binner);

  int bin = SPECTRUM_MIN_BIN;

  for (int b = 0; b < PWVIZ_BAR_COUNT; b++) {
    int start_bin = bin;
    int end_bin = start_bin + (int)binner->bin_counts[b];
    if (end_bin > PWVIZ_FFT_SIZE / 2 + 1)
      end_bin = PWVIZ_FFT_SIZE / 2 + 1;

    float peak = 0.0f;
    float sum_sq = 0.0f;
    int count = 0;

    for (int i = start_bin; i < end_bin; i++) {
      float magnitude = magnitudes[i];

      if (magnitude > peak)
        peak = magnitude;

      sum_sq += magnitude * magnitude;
      count++;
    }

    bin = end_bin;

    float rms = count > 0 ? sqrtf(sum_sq / count) : 0.0f;
    float mixed = MAX(peak * 0.72f, rms);
    float db =
        20.0f * log10f(mixed / (float)(PWVIZ_FFT_SIZE / 2) + 0.000001f);
    float value = (db - SPECTRUM_NOISE_FLOOR_DB) / SPECTRUM_RANGE_DB;

    value = CLAMP(value, 0.0f, 1.0f);
    levels[b] = powf(value, 0.62f);
  }
}
