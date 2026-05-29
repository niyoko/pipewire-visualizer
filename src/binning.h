#pragma once

#include "config.h"

typedef struct {
  unsigned int bin_counts[PWVIZ_BAR_COUNT];
  int initialized;
} PwvizBinner;

void pwviz_binner_init(PwvizBinner *binner);
void pwviz_binner_calculate(PwvizBinner *binner, const float *magnitudes,
                            float *levels);
