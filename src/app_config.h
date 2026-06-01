#pragma once

#include <gdk/gdk.h>

typedef enum {
  PWVIZ_ANALYZER_BARS = 0,
  PWVIZ_ANALYZER_PEAK = 1,
  PWVIZ_ANALYZER_FLASH = 2,
} PwvizAnalyzerMode;

typedef enum {
  PWVIZ_LEVEL_PEAK = 0,
  PWVIZ_LEVEL_AVERAGE = 1,
} PwvizLevelMode;

typedef enum {
  PWVIZ_BAR_STYLE_CLASSIC = 0,
  PWVIZ_BAR_STYLE_SOFT_FLAME = 1,
  PWVIZ_BAR_STYLE_FIRE = 2,
  PWVIZ_BAR_STYLE_SOLID_LINES = 3,
  PWVIZ_BAR_STYLE_WINAMP_FIRE = 4,
  PWVIZ_BAR_STYLE_RANDOM = 5,
} PwvizBarStyle;

typedef enum {
  PWVIZ_BACKGROUND_BLACK = 0,
  PWVIZ_BACKGROUND_GRID = 1,
  PWVIZ_BACKGROUND_SOLID = 2,
  PWVIZ_BACKGROUND_FLASH = 3,
  PWVIZ_BACKGROUND_FLASH_GRID = 4,
} PwvizBackgroundMode;

typedef enum {
  PWVIZ_PEAK_COLOR_FADE = 0,
  PWVIZ_PEAK_COLOR_LEVEL = 1,
  PWVIZ_PEAK_COLOR_LEVEL_FADE = 2,
} PwvizPeakColorMode;

typedef enum {
  PWVIZ_PEAK_MOTION_NORMAL = 0,
  PWVIZ_PEAK_MOTION_FALL = 1,
  PWVIZ_PEAK_MOTION_RISE = 2,
  PWVIZ_PEAK_MOTION_FALL_RISE = 3,
  PWVIZ_PEAK_MOTION_RISE_FALL = 4,
  PWVIZ_PEAK_MOTION_SPARKS = 5,
} PwvizPeakMotion;

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
  PwvizLevelMode level_mode;
  PwvizBarStyle bar_style;
  PwvizBackgroundMode background_mode;
  PwvizPeakColorMode peak_color_mode;
  PwvizPeakMotion peak_motion;
  PwvizWindowAnchor window_anchor;
  int bar_count;
  int auto_bar_count;
  int bar_width;
  int x_spacing;
  int block_height;
  int block_gap;
  int window_width;
  int window_height;
  int x_margin;
  int y_margin;
  int now_playing_height;
  int target_fps;
  int falloff_rate;
  int peak_change_rate;
  float peak_fall_per_frame;
  float display_threshold;
  float fft_envelope;
  float fft_scale;
  double silence_fade_seconds;
  double background_alpha;
  double bar_alpha;
  double now_playing_glow_size;
  double now_playing_glow_feather;
  double lyrics_top_glow_size;
  double lyrics_top_glow_feather;
  double lyrics_bottom_glow_size;
  double lyrics_bottom_glow_feather;
  gboolean fft_equalize;
  gboolean now_playing_enabled;
  gboolean now_playing_show_app;
  gboolean now_playing_show_title;
  gboolean now_playing_show_artist;
  gboolean now_playing_show_album;
  gboolean lyrics_enabled;
  gboolean lyrics_two_lines;
  GdkRGBA low_color;
  GdkRGBA high_color;
  GdkRGBA peak_color;
  GdkRGBA background_color;
  GdkRGBA now_playing_text_color;
  GdkRGBA now_playing_glow_color;
  GdkRGBA lyrics_top_text_color;
  GdkRGBA lyrics_top_glow_color;
  GdkRGBA lyrics_bottom_text_color;
  GdkRGBA lyrics_bottom_glow_color;
  char now_playing_font[128];
  char lyrics_top_font[128];
  char lyrics_bottom_font[128];
} PwvizAppConfig;

void pwviz_app_config_set_defaults(PwvizAppConfig *config);
void pwviz_app_config_load(PwvizAppConfig *config);
void pwviz_app_config_save(const PwvizAppConfig *config);
