#pragma once

#include <gdk/gdk.h>

typedef enum {
  PWVIZ_ANALYZER_BARS = 0,
  PWVIZ_ANALYZER_PEAK = 1,
  PWVIZ_ANALYZER_FLASH = 2,
} PwvizAnalyzerMode;

typedef enum {
  PWVIZ_ANCHOR_TOP_LEFT = 0,
  PWVIZ_ANCHOR_TOP = 1,
  PWVIZ_ANCHOR_TOP_RIGHT = 2,
  PWVIZ_ANCHOR_LEFT = 3,
  PWVIZ_ANCHOR_CENTER = 4,
  PWVIZ_ANCHOR_RIGHT = 5,
  PWVIZ_ANCHOR_BOTTOM_LEFT = 6,
  PWVIZ_ANCHOR_BOTTOM = 7,
  PWVIZ_ANCHOR_BOTTOM_RIGHT = 8,
} PwvizWindowAnchor;

typedef struct {
  PwvizAnalyzerMode analyzer_mode;
  PwvizWindowAnchor window_anchor;
  int bar_count;
  int block_height;
  int block_gap;
  int window_width;
  int window_height;
  int x_margin;
  int y_margin;
  int peak_hold_frames;
  float peak_fall_per_frame;
  float display_threshold;
  double background_alpha;
  double bar_alpha;
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
