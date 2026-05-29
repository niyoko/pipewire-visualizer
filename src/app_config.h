#pragma once

#include <gdk/gdk.h>

typedef enum {
  PWVIZ_ANALYZER_BARS = 0,
  PWVIZ_ANALYZER_PEAK = 1,
  PWVIZ_ANALYZER_FLASH = 2,
} PwvizAnalyzerMode;

typedef struct {
  PwvizAnalyzerMode analyzer_mode;
  int bar_count;
  int block_height;
  int block_gap;
  int peak_hold_frames;
  float peak_fall_per_frame;
  float display_threshold;
  double background_alpha;
  gboolean show_border;
  GdkRGBA low_color;
  GdkRGBA high_color;
  GdkRGBA peak_color;
  GdkRGBA background_color;
  char profile_name[64];
} PwvizAppConfig;

void pwviz_app_config_set_defaults(PwvizAppConfig *config);
void pwviz_app_config_load(PwvizAppConfig *config);
void pwviz_app_config_save(const PwvizAppConfig *config);
void pwviz_app_config_apply_profile(PwvizAppConfig *config,
                                    const char *profile_name);
