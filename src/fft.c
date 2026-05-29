#include "fft.h"

#include <math.h>

void pwviz_fft_init(PwvizFft *fft) {
  fft->initialized = 0;
}

static float envelope_value(int i, float power) {
  if (power <= 0.0f)
    return 1.0f;

  float value =
      0.5f + 0.5f * sinf((float)i * (2.0f * M_PI / PWVIZ_FFT_SIZE) -
                         (float)(M_PI / 2.0));
  return power == 1.0f ? value : powf(value, power);
}

static float equalize_value(int i) {
  float bias = 0.04f / powf(1.0025f, (float)i);
  float inv_half_nfreq =
      (9.0f - bias) / (float)(PWVIZ_FFT_SIZE / 2 + 1);
  return log10f(1.0f + bias + (float)(i + 1) * inv_half_nfreq);
}

void pwviz_fft_analyze(PwvizFft *fft, const float *samples,
                       float *magnitudes, int equalize, float envelope_power) {
  if (!fft->initialized) {
    fft->plan =
        fftwf_plan_dft_r2c_1d(PWVIZ_FFT_SIZE, fft->input, fft->output,
                              FFTW_MEASURE);
    fft->initialized = 1;
  }

  for (int i = 0; i < PWVIZ_FFT_SIZE; i++) {
    fft->input[i] = samples[i] * 128.0f * envelope_value(i, envelope_power);
  }

  fftwf_execute(fft->plan);

  for (int i = 0; i < PWVIZ_FFT_SIZE / 2 + 1; i++) {
    float re = fft->output[i][0];
    float im = fft->output[i][1];
    float magnitude = sqrtf(re * re + im * im);
    magnitudes[i] = equalize ? magnitude * equalize_value(i) : magnitude;
  }
}
