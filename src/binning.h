#pragma once

#include "app_config.h"
#include "config.h"

typedef struct {
  unsigned int bin_counts[PWVIZ_BAR_COUNT];
  int bar_count;
  int initialized;
} PwvizBinner;

void pwviz_binner_init(PwvizBinner *binner);
void pwviz_binner_calculate(PwvizBinner *binner, const float *magnitudes,
                            float *levels, const PwvizAppConfig *config);
