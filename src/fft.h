#pragma once

#include "config.h"

#include <fftw3.h>

typedef struct {
  float input[PWVIZ_FFT_SIZE];
  fftwf_complex output[PWVIZ_FFT_SIZE / 2 + 1];
  float envelope[PWVIZ_FFT_SIZE];
  float equalize[PWVIZ_FFT_SIZE / 2 + 1];
  fftwf_plan plan;
  float envelope_power;
  int initialized;
  int equalize_initialized;
} PwvizFft;

void pwviz_fft_init(PwvizFft *fft);
void pwviz_fft_analyze(PwvizFft *fft, const float *samples,
                       float *magnitudes, int equalize, float envelope_power);
