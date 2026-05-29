#include "app_config.h"

#include "config.h"

#include <glib.h>
#include <string.h>

#define CONFIG_DIR "pwviz"
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

void pwviz_app_config_set_defaults(PwvizAppConfig *config) {
  config->analyzer_mode = PWVIZ_ANALYZER_BARS;
  config->bar_count = PWVIZ_BAR_COUNT;
  config->block_height = 4;
  config->block_gap = 2;
  config->peak_hold_frames = PWVIZ_PEAK_HOLD_FRAMES;
  config->peak_fall_per_frame = PWVIZ_PEAK_FALL_PER_FRAME;
  config->background_alpha = 0.0;
  config->show_border = TRUE;
  set_rgba(&config->low_color, 0.45, 0.0, 0.0, 1.0);
  set_rgba(&config->high_color, 1.0, 0.86, 0.0, 1.0);
  set_rgba(&config->peak_color, 1.0, 0.92, 0.20, 1.0);
  set_rgba(&config->background_color, 0.015, 0.010, 0.008, 1.0);
  g_strlcpy(config->profile_name, "Default Red & Yellow",
            sizeof(config->profile_name));
}

void pwviz_app_config_apply_profile(PwvizAppConfig *config,
                                    const char *profile_name) {
  if (g_strcmp0(profile_name, "Classic") == 0) {
    set_rgba(&config->low_color, 0.00, 0.25, 0.75, 1.0);
    set_rgba(&config->high_color, 0.00, 1.00, 0.74, 1.0);
    set_rgba(&config->peak_color, 0.42, 0.00, 0.79, 1.0);
  } else if (g_strcmp0(profile_name, "Classic LED") == 0) {
    set_rgba(&config->low_color, 0.05, 0.20, 0.00, 1.0);
    set_rgba(&config->high_color, 0.50, 1.00, 0.10, 1.0);
    set_rgba(&config->peak_color, 0.90, 1.00, 0.30, 1.0);
  } else if (g_strcmp0(profile_name, "Blue Flames") == 0) {
    set_rgba(&config->low_color, 0.00, 0.10, 0.45, 1.0);
    set_rgba(&config->high_color, 0.00, 0.95, 1.00, 1.0);
    set_rgba(&config->peak_color, 0.58, 0.70, 1.00, 1.0);
  } else if (g_strcmp0(profile_name, "Blue on Grey") == 0) {
    set_rgba(&config->low_color, 0.08, 0.10, 0.15, 1.0);
    set_rgba(&config->high_color, 0.30, 0.62, 1.00, 1.0);
    set_rgba(&config->peak_color, 0.75, 0.84, 1.00, 1.0);
  } else if (g_strcmp0(profile_name, "Flames") == 0) {
    set_rgba(&config->low_color, 0.40, 0.00, 0.00, 1.0);
    set_rgba(&config->high_color, 1.00, 0.62, 0.00, 1.0);
    set_rgba(&config->peak_color, 1.00, 0.95, 0.20, 1.0);
  } else if (g_strcmp0(profile_name, "LCD") == 0) {
    set_rgba(&config->low_color, 0.14, 0.20, 0.12, 1.0);
    set_rgba(&config->high_color, 0.56, 0.72, 0.42, 1.0);
    set_rgba(&config->peak_color, 0.82, 0.92, 0.62, 1.0);
  } else if (g_strcmp0(profile_name, "Northern Lights") == 0) {
    set_rgba(&config->low_color, 0.00, 0.12, 0.20, 1.0);
    set_rgba(&config->high_color, 0.00, 0.96, 0.62, 1.0);
    set_rgba(&config->peak_color, 0.62, 0.20, 1.00, 1.0);
  } else if (g_strcmp0(profile_name, "Purple Neon") == 0) {
    set_rgba(&config->low_color, 0.24, 0.00, 0.42, 1.0);
    set_rgba(&config->high_color, 0.78, 0.00, 1.00, 1.0);
    set_rgba(&config->peak_color, 1.00, 0.45, 1.00, 1.0);
  } else if (g_strcmp0(profile_name, "Lavender Pink Tips") == 0) {
    set_rgba(&config->low_color, 0.35, 0.18, 0.62, 1.0);
    set_rgba(&config->high_color, 0.95, 0.62, 1.00, 1.0);
    set_rgba(&config->peak_color, 1.00, 0.32, 0.72, 1.0);
  } else {
    set_rgba(&config->low_color, 0.45, 0.0, 0.0, 1.0);
    set_rgba(&config->high_color, 1.0, 0.86, 0.0, 1.0);
    set_rgba(&config->peak_color, 1.0, 0.92, 0.20, 1.0);
  }

  g_strlcpy(config->profile_name, profile_name, sizeof(config->profile_name));
}

void pwviz_app_config_load(PwvizAppConfig *config) {
  GKeyFile *key_file = g_key_file_new();
  char *path = config_path();

  pwviz_app_config_set_defaults(config);

  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL))
    goto done;

  if (has_key(key_file, "Analyzer", "mode"))
    config->analyzer_mode =
        CLAMP(g_key_file_get_integer(key_file, "Analyzer", "mode", NULL),
              PWVIZ_ANALYZER_BARS, PWVIZ_ANALYZER_FLASH);
  if (has_key(key_file, "Analyzer", "bar_count"))
    config->bar_count =
        CLAMP(g_key_file_get_integer(key_file, "Analyzer", "bar_count", NULL),
              8, PWVIZ_BAR_COUNT);
  if (has_key(key_file, "Analyzer", "peak_hold_frames"))
    config->peak_hold_frames =
        CLAMP(g_key_file_get_integer(key_file, "Analyzer", "peak_hold_frames",
                                     NULL),
              0, 120);
  if (has_key(key_file, "Analyzer", "peak_fall_per_frame"))
    config->peak_fall_per_frame =
        CLAMP(g_key_file_get_double(key_file, "Analyzer", "peak_fall_per_frame",
                                    NULL),
              0.001, 0.08);

  if (has_key(key_file, "Style", "block_height"))
    config->block_height =
        CLAMP(g_key_file_get_integer(key_file, "Style", "block_height", NULL),
              1, 16);
  if (has_key(key_file, "Style", "block_gap"))
    config->block_gap =
        CLAMP(g_key_file_get_integer(key_file, "Style", "block_gap", NULL), 0,
              12);
  if (has_key(key_file, "Style", "background_alpha"))
    config->background_alpha =
        CLAMP(
            g_key_file_get_double(key_file, "Style", "background_alpha", NULL),
            0.0, 1.0);
  if (has_key(key_file, "Style", "show_border"))
    config->show_border =
        g_key_file_get_boolean(key_file, "Style", "show_border", NULL);

  get_color(key_file, "Colour Factory", "low_color", &config->low_color);
  get_color(key_file, "Colour Factory", "high_color", &config->high_color);
  get_color(key_file, "Colour Factory", "peak_color", &config->peak_color);
  get_color(key_file, "Colour Factory", "background_color",
            &config->background_color);

  char *profile =
      g_key_file_get_string(key_file, "Profiles", "current", NULL);
  if (profile) {
    g_strlcpy(config->profile_name, profile, sizeof(config->profile_name));
    g_free(profile);
  }

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
  g_key_file_set_integer(key_file, "Analyzer", "bar_count",
                         config->bar_count);
  g_key_file_set_integer(key_file, "Analyzer", "peak_hold_frames",
                         config->peak_hold_frames);
  g_key_file_set_double(key_file, "Analyzer", "peak_fall_per_frame",
                        config->peak_fall_per_frame);

  g_key_file_set_integer(key_file, "Style", "block_height",
                         config->block_height);
  g_key_file_set_integer(key_file, "Style", "block_gap", config->block_gap);
  g_key_file_set_double(key_file, "Style", "background_alpha",
                        config->background_alpha);
  g_key_file_set_boolean(key_file, "Style", "show_border",
                         config->show_border);

  set_color(key_file, "Colour Factory", "low_color", &config->low_color);
  set_color(key_file, "Colour Factory", "high_color", &config->high_color);
  set_color(key_file, "Colour Factory", "peak_color", &config->peak_color);
  set_color(key_file, "Colour Factory", "background_color",
            &config->background_color);

  g_key_file_set_string(key_file, "Profiles", "current",
                        config->profile_name);

  char *data = g_key_file_to_data(key_file, &length, NULL);
  g_mkdir_with_parents(dir, 0700);
  g_file_set_contents(path, data, length, NULL);

  g_free(data);
  g_free(path);
  g_free(dir);
  g_key_file_unref(key_file);
}
