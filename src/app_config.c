#include "app_config.h"

#include "config.h"

#include <glib.h>
#include <string.h>

#define CONFIG_DIR "pipewire-visualizer"
#define LEGACY_CONFIG_DIR "pwviz"
#define CONFIG_FILE "config.ini"

static void set_rgba(GdkRGBA *color, double red, double green, double blue,
                     double alpha) {
  color->red = red;
  color->green = green;
  color->blue = blue;
  color->alpha = alpha;
}

static char *config_path(void) {
  return g_build_filename(g_get_user_config_dir(), CONFIG_DIR, CONFIG_FILE,
                          NULL);
}

static char *legacy_config_path(void) {
  return g_build_filename(g_get_user_config_dir(), LEGACY_CONFIG_DIR,
                          CONFIG_FILE, NULL);
}

static char *config_dir(void) {
  return g_build_filename(g_get_user_config_dir(), CONFIG_DIR, NULL);
}

static void get_color(GKeyFile *key_file, const char *group, const char *key,
                      GdkRGBA *color) {
  char *value = g_key_file_get_string(key_file, group, key, NULL);

  if (value) {
    gdk_rgba_parse(color, value);
    g_free(value);
  }
}

static void set_color(GKeyFile *key_file, const char *group, const char *key,
                      const GdkRGBA *color) {
  char *value = gdk_rgba_to_string(color);

  g_key_file_set_string(key_file, group, key, value);
  g_free(value);
}

static gboolean has_key(GKeyFile *key_file, const char *group,
                        const char *key) {
  return g_key_file_has_key(key_file, group, key, NULL);
}

static void load_font_setting(GKeyFile *key_file, const char *group,
                              const char *font_key,
                              const char *legacy_family_key,
                              const char *legacy_size_key, char *dest,
                              gsize dest_size) {
  if (has_key(key_file, group, font_key)) {
    char *font = g_key_file_get_string(key_file, group, font_key, NULL);
    g_strlcpy(dest, font, dest_size);
    g_free(font);
    return;
  }

  if (has_key(key_file, group, legacy_family_key) ||
      has_key(key_file, group, legacy_size_key)) {
    char *family = has_key(key_file, group, legacy_family_key)
                       ? g_key_file_get_string(key_file, group,
                                               legacy_family_key, NULL)
                       : g_strdup("Sans");
    int size = has_key(key_file, group, legacy_size_key)
                   ? CLAMP(g_key_file_get_integer(key_file, group,
                                                  legacy_size_key, NULL),
                           8, 32)
                   : 12;
    char *font = g_strdup_printf("%s %d", family, size);

    g_strlcpy(dest, font, dest_size);
    g_free(font);
    g_free(family);
  }
}

static const char *anchor_to_string(PwvizWindowAnchor anchor) {
  switch (anchor) {
  case PWVIZ_ANCHOR_TOP_LEFT:
    return "top-left";
  case PWVIZ_ANCHOR_TOP:
    return "top";
  case PWVIZ_ANCHOR_TOP_RIGHT:
    return "top-right";
  case PWVIZ_ANCHOR_LEFT:
    return "left";
  case PWVIZ_ANCHOR_CENTER:
    return "center";
  case PWVIZ_ANCHOR_RIGHT:
    return "right";
  case PWVIZ_ANCHOR_BOTTOM_LEFT:
    return "bottom-left";
  case PWVIZ_ANCHOR_BOTTOM:
    return "bottom";
  case PWVIZ_ANCHOR_BOTTOM_RIGHT:
    return "bottom-right";
  }

  return "bottom-right";
}

static PwvizWindowAnchor anchor_from_string(const char *value) {
  if (g_strcmp0(value, "top-left") == 0)
    return PWVIZ_ANCHOR_TOP_LEFT;
  if (g_strcmp0(value, "top") == 0)
    return PWVIZ_ANCHOR_TOP;
  if (g_strcmp0(value, "top-right") == 0)
    return PWVIZ_ANCHOR_TOP_RIGHT;
  if (g_strcmp0(value, "left") == 0)
    return PWVIZ_ANCHOR_LEFT;
  if (g_strcmp0(value, "center") == 0)
    return PWVIZ_ANCHOR_CENTER;
  if (g_strcmp0(value, "right") == 0)
    return PWVIZ_ANCHOR_RIGHT;
  if (g_strcmp0(value, "bottom-left") == 0)
    return PWVIZ_ANCHOR_BOTTOM_LEFT;
  if (g_strcmp0(value, "bottom") == 0)
    return PWVIZ_ANCHOR_BOTTOM;
  if (g_strcmp0(value, "bottom-right") == 0)
    return PWVIZ_ANCHOR_BOTTOM_RIGHT;

  return PWVIZ_ANCHOR_BOTTOM_RIGHT;
}

void pwviz_app_config_set_defaults(PwvizAppConfig *config) {
  config->analyzer_mode = PWVIZ_ANALYZER_BARS;
  config->level_mode = PWVIZ_LEVEL_AVERAGE;
  config->bar_style = PWVIZ_BAR_STYLE_CLASSIC;
  config->background_mode = PWVIZ_BACKGROUND_SOLID;
  config->peak_color_mode = PWVIZ_PEAK_COLOR_LEVEL_FADE;
  config->peak_motion = PWVIZ_PEAK_MOTION_FALL;
  config->window_anchor = PWVIZ_ANCHOR_BOTTOM_RIGHT;
  config->bar_count = PWVIZ_BAR_COUNT;
  config->auto_bar_count = FALSE;
  config->bar_width = 3;
  config->x_spacing = 1;
  config->block_height = 4;
  config->block_gap = 2;
  config->window_width = 900;
  config->window_height = 240;
  config->x_margin = 0;
  config->y_margin = 0;
  config->now_playing_height = 112;
  config->falloff_rate = 12;
  config->peak_change_rate = 80;
  config->peak_fall_per_frame = PWVIZ_PEAK_FALL_PER_FRAME;
  config->display_threshold = 0.08f;
  config->fft_envelope = 0.2f;
  config->fft_scale = 2.0f;
  config->background_alpha = 0.0;
  config->bar_alpha = 0.35;
  config->now_playing_alpha = 0.72;
  config->now_playing_outline_width = 1.2;
  config->now_playing_shadow_x = 2.0;
  config->now_playing_shadow_y = 2.0;
  config->now_playing_shadow_opacity = 0.0;
  config->lyrics_top_outline_width = 1.2;
  config->lyrics_top_shadow_x = 2.0;
  config->lyrics_top_shadow_y = 2.0;
  config->lyrics_top_shadow_opacity = 0.0;
  config->lyrics_bottom_outline_width = 1.2;
  config->lyrics_bottom_shadow_x = 2.0;
  config->lyrics_bottom_shadow_y = 2.0;
  config->lyrics_bottom_shadow_opacity = 0.0;
  config->fft_equalize = TRUE;
  config->now_playing_enabled = TRUE;
  config->now_playing_show_app = TRUE;
  config->now_playing_show_title = TRUE;
  config->now_playing_show_artist = TRUE;
  config->now_playing_show_album = TRUE;
  config->lyrics_enabled = TRUE;
  config->lyrics_two_lines = TRUE;
  set_rgba(&config->low_color, 0.45, 0.0, 0.0, 1.0);
  set_rgba(&config->high_color, 1.0, 0.86, 0.0, 1.0);
  set_rgba(&config->peak_color, 1.0, 0.92, 0.20, 1.0);
  set_rgba(&config->background_color, 0.015, 0.010, 0.008, 1.0);
  set_rgba(&config->now_playing_text_color, 1.0, 1.0, 1.0, 1.0);
  set_rgba(&config->now_playing_outline_color, 0.0, 0.0, 0.0, 1.0);
  set_rgba(&config->now_playing_shadow_color, 0.0, 0.0, 0.0, 1.0);
  set_rgba(&config->lyrics_top_text_color, 1.0, 1.0, 1.0, 1.0);
  set_rgba(&config->lyrics_top_outline_color, 0.0, 0.0, 0.0, 1.0);
  set_rgba(&config->lyrics_top_shadow_color, 0.0, 0.0, 0.0, 1.0);
  set_rgba(&config->lyrics_bottom_text_color, 1.0, 1.0, 1.0, 1.0);
  set_rgba(&config->lyrics_bottom_outline_color, 0.0, 0.0, 0.0, 1.0);
  set_rgba(&config->lyrics_bottom_shadow_color, 0.0, 0.0, 0.0, 1.0);
  g_strlcpy(config->now_playing_font, "Sans 13",
            sizeof(config->now_playing_font));
  g_strlcpy(config->lyrics_top_font, "Sans 12",
            sizeof(config->lyrics_top_font));
  g_strlcpy(config->lyrics_bottom_font, "Sans 12",
            sizeof(config->lyrics_bottom_font));
}

void pwviz_app_config_load(PwvizAppConfig *config) {
  GKeyFile *key_file = g_key_file_new();
  char *path = config_path();

  pwviz_app_config_set_defaults(config);

  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL)) {
    g_free(path);
    path = legacy_config_path();
    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL))
      goto done;
  }

  if (has_key(key_file, "Analyzer", "mode"))
    config->analyzer_mode =
        CLAMP(g_key_file_get_integer(key_file, "Analyzer", "mode", NULL),
              PWVIZ_ANALYZER_BARS, PWVIZ_ANALYZER_FLASH);
  if (has_key(key_file, "Analyzer", "level_mode"))
    config->level_mode =
        CLAMP(g_key_file_get_integer(key_file, "Analyzer", "level_mode", NULL),
              PWVIZ_LEVEL_PEAK, PWVIZ_LEVEL_AVERAGE);
  if (has_key(key_file, "Analyzer", "peak_motion"))
    config->peak_motion =
        CLAMP(g_key_file_get_integer(key_file, "Analyzer", "peak_motion", NULL),
              PWVIZ_PEAK_MOTION_NORMAL, PWVIZ_PEAK_MOTION_SPARKS);
  if (has_key(key_file, "Analyzer", "bar_count"))
    config->bar_count =
        CLAMP(g_key_file_get_integer(key_file, "Analyzer", "bar_count", NULL),
              8, PWVIZ_BAR_COUNT);
  if (has_key(key_file, "Analyzer", "auto_bar_count"))
    config->auto_bar_count =
        g_key_file_get_boolean(key_file, "Analyzer", "auto_bar_count", NULL);
  if (has_key(key_file, "Analyzer", "falloff_rate"))
    config->falloff_rate =
        CLAMP(g_key_file_get_integer(key_file, "Analyzer", "falloff_rate",
                                     NULL),
              0, 75);
  if (has_key(key_file, "Analyzer", "peak_change_rate"))
    config->peak_change_rate =
        CLAMP(g_key_file_get_integer(key_file, "Analyzer", "peak_change_rate",
                                     NULL),
              0, 255);
  else if (has_key(key_file, "Analyzer", "peak_hold_frames"))
    config->peak_change_rate =
        CLAMP(g_key_file_get_integer(key_file, "Analyzer", "peak_hold_frames",
                                     NULL),
              0, 255);
  if (has_key(key_file, "Analyzer", "peak_fall_per_frame"))
    config->peak_fall_per_frame =
        CLAMP(g_key_file_get_double(key_file, "Analyzer", "peak_fall_per_frame",
                                    NULL),
              0.001, 0.08);
  if (has_key(key_file, "Analyzer", "display_threshold"))
    config->display_threshold =
        CLAMP(g_key_file_get_double(key_file, "Analyzer", "display_threshold",
                                    NULL),
              0.0, 0.5);
  if (has_key(key_file, "Analyzer", "fft_equalize"))
    config->fft_equalize =
        g_key_file_get_boolean(key_file, "Analyzer", "fft_equalize", NULL);
  if (has_key(key_file, "Analyzer", "fft_envelope"))
    config->fft_envelope =
        CLAMP(g_key_file_get_double(key_file, "Analyzer", "fft_envelope",
                                    NULL),
              0.0, 5.0);
  if (has_key(key_file, "Analyzer", "fft_scale"))
    config->fft_scale =
        CLAMP(g_key_file_get_double(key_file, "Analyzer", "fft_scale", NULL),
              0.1, 25.0);

  if (has_key(key_file, "Window", "anchor")) {
    char *anchor = g_key_file_get_string(key_file, "Window", "anchor", NULL);
    config->window_anchor = anchor_from_string(anchor);
    g_free(anchor);
  }
  if (has_key(key_file, "Window", "width"))
    config->window_width =
        CLAMP(g_key_file_get_integer(key_file, "Window", "width", NULL),
              PWVIZ_MIN_WINDOW_WIDTH, 10000);
  if (has_key(key_file, "Window", "height"))
    config->window_height =
        CLAMP(g_key_file_get_integer(key_file, "Window", "height", NULL),
              PWVIZ_MIN_WINDOW_HEIGHT, 10000);
  if (has_key(key_file, "Window", "x_margin"))
    config->x_margin =
        CLAMP(g_key_file_get_integer(key_file, "Window", "x_margin", NULL), 0,
              10000);
  if (has_key(key_file, "Window", "y_margin"))
    config->y_margin =
        CLAMP(g_key_file_get_integer(key_file, "Window", "y_margin", NULL), 0,
              10000);

  if (has_key(key_file, "Now Playing", "enabled"))
    config->now_playing_enabled =
        g_key_file_get_boolean(key_file, "Now Playing", "enabled", NULL);
  if (has_key(key_file, "Now Playing", "height"))
    config->now_playing_height =
        CLAMP(g_key_file_get_integer(key_file, "Now Playing", "height", NULL),
              0, 160);
  load_font_setting(key_file, "Now Playing", "font", "font_family",
                    "font_size", config->now_playing_font,
                    sizeof(config->now_playing_font));
  if (has_key(key_file, "Now Playing", "outline_width"))
    config->now_playing_outline_width =
        CLAMP(g_key_file_get_double(key_file, "Now Playing", "outline_width",
                                    NULL),
              0.0, 6.0);
  if (has_key(key_file, "Now Playing", "shadow_x"))
    config->now_playing_shadow_x =
        CLAMP(g_key_file_get_double(key_file, "Now Playing", "shadow_x", NULL),
              -64.0, 64.0);
  if (has_key(key_file, "Now Playing", "shadow_y"))
    config->now_playing_shadow_y =
        CLAMP(g_key_file_get_double(key_file, "Now Playing", "shadow_y", NULL),
              -64.0, 64.0);
  if (has_key(key_file, "Now Playing", "shadow_opacity"))
    config->now_playing_shadow_opacity = CLAMP(
        g_key_file_get_double(key_file, "Now Playing", "shadow_opacity", NULL),
        0.0, 1.0);
  if (has_key(key_file, "Now Playing", "alpha"))
    config->now_playing_alpha =
        CLAMP(g_key_file_get_double(key_file, "Now Playing", "alpha", NULL),
              0.0, 1.0);
  if (has_key(key_file, "Now Playing", "show_app"))
    config->now_playing_show_app =
        g_key_file_get_boolean(key_file, "Now Playing", "show_app", NULL);
  if (has_key(key_file, "Now Playing", "show_title"))
    config->now_playing_show_title =
        g_key_file_get_boolean(key_file, "Now Playing", "show_title", NULL);
  if (has_key(key_file, "Now Playing", "show_artist"))
    config->now_playing_show_artist =
        g_key_file_get_boolean(key_file, "Now Playing", "show_artist", NULL);
  if (has_key(key_file, "Now Playing", "show_album"))
    config->now_playing_show_album =
        g_key_file_get_boolean(key_file, "Now Playing", "show_album", NULL);
  if (has_key(key_file, "Now Playing", "lyrics_enabled"))
    config->lyrics_enabled =
        g_key_file_get_boolean(key_file, "Now Playing", "lyrics_enabled",
                               NULL);
  if (has_key(key_file, "Now Playing", "lyrics_two_lines"))
    config->lyrics_two_lines =
        g_key_file_get_boolean(key_file, "Now Playing", "lyrics_two_lines",
                               NULL);
  load_font_setting(key_file, "Now Playing", "lyrics_top_font",
                    "lyrics_font_family", "lyrics_font_size",
                    config->lyrics_top_font, sizeof(config->lyrics_top_font));
  if (has_key(key_file, "Now Playing", "lyrics_bottom_font")) {
    char *font =
        g_key_file_get_string(key_file, "Now Playing", "lyrics_bottom_font",
                              NULL);
    g_strlcpy(config->lyrics_bottom_font, font,
              sizeof(config->lyrics_bottom_font));
    g_free(font);
  } else if (has_key(key_file, "Now Playing", "lyrics_font")) {
    char *font =
        g_key_file_get_string(key_file, "Now Playing", "lyrics_font", NULL);
    g_strlcpy(config->lyrics_bottom_font, font,
              sizeof(config->lyrics_bottom_font));
    g_free(font);
  } else {
    g_strlcpy(config->lyrics_bottom_font, config->lyrics_top_font,
              sizeof(config->lyrics_bottom_font));
  }
  if (has_key(key_file, "Now Playing", "lyrics_top_outline_width"))
    config->lyrics_top_outline_width =
        CLAMP(g_key_file_get_double(key_file, "Now Playing",
                                    "lyrics_top_outline_width", NULL),
              0.0, 6.0);
  else if (has_key(key_file, "Now Playing", "lyrics_outline_width"))
    config->lyrics_top_outline_width =
        CLAMP(g_key_file_get_double(key_file, "Now Playing",
                                    "lyrics_outline_width", NULL),
              0.0, 6.0);
  if (has_key(key_file, "Now Playing", "lyrics_bottom_outline_width"))
    config->lyrics_bottom_outline_width =
        CLAMP(g_key_file_get_double(key_file, "Now Playing",
                                    "lyrics_bottom_outline_width", NULL),
              0.0, 6.0);
  else
    config->lyrics_bottom_outline_width = config->lyrics_top_outline_width;

  if (has_key(key_file, "Now Playing", "lyrics_top_shadow_x"))
    config->lyrics_top_shadow_x =
        CLAMP(g_key_file_get_double(key_file, "Now Playing",
                                    "lyrics_top_shadow_x", NULL),
              -64.0, 64.0);
  else if (has_key(key_file, "Now Playing", "lyrics_shadow_x"))
    config->lyrics_top_shadow_x = CLAMP(
        g_key_file_get_double(key_file, "Now Playing", "lyrics_shadow_x", NULL),
        -64.0, 64.0);
  if (has_key(key_file, "Now Playing", "lyrics_top_shadow_y"))
    config->lyrics_top_shadow_y =
        CLAMP(g_key_file_get_double(key_file, "Now Playing",
                                    "lyrics_top_shadow_y", NULL),
              -64.0, 64.0);
  else if (has_key(key_file, "Now Playing", "lyrics_shadow_y"))
    config->lyrics_top_shadow_y = CLAMP(
        g_key_file_get_double(key_file, "Now Playing", "lyrics_shadow_y", NULL),
        -64.0, 64.0);
  if (has_key(key_file, "Now Playing", "lyrics_top_shadow_opacity"))
    config->lyrics_top_shadow_opacity =
        CLAMP(g_key_file_get_double(key_file, "Now Playing",
                                    "lyrics_top_shadow_opacity", NULL),
              0.0, 1.0);
  else if (has_key(key_file, "Now Playing", "lyrics_shadow_opacity"))
    config->lyrics_top_shadow_opacity =
        CLAMP(g_key_file_get_double(key_file, "Now Playing",
                                    "lyrics_shadow_opacity", NULL),
              0.0, 1.0);

  if (has_key(key_file, "Now Playing", "lyrics_bottom_shadow_x"))
    config->lyrics_bottom_shadow_x =
        CLAMP(g_key_file_get_double(key_file, "Now Playing",
                                    "lyrics_bottom_shadow_x", NULL),
              -64.0, 64.0);
  else
    config->lyrics_bottom_shadow_x = config->lyrics_top_shadow_x;
  if (has_key(key_file, "Now Playing", "lyrics_bottom_shadow_y"))
    config->lyrics_bottom_shadow_y =
        CLAMP(g_key_file_get_double(key_file, "Now Playing",
                                    "lyrics_bottom_shadow_y", NULL),
              -64.0, 64.0);
  else
    config->lyrics_bottom_shadow_y = config->lyrics_top_shadow_y;
  if (has_key(key_file, "Now Playing", "lyrics_bottom_shadow_opacity"))
    config->lyrics_bottom_shadow_opacity =
        CLAMP(g_key_file_get_double(key_file, "Now Playing",
                                    "lyrics_bottom_shadow_opacity", NULL),
              0.0, 1.0);
  else
    config->lyrics_bottom_shadow_opacity = config->lyrics_top_shadow_opacity;

  get_color(key_file, "Now Playing", "lyrics_top_text_color",
            &config->lyrics_top_text_color);
  get_color(key_file, "Now Playing", "lyrics_top_outline_color",
            &config->lyrics_top_outline_color);
  get_color(key_file, "Now Playing", "lyrics_top_shadow_color",
            &config->lyrics_top_shadow_color);
  get_color(key_file, "Now Playing", "lyrics_bottom_text_color",
            &config->lyrics_bottom_text_color);
  get_color(key_file, "Now Playing", "lyrics_bottom_outline_color",
            &config->lyrics_bottom_outline_color);
  get_color(key_file, "Now Playing", "lyrics_bottom_shadow_color",
            &config->lyrics_bottom_shadow_color);
  if (!has_key(key_file, "Now Playing", "lyrics_top_text_color"))
    get_color(key_file, "Now Playing", "lyrics_text_color",
              &config->lyrics_top_text_color);
  if (!has_key(key_file, "Now Playing", "lyrics_top_outline_color"))
    get_color(key_file, "Now Playing", "lyrics_outline_color",
              &config->lyrics_top_outline_color);
  if (!has_key(key_file, "Now Playing", "lyrics_top_shadow_color"))
    get_color(key_file, "Now Playing", "lyrics_shadow_color",
              &config->lyrics_top_shadow_color);
  if (!has_key(key_file, "Now Playing", "lyrics_bottom_text_color"))
    config->lyrics_bottom_text_color = config->lyrics_top_text_color;
  if (!has_key(key_file, "Now Playing", "lyrics_bottom_outline_color"))
    config->lyrics_bottom_outline_color = config->lyrics_top_outline_color;
  if (!has_key(key_file, "Now Playing", "lyrics_bottom_shadow_color"))
    config->lyrics_bottom_shadow_color = config->lyrics_top_shadow_color;

  if (has_key(key_file, "Style", "block_height"))
    config->block_height =
        CLAMP(g_key_file_get_integer(key_file, "Style", "block_height", NULL),
              1, 16);
  if (has_key(key_file, "Style", "bar_style"))
    config->bar_style =
        CLAMP(g_key_file_get_integer(key_file, "Style", "bar_style", NULL),
              PWVIZ_BAR_STYLE_CLASSIC, PWVIZ_BAR_STYLE_RANDOM);
  if (has_key(key_file, "Style", "background_mode"))
    config->background_mode =
        CLAMP(g_key_file_get_integer(key_file, "Style", "background_mode",
                                     NULL),
              PWVIZ_BACKGROUND_BLACK, PWVIZ_BACKGROUND_FLASH_GRID);
  if (has_key(key_file, "Style", "peak_color_mode"))
    config->peak_color_mode =
        CLAMP(g_key_file_get_integer(key_file, "Style", "peak_color_mode",
                                     NULL),
              PWVIZ_PEAK_COLOR_FADE, PWVIZ_PEAK_COLOR_LEVEL_FADE);
  if (has_key(key_file, "Style", "block_gap"))
    config->block_gap =
        CLAMP(g_key_file_get_integer(key_file, "Style", "block_gap", NULL), 0,
              12);
  if (has_key(key_file, "Style", "bar_width"))
    config->bar_width =
        CLAMP(g_key_file_get_integer(key_file, "Style", "bar_width", NULL), 1,
              50);
  if (has_key(key_file, "Style", "x_spacing"))
    config->x_spacing =
        CLAMP(g_key_file_get_integer(key_file, "Style", "x_spacing", NULL), 0,
              10);
  if (has_key(key_file, "Style", "background_alpha"))
    config->background_alpha =
        CLAMP(
            g_key_file_get_double(key_file, "Style", "background_alpha", NULL),
            0.0, 1.0);
  if (has_key(key_file, "Style", "bar_alpha"))
    config->bar_alpha =
        CLAMP(g_key_file_get_double(key_file, "Style", "bar_alpha", NULL), 0.0,
              1.0);
  get_color(key_file, "Colour Factory", "low_color", &config->low_color);
  get_color(key_file, "Colour Factory", "high_color", &config->high_color);
  get_color(key_file, "Colour Factory", "peak_color", &config->peak_color);
  get_color(key_file, "Colour Factory", "background_color",
            &config->background_color);
  get_color(key_file, "Now Playing", "text_color",
            &config->now_playing_text_color);
  get_color(key_file, "Now Playing", "outline_color",
            &config->now_playing_outline_color);
  get_color(key_file, "Now Playing", "shadow_color",
            &config->now_playing_shadow_color);

done:
  g_free(path);
  g_key_file_unref(key_file);
}

void pwviz_app_config_save(const PwvizAppConfig *config) {
  GKeyFile *key_file = g_key_file_new();
  char *dir = config_dir();
  char *path = config_path();
  gsize length = 0;

  g_key_file_set_integer(key_file, "Analyzer", "mode",
                         config->analyzer_mode);
  g_key_file_set_integer(key_file, "Analyzer", "level_mode",
                         config->level_mode);
  g_key_file_set_integer(key_file, "Analyzer", "peak_motion",
                         config->peak_motion);
  g_key_file_set_integer(key_file, "Analyzer", "bar_count",
                         config->bar_count);
  g_key_file_set_boolean(key_file, "Analyzer", "auto_bar_count",
                         config->auto_bar_count);
  g_key_file_set_integer(key_file, "Analyzer", "falloff_rate",
                         config->falloff_rate);
  g_key_file_set_integer(key_file, "Analyzer", "peak_change_rate",
                         config->peak_change_rate);
  g_key_file_set_double(key_file, "Analyzer", "peak_fall_per_frame",
                        config->peak_fall_per_frame);
  g_key_file_set_double(key_file, "Analyzer", "display_threshold",
                        config->display_threshold);
  g_key_file_set_boolean(key_file, "Analyzer", "fft_equalize",
                         config->fft_equalize);
  g_key_file_set_double(key_file, "Analyzer", "fft_envelope",
                        config->fft_envelope);
  g_key_file_set_double(key_file, "Analyzer", "fft_scale", config->fft_scale);

  g_key_file_set_string(key_file, "Window", "anchor",
                        anchor_to_string(config->window_anchor));
  g_key_file_set_integer(key_file, "Window", "width", config->window_width);
  g_key_file_set_integer(key_file, "Window", "height", config->window_height);
  g_key_file_set_integer(key_file, "Window", "x_margin", config->x_margin);
  g_key_file_set_integer(key_file, "Window", "y_margin", config->y_margin);

  g_key_file_set_boolean(key_file, "Now Playing", "enabled",
                         config->now_playing_enabled);
  g_key_file_set_integer(key_file, "Now Playing", "height",
                         config->now_playing_height);
  g_key_file_set_string(key_file, "Now Playing", "font",
                        config->now_playing_font);
  g_key_file_set_double(key_file, "Now Playing", "outline_width",
                        config->now_playing_outline_width);
  g_key_file_set_double(key_file, "Now Playing", "shadow_x",
                        config->now_playing_shadow_x);
  g_key_file_set_double(key_file, "Now Playing", "shadow_y",
                        config->now_playing_shadow_y);
  g_key_file_set_double(key_file, "Now Playing", "shadow_opacity",
                        config->now_playing_shadow_opacity);
  g_key_file_set_double(key_file, "Now Playing", "alpha",
                        config->now_playing_alpha);
  g_key_file_set_boolean(key_file, "Now Playing", "show_app",
                         config->now_playing_show_app);
  g_key_file_set_boolean(key_file, "Now Playing", "show_title",
                         config->now_playing_show_title);
  g_key_file_set_boolean(key_file, "Now Playing", "show_artist",
                         config->now_playing_show_artist);
  g_key_file_set_boolean(key_file, "Now Playing", "show_album",
                         config->now_playing_show_album);
  g_key_file_set_boolean(key_file, "Now Playing", "lyrics_enabled",
                         config->lyrics_enabled);
  g_key_file_set_boolean(key_file, "Now Playing", "lyrics_two_lines",
                         config->lyrics_two_lines);
  g_key_file_set_string(key_file, "Now Playing", "lyrics_top_font",
                        config->lyrics_top_font);
  g_key_file_set_string(key_file, "Now Playing", "lyrics_bottom_font",
                        config->lyrics_bottom_font);
  g_key_file_set_double(key_file, "Now Playing", "lyrics_top_outline_width",
                        config->lyrics_top_outline_width);
  g_key_file_set_double(key_file, "Now Playing", "lyrics_top_shadow_x",
                        config->lyrics_top_shadow_x);
  g_key_file_set_double(key_file, "Now Playing", "lyrics_top_shadow_y",
                        config->lyrics_top_shadow_y);
  g_key_file_set_double(key_file, "Now Playing", "lyrics_top_shadow_opacity",
                        config->lyrics_top_shadow_opacity);
  g_key_file_set_double(key_file, "Now Playing", "lyrics_bottom_outline_width",
                        config->lyrics_bottom_outline_width);
  g_key_file_set_double(key_file, "Now Playing", "lyrics_bottom_shadow_x",
                        config->lyrics_bottom_shadow_x);
  g_key_file_set_double(key_file, "Now Playing", "lyrics_bottom_shadow_y",
                        config->lyrics_bottom_shadow_y);
  g_key_file_set_double(key_file, "Now Playing",
                        "lyrics_bottom_shadow_opacity",
                        config->lyrics_bottom_shadow_opacity);

  g_key_file_set_integer(key_file, "Style", "block_height",
                         config->block_height);
  g_key_file_set_integer(key_file, "Style", "bar_style", config->bar_style);
  g_key_file_set_integer(key_file, "Style", "background_mode",
                         config->background_mode);
  g_key_file_set_integer(key_file, "Style", "peak_color_mode",
                         config->peak_color_mode);
  g_key_file_set_integer(key_file, "Style", "block_gap", config->block_gap);
  g_key_file_set_integer(key_file, "Style", "bar_width", config->bar_width);
  g_key_file_set_integer(key_file, "Style", "x_spacing", config->x_spacing);
  g_key_file_set_double(key_file, "Style", "background_alpha",
                        config->background_alpha);
  g_key_file_set_double(key_file, "Style", "bar_alpha", config->bar_alpha);
  set_color(key_file, "Colour Factory", "low_color", &config->low_color);
  set_color(key_file, "Colour Factory", "high_color", &config->high_color);
  set_color(key_file, "Colour Factory", "peak_color", &config->peak_color);
  set_color(key_file, "Colour Factory", "background_color",
            &config->background_color);
  set_color(key_file, "Now Playing", "text_color",
            &config->now_playing_text_color);
  set_color(key_file, "Now Playing", "outline_color",
            &config->now_playing_outline_color);
  set_color(key_file, "Now Playing", "shadow_color",
            &config->now_playing_shadow_color);
  set_color(key_file, "Now Playing", "lyrics_top_text_color",
            &config->lyrics_top_text_color);
  set_color(key_file, "Now Playing", "lyrics_top_outline_color",
            &config->lyrics_top_outline_color);
  set_color(key_file, "Now Playing", "lyrics_top_shadow_color",
            &config->lyrics_top_shadow_color);
  set_color(key_file, "Now Playing", "lyrics_bottom_text_color",
            &config->lyrics_bottom_text_color);
  set_color(key_file, "Now Playing", "lyrics_bottom_outline_color",
            &config->lyrics_bottom_outline_color);
  set_color(key_file, "Now Playing", "lyrics_bottom_shadow_color",
            &config->lyrics_bottom_shadow_color);

  char *data = g_key_file_to_data(key_file, &length, NULL);
  g_mkdir_with_parents(dir, 0700);
  g_file_set_contents(path, data, length, NULL);

  g_free(data);
  g_free(path);
  g_free(dir);
  g_key_file_unref(key_file);
}
