#include "visualization.h"

#include "app_config.h"
#include "binning.h"
#include "config.h"
#include "fft.h"
#include "global_shortcut.h"
#include "lyrics.h"
#include "now_playing.h"

#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <math.h>
#include <pango/pangocairo.h>

typedef struct {
  GtkWindow *window;
  GtkWindow *config_window;
  GtkWidget *drawing_area;
  PwvizAudioBuffer *audio_buffer;
  PwvizGlobalShortcut *global_shortcut;
  PwvizNowPlaying now_playing;
  PwvizLyrics *lyrics;
  PwvizAppConfig config;
  PwvizAppConfig config_snapshot;
  PwvizFft fft;
  PwvizBinner binner;
  float fft_samples[PWVIZ_FFT_SIZE];
  float magnitudes[PWVIZ_FFT_SIZE / 2 + 1];
  float levels[PWVIZ_BAR_COUNT];
  float bars[PWVIZ_BAR_COUNT];
  float peak_caps[PWVIZ_BAR_COUNT];
  int peak_holds[PWVIZ_BAR_COUNT];
  guint now_playing_source;
  GAsyncQueue *lyrics_results;
  gboolean lyrics_fetching;
  char lyrics_key[512];
  char lyrics_fetching_key[512];
  gboolean destroying;
  int width;
  int height;
} PwvizVisualizer;

typedef enum {
  COLOR_LOW,
  COLOR_HIGH,
  COLOR_PEAK,
  COLOR_BACKGROUND,
  COLOR_NOW_PLAYING_TEXT,
  COLOR_NOW_PLAYING_OUTLINE,
  COLOR_NOW_PLAYING_SHADOW,
  COLOR_LYRICS_TEXT,
  COLOR_LYRICS_OUTLINE,
  COLOR_LYRICS_SHADOW,
} ColorTarget;

enum {
  INACTIVE_BAR_ALPHA = 0,
  LYRICS_OFFSET_STEP_MS = 250,
};

typedef struct {
  PwvizWindowAnchor anchor;
  const char *label;
} AnchorOption;

typedef struct {
  int value;
  const char *label;
} RadioOption;

typedef enum {
  CLASSIC_OPTION_BAR_STYLE,
  CLASSIC_OPTION_BACKGROUND,
  CLASSIC_OPTION_PEAK_COLOR,
  CLASSIC_OPTION_PEAK_MOTION,
} ClassicOptionTarget;

typedef struct {
  PwvizVisualizer *visualizer;
  ColorTarget target;
} ColorBinding;

typedef enum {
  FONT_NOW_PLAYING,
  FONT_LYRICS,
} FontTarget;

static void apply_layer_position(PwvizVisualizer *visualizer);
static void apply_window_geometry(PwvizVisualizer *visualizer);

static const AnchorOption ANCHOR_OPTIONS[] = {
    {PWVIZ_ANCHOR_TOP_LEFT, "Top left"},
    {PWVIZ_ANCHOR_TOP, "Top"},
    {PWVIZ_ANCHOR_TOP_RIGHT, "Top right"},
    {PWVIZ_ANCHOR_LEFT, "Left"},
    {PWVIZ_ANCHOR_CENTER, "Center"},
    {PWVIZ_ANCHOR_RIGHT, "Right"},
    {PWVIZ_ANCHOR_BOTTOM_LEFT, "Bottom left"},
    {PWVIZ_ANCHOR_BOTTOM, "Bottom"},
    {PWVIZ_ANCHOR_BOTTOM_RIGHT, "Bottom right"},
};

static const RadioOption BAR_STYLE_OPTIONS[] = {
    {PWVIZ_BAR_STYLE_CLASSIC, "Classic"},
    {PWVIZ_BAR_STYLE_SOFT_FLAME, "Soft Flame"},
    {PWVIZ_BAR_STYLE_FIRE, "Fire"},
    {PWVIZ_BAR_STYLE_SOLID_LINES, "Solid Lines"},
    {PWVIZ_BAR_STYLE_WINAMP_FIRE, "Winamp Fire"},
    {PWVIZ_BAR_STYLE_RANDOM, "Random"},
};

static const RadioOption BACKGROUND_OPTIONS[] = {
    {PWVIZ_BACKGROUND_BLACK, "Black"},
    {PWVIZ_BACKGROUND_GRID, "Grid"},
    {PWVIZ_BACKGROUND_SOLID, "Solid Colour"},
    {PWVIZ_BACKGROUND_FLASH, "Flash"},
    {PWVIZ_BACKGROUND_FLASH_GRID, "Flash Grid"},
};

static const RadioOption PEAK_COLOR_OPTIONS[] = {
    {PWVIZ_PEAK_COLOR_FADE, "Fade"},
    {PWVIZ_PEAK_COLOR_LEVEL, "Level"},
    {PWVIZ_PEAK_COLOR_LEVEL_FADE, "Level & Fade"},
};

static const RadioOption PEAK_MOTION_OPTIONS[] = {
    {PWVIZ_PEAK_MOTION_NORMAL, "Normal"},
    {PWVIZ_PEAK_MOTION_FALL, "Fall"},
    {PWVIZ_PEAK_MOTION_RISE, "Rise"},
    {PWVIZ_PEAK_MOTION_FALL_RISE, "Fall & Rise"},
    {PWVIZ_PEAK_MOTION_RISE_FALL, "Rise Fall"},
    {PWVIZ_PEAK_MOTION_SPARKS, "Sparks"},
};

static guint anchor_index_for_anchor(PwvizWindowAnchor anchor) {
  for (guint i = 0; i < G_N_ELEMENTS(ANCHOR_OPTIONS); i++) {
    if (ANCHOR_OPTIONS[i].anchor == anchor)
      return i;
  }

  return G_N_ELEMENTS(ANCHOR_OPTIONS) - 1;
}

static double mix(double a, double b, double t) {
  return a + (b - a) * t;
}

static double pct_alpha(int percent) {
  return percent / 100.0;
}

static double color_alpha(double config_alpha, double color_alpha) {
  return CLAMP(config_alpha, 0.0, 1.0) * CLAMP(color_alpha, 0.0, 1.0);
}

static double average_bar_level(PwvizVisualizer *visualizer, int bar_count) {
  double total = 0.0;

  if (bar_count <= 0)
    return 0.0;

  for (int i = 0; i < bar_count; i++)
    total += visualizer->bars[i];

  return CLAMP(total / bar_count, 0.0, 1.0);
}

static void style_color(PwvizVisualizer *visualizer, int bar_index,
                        double level, double *red, double *green,
                        double *blue, double *source_alpha) {
  switch (visualizer->config.bar_style) {
  case PWVIZ_BAR_STYLE_SOFT_FLAME:
    *red = mix(0.72, 1.00, level);
    *green = mix(0.05, 0.58, level);
    *blue = mix(0.02, 0.16, level);
    *source_alpha = mix(0.82, 1.0, level);
    break;
  case PWVIZ_BAR_STYLE_FIRE:
    *red = mix(0.55, 1.00, level);
    *green = mix(0.00, 0.82, level);
    *blue = mix(0.00, 0.04, level);
    *source_alpha = 1.0;
    break;
  case PWVIZ_BAR_STYLE_SOLID_LINES:
    if (level > 0.72) {
      *red = 1.0;
      *green = 0.08;
      *blue = 0.02;
    } else if (level > 0.42) {
      *red = 1.0;
      *green = 0.86;
      *blue = 0.0;
    } else {
      *red = 0.04;
      *green = 0.86;
      *blue = 0.12;
    }
    *source_alpha = 1.0;
    break;
  case PWVIZ_BAR_STYLE_WINAMP_FIRE:
    *red = 1.0;
    *green = pow(level, 0.72);
    *blue = level > 0.86 ? mix(0.0, 0.18, (level - 0.86) / 0.14) : 0.0;
    *source_alpha = 1.0;
    break;
  case PWVIZ_BAR_STYLE_RANDOM:
    *red = 0.50 + 0.50 * sin(bar_index * 0.73 + 0.2);
    *green = 0.50 + 0.50 * sin(bar_index * 1.13 + 2.0);
    *blue = 0.50 + 0.50 * sin(bar_index * 0.97 + 4.0);
    *red = mix(*red * 0.55, *red, level);
    *green = mix(*green * 0.55, *green, level);
    *blue = mix(*blue * 0.55, *blue, level);
    *source_alpha = 1.0;
    break;
  case PWVIZ_BAR_STYLE_CLASSIC:
  default:
    *red = mix(visualizer->config.low_color.red,
               visualizer->config.high_color.red, level);
    *green = mix(visualizer->config.low_color.green,
                 visualizer->config.high_color.green, level);
    *blue = mix(visualizer->config.low_color.blue,
                visualizer->config.high_color.blue, level);
    *source_alpha = mix(visualizer->config.low_color.alpha,
                        visualizer->config.high_color.alpha, level);
    break;
  }
}

static void draw_grid(cairo_t *cr, int width, int height, double alpha) {
  if (alpha <= 0.0)
    return;

  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, alpha);
  cairo_set_line_width(cr, 1.0);
  for (int x = 0; x <= width; x += 24) {
    cairo_move_to(cr, x + 0.5, 0);
    cairo_line_to(cr, x + 0.5, height);
  }
  for (int y = 0; y <= height; y += 24) {
    cairo_move_to(cr, 0, y + 0.5);
    cairo_line_to(cr, width, y + 0.5);
  }
  cairo_stroke(cr);
}

static void draw_background(PwvizVisualizer *visualizer, cairo_t *cr,
                            int width, int height, int bar_count) {
  double red = visualizer->config.background_color.red;
  double green = visualizer->config.background_color.green;
  double blue = visualizer->config.background_color.blue;
  double alpha = color_alpha(visualizer->config.background_alpha,
                             visualizer->config.background_color.alpha);
  double flash = average_bar_level(visualizer, bar_count);
  gboolean grid = FALSE;

  switch (visualizer->config.background_mode) {
  case PWVIZ_BACKGROUND_BLACK:
    red = 0.0;
    green = 0.0;
    blue = 0.0;
    break;
  case PWVIZ_BACKGROUND_GRID:
    grid = TRUE;
    break;
  case PWVIZ_BACKGROUND_FLASH:
    alpha = MAX(alpha, flash * 0.28);
    red = mix(red, visualizer->config.high_color.red, flash);
    green = mix(green, visualizer->config.high_color.green, flash);
    blue = mix(blue, visualizer->config.high_color.blue, flash);
    break;
  case PWVIZ_BACKGROUND_FLASH_GRID:
    grid = TRUE;
    alpha = MAX(alpha, flash * 0.24);
    red = mix(red, visualizer->config.high_color.red, flash);
    green = mix(green, visualizer->config.high_color.green, flash);
    blue = mix(blue, visualizer->config.high_color.blue, flash);
    break;
  case PWVIZ_BACKGROUND_SOLID:
  default:
    break;
  }

  cairo_set_source_rgba(cr, red, green, blue, alpha);
  cairo_paint(cr);
  draw_grid(cr, width, height, grid ? MAX(0.06, alpha * 0.55) : 0.0);
}

static int effective_bar_count(PwvizVisualizer *visualizer, int width) {
  if (visualizer->config.auto_bar_count) {
    int total_width = MAX(1, visualizer->config.bar_width +
                                 visualizer->config.x_spacing);
    return CLAMP((width + visualizer->config.x_spacing) / total_width, 1,
                 PWVIZ_BAR_COUNT);
  }

  return CLAMP(visualizer->config.bar_count, 1, PWVIZ_BAR_COUNT);
}

static int font_pixel_size(const char *font_desc, int fallback) {
  PangoFontDescription *font = pango_font_description_from_string(
      font_desc && font_desc[0] != '\0' ? font_desc : "Sans 12");
  int size = pango_font_description_get_size(font);
  int pixels = fallback;

  if (size > 0)
    pixels = pango_font_description_get_size_is_absolute(font)
                 ? PANGO_PIXELS(size)
                 : size / PANGO_SCALE;

  pango_font_description_free(font);
  return CLAMP(pixels, 8, 64);
}

static int now_playing_height(PwvizVisualizer *visualizer) {
  if (!visualizer->config.now_playing_enabled ||
      !visualizer->now_playing.available)
    return 0;

  int metadata_size = font_pixel_size(visualizer->config.now_playing_font, 13);
  int lyric_size = font_pixel_size(visualizer->config.lyrics_font, 12);
  int metadata_h = metadata_size + 16;
  int lyric_h = visualizer->config.lyrics_enabled
                    ? lyric_size *
                              (visualizer->config.lyrics_two_lines ? 2 : 1) +
                          24
                    : 0;
  int minimum = metadata_h + lyric_h;

  return CLAMP(MAX(visualizer->config.now_playing_height, minimum), 0,
               MAX(0, visualizer->height - 24));
}

static void update_peak_cap(PwvizVisualizer *visualizer, int index) {
  float current = visualizer->bars[index];
  float speed = visualizer->config.peak_fall_per_frame;
  float cap = visualizer->peak_caps[index];

  switch (visualizer->config.peak_motion) {
  case PWVIZ_PEAK_MOTION_NORMAL:
    visualizer->peak_caps[index] = current;
    visualizer->peak_holds[index] = 0;
    return;
  case PWVIZ_PEAK_MOTION_RISE:
    if (current > cap) {
      visualizer->peak_holds[index] = visualizer->config.peak_change_rate;
      cap = current;
    } else if (visualizer->peak_holds[index] > 0) {
      visualizer->peak_holds[index]--;
      cap = MIN(1.0f, cap + speed * 0.65f);
    } else {
      cap = MAX(current, cap - speed);
    }
    break;
  case PWVIZ_PEAK_MOTION_FALL_RISE:
    if (current > cap) {
      visualizer->peak_holds[index] = visualizer->config.peak_change_rate;
      cap = current;
    } else if (visualizer->peak_holds[index] > 0) {
      visualizer->peak_holds[index]--;
    } else if (cap > 0.18f) {
      cap = MAX(0.18f, cap - speed);
    } else {
      cap = MIN(current, cap + speed * 0.75f);
    }
    break;
  case PWVIZ_PEAK_MOTION_RISE_FALL:
    if (current > cap) {
      visualizer->peak_holds[index] = visualizer->config.peak_change_rate;
      cap = current;
    } else if (visualizer->peak_holds[index] > 0) {
      visualizer->peak_holds[index]--;
      cap = MIN(1.0f, cap + speed * 0.45f);
    } else {
      cap = MAX(0.0f, cap - speed * 1.35f);
    }
    break;
  case PWVIZ_PEAK_MOTION_SPARKS:
    if (current > cap) {
      float spark = (float)(((index * 37 + visualizer->peak_holds[index] * 11) %
                             17) /
                            255.0);
      cap = MIN(1.0f, current + 0.04f + spark);
      visualizer->peak_holds[index] = visualizer->config.peak_change_rate / 3;
    } else if (visualizer->peak_holds[index] > 0) {
      visualizer->peak_holds[index]--;
    } else {
      cap = MAX(0.0f, cap - speed * 2.1f);
    }
    break;
  case PWVIZ_PEAK_MOTION_FALL:
  default:
    if (current > cap) {
      cap = current;
      visualizer->peak_holds[index] = visualizer->config.peak_change_rate;
    } else if (visualizer->peak_holds[index] > 0) {
      visualizer->peak_holds[index]--;
    } else {
      cap = MAX(0.0f, cap - speed);
    }
    break;
  }

  visualizer->peak_caps[index] = CLAMP(cap, 0.0f, 1.0f);
}

static void peak_color(PwvizVisualizer *visualizer, int bar_index,
                       double level, double *red, double *green, double *blue,
                       double *source_alpha) {
  double bar_red;
  double bar_green;
  double bar_blue;
  double bar_alpha;

  style_color(visualizer, bar_index, level, &bar_red, &bar_green, &bar_blue,
              &bar_alpha);

  switch (visualizer->config.peak_color_mode) {
  case PWVIZ_PEAK_COLOR_FADE:
    *red = visualizer->config.peak_color.red;
    *green = visualizer->config.peak_color.green;
    *blue = visualizer->config.peak_color.blue;
    *source_alpha = visualizer->config.peak_color.alpha;
    break;
  case PWVIZ_PEAK_COLOR_LEVEL:
    *red = bar_red;
    *green = bar_green;
    *blue = bar_blue;
    *source_alpha = bar_alpha;
    break;
  case PWVIZ_PEAK_COLOR_LEVEL_FADE:
  default:
    *red = mix(visualizer->config.peak_color.red, bar_red, level);
    *green = mix(visualizer->config.peak_color.green, bar_green, level);
    *blue = mix(visualizer->config.peak_color.blue, bar_blue, level);
    *source_alpha = mix(visualizer->config.peak_color.alpha, bar_alpha, 0.5);
    break;
  }
}

static void append_now_playing_part(GString *line, const char *text) {
  if (!text || text[0] == '\0')
    return;

  if (line->len > 0)
    g_string_append(line, "  |  ");
  g_string_append(line, text);
}

static char *now_playing_text(PwvizVisualizer *visualizer) {
  GString *line = g_string_new(NULL);

  if (visualizer->config.now_playing_show_app)
    append_now_playing_part(line, visualizer->now_playing.app);
  if (visualizer->config.now_playing_show_title)
    append_now_playing_part(line, visualizer->now_playing.title);
  if (visualizer->config.now_playing_show_artist)
    append_now_playing_part(line, visualizer->now_playing.artist);
  if (visualizer->config.now_playing_show_album)
    append_now_playing_part(line, visualizer->now_playing.album);

  return g_string_free(line, FALSE);
}

static void draw_ellipsized_text(cairo_t *cr, const char *text,
                                 const char *font_desc, double x, double y,
                                 int width,
                                 const GdkRGBA *text_color,
                                 const GdkRGBA *outline_color,
                                 double outline_width,
                                 const GdkRGBA *shadow_color,
                                 double shadow_x, double shadow_y,
                                 double shadow_opacity, double alpha) {
  if (!text || text[0] == '\0')
    return;

  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *font = pango_font_description_from_string(
      font_desc && font_desc[0] != '\0' ? font_desc : "Sans 12");

  pango_layout_set_font_description(layout, font);
  pango_layout_set_width(layout, MAX(1, width) * PANGO_SCALE);
  pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
  pango_layout_set_single_paragraph_mode(layout, TRUE);
  char *display_text = g_strdup(text);
  for (char *p = display_text; *p; p++) {
    if (*p == '\n' || *p == '\r' || *p == '\t')
      *p = ' ';
  }
  pango_layout_set_text(layout, display_text, -1);

  if (shadow_color && shadow_opacity > 0.0) {
    cairo_move_to(cr, x + shadow_x, y + shadow_y);
    pango_cairo_layout_path(cr, layout);
    cairo_set_source_rgba(cr, shadow_color->red, shadow_color->green,
                          shadow_color->blue,
                          color_alpha(alpha, shadow_color->alpha) *
                              CLAMP(shadow_opacity, 0.0, 1.0));
    cairo_fill(cr);
  }

  cairo_move_to(cr, x, y);
  pango_cairo_layout_path(cr, layout);
  if (outline_width > 0.0) {
    cairo_set_source_rgba(cr, outline_color->red, outline_color->green,
                          outline_color->blue,
                          color_alpha(alpha, outline_color->alpha));
    cairo_set_line_width(cr, outline_width);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke_preserve(cr);
  }
  cairo_set_source_rgba(cr, text_color->red, text_color->green,
                        text_color->blue, color_alpha(alpha, text_color->alpha));
  cairo_fill(cr);

  pango_font_description_free(font);
  g_free(display_text);
  g_object_unref(layout);
}

static void draw_now_playing(PwvizVisualizer *visualizer, cairo_t *cr,
                             int width, int height) {
  int section_h = now_playing_height(visualizer);

  if (section_h <= 0)
    return;

  char *text = now_playing_text(visualizer);
  if (!text || text[0] == '\0') {
    g_free(text);
    return;
  }

  double y = height - section_h;
  double alpha = CLAMP(visualizer->config.now_playing_alpha, 0.0, 1.0);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, alpha);
  cairo_rectangle(cr, 0.0, y, width, section_h);
  cairo_fill(cr);

  const char *current_lyric = NULL;
  const char *next_lyric = NULL;
  if (visualizer->config.lyrics_enabled)
    pwviz_lyrics_current_lines(visualizer->lyrics,
                               visualizer->now_playing.position_us,
                               &current_lyric, &next_lyric);

  int content_w = MAX(1, width - 16);
  int metadata_size = font_pixel_size(visualizer->config.now_playing_font, 13);
  int lyric_size = font_pixel_size(visualizer->config.lyrics_font, 12);
  if (current_lyric) {
    double title_y = y + 7.0;
    double current_y = y + 10.0 + metadata_size + 8.0;
    double next_y = current_y + lyric_size + 10.0;

    draw_ellipsized_text(cr, text, visualizer->config.now_playing_font, 8.0,
                         title_y, content_w,
                         &visualizer->config.now_playing_text_color,
                         &visualizer->config.now_playing_outline_color,
                         visualizer->config.now_playing_outline_width,
                         &visualizer->config.now_playing_shadow_color,
                         visualizer->config.now_playing_shadow_x,
                         visualizer->config.now_playing_shadow_y,
                         visualizer->config.now_playing_shadow_opacity, 0.78);
    draw_ellipsized_text(cr, current_lyric, visualizer->config.lyrics_font, 8.0,
                         current_y, content_w,
                         &visualizer->config.lyrics_text_color,
                         &visualizer->config.lyrics_outline_color,
                         visualizer->config.lyrics_outline_width,
                         &visualizer->config.lyrics_shadow_color,
                         visualizer->config.lyrics_shadow_x,
                         visualizer->config.lyrics_shadow_y,
                         visualizer->config.lyrics_shadow_opacity, 0.98);
    if (visualizer->config.lyrics_two_lines && next_lyric &&
        next_y + lyric_size <= y + section_h - 4.0)
      draw_ellipsized_text(cr, next_lyric, visualizer->config.lyrics_font, 8.0,
                           next_y, content_w,
                           &visualizer->config.lyrics_text_color,
                           &visualizer->config.lyrics_outline_color,
                           visualizer->config.lyrics_outline_width,
                           &visualizer->config.lyrics_shadow_color,
                           visualizer->config.lyrics_shadow_x,
                           visualizer->config.lyrics_shadow_y,
                           visualizer->config.lyrics_shadow_opacity, 0.62);
  } else {
    draw_ellipsized_text(cr, text, visualizer->config.now_playing_font,
                         8.0, y + MAX(0, section_h - metadata_size) / 2.0,
                         content_w, &visualizer->config.now_playing_text_color,
                         &visualizer->config.now_playing_outline_color,
                         visualizer->config.now_playing_outline_width,
                         &visualizer->config.now_playing_shadow_color,
                         visualizer->config.now_playing_shadow_x,
                         visualizer->config.now_playing_shadow_y,
                         visualizer->config.now_playing_shadow_opacity, 0.96);
  }

  g_free(text);
}

static void queue_visualizer_draw(PwvizVisualizer *visualizer) {
  if (!visualizer || visualizer->destroying || !visualizer->drawing_area ||
      !GTK_IS_WIDGET(visualizer->drawing_area))
    return;

  gtk_widget_queue_draw(visualizer->drawing_area);
}

static void update_input_region(PwvizVisualizer *visualizer) {
  if (!visualizer || visualizer->destroying || !visualizer->window ||
      !GTK_IS_WINDOW(visualizer->window))
    return;

  GdkSurface *surface =
      gtk_native_get_surface(GTK_NATIVE(visualizer->window));
  if (!surface)
    return;

  cairo_region_t *region = cairo_region_create();

  gdk_surface_set_input_region(surface, region);
  cairo_region_destroy(region);
}

static void update_spectrum(PwvizVisualizer *visualizer) {
  pwviz_audio_buffer_copy_latest(visualizer->audio_buffer,
                                 visualizer->fft_samples, PWVIZ_FFT_SIZE);
  pwviz_fft_analyze(&visualizer->fft, visualizer->fft_samples,
                    visualizer->magnitudes, visualizer->config.fft_equalize,
                    visualizer->config.fft_envelope);
  PwvizAppConfig analysis_config = visualizer->config;
  analysis_config.bar_count =
      effective_bar_count(visualizer, visualizer->width);
  pwviz_binner_calculate(&visualizer->binner, visualizer->magnitudes,
                         visualizer->levels, &analysis_config);

  int bar_count = effective_bar_count(visualizer, visualizer->width);
  float falloff = visualizer->config.falloff_rate / 255.0f;

  for (int i = 0; i < bar_count; i++) {
    float value = visualizer->levels[i];

    if (value < visualizer->config.display_threshold)
      value = 0.0f;

    float falling = MAX(0.0f, visualizer->bars[i] - falloff);
    visualizer->bars[i] = value > falling ? value : falling;
  }

  for (int i = bar_count; i < PWVIZ_BAR_COUNT; i++)
    visualizer->bars[i] = 0.0f;
}

static void draw_spectrum(PwvizVisualizer *visualizer, cairo_t *cr, int width,
                          int height) {
  double visual_top = PWVIZ_SPECTRUM_TOP_PADDING;
  double visual_bottom =
      height - PWVIZ_SPECTRUM_BOTTOM_PADDING - now_playing_height(visualizer);
  double visual_height = visual_bottom - visual_top;
  int bar_count = effective_bar_count(visualizer, width);
  double bar_w = (double)width / bar_count;
  double block_h = visualizer->config.block_height;
  double block_gap = visualizer->config.block_gap;
  double block_w = MAX(1.0, bar_w - visualizer->config.x_spacing);

  if (visual_height <= block_h)
    return;

  for (int i = 0; i < bar_count; i++) {
    update_peak_cap(visualizer, i);

    double bar_value = visualizer->config.analyzer_mode == PWVIZ_ANALYZER_PEAK
                           ? visualizer->peak_caps[i]
                           : visualizer->bars[i];
    double h = bar_value * visual_height;
    double x = i * bar_w;
    double bar_x = x + visualizer->config.x_spacing / 2.0;
    double lit_top = visual_bottom - h;

    for (double y = visual_bottom - block_h; y >= visual_top;
         y -= block_h + block_gap) {
      double level = (visual_bottom - y) / visual_height;
      gboolean lit = y >= lit_top;
      double red;
      double green;
      double blue;
      double source_alpha;
      style_color(visualizer, i, level, &red, &green, &blue, &source_alpha);
      double alpha =
          lit ? color_alpha(visualizer->config.bar_alpha, source_alpha)
              : pct_alpha(INACTIVE_BAR_ALPHA);

      if (visualizer->config.analyzer_mode == PWVIZ_ANALYZER_FLASH &&
          visualizer->bars[i] > 0.75f)
        alpha = color_alpha(visualizer->config.bar_alpha,
                            visualizer->config.high_color.alpha);

      cairo_set_source_rgba(cr, red, green, blue, alpha);
      cairo_rectangle(cr, bar_x, y, block_w, block_h);
      cairo_fill(cr);
    }

    if (visualizer->config.analyzer_mode == PWVIZ_ANALYZER_FLASH ||
        visualizer->config.peak_change_rate == 0)
      continue;

    double peak_y = visual_bottom - visualizer->peak_caps[i] * visual_height;
    double snapped_peak_y =
        visual_bottom -
        floor((visual_bottom - peak_y) / (block_h + block_gap)) *
            (block_h + block_gap);

    double peak_block_y =
        CLAMP(snapped_peak_y, visual_top, visual_bottom - block_h);
    double peak_level = visualizer->peak_caps[i];
    double peak_red;
    double peak_green;
    double peak_blue;
    double peak_source_alpha;
    peak_color(visualizer, i, peak_level, &peak_red, &peak_green, &peak_blue,
               &peak_source_alpha);
    double peak_alpha =
        color_alpha(visualizer->config.bar_alpha, peak_source_alpha);

    cairo_set_source_rgba(cr, peak_red, peak_green, peak_blue, peak_alpha);
    cairo_rectangle(cr, bar_x, peak_block_y, block_w, block_h);
    cairo_fill(cr);
  }
}

static void draw_cb(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                    gpointer data) {
  (void)area;

  PwvizVisualizer *visualizer = data;
  visualizer->width = width;
  visualizer->height = height;
  update_input_region(visualizer);
  update_spectrum(visualizer);

  draw_background(visualizer, cr, width, height,
                  effective_bar_count(visualizer, width));
  draw_spectrum(visualizer, cr, width, height);
  draw_now_playing(visualizer, cr, width, height);
}

static gboolean tick_cb(GtkWidget *widget, GdkFrameClock *clock,
                        gpointer data) {
  (void)clock;
  (void)data;

  gtk_widget_queue_draw(widget);
  return G_SOURCE_CONTINUE;
}

typedef struct {
  GAsyncQueue *queue;
  PwvizNowPlaying now_playing;
  PwvizLyrics *lyrics;
  char key[512];
} LyricsFetch;

static gpointer lyrics_fetch_thread(gpointer data) {
  LyricsFetch *fetch = data;

  fetch->lyrics = pwviz_lyrics_fetch(&fetch->now_playing);
  g_async_queue_push(fetch->queue, fetch);
  g_async_queue_unref(fetch->queue);
  return NULL;
}

static void lyrics_fetch_free(LyricsFetch *fetch) {
  if (!fetch)
    return;

  pwviz_lyrics_free(fetch->lyrics);
  g_free(fetch);
}

static void poll_lyrics_results(PwvizVisualizer *visualizer) {
  LyricsFetch *fetch = NULL;

  while ((fetch = g_async_queue_try_pop(visualizer->lyrics_results))) {
    if (visualizer->lyrics_fetching &&
        g_strcmp0(visualizer->lyrics_fetching_key, fetch->key) == 0) {
      visualizer->lyrics_fetching = FALSE;
      visualizer->lyrics_fetching_key[0] = '\0';
      g_strlcpy(visualizer->lyrics_key, fetch->key,
                sizeof(visualizer->lyrics_key));
      pwviz_lyrics_free(visualizer->lyrics);
      visualizer->lyrics = fetch->lyrics;
      fetch->lyrics = NULL;
    }
    lyrics_fetch_free(fetch);
  }
}

static void maybe_start_lyrics_fetch(PwvizVisualizer *visualizer) {
  if (!visualizer->config.lyrics_enabled || !visualizer->now_playing.available ||
      visualizer->now_playing.title[0] == '\0') {
    pwviz_lyrics_free(visualizer->lyrics);
    visualizer->lyrics = NULL;
    visualizer->lyrics_key[0] = '\0';
    visualizer->lyrics_fetching = FALSE;
    visualizer->lyrics_fetching_key[0] = '\0';
    return;
  }

  char key[512];
  pwviz_lyrics_key_for_track(&visualizer->now_playing, key, sizeof(key));
  if (g_strcmp0(visualizer->lyrics_key, key) == 0 ||
      (visualizer->lyrics_fetching &&
       g_strcmp0(visualizer->lyrics_fetching_key, key) == 0))
    return;

  LyricsFetch *fetch = g_new0(LyricsFetch, 1);
  fetch->queue = g_async_queue_ref(visualizer->lyrics_results);
  fetch->now_playing = visualizer->now_playing;
  g_strlcpy(fetch->key, key, sizeof(fetch->key));
  visualizer->lyrics_fetching = TRUE;
  g_strlcpy(visualizer->lyrics_fetching_key, key,
            sizeof(visualizer->lyrics_fetching_key));
  g_thread_unref(g_thread_new("lyrics-fetch", lyrics_fetch_thread, fetch));
}

static gboolean now_playing_refresh_cb(gpointer data) {
  PwvizVisualizer *visualizer = data;

  poll_lyrics_results(visualizer);
  if (visualizer->config.now_playing_enabled) {
    pwviz_now_playing_refresh(&visualizer->now_playing);
    maybe_start_lyrics_fetch(visualizer);
  } else {
    pwviz_now_playing_clear(&visualizer->now_playing);
    maybe_start_lyrics_fetch(visualizer);
  }

  queue_visualizer_draw(visualizer);
  return G_SOURCE_CONTINUE;
}

static void save_current_config(PwvizVisualizer *visualizer) {
  pwviz_app_config_save(&visualizer->config);
  visualizer->config_snapshot = visualizer->config;
}

static GtkWidget *section_label(const char *label) {
  GtkWidget *text = gtk_label_new(label);

  gtk_widget_add_css_class(text, "heading");
  gtk_widget_set_halign(text, GTK_ALIGN_START);
  gtk_widget_set_margin_top(text, 6);
  gtk_widget_set_margin_bottom(text, 2);
  return text;
}

static void prepare_scale(GtkWidget *scale, int digits) {
  gtk_scale_set_digits(GTK_SCALE(scale), digits);
  gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
  gtk_widget_set_size_request(scale, 180, 32);
  gtk_widget_add_css_class(scale, "pwviz-scale");
}

static void prepare_spin(GtkWidget *spin) {
  gtk_widget_set_hexpand(spin, TRUE);
  gtk_widget_set_halign(spin, GTK_ALIGN_FILL);
}

static GtkWidget *control_row(const char *label, GtkWidget *control) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *text = gtk_label_new(label);

  gtk_widget_set_hexpand(control, TRUE);
  gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(text, GTK_ALIGN_START);
  gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
  gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
  gtk_label_set_width_chars(GTK_LABEL(text), 18);
  gtk_box_append(GTK_BOX(row), text);
  gtk_box_append(GTK_BOX(row), control);
  return row;
}

static GtkWidget *paired_control_row(const char *label, const char *first_label,
                                     GtkWidget *first,
                                     const char *second_label,
                                     GtkWidget *second) {
  GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *first_text = gtk_label_new(first_label);
  GtkWidget *second_text = gtk_label_new(second_label);

  gtk_widget_set_hexpand(first, TRUE);
  gtk_widget_set_hexpand(second, TRUE);
  gtk_box_append(GTK_BOX(controls), first_text);
  gtk_box_append(GTK_BOX(controls), first);
  gtk_box_append(GTK_BOX(controls), second_text);
  gtk_box_append(GTK_BOX(controls), second);
  return control_row(label, controls);
}

static GtkWidget *tab_box(void) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

  gtk_widget_set_margin_top(box, 14);
  gtk_widget_set_margin_bottom(box, 14);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);
  return box;
}

static void analyzer_mode_toggled_cb(GtkCheckButton *button, gpointer data) {
  if (!gtk_check_button_get_active(button))
    return;

  PwvizVisualizer *visualizer = data;
  visualizer->config.analyzer_mode =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "mode"));
  queue_visualizer_draw(visualizer);
}

static void level_mode_toggled_cb(GtkCheckButton *button, gpointer data) {
  if (!gtk_check_button_get_active(button))
    return;

  PwvizVisualizer *visualizer = data;
  visualizer->config.level_mode =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "level-mode"));
  queue_visualizer_draw(visualizer);
}

static void classic_option_toggled_cb(GtkCheckButton *button, gpointer data) {
  if (!gtk_check_button_get_active(button))
    return;

  PwvizVisualizer *visualizer = data;
  ClassicOptionTarget target =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "classic-target"));
  int value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),
                                               "classic-value"));

  switch (target) {
  case CLASSIC_OPTION_BAR_STYLE:
    visualizer->config.bar_style = value;
    break;
  case CLASSIC_OPTION_BACKGROUND:
    visualizer->config.background_mode = value;
    break;
  case CLASSIC_OPTION_PEAK_COLOR:
    visualizer->config.peak_color_mode = value;
    break;
  case CLASSIC_OPTION_PEAK_MOTION:
    visualizer->config.peak_motion = value;
    break;
  }

  queue_visualizer_draw(visualizer);
}

static GtkWidget *classic_radio_group(PwvizVisualizer *visualizer,
                                      ClassicOptionTarget target,
                                      const RadioOption *options,
                                      guint option_count, int active_value) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  GtkWidget *first = NULL;

  for (guint i = 0; i < option_count; i++) {
    GtkWidget *button = gtk_check_button_new_with_label(options[i].label);

    if (first)
      gtk_check_button_set_group(GTK_CHECK_BUTTON(button),
                                 GTK_CHECK_BUTTON(first));
    else
      first = button;

    g_object_set_data(G_OBJECT(button), "classic-target",
                      GINT_TO_POINTER(target));
    g_object_set_data(G_OBJECT(button), "classic-value",
                      GINT_TO_POINTER(options[i].value));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(button),
                                options[i].value == active_value);
    g_signal_connect(button, "toggled", G_CALLBACK(classic_option_toggled_cb),
                     visualizer);
    gtk_box_append(GTK_BOX(box), button);
  }

  return box;
}

static GtkWidget *classic_group(PwvizVisualizer *visualizer, const char *label,
                                ClassicOptionTarget target,
                                const RadioOption *options,
                                guint option_count, int active_value) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

  gtk_box_append(GTK_BOX(box), section_label(label));
  gtk_box_append(GTK_BOX(box),
                 classic_radio_group(visualizer, target, options, option_count,
                                     active_value));
  return box;
}

static void bar_count_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.bar_count = gtk_spin_button_get_value_as_int(spin);
  queue_visualizer_draw(visualizer);
}

static void auto_bar_count_toggled_cb(GtkCheckButton *button, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.auto_bar_count = gtk_check_button_get_active(button);
  queue_visualizer_draw(visualizer);
}

static void bar_width_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.bar_width = gtk_spin_button_get_value_as_int(spin);
  queue_visualizer_draw(visualizer);
}

static void x_spacing_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.x_spacing = gtk_spin_button_get_value_as_int(spin);
  queue_visualizer_draw(visualizer);
}

static void block_height_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.block_height = gtk_spin_button_get_value_as_int(spin);
  queue_visualizer_draw(visualizer);
}

static void block_gap_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.block_gap = gtk_spin_button_get_value_as_int(spin);
  queue_visualizer_draw(visualizer);
}

static void falloff_rate_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.falloff_rate = gtk_spin_button_get_value_as_int(spin);
}

static void peak_change_rate_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.peak_change_rate = gtk_spin_button_get_value_as_int(spin);
}

static void peak_fall_changed_cb(GtkRange *range, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.peak_fall_per_frame = gtk_range_get_value(range);
}

static void fft_equalize_toggled_cb(GtkCheckButton *button, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.fft_equalize = gtk_check_button_get_active(button);
  queue_visualizer_draw(visualizer);
}

static void fft_envelope_changed_cb(GtkRange *range, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.fft_envelope = gtk_range_get_value(range);
  queue_visualizer_draw(visualizer);
}

static void fft_scale_changed_cb(GtkRange *range, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.fft_scale = gtk_range_get_value(range);
  queue_visualizer_draw(visualizer);
}

static void display_threshold_changed_cb(GtkRange *range, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.display_threshold = gtk_range_get_value(range);
  queue_visualizer_draw(visualizer);
}

static void background_alpha_changed_cb(GtkRange *range, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.background_alpha = gtk_range_get_value(range);
  queue_visualizer_draw(visualizer);
}

static void bar_alpha_changed_cb(GtkRange *range, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.bar_alpha = gtk_range_get_value(range);
  queue_visualizer_draw(visualizer);
}

static void now_playing_enabled_toggled_cb(GtkCheckButton *button,
                                           gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.now_playing_enabled = gtk_check_button_get_active(button);
  if (visualizer->config.now_playing_enabled)
    pwviz_now_playing_refresh(&visualizer->now_playing);
  else
    pwviz_now_playing_clear(&visualizer->now_playing);
  maybe_start_lyrics_fetch(visualizer);
  queue_visualizer_draw(visualizer);
}

static void now_playing_show_toggled_cb(GtkCheckButton *button, gpointer data) {
  PwvizVisualizer *visualizer = data;
  const char *field = g_object_get_data(G_OBJECT(button), "now-playing-field");
  gboolean active = gtk_check_button_get_active(button);

  if (g_strcmp0(field, "app") == 0)
    visualizer->config.now_playing_show_app = active;
  else if (g_strcmp0(field, "title") == 0)
    visualizer->config.now_playing_show_title = active;
  else if (g_strcmp0(field, "artist") == 0)
    visualizer->config.now_playing_show_artist = active;
  else if (g_strcmp0(field, "album") == 0)
    visualizer->config.now_playing_show_album = active;

  queue_visualizer_draw(visualizer);
}

static void now_playing_height_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.now_playing_height = gtk_spin_button_get_value_as_int(spin);
  queue_visualizer_draw(visualizer);
}

static void now_playing_outline_width_changed_cb(GtkSpinButton *spin,
                                                 gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.now_playing_outline_width =
      gtk_spin_button_get_value(spin);
  queue_visualizer_draw(visualizer);
}

static void lyrics_outline_width_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.lyrics_outline_width = gtk_spin_button_get_value(spin);
  queue_visualizer_draw(visualizer);
}

static void now_playing_shadow_x_changed_cb(GtkSpinButton *spin,
                                            gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.now_playing_shadow_x = gtk_spin_button_get_value(spin);
  queue_visualizer_draw(visualizer);
}

static void now_playing_shadow_y_changed_cb(GtkSpinButton *spin,
                                            gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.now_playing_shadow_y = gtk_spin_button_get_value(spin);
  queue_visualizer_draw(visualizer);
}

static void now_playing_shadow_opacity_changed_cb(GtkSpinButton *spin,
                                                  gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.now_playing_shadow_opacity =
      gtk_spin_button_get_value(spin);
  queue_visualizer_draw(visualizer);
}

static void lyrics_shadow_x_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.lyrics_shadow_x = gtk_spin_button_get_value(spin);
  queue_visualizer_draw(visualizer);
}

static void lyrics_shadow_y_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.lyrics_shadow_y = gtk_spin_button_get_value(spin);
  queue_visualizer_draw(visualizer);
}

static void lyrics_shadow_opacity_changed_cb(GtkSpinButton *spin,
                                             gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.lyrics_shadow_opacity = gtk_spin_button_get_value(spin);
  queue_visualizer_draw(visualizer);
}

static void now_playing_alpha_changed_cb(GtkRange *range, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.now_playing_alpha = gtk_range_get_value(range);
  queue_visualizer_draw(visualizer);
}

static void lyrics_enabled_toggled_cb(GtkCheckButton *button, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.lyrics_enabled = gtk_check_button_get_active(button);
  maybe_start_lyrics_fetch(visualizer);
  queue_visualizer_draw(visualizer);
}

static void lyrics_two_lines_toggled_cb(GtkCheckButton *button, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.lyrics_two_lines = gtk_check_button_get_active(button);
  queue_visualizer_draw(visualizer);
}

static void anchor_selected_cb(GtkDropDown *dropdown, GParamSpec *pspec,
                               gpointer data) {
  (void)pspec;

  PwvizVisualizer *visualizer = data;
  guint selected = gtk_drop_down_get_selected(dropdown);

  if (selected >= G_N_ELEMENTS(ANCHOR_OPTIONS))
    return;

  visualizer->config.window_anchor = ANCHOR_OPTIONS[selected].anchor;
  apply_layer_position(visualizer);
}

static void x_margin_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.x_margin = gtk_spin_button_get_value_as_int(spin);
  apply_layer_position(visualizer);
}

static void y_margin_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.y_margin = gtk_spin_button_get_value_as_int(spin);
  apply_layer_position(visualizer);
}

static void window_width_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.window_width = gtk_spin_button_get_value_as_int(spin);
  apply_window_geometry(visualizer);
}

static void window_height_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.window_height = gtk_spin_button_get_value_as_int(spin);
  apply_window_geometry(visualizer);
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS

static void color_changed_cb(GtkColorButton *button, gpointer data) {
  ColorBinding *binding = data;
  GdkRGBA color;

  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &color);

  switch (binding->target) {
  case COLOR_LOW:
    binding->visualizer->config.low_color = color;
    break;
  case COLOR_HIGH:
    binding->visualizer->config.high_color = color;
    break;
  case COLOR_PEAK:
    binding->visualizer->config.peak_color = color;
    break;
  case COLOR_BACKGROUND:
    binding->visualizer->config.background_color = color;
    break;
  case COLOR_NOW_PLAYING_TEXT:
    binding->visualizer->config.now_playing_text_color = color;
    break;
  case COLOR_NOW_PLAYING_OUTLINE:
    binding->visualizer->config.now_playing_outline_color = color;
    break;
  case COLOR_NOW_PLAYING_SHADOW:
    binding->visualizer->config.now_playing_shadow_color = color;
    break;
  case COLOR_LYRICS_TEXT:
    binding->visualizer->config.lyrics_text_color = color;
    break;
  case COLOR_LYRICS_OUTLINE:
    binding->visualizer->config.lyrics_outline_color = color;
    break;
  case COLOR_LYRICS_SHADOW:
    binding->visualizer->config.lyrics_shadow_color = color;
    break;
  }

  queue_visualizer_draw(binding->visualizer);
}

static void free_color_binding(gpointer data, GClosure *closure) {
  (void)closure;
  g_free(data);
}

static GtkWidget *color_control(PwvizVisualizer *visualizer, ColorTarget target,
                                const GdkRGBA *initial, const char *title) {
  GtkWidget *button = gtk_color_button_new_with_rgba(initial);
  ColorBinding *binding = g_new0(ColorBinding, 1);

  binding->visualizer = visualizer;
  binding->target = target;

  gtk_color_button_set_title(GTK_COLOR_BUTTON(button), title);
  gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(button), TRUE);
  g_signal_connect_data(button, "color-set", G_CALLBACK(color_changed_cb),
                        binding, free_color_binding, 0);
  return button;
}

G_GNUC_END_IGNORE_DEPRECATIONS

static gint string_pointer_compare(gconstpointer a, gconstpointer b) {
  return g_utf8_collate(*(const char *const *)a, *(const char *const *)b);
}

static int font_point_size(const char *font_desc, int fallback) {
  PangoFontDescription *font = pango_font_description_from_string(
      font_desc && font_desc[0] != '\0' ? font_desc : "Sans 12");
  int size = pango_font_description_get_size(font);
  int points = fallback;

  if (size > 0)
    points = pango_font_description_get_size_is_absolute(font)
                 ? PANGO_PIXELS(size)
                 : size / PANGO_SCALE;

  pango_font_description_free(font);
  return CLAMP(points, 8, 64);
}

static const char *font_family_name(const char *font_desc) {
  PangoFontDescription *font = pango_font_description_from_string(
      font_desc && font_desc[0] != '\0' ? font_desc : "Sans 12");
  const char *family = pango_font_description_get_family(font);
  const char *name = family && family[0] != '\0' ? family : "Sans";
  const char *interned = g_intern_string(name);

  pango_font_description_free(font);
  return interned;
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS

static GtkListStore *font_family_completion_model(void) {
  PangoFontMap *font_map = pango_cairo_font_map_get_default();
  PangoFontFamily **families = NULL;
  int n_families = 0;
  GPtrArray *names = g_ptr_array_new();
  GtkListStore *model = gtk_list_store_new(1, G_TYPE_STRING);

  pango_font_map_list_families(font_map, &families, &n_families);
  for (int i = 0; i < n_families; i++)
    g_ptr_array_add(names, (gpointer)pango_font_family_get_name(families[i]));
  g_ptr_array_sort(names, string_pointer_compare);

  for (guint i = 0; i < names->len; i++) {
    GtkTreeIter iter;

    gtk_list_store_append(model, &iter);
    gtk_list_store_set(model, &iter, 0, g_ptr_array_index(names, i), -1);
  }

  g_ptr_array_free(names, TRUE);
  g_free(families);
  return model;
}

static void append_font_style(GtkComboBoxText *styles,
                              const PangoFontDescription *desc,
                              const char *label) {
  PangoFontDescription *copy = pango_font_description_copy(desc);
  pango_font_description_unset_fields(copy, PANGO_FONT_MASK_SIZE);
  char *id = pango_font_description_to_string(copy);

  gtk_combo_box_text_append(styles, id, label);
  g_free(id);
  pango_font_description_free(copy);
}

static gboolean populate_font_styles(GtkComboBoxText *styles,
                                     const char *family,
                                     const PangoFontDescription *preferred) {
  PangoFontMap *font_map = pango_cairo_font_map_get_default();
  PangoFontFamily **families = NULL;
  int n_families = 0;
  int best_index = -1;
  int count = 0;
  PangoFontDescription *best = NULL;

  gtk_combo_box_text_remove_all(styles);
  pango_font_map_list_families(font_map, &families, &n_families);

  for (int i = 0; i < n_families; i++) {
    if (g_strcmp0(pango_font_family_get_name(families[i]), family) != 0)
      continue;

    PangoFontFace **faces = NULL;
    int n_faces = 0;

    pango_font_family_list_faces(families[i], &faces, &n_faces);
    for (int j = 0; j < n_faces; j++) {
      PangoFontDescription *desc = pango_font_face_describe(faces[j]);
      const char *face_name = pango_font_face_get_face_name(faces[j]);

      append_font_style(styles, desc, face_name);
      if (preferred &&
          pango_font_description_better_match(preferred, best, desc)) {
        if (best)
          pango_font_description_free(best);
        best = desc;
        best_index = count;
      } else {
        pango_font_description_free(desc);
      }
      count++;
    }
    g_free(faces);
    break;
  }

  if (count == 0) {
    PangoFontDescription *regular = pango_font_description_new();
    PangoFontDescription *bold = pango_font_description_new();
    PangoFontDescription *italic = pango_font_description_new();
    PangoFontDescription *bold_italic = pango_font_description_new();

    pango_font_description_set_family(regular, family);
    pango_font_description_set_family(bold, family);
    pango_font_description_set_weight(bold, PANGO_WEIGHT_BOLD);
    pango_font_description_set_family(italic, family);
    pango_font_description_set_style(italic, PANGO_STYLE_ITALIC);
    pango_font_description_set_family(bold_italic, family);
    pango_font_description_set_weight(bold_italic, PANGO_WEIGHT_BOLD);
    pango_font_description_set_style(bold_italic, PANGO_STYLE_ITALIC);

    append_font_style(styles, regular, "Regular");
    append_font_style(styles, bold, "Bold");
    append_font_style(styles, italic, "Italic");
    append_font_style(styles, bold_italic, "Bold Italic");
    best_index = 0;
    count = 4;

    pango_font_description_free(regular);
    pango_font_description_free(bold);
    pango_font_description_free(italic);
    pango_font_description_free(bold_italic);
  }

  gtk_combo_box_set_active(GTK_COMBO_BOX(styles), best_index >= 0 ? best_index : 0);
  if (best)
    pango_font_description_free(best);
  g_free(families);
  return count > 0;
}

static void apply_font_selection(GtkWidget *box) {
  if (!box)
    return;

  PwvizVisualizer *visualizer = g_object_get_data(G_OBJECT(box), "visualizer");
  GtkEditable *family = g_object_get_data(G_OBJECT(box), "family");
  GtkComboBox *style = g_object_get_data(G_OBJECT(box), "style");
  GtkSpinButton *size = g_object_get_data(G_OBJECT(box), "size");
  FontTarget target =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(box), "target"));

  if (!visualizer || !family || !style || !size)
    return;

  const char *style_id = gtk_combo_box_get_active_id(style);
  PangoFontDescription *font =
      style_id ? pango_font_description_from_string(style_id)
               : pango_font_description_new();
  const char *family_text = gtk_editable_get_text(family);

  pango_font_description_set_family(
      font, family_text && family_text[0] != '\0' ? family_text : "Sans");
  pango_font_description_set_size(
      font, gtk_spin_button_get_value_as_int(size) * PANGO_SCALE);

  char *font_string = pango_font_description_to_string(font);

  switch (target) {
  case FONT_NOW_PLAYING:
    g_strlcpy(visualizer->config.now_playing_font, font_string,
              sizeof(visualizer->config.now_playing_font));
    break;
  case FONT_LYRICS:
    g_strlcpy(visualizer->config.lyrics_font, font_string,
              sizeof(visualizer->config.lyrics_font));
    break;
  }

  g_message("%s font: %s",
            target == FONT_NOW_PLAYING ? "Now playing" : "Lyrics", font_string);
  g_free(font_string);
  pango_font_description_free(font);
  queue_visualizer_draw(visualizer);
}

static void font_family_changed_cb(GtkEditable *editable, gpointer data) {
  (void)editable;

  GtkWidget *box = data;
  GtkComboBoxText *styles = g_object_get_data(G_OBJECT(box), "style");
  const char *family = gtk_editable_get_text(editable);
  const char *style_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(styles));
  PangoFontDescription *preferred =
      style_id ? pango_font_description_from_string(style_id) : NULL;

  g_object_set_data(G_OBJECT(box), "updating-font-style", GINT_TO_POINTER(TRUE));
  populate_font_styles(styles, family && family[0] != '\0' ? family : "Sans",
                       preferred);
  g_object_set_data(G_OBJECT(box), "updating-font-style", NULL);

  if (preferred)
    pango_font_description_free(preferred);
  apply_font_selection(data);
}

static void font_style_changed_cb(GtkComboBox *combo, gpointer data) {
  (void)combo;

  if (g_object_get_data(G_OBJECT(data), "updating-font-style"))
    return;
  apply_font_selection(data);
}

static void font_size_changed_cb(GtkSpinButton *spin, gpointer data) {
  (void)spin;
  apply_font_selection(data);
}

static GtkWidget *font_control(PwvizVisualizer *visualizer, FontTarget target,
                               const char *initial, const char *title) {
  const char *family = font_family_name(initial);
  int fallback_size = target == FONT_LYRICS ? 12 : 13;
  PangoFontDescription *initial_desc = pango_font_description_from_string(
      initial && initial[0] != '\0' ? initial : "Sans 12");
  GtkListStore *families = font_family_completion_model();
  GtkEntryCompletion *completion = gtk_entry_completion_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *family_entry = gtk_entry_new();
  GtkWidget *style = gtk_combo_box_text_new();
  GtkWidget *size = gtk_spin_button_new_with_range(8, 64, 1);

  gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(families));
  gtk_entry_completion_set_text_column(completion, 0);
  gtk_entry_completion_set_inline_completion(completion, TRUE);
  gtk_entry_completion_set_popup_completion(completion, TRUE);
  gtk_entry_set_completion(GTK_ENTRY(family_entry), completion);
  gtk_editable_set_text(GTK_EDITABLE(family_entry), family);
  populate_font_styles(GTK_COMBO_BOX_TEXT(style), family, initial_desc);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(size),
                            font_point_size(initial, fallback_size));
  gtk_widget_set_tooltip_text(box, title);
  gtk_widget_set_hexpand(family_entry, TRUE);
  gtk_widget_set_halign(family_entry, GTK_ALIGN_FILL);
  gtk_widget_set_size_request(style, 120, -1);
  gtk_widget_set_size_request(size, 72, -1);
  gtk_box_append(GTK_BOX(box), family_entry);
  gtk_box_append(GTK_BOX(box), style);
  gtk_box_append(GTK_BOX(box), size);

  g_object_set_data(G_OBJECT(box), "visualizer", visualizer);
  g_object_set_data(G_OBJECT(box), "target", GINT_TO_POINTER(target));
  g_object_set_data(G_OBJECT(box), "family", family_entry);
  g_object_set_data(G_OBJECT(box), "style", style);
  g_object_set_data(G_OBJECT(box), "size", size);
  g_signal_connect(family_entry, "changed", G_CALLBACK(font_family_changed_cb),
                   box);
  g_signal_connect(style, "changed", G_CALLBACK(font_style_changed_cb), box);
  g_signal_connect(size, "value-changed", G_CALLBACK(font_size_changed_cb),
                   box);
  pango_font_description_free(initial_desc);
  g_object_unref(completion);
  g_object_unref(families);
  return box;
}

G_GNUC_END_IGNORE_DEPRECATIONS

static GtkWidget *build_analyzer_tab(PwvizVisualizer *visualizer) {
  GtkWidget *box = tab_box();
  GtkWidget *mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  GtkWidget *level_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  GtkWidget *bars = gtk_check_button_new_with_label("Bars");
  GtkWidget *peak = gtk_check_button_new_with_label("Peak");
  GtkWidget *flash = gtk_check_button_new_with_label("Flash");
  GtkWidget *level_peak = gtk_check_button_new_with_label("Peak");
  GtkWidget *level_average = gtk_check_button_new_with_label("Average");
  GtkWidget *falloff = gtk_spin_button_new_with_range(0, 75, 1);
  GtkWidget *display_threshold =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 0.5, 0.01);
  GtkWidget *fft_equalize = gtk_check_button_new_with_label("Equalize");
  GtkWidget *fft_envelope =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 5.0, 0.01);
  GtkWidget *fft_scale =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 25.0, 0.1);

  prepare_spin(falloff);
  prepare_scale(display_threshold, 2);
  prepare_scale(fft_envelope, 2);
  prepare_scale(fft_scale, 1);

  gtk_check_button_set_group(GTK_CHECK_BUTTON(peak), GTK_CHECK_BUTTON(bars));
  gtk_check_button_set_group(GTK_CHECK_BUTTON(flash), GTK_CHECK_BUTTON(bars));
  gtk_check_button_set_group(GTK_CHECK_BUTTON(level_average),
                             GTK_CHECK_BUTTON(level_peak));

  g_object_set_data(G_OBJECT(bars), "mode",
                    GINT_TO_POINTER(PWVIZ_ANALYZER_BARS));
  g_object_set_data(G_OBJECT(peak), "mode",
                    GINT_TO_POINTER(PWVIZ_ANALYZER_PEAK));
  g_object_set_data(G_OBJECT(flash), "mode",
                    GINT_TO_POINTER(PWVIZ_ANALYZER_FLASH));
  g_object_set_data(G_OBJECT(level_peak), "level-mode",
                    GINT_TO_POINTER(PWVIZ_LEVEL_PEAK));
  g_object_set_data(G_OBJECT(level_average), "level-mode",
                    GINT_TO_POINTER(PWVIZ_LEVEL_AVERAGE));

  gtk_check_button_set_active(
      GTK_CHECK_BUTTON(visualizer->config.analyzer_mode == PWVIZ_ANALYZER_PEAK
                           ? peak
                           : visualizer->config.analyzer_mode ==
                                     PWVIZ_ANALYZER_FLASH
                                 ? flash
                                 : bars),
      TRUE);
  gtk_check_button_set_active(
      GTK_CHECK_BUTTON(visualizer->config.level_mode == PWVIZ_LEVEL_PEAK
                           ? level_peak
                           : level_average),
      TRUE);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(falloff),
                            visualizer->config.falloff_rate);
  gtk_range_set_value(GTK_RANGE(display_threshold),
                      visualizer->config.display_threshold);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(fft_equalize),
                              visualizer->config.fft_equalize);
  gtk_range_set_value(GTK_RANGE(fft_envelope),
                      visualizer->config.fft_envelope);
  gtk_range_set_value(GTK_RANGE(fft_scale), visualizer->config.fft_scale);

  g_signal_connect(bars, "toggled", G_CALLBACK(analyzer_mode_toggled_cb),
                   visualizer);
  g_signal_connect(peak, "toggled", G_CALLBACK(analyzer_mode_toggled_cb),
                   visualizer);
  g_signal_connect(flash, "toggled", G_CALLBACK(analyzer_mode_toggled_cb),
                   visualizer);
  g_signal_connect(level_peak, "toggled", G_CALLBACK(level_mode_toggled_cb),
                   visualizer);
  g_signal_connect(level_average, "toggled", G_CALLBACK(level_mode_toggled_cb),
                   visualizer);
  g_signal_connect(falloff, "value-changed",
                   G_CALLBACK(falloff_rate_changed_cb), visualizer);
  g_signal_connect(display_threshold, "value-changed",
                   G_CALLBACK(display_threshold_changed_cb), visualizer);
  g_signal_connect(fft_equalize, "toggled",
                   G_CALLBACK(fft_equalize_toggled_cb), visualizer);
  g_signal_connect(fft_envelope, "value-changed",
                   G_CALLBACK(fft_envelope_changed_cb), visualizer);
  g_signal_connect(fft_scale, "value-changed", G_CALLBACK(fft_scale_changed_cb),
                   visualizer);

  gtk_box_append(GTK_BOX(mode_row), bars);
  gtk_box_append(GTK_BOX(mode_row), peak);
  gtk_box_append(GTK_BOX(mode_row), flash);
  gtk_box_append(GTK_BOX(level_row), level_peak);
  gtk_box_append(GTK_BOX(level_row), level_average);

  gtk_box_append(GTK_BOX(box), section_label("Output"));
  gtk_box_append(GTK_BOX(box), control_row("Display mode", mode_row));
  gtk_box_append(GTK_BOX(box), control_row("Bin calculation", level_row));
  gtk_box_append(GTK_BOX(box), control_row("Display threshold",
                                           display_threshold));
  gtk_box_append(GTK_BOX(box), control_row("Falloff", falloff));
  gtk_box_append(GTK_BOX(box), section_label("FFT"));
  gtk_box_append(GTK_BOX(box), fft_equalize);
  gtk_box_append(GTK_BOX(box), control_row("Envelope", fft_envelope));
  gtk_box_append(GTK_BOX(box), control_row("Scale", fft_scale));
  return box;
}

static GtkWidget *build_layout_tab(PwvizVisualizer *visualizer) {
  const char *anchor_labels[G_N_ELEMENTS(ANCHOR_OPTIONS) + 1];
  GtkWidget *box = tab_box();
  GtkWidget *x_margin = gtk_spin_button_new_with_range(0, 10000, 1);
  GtkWidget *y_margin = gtk_spin_button_new_with_range(0, 10000, 1);
  GtkWidget *width =
      gtk_spin_button_new_with_range(PWVIZ_MIN_WINDOW_WIDTH, 10000, 1);
  GtkWidget *height =
      gtk_spin_button_new_with_range(PWVIZ_MIN_WINDOW_HEIGHT, 10000, 1);

  prepare_spin(x_margin);
  prepare_spin(y_margin);
  prepare_spin(width);
  prepare_spin(height);

  for (guint i = 0; i < G_N_ELEMENTS(ANCHOR_OPTIONS); i++)
    anchor_labels[i] = ANCHOR_OPTIONS[i].label;
  anchor_labels[G_N_ELEMENTS(ANCHOR_OPTIONS)] = NULL;

  GtkWidget *anchor = gtk_drop_down_new_from_strings(anchor_labels);
  gtk_drop_down_set_selected(
      GTK_DROP_DOWN(anchor),
      anchor_index_for_anchor(visualizer->config.window_anchor));
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(x_margin),
                            visualizer->config.x_margin);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(y_margin),
                            visualizer->config.y_margin);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(width),
                            visualizer->config.window_width);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(height),
                            visualizer->config.window_height);

  g_signal_connect(anchor, "notify::selected", G_CALLBACK(anchor_selected_cb),
                   visualizer);
  g_signal_connect(x_margin, "value-changed",
                   G_CALLBACK(x_margin_changed_cb), visualizer);
  g_signal_connect(y_margin, "value-changed",
                   G_CALLBACK(y_margin_changed_cb), visualizer);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(window_width_changed_cb), visualizer);
  g_signal_connect(height, "value-changed",
                   G_CALLBACK(window_height_changed_cb), visualizer);

  gtk_box_append(GTK_BOX(box), section_label("Position"));
  gtk_box_append(GTK_BOX(box), control_row("Anchor", anchor));
  gtk_box_append(GTK_BOX(box),
                 paired_control_row("Margins", "X", x_margin, "Y", y_margin));
  gtk_box_append(GTK_BOX(box), section_label("Size"));
  gtk_box_append(GTK_BOX(box),
                 paired_control_row("Window", "W", width, "H", height));
  return box;
}

static GtkWidget *build_style_tab(PwvizVisualizer *visualizer) {
  GtkWidget *box = tab_box();
  GtkWidget *style_grid = gtk_grid_new();
  GtkWidget *bar_width = gtk_spin_button_new_with_range(1, 50, 1);
  GtkWidget *x_spacing = gtk_spin_button_new_with_range(0, 10, 1);
  GtkWidget *block_height = gtk_spin_button_new_with_range(1, 16, 1);
  GtkWidget *block_gap = gtk_spin_button_new_with_range(0, 12, 1);
  GtkWidget *bar_count = gtk_spin_button_new_with_range(8, PWVIZ_BAR_COUNT, 1);
  GtkWidget *auto_bars = gtk_check_button_new_with_label("Auto bars from width");
  GtkWidget *peak_change = gtk_spin_button_new_with_range(0, 255, 1);
  GtkWidget *peak_fall =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.001, 0.08, 0.001);
  GtkWidget *alpha =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
  GtkWidget *bar_alpha =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);

  gtk_grid_set_column_spacing(GTK_GRID(style_grid), 24);
  gtk_grid_set_row_spacing(GTK_GRID(style_grid), 8);
  gtk_widget_set_hexpand(style_grid, TRUE);
  prepare_spin(bar_width);
  prepare_spin(x_spacing);
  prepare_spin(block_height);
  prepare_spin(block_gap);
  prepare_spin(bar_count);
  prepare_spin(peak_change);
  prepare_scale(peak_fall, 3);
  prepare_scale(alpha, 2);
  prepare_scale(bar_alpha, 2);

  gtk_spin_button_set_value(GTK_SPIN_BUTTON(bar_count),
                            visualizer->config.bar_count);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(auto_bars),
                              visualizer->config.auto_bar_count);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(bar_width),
                            visualizer->config.bar_width);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(x_spacing),
                            visualizer->config.x_spacing);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(block_height),
                            visualizer->config.block_height);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(block_gap),
                            visualizer->config.block_gap);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(peak_change),
                            visualizer->config.peak_change_rate);
  gtk_range_set_value(GTK_RANGE(peak_fall),
                      visualizer->config.peak_fall_per_frame);
  gtk_range_set_value(GTK_RANGE(alpha), visualizer->config.background_alpha);
  gtk_range_set_value(GTK_RANGE(bar_alpha), visualizer->config.bar_alpha);

  g_signal_connect(bar_count, "value-changed",
                   G_CALLBACK(bar_count_changed_cb), visualizer);
  g_signal_connect(auto_bars, "toggled",
                   G_CALLBACK(auto_bar_count_toggled_cb), visualizer);
  g_signal_connect(bar_width, "value-changed",
                   G_CALLBACK(bar_width_changed_cb), visualizer);
  g_signal_connect(x_spacing, "value-changed",
                   G_CALLBACK(x_spacing_changed_cb), visualizer);
  g_signal_connect(block_height, "value-changed",
                   G_CALLBACK(block_height_changed_cb), visualizer);
  g_signal_connect(block_gap, "value-changed", G_CALLBACK(block_gap_changed_cb),
                   visualizer);
  g_signal_connect(peak_change, "value-changed",
                   G_CALLBACK(peak_change_rate_changed_cb), visualizer);
  g_signal_connect(peak_fall, "value-changed",
                   G_CALLBACK(peak_fall_changed_cb), visualizer);
  g_signal_connect(alpha, "value-changed",
                   G_CALLBACK(background_alpha_changed_cb), visualizer);
  g_signal_connect(bar_alpha, "value-changed",
                   G_CALLBACK(bar_alpha_changed_cb), visualizer);

  gtk_grid_attach(
      GTK_GRID(style_grid),
      classic_group(visualizer, "Frequency Bars", CLASSIC_OPTION_BAR_STYLE,
                    BAR_STYLE_OPTIONS, G_N_ELEMENTS(BAR_STYLE_OPTIONS),
                    visualizer->config.bar_style),
      0, 0, 1, 1);
  gtk_grid_attach(
      GTK_GRID(style_grid),
      classic_group(visualizer, "Background", CLASSIC_OPTION_BACKGROUND,
                    BACKGROUND_OPTIONS, G_N_ELEMENTS(BACKGROUND_OPTIONS),
                    visualizer->config.background_mode),
      1, 0, 1, 1);
  gtk_grid_attach(
      GTK_GRID(style_grid),
      classic_group(visualizer, "Peak Colour", CLASSIC_OPTION_PEAK_COLOR,
                    PEAK_COLOR_OPTIONS, G_N_ELEMENTS(PEAK_COLOR_OPTIONS),
                    visualizer->config.peak_color_mode),
      2, 0, 1, 1);
  gtk_grid_attach(
      GTK_GRID(style_grid),
      classic_group(visualizer, "Peak Motion", CLASSIC_OPTION_PEAK_MOTION,
                    PEAK_MOTION_OPTIONS, G_N_ELEMENTS(PEAK_MOTION_OPTIONS),
                    visualizer->config.peak_motion),
      3, 0, 1, 1);

  gtk_box_append(GTK_BOX(box), style_grid);
  gtk_box_append(GTK_BOX(box), section_label("Bars"));
  gtk_box_append(GTK_BOX(box), control_row("Count", bar_count));
  gtk_box_append(GTK_BOX(box), auto_bars);
  gtk_box_append(GTK_BOX(box),
                 paired_control_row("Geometry", "Width", bar_width, "X gap",
                                    x_spacing));
  gtk_box_append(GTK_BOX(box),
                 paired_control_row("Blocks", "Height", block_height, "Gap",
                                    block_gap));
  gtk_box_append(GTK_BOX(box), section_label("Peak Indicators"));
  gtk_box_append(GTK_BOX(box), control_row("Change", peak_change));
  gtk_box_append(GTK_BOX(box), control_row("Fall speed", peak_fall));
  gtk_box_append(GTK_BOX(box), section_label("Transparency"));
  gtk_box_append(GTK_BOX(box), control_row("Background alpha", alpha));
  gtk_box_append(GTK_BOX(box), control_row("Bar opacity", bar_alpha));
  return box;
}

static GtkWidget *build_colour_tab(PwvizVisualizer *visualizer) {
  GtkWidget *box = tab_box();

  gtk_box_append(GTK_BOX(box), section_label("Bars"));
  gtk_box_append(GTK_BOX(box),
                 control_row("Low colour",
                             color_control(visualizer, COLOR_LOW,
                                           &visualizer->config.low_color,
                                           "Low Bar Colour")));
  gtk_box_append(GTK_BOX(box),
                 control_row("High colour",
                             color_control(visualizer, COLOR_HIGH,
                                           &visualizer->config.high_color,
                                           "High Bar Colour")));
  gtk_box_append(GTK_BOX(box),
                 control_row("Peak colour",
                             color_control(visualizer, COLOR_PEAK,
                                           &visualizer->config.peak_color,
                                           "Peak Colour")));
  gtk_box_append(GTK_BOX(box), section_label("Window"));
  gtk_box_append(GTK_BOX(box),
                 control_row("Background",
                             color_control(visualizer, COLOR_BACKGROUND,
                                           &visualizer->config.background_color,
                                           "Background Colour")));
  return box;
}

static GtkWidget *now_playing_field_toggle(PwvizVisualizer *visualizer,
                                           const char *label,
                                           const char *field,
                                           gboolean active) {
  GtkWidget *button = gtk_check_button_new_with_label(label);

  g_object_set_data(G_OBJECT(button), "now-playing-field", (gpointer)field);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(button), active);
  g_signal_connect(button, "toggled",
                   G_CALLBACK(now_playing_show_toggled_cb), visualizer);
  return button;
}

static GtkWidget *build_now_playing_tab(PwvizVisualizer *visualizer) {
  GtkWidget *scroller = gtk_scrolled_window_new();
  GtkWidget *box = tab_box();
  GtkWidget *enabled = gtk_check_button_new_with_label("Show Now Playing");
  GtkWidget *lyrics = gtk_check_button_new_with_label("Fetch lyrics");
  GtkWidget *two_lines = gtk_check_button_new_with_label("Two lyric lines");
  GtkWidget *fields = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *height = gtk_spin_button_new_with_range(0, 160, 1);
  GtkWidget *font =
      font_control(visualizer, FONT_NOW_PLAYING,
                   visualizer->config.now_playing_font, "Now Playing Font");
  GtkWidget *lyrics_font = font_control(
      visualizer, FONT_LYRICS, visualizer->config.lyrics_font, "Lyrics Font");
  GtkWidget *outline_width = gtk_spin_button_new_with_range(0.0, 6.0, 0.1);
  GtkWidget *lyrics_outline_width =
      gtk_spin_button_new_with_range(0.0, 6.0, 0.1);
  GtkWidget *shadow_x = gtk_spin_button_new_with_range(-64.0, 64.0, 0.5);
  GtkWidget *shadow_y = gtk_spin_button_new_with_range(-64.0, 64.0, 0.5);
  GtkWidget *shadow_opacity =
      gtk_spin_button_new_with_range(0.0, 1.0, 0.05);
  GtkWidget *lyrics_shadow_x =
      gtk_spin_button_new_with_range(-64.0, 64.0, 0.5);
  GtkWidget *lyrics_shadow_y =
      gtk_spin_button_new_with_range(-64.0, 64.0, 0.5);
  GtkWidget *lyrics_shadow_opacity =
      gtk_spin_button_new_with_range(0.0, 1.0, 0.05);
  GtkWidget *alpha =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);

  prepare_spin(height);
  prepare_spin(outline_width);
  prepare_spin(lyrics_outline_width);
  prepare_spin(shadow_x);
  prepare_spin(shadow_y);
  prepare_spin(shadow_opacity);
  prepare_spin(lyrics_shadow_x);
  prepare_spin(lyrics_shadow_y);
  prepare_spin(lyrics_shadow_opacity);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(shadow_x), 1);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(shadow_y), 1);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(shadow_opacity), 2);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(lyrics_shadow_x), 1);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(lyrics_shadow_y), 1);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(lyrics_shadow_opacity), 2);
  prepare_scale(alpha, 2);

  gtk_check_button_set_active(GTK_CHECK_BUTTON(enabled),
                              visualizer->config.now_playing_enabled);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(lyrics),
                              visualizer->config.lyrics_enabled);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(two_lines),
                              visualizer->config.lyrics_two_lines);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(height),
                            visualizer->config.now_playing_height);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(outline_width),
                            visualizer->config.now_playing_outline_width);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(lyrics_outline_width),
                            visualizer->config.lyrics_outline_width);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(shadow_x),
                            visualizer->config.now_playing_shadow_x);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(shadow_y),
                            visualizer->config.now_playing_shadow_y);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(shadow_opacity),
                            visualizer->config.now_playing_shadow_opacity);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(lyrics_shadow_x),
                            visualizer->config.lyrics_shadow_x);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(lyrics_shadow_y),
                            visualizer->config.lyrics_shadow_y);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(lyrics_shadow_opacity),
                            visualizer->config.lyrics_shadow_opacity);
  gtk_range_set_value(GTK_RANGE(alpha), visualizer->config.now_playing_alpha);

  gtk_box_append(GTK_BOX(fields),
                 now_playing_field_toggle(
                     visualizer, "App", "app",
                     visualizer->config.now_playing_show_app));
  gtk_box_append(GTK_BOX(fields),
                 now_playing_field_toggle(
                     visualizer, "Title", "title",
                     visualizer->config.now_playing_show_title));
  gtk_box_append(GTK_BOX(fields),
                 now_playing_field_toggle(
                     visualizer, "Artist", "artist",
                     visualizer->config.now_playing_show_artist));
  gtk_box_append(GTK_BOX(fields),
                 now_playing_field_toggle(
                     visualizer, "Album", "album",
                     visualizer->config.now_playing_show_album));

  g_signal_connect(enabled, "toggled",
                   G_CALLBACK(now_playing_enabled_toggled_cb), visualizer);
  g_signal_connect(lyrics, "toggled", G_CALLBACK(lyrics_enabled_toggled_cb),
                   visualizer);
  g_signal_connect(two_lines, "toggled",
                   G_CALLBACK(lyrics_two_lines_toggled_cb), visualizer);
  g_signal_connect(height, "value-changed",
                   G_CALLBACK(now_playing_height_changed_cb), visualizer);
  g_signal_connect(outline_width, "value-changed",
                   G_CALLBACK(now_playing_outline_width_changed_cb),
                   visualizer);
  g_signal_connect(lyrics_outline_width, "value-changed",
                   G_CALLBACK(lyrics_outline_width_changed_cb), visualizer);
  g_signal_connect(shadow_x, "value-changed",
                   G_CALLBACK(now_playing_shadow_x_changed_cb), visualizer);
  g_signal_connect(shadow_y, "value-changed",
                   G_CALLBACK(now_playing_shadow_y_changed_cb), visualizer);
  g_signal_connect(shadow_opacity, "value-changed",
                   G_CALLBACK(now_playing_shadow_opacity_changed_cb),
                   visualizer);
  g_signal_connect(lyrics_shadow_x, "value-changed",
                   G_CALLBACK(lyrics_shadow_x_changed_cb), visualizer);
  g_signal_connect(lyrics_shadow_y, "value-changed",
                   G_CALLBACK(lyrics_shadow_y_changed_cb), visualizer);
  g_signal_connect(lyrics_shadow_opacity, "value-changed",
                   G_CALLBACK(lyrics_shadow_opacity_changed_cb), visualizer);
  g_signal_connect(alpha, "value-changed",
                   G_CALLBACK(now_playing_alpha_changed_cb), visualizer);

  gtk_box_append(GTK_BOX(box), section_label("Metadata"));
  gtk_box_append(GTK_BOX(box), enabled);
  gtk_box_append(GTK_BOX(box), control_row("Fields", fields));
  gtk_box_append(GTK_BOX(box), control_row("Font", font));
  gtk_box_append(GTK_BOX(box), control_row("Outline width", outline_width));
  gtk_box_append(
      GTK_BOX(box),
      paired_control_row(
          "Colours", "Text",
          color_control(visualizer, COLOR_NOW_PLAYING_TEXT,
                        &visualizer->config.now_playing_text_color,
                        "Now Playing Text Colour"),
          "Outline",
          color_control(visualizer, COLOR_NOW_PLAYING_OUTLINE,
                        &visualizer->config.now_playing_outline_color,
                        "Now Playing Outline Colour")));
  gtk_box_append(GTK_BOX(box),
                 paired_control_row("Shadow offset", "X", shadow_x, "Y",
                                    shadow_y));
  gtk_box_append(
      GTK_BOX(box),
      paired_control_row("Shadow", "Colour",
                         color_control(visualizer, COLOR_NOW_PLAYING_SHADOW,
                                       &visualizer->config
                                            .now_playing_shadow_color,
                                       "Now Playing Shadow Colour"),
                         "Opacity", shadow_opacity));
  gtk_box_append(GTK_BOX(box), section_label("Lyrics"));
  gtk_box_append(GTK_BOX(box), lyrics);
  gtk_box_append(GTK_BOX(box), two_lines);
  gtk_box_append(GTK_BOX(box), control_row("Font", lyrics_font));
  gtk_box_append(GTK_BOX(box),
                 control_row("Outline width", lyrics_outline_width));
  gtk_box_append(
      GTK_BOX(box),
      paired_control_row(
          "Colours", "Text",
          color_control(visualizer, COLOR_LYRICS_TEXT,
                        &visualizer->config.lyrics_text_color,
                        "Lyrics Text Colour"),
          "Outline",
          color_control(visualizer, COLOR_LYRICS_OUTLINE,
                        &visualizer->config.lyrics_outline_color,
                        "Lyrics Outline Colour")));
  gtk_box_append(GTK_BOX(box),
                 paired_control_row("Shadow offset", "X", lyrics_shadow_x, "Y",
                                    lyrics_shadow_y));
  gtk_box_append(
      GTK_BOX(box),
      paired_control_row(
          "Shadow", "Colour",
          color_control(visualizer, COLOR_LYRICS_SHADOW,
                        &visualizer->config.lyrics_shadow_color,
                        "Lyrics Shadow Colour"),
          "Opacity", lyrics_shadow_opacity));
  gtk_box_append(GTK_BOX(box), section_label("Bottom Section"));
  gtk_box_append(GTK_BOX(box),
                 control_row("Height", height));
  gtk_box_append(GTK_BOX(box), control_row("Background alpha", alpha));
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), box);
  return scroller;
}

static gboolean config_close_request_cb(GtkWindow *window, gpointer data) {
  (void)window;

  PwvizVisualizer *visualizer = data;

  visualizer->config = visualizer->config_snapshot;
  apply_window_geometry(visualizer);
  apply_layer_position(visualizer);
  queue_visualizer_draw(visualizer);
  visualizer->config_window = NULL;
  return FALSE;
}

static void config_destroy_cb(GtkWindow *window, gpointer data) {
  (void)window;

  PwvizVisualizer *visualizer = data;

  if (visualizer->config_window == window)
    visualizer->config_window = NULL;
}

static void config_save_clicked_cb(GtkButton *button, gpointer data) {
  (void)button;
  save_current_config(data);
}

static void config_ok_clicked_cb(GtkButton *button, gpointer data) {
  (void)button;

  PwvizVisualizer *visualizer = data;
  save_current_config(visualizer);
  gtk_window_destroy(visualizer->config_window);
}

static void config_cancel_clicked_cb(GtkButton *button, gpointer data) {
  (void)button;

  PwvizVisualizer *visualizer = data;
  visualizer->config = visualizer->config_snapshot;
  apply_window_geometry(visualizer);
  apply_layer_position(visualizer);
  queue_visualizer_draw(visualizer);
  gtk_window_destroy(visualizer->config_window);
}

static void show_config_window(PwvizVisualizer *visualizer) {
  if (visualizer->config_window) {
    gtk_window_present(visualizer->config_window);
    return;
  }

  GtkApplication *app = gtk_window_get_application(visualizer->window);
  GtkWidget *window = gtk_application_window_new(app);
  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  GtkWidget *notebook = gtk_notebook_new();
  GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *save = gtk_button_new_with_label("Save");
  GtkWidget *ok = gtk_button_new_with_label("OK");
  GtkWidget *cancel = gtk_button_new_with_label("Cancel");

  visualizer->config_snapshot = visualizer->config;
  visualizer->config_window = GTK_WINDOW(window);

  gtk_window_set_title(GTK_WINDOW(window), "Classic Spectrum Analyzer Settings");
  gtk_window_set_default_size(GTK_WINDOW(window), 680, 460);
  gtk_window_set_transient_for(GTK_WINDOW(window), visualizer->window);

  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                           build_analyzer_tab(visualizer),
                           gtk_label_new("Analyzer"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_layout_tab(visualizer),
                           gtk_label_new("Layout"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_style_tab(visualizer),
                           gtk_label_new("Style"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_colour_tab(visualizer),
                           gtk_label_new("Colour Factory"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                           build_now_playing_tab(visualizer),
                           gtk_label_new("Now Playing"));

  gtk_widget_set_margin_top(root, 8);
  gtk_widget_set_margin_bottom(root, 8);
  gtk_widget_set_margin_start(root, 8);
  gtk_widget_set_margin_end(root, 8);
  gtk_widget_set_hexpand(notebook, TRUE);
  gtk_widget_set_vexpand(notebook, TRUE);

  gtk_widget_set_halign(buttons, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(buttons), save);
  gtk_box_append(GTK_BOX(buttons), ok);
  gtk_box_append(GTK_BOX(buttons), cancel);
  gtk_box_append(GTK_BOX(root), notebook);
  gtk_box_append(GTK_BOX(root), buttons);
  gtk_window_set_child(GTK_WINDOW(window), root);

  g_signal_connect(window, "close-request",
                   G_CALLBACK(config_close_request_cb), visualizer);
  g_signal_connect(window, "destroy", G_CALLBACK(config_destroy_cb),
                   visualizer);
  g_signal_connect(save, "clicked", G_CALLBACK(config_save_clicked_cb),
                   visualizer);
  g_signal_connect(ok, "clicked", G_CALLBACK(config_ok_clicked_cb),
                   visualizer);
  g_signal_connect(cancel, "clicked", G_CALLBACK(config_cancel_clicked_cb),
                   visualizer);

  gtk_window_present(GTK_WINDOW(window));
}

static gboolean adjust_lyrics_offset(PwvizVisualizer *visualizer,
                                     gint64 delta_ms) {
  if (!visualizer || !visualizer->lyrics ||
      !pwviz_lyrics_adjust_offset(visualizer->lyrics, delta_ms))
    return FALSE;

  g_message("Lyrics offset: %" G_GINT64_FORMAT " ms",
            visualizer->lyrics->offset_ms);
  queue_visualizer_draw(visualizer);
  return TRUE;
}

static void global_shortcut_cb(PwvizGlobalShortcutAction action,
                               gpointer data) {
  PwvizVisualizer *visualizer = data;

  switch (action) {
  case PWVIZ_GLOBAL_SHORTCUT_OPEN_SETTINGS:
    show_config_window(visualizer);
    break;
  case PWVIZ_GLOBAL_SHORTCUT_LYRICS_OFFSET_BACK:
    adjust_lyrics_offset(visualizer, -LYRICS_OFFSET_STEP_MS);
    break;
  case PWVIZ_GLOBAL_SHORTCUT_LYRICS_OFFSET_FORWARD:
    adjust_lyrics_offset(visualizer, LYRICS_OFFSET_STEP_MS);
    break;
  }
}

static gboolean key_pressed_cb(GtkEventControllerKey *controller, guint keyval,
                               guint keycode, GdkModifierType state,
                               gpointer data) {
  (void)controller;
  (void)keycode;

  gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
  gboolean alt = (state & GDK_ALT_MASK) != 0;
  gboolean shift = (state & GDK_SHIFT_MASK) != 0;

  if (ctrl && alt && shift && keyval == GDK_KEY_F12) {
    show_config_window(data);
    return TRUE;
  }

  if (ctrl && shift && !alt && keyval == GDK_KEY_Left)
    return adjust_lyrics_offset(data, -LYRICS_OFFSET_STEP_MS);
  if (ctrl && shift && !alt && keyval == GDK_KEY_Right)
    return adjust_lyrics_offset(data, LYRICS_OFFSET_STEP_MS);

  return FALSE;
}

static void apply_layer_position(PwvizVisualizer *visualizer) {
  if (!visualizer || visualizer->destroying || !visualizer->window ||
      !GTK_IS_WINDOW(visualizer->window))
    return;

  gboolean top = FALSE;
  gboolean bottom = FALSE;
  gboolean left = FALSE;
  gboolean right = FALSE;

  switch (visualizer->config.window_anchor) {
  case PWVIZ_ANCHOR_TOP_LEFT:
    top = TRUE;
    left = TRUE;
    break;
  case PWVIZ_ANCHOR_TOP:
    top = TRUE;
    break;
  case PWVIZ_ANCHOR_TOP_RIGHT:
    top = TRUE;
    right = TRUE;
    break;
  case PWVIZ_ANCHOR_LEFT:
    left = TRUE;
    break;
  case PWVIZ_ANCHOR_CENTER:
    break;
  case PWVIZ_ANCHOR_RIGHT:
    right = TRUE;
    break;
  case PWVIZ_ANCHOR_BOTTOM_LEFT:
    bottom = TRUE;
    left = TRUE;
    break;
  case PWVIZ_ANCHOR_BOTTOM:
    bottom = TRUE;
    break;
  case PWVIZ_ANCHOR_BOTTOM_RIGHT:
    bottom = TRUE;
    right = TRUE;
    break;
  }

  gtk_layer_set_anchor(visualizer->window, GTK_LAYER_SHELL_EDGE_TOP, top);
  gtk_layer_set_anchor(visualizer->window, GTK_LAYER_SHELL_EDGE_BOTTOM,
                       bottom);
  gtk_layer_set_anchor(visualizer->window, GTK_LAYER_SHELL_EDGE_LEFT, left);
  gtk_layer_set_anchor(visualizer->window, GTK_LAYER_SHELL_EDGE_RIGHT, right);

  gtk_layer_set_margin(visualizer->window, GTK_LAYER_SHELL_EDGE_TOP,
                       top ? visualizer->config.y_margin : 0);
  gtk_layer_set_margin(visualizer->window, GTK_LAYER_SHELL_EDGE_BOTTOM,
                       bottom ? visualizer->config.y_margin : 0);
  gtk_layer_set_margin(visualizer->window, GTK_LAYER_SHELL_EDGE_LEFT,
                       left ? visualizer->config.x_margin : 0);
  gtk_layer_set_margin(visualizer->window, GTK_LAYER_SHELL_EDGE_RIGHT,
                       right ? visualizer->config.x_margin : 0);
}

static void apply_window_geometry(PwvizVisualizer *visualizer) {
  if (!visualizer || visualizer->destroying || !visualizer->window ||
      !GTK_IS_WINDOW(visualizer->window))
    return;

  gtk_window_set_default_size(GTK_WINDOW(visualizer->window),
                              visualizer->config.window_width,
                              visualizer->config.window_height);

  if (visualizer->drawing_area) {
    gtk_drawing_area_set_content_width(
        GTK_DRAWING_AREA(visualizer->drawing_area),
        visualizer->config.window_width);
    gtk_drawing_area_set_content_height(
        GTK_DRAWING_AREA(visualizer->drawing_area),
        visualizer->config.window_height);
  }
}

static void install_transparent_window_css(void) {
  GtkCssProvider *provider = gtk_css_provider_new();

  gtk_css_provider_load_from_string(
      provider,
      "window.pipewire-visualizer-window, .pipewire-visualizer-overlay { "
      "background: transparent; } "
      "scale.pwviz-scale slider { min-width: 18px; min-height: 18px; }");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

static void visualizer_init(PwvizVisualizer *visualizer,
                            PwvizAudioBuffer *audio_buffer,
                            GtkWidget *window) {
  visualizer->window = GTK_WINDOW(window);
  visualizer->audio_buffer = audio_buffer;
  visualizer->lyrics_results = g_async_queue_new();

  pwviz_app_config_load(&visualizer->config);
  visualizer->config_snapshot = visualizer->config;
  if (visualizer->config.now_playing_enabled) {
    pwviz_now_playing_refresh(&visualizer->now_playing);
    maybe_start_lyrics_fetch(visualizer);
  }
  pwviz_fft_init(&visualizer->fft);
  pwviz_binner_init(&visualizer->binner);
}

static void visualizer_free(PwvizVisualizer *visualizer) {
  if (!visualizer)
    return;

  visualizer->destroying = TRUE;
  if (visualizer->now_playing_source)
    g_source_remove(visualizer->now_playing_source);
  if (visualizer->config_window && GTK_IS_WINDOW(visualizer->config_window)) {
    GtkWindow *config_window = visualizer->config_window;

    visualizer->config_window = NULL;
    gtk_window_destroy(config_window);
  }
  if (visualizer->lyrics_results) {
    poll_lyrics_results(visualizer);
    g_async_queue_unref(visualizer->lyrics_results);
  }
  pwviz_lyrics_free(visualizer->lyrics);
  pwviz_global_shortcut_free(visualizer->global_shortcut);
  visualizer->drawing_area = NULL;
  visualizer->window = NULL;
  g_free(visualizer);
}

static void activate(GtkApplication *app, gpointer user_data) {
  PwvizAudioBuffer *audio_buffer = user_data;
  GtkWidget *window = gtk_application_window_new(app);
  PwvizVisualizer *visualizer = g_new0(PwvizVisualizer, 1);

  visualizer_init(visualizer, audio_buffer, window);

  gtk_window_set_title(GTK_WINDOW(window), "pipewire-visualizer");
  gtk_widget_add_css_class(window, "pipewire-visualizer-window");
  gtk_window_set_default_size(GTK_WINDOW(window),
                              visualizer->config.window_width,
                              visualizer->config.window_height);
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

  gtk_layer_init_for_window(GTK_WINDOW(window));
  gtk_layer_set_namespace(GTK_WINDOW(window), "pipewire-visualizer");
  gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window),
                              GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  gtk_layer_set_exclusive_zone(GTK_WINDOW(window), 0);
  apply_layer_position(visualizer);

  install_transparent_window_css();

  GtkWidget *overlay = gtk_overlay_new();
  gtk_widget_add_css_class(overlay, "pipewire-visualizer-overlay");

  GtkWidget *area = gtk_drawing_area_new();
  visualizer->drawing_area = area;
  apply_window_geometry(visualizer);
  gtk_widget_set_can_target(area, FALSE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_cb, visualizer,
                                 NULL);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), area);

  gtk_window_set_child(GTK_WINDOW(window), overlay);

  GtkEventController *key_controller = gtk_event_controller_key_new();
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(key_pressed_cb),
                   visualizer);
  gtk_widget_add_controller(window, key_controller);

  visualizer->global_shortcut =
      pwviz_global_shortcut_register(global_shortcut_cb, visualizer);
  visualizer->now_playing_source =
      g_timeout_add_seconds(1, now_playing_refresh_cb, visualizer);

  gtk_widget_add_tick_callback(area, tick_cb, NULL, NULL);
  g_signal_connect_swapped(window, "map", G_CALLBACK(update_input_region),
                           visualizer);
  g_signal_connect_swapped(window, "destroy", G_CALLBACK(visualizer_free),
                           visualizer);

  gtk_window_present(GTK_WINDOW(window));
}

int pwviz_visualization_run(PwvizAudioBuffer *audio_buffer, int argc,
                            char **argv) {
  GtkApplication *app =
      gtk_application_new("local.pipewire_visualizer",
                          G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), audio_buffer);

  int status = g_application_run(G_APPLICATION(app), argc, argv);

  g_object_unref(app);
  return status;
}
