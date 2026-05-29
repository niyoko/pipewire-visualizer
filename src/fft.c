#include "fft.h"

#include <math.h>

void pwviz_fft_init(PwvizFft *fft) {
  fft->initialized = 0;
}

void pwviz_fft_analyze(PwvizFft *fft, const float *samples,
                       float *magnitudes) {
  if (!fft->initialized) {
    fft->plan =
        fftwf_plan_dft_r2c_1d(PWVIZ_FFT_SIZE, fft->input, fft->output,
                              FFTW_MEASURE);
    fft->initialized = 1;
  }

  for (int i = 0; i < PWVIZ_FFT_SIZE; i++) {
    float hann =
        0.5f * (1.0f - cosf(2.0f * M_PI * i / (PWVIZ_FFT_SIZE - 1)));
    fft->input[i] = samples[i] * hann;
  }

  fftwf_execute(fft->plan);

  for (int i = 0; i < PWVIZ_FFT_SIZE / 2 + 1; i++) {
    float re = fft->output[i][0];
    float im = fft->output[i][1];
    magnitudes[i] = sqrtf(re * re + im * im);
  }
}
