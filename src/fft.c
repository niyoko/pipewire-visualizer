#include "fft.h"

#include <math.h>

void pwviz_fft_init(PwvizFft *fft) {
  fft->initialized = 0;
  fft->equalize_initialized = 0;
  fft->envelope_power = -1.0f;
}

static float calculate_envelope_value(int i, float power) {
  if (power <= 0.0f)
    return 1.0f;

  float value =
      0.5f + 0.5f * sinf((float)i * (2.0f * M_PI / PWVIZ_FFT_SIZE) -
                         (float)(M_PI / 2.0));
  return power == 1.0f ? value : powf(value, power);
}

static float calculate_equalize_value(int i) {
  float bias = 0.04f / powf(1.0025f, (float)i);
  float inv_half_nfreq =
      (9.0f - bias) / (float)(PWVIZ_FFT_SIZE / 2 + 1);
  return log10f(1.0f + bias + (float)(i + 1) * inv_half_nfreq);
}

static void update_envelope_table(PwvizFft *fft, float envelope_power) {
  if (fft->envelope_power == envelope_power)
    return;

  for (int i = 0; i < PWVIZ_FFT_SIZE; i++)
    fft->envelope[i] = calculate_envelope_value(i, envelope_power);
  fft->envelope_power = envelope_power;
}

static void init_equalize_table(PwvizFft *fft) {
  if (fft->equalize_initialized)
    return;

  for (int i = 0; i < PWVIZ_FFT_SIZE / 2 + 1; i++)
    fft->equalize[i] = calculate_equalize_value(i);
  fft->equalize_initialized = 1;
}

void pwviz_fft_analyze(PwvizFft *fft, const float *samples,
                       float *magnitudes, int equalize, float envelope_power) {
  if (!fft->initialized) {
    fft->plan =
        fftwf_plan_dft_r2c_1d(PWVIZ_FFT_SIZE, fft->input, fft->output,
                              FFTW_MEASURE);
    fft->initialized = 1;
  }

  update_envelope_table(fft, envelope_power);
  if (equalize)
    init_equalize_table(fft);

  for (int i = 0; i < PWVIZ_FFT_SIZE; i++) {
    fft->input[i] = samples[i] * 128.0f * fft->envelope[i];
  }

  fftwf_execute(fft->plan);

  for (int i = 0; i < PWVIZ_FFT_SIZE / 2 + 1; i++) {
    float re = fft->output[i][0];
    float im = fft->output[i][1];
    float magnitude = sqrtf(re * re + im * im);
    magnitudes[i] = equalize ? magnitude * fft->equalize[i] : magnitude;
  }
}
