#include "visualization.h"

#include "app_config.h"
#include "binning.h"
#include "config.h"
#include "fft.h"
#include "global_shortcut.h"

#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <math.h>

typedef struct {
  GtkWindow *window;
  GtkWindow *config_window;
  PwvizAudioBuffer *audio_buffer;
  PwvizGlobalShortcut *global_shortcut;
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
  int x;
  int y;
  int width;
  int height;
} PwvizVisualizer;

typedef enum {
  COLOR_LOW,
  COLOR_HIGH,
  COLOR_PEAK,
  COLOR_BACKGROUND,
} ColorTarget;

enum {
  BACKGROUND_ALPHA = 0,
  INACTIVE_BAR_ALPHA = 0,
  ACTIVE_BAR_ALPHA = 35,
  FLASH_BAR_ALPHA = 35,
  PEAK_ALPHA = 35,
  BORDER_ALPHA = 0,
};

typedef struct {
  PwvizVisualizer *visualizer;
  ColorTarget target;
} ColorBinding;

static double mix(double a, double b, double t) {
  return a + (b - a) * t;
}

static double pct_alpha(int percent) {
  return percent / 100.0;
}

static void queue_visualizer_draw(PwvizVisualizer *visualizer) {
  gtk_widget_queue_draw(GTK_WIDGET(visualizer->window));
}

static void update_input_region(PwvizVisualizer *visualizer) {
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
                    visualizer->magnitudes);
  pwviz_binner_calculate(&visualizer->binner, visualizer->magnitudes,
                         visualizer->levels);

  for (int i = 0; i < PWVIZ_BAR_COUNT; i++) {
    float value = visualizer->levels[i];

    if (value < visualizer->config.display_threshold)
      value = 0.0f;

    if (value > visualizer->bars[i])
      visualizer->bars[i] = visualizer->bars[i] * 0.4f + value * 0.6f;
    else {
      float falloff = value > 0.0f ? 0.85f : 0.6f;
      visualizer->bars[i] = visualizer->bars[i] * falloff + value * 0.15f;
    }
  }
}

static void draw_spectrum(PwvizVisualizer *visualizer, cairo_t *cr, int width,
                          int height) {
  double visual_top = PWVIZ_SPECTRUM_TOP_PADDING;
  double visual_bottom = height - PWVIZ_SPECTRUM_BOTTOM_PADDING;
  double visual_height = visual_bottom - visual_top;
  int bar_count = CLAMP(visualizer->config.bar_count, 1, PWVIZ_BAR_COUNT);
  double bar_w = (double)width / bar_count;
  double block_h = visualizer->config.block_height;
  double block_gap = visualizer->config.block_gap;

  if (visual_height <= block_h)
    return;

  if (visualizer->config.show_border) {
    cairo_set_source_rgba(cr, visualizer->config.high_color.red,
                          visualizer->config.high_color.green,
                          visualizer->config.high_color.blue,
                          pct_alpha(BORDER_ALPHA));
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, visual_top - 0.5, width - 1.0,
                    visual_height + 1.0);
    cairo_stroke(cr);
  }

  for (int i = 0; i < bar_count; i++) {
    if (visualizer->bars[i] > visualizer->peak_caps[i]) {
      visualizer->peak_caps[i] = visualizer->bars[i];
      visualizer->peak_holds[i] = visualizer->config.peak_hold_frames;
    } else if (visualizer->peak_holds[i] > 0) {
      visualizer->peak_holds[i]--;
    } else {
      visualizer->peak_caps[i] =
          MAX(0.0f, visualizer->peak_caps[i] -
                         visualizer->config.peak_fall_per_frame);
    }

    double bar_value = visualizer->config.analyzer_mode == PWVIZ_ANALYZER_PEAK
                           ? visualizer->peak_caps[i]
                           : visualizer->bars[i];
    double h = bar_value * visual_height;
    double x = i * bar_w;
    double lit_top = visual_bottom - h;
    double block_w = MAX(1.0, bar_w - 2.0);

    for (double y = visual_bottom - block_h; y >= visual_top;
         y -= block_h + block_gap) {
      double level = (visual_bottom - y) / visual_height;
      gboolean lit = y >= lit_top;
      double red = mix(visualizer->config.low_color.red,
                       visualizer->config.high_color.red, level);
      double green = mix(visualizer->config.low_color.green,
                         visualizer->config.high_color.green, level);
      double blue = mix(visualizer->config.low_color.blue,
                        visualizer->config.high_color.blue, level);
      double alpha = lit ? pct_alpha(ACTIVE_BAR_ALPHA)
                         : pct_alpha(INACTIVE_BAR_ALPHA);

      if (visualizer->config.analyzer_mode == PWVIZ_ANALYZER_FLASH &&
          visualizer->bars[i] > 0.75f)
        alpha = pct_alpha(FLASH_BAR_ALPHA);

      cairo_set_source_rgba(cr, red, green, blue, alpha);
      cairo_rectangle(cr, x + 1.0, y, block_w, block_h);
      cairo_fill(cr);
    }

    if (visualizer->config.analyzer_mode == PWVIZ_ANALYZER_FLASH)
      continue;

    double peak_y = visual_bottom - visualizer->peak_caps[i] * visual_height;
    double snapped_peak_y =
        visual_bottom -
        floor((visual_bottom - peak_y) / (block_h + block_gap)) *
            (block_h + block_gap);

    cairo_set_source_rgba(cr, visualizer->config.peak_color.red,
                          visualizer->config.peak_color.green,
                          visualizer->config.peak_color.blue,
                          pct_alpha(PEAK_ALPHA));
    cairo_rectangle(cr, x + 1.0,
                    CLAMP(snapped_peak_y, visual_top, visual_bottom - block_h),
                    block_w, block_h);
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

  cairo_set_source_rgba(cr, visualizer->config.background_color.red,
                        visualizer->config.background_color.green,
                        visualizer->config.background_color.blue,
                        pct_alpha(BACKGROUND_ALPHA));
  cairo_paint(cr);
  draw_spectrum(visualizer, cr, width, height);
}

static gboolean tick_cb(GtkWidget *widget, GdkFrameClock *clock,
                        gpointer data) {
  (void)clock;
  (void)data;

  gtk_widget_queue_draw(widget);
  return G_SOURCE_CONTINUE;
}

static void save_current_config(PwvizVisualizer *visualizer) {
  pwviz_app_config_save(&visualizer->config);
  visualizer->config_snapshot = visualizer->config;
}

static GtkWidget *control_row(const char *label, GtkWidget *control) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *text = gtk_label_new(label);

  gtk_widget_set_hexpand(control, TRUE);
  gtk_widget_set_halign(text, GTK_ALIGN_START);
  gtk_widget_set_hexpand(text, TRUE);
  gtk_box_append(GTK_BOX(row), text);
  gtk_box_append(GTK_BOX(row), control);
  return row;
}

static GtkWidget *tab_box(void) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

  gtk_widget_set_margin_top(box, 12);
  gtk_widget_set_margin_bottom(box, 12);
  gtk_widget_set_margin_start(box, 12);
  gtk_widget_set_margin_end(box, 12);
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

static void bar_count_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.bar_count = gtk_spin_button_get_value_as_int(spin);
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

static void peak_hold_changed_cb(GtkSpinButton *spin, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.peak_hold_frames = gtk_spin_button_get_value_as_int(spin);
}

static void peak_fall_changed_cb(GtkRange *range, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.peak_fall_per_frame = gtk_range_get_value(range);
}

static void display_threshold_changed_cb(GtkRange *range, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.display_threshold = gtk_range_get_value(range);
  queue_visualizer_draw(visualizer);
}

static void alpha_changed_cb(GtkRange *range, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.background_alpha = gtk_range_get_value(range);
  queue_visualizer_draw(visualizer);
}

static void show_border_toggled_cb(GtkCheckButton *button, gpointer data) {
  PwvizVisualizer *visualizer = data;

  visualizer->config.show_border = gtk_check_button_get_active(button);
  queue_visualizer_draw(visualizer);
}

static void color_changed_cb(GtkColorDialogButton *button, gpointer data) {
  ColorBinding *binding = data;
  const GdkRGBA *color = gtk_color_dialog_button_get_rgba(button);

  switch (binding->target) {
  case COLOR_LOW:
    binding->visualizer->config.low_color = *color;
    break;
  case COLOR_HIGH:
    binding->visualizer->config.high_color = *color;
    break;
  case COLOR_PEAK:
    binding->visualizer->config.peak_color = *color;
    break;
  case COLOR_BACKGROUND:
    binding->visualizer->config.background_color = *color;
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
  GtkColorDialog *dialog = gtk_color_dialog_new();
  GtkWidget *button = gtk_color_dialog_button_new(dialog);
  ColorBinding *binding = g_new0(ColorBinding, 1);

  binding->visualizer = visualizer;
  binding->target = target;

  gtk_color_dialog_set_title(dialog, title);
  gtk_color_dialog_set_with_alpha(dialog, TRUE);
  gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(button), initial);
  g_signal_connect_data(button, "notify::rgba", G_CALLBACK(color_changed_cb),
                        binding, free_color_binding, 0);
  return button;
}

static GtkWidget *build_analyzer_tab(PwvizVisualizer *visualizer) {
  GtkWidget *box = tab_box();
  GtkWidget *bars = gtk_check_button_new_with_label("Bars");
  GtkWidget *peak = gtk_check_button_new_with_label("Peak");
  GtkWidget *flash = gtk_check_button_new_with_label("Flash");
  GtkWidget *bar_count = gtk_spin_button_new_with_range(8, PWVIZ_BAR_COUNT, 1);
  GtkWidget *peak_hold = gtk_spin_button_new_with_range(0, 120, 1);
  GtkWidget *peak_fall =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.001, 0.08, 0.001);
  GtkWidget *display_threshold =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 0.5, 0.01);

  gtk_check_button_set_group(GTK_CHECK_BUTTON(peak), GTK_CHECK_BUTTON(bars));
  gtk_check_button_set_group(GTK_CHECK_BUTTON(flash), GTK_CHECK_BUTTON(bars));

  g_object_set_data(G_OBJECT(bars), "mode",
                    GINT_TO_POINTER(PWVIZ_ANALYZER_BARS));
  g_object_set_data(G_OBJECT(peak), "mode",
                    GINT_TO_POINTER(PWVIZ_ANALYZER_PEAK));
  g_object_set_data(G_OBJECT(flash), "mode",
                    GINT_TO_POINTER(PWVIZ_ANALYZER_FLASH));

  gtk_check_button_set_active(
      GTK_CHECK_BUTTON(visualizer->config.analyzer_mode == PWVIZ_ANALYZER_PEAK
                           ? peak
                           : visualizer->config.analyzer_mode ==
                                     PWVIZ_ANALYZER_FLASH
                                 ? flash
                                 : bars),
      TRUE);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(bar_count),
                            visualizer->config.bar_count);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(peak_hold),
                            visualizer->config.peak_hold_frames);
  gtk_range_set_value(GTK_RANGE(peak_fall),
                      visualizer->config.peak_fall_per_frame);
  gtk_range_set_value(GTK_RANGE(display_threshold),
                      visualizer->config.display_threshold);

  g_signal_connect(bars, "toggled", G_CALLBACK(analyzer_mode_toggled_cb),
                   visualizer);
  g_signal_connect(peak, "toggled", G_CALLBACK(analyzer_mode_toggled_cb),
                   visualizer);
  g_signal_connect(flash, "toggled", G_CALLBACK(analyzer_mode_toggled_cb),
                   visualizer);
  g_signal_connect(bar_count, "value-changed",
                   G_CALLBACK(bar_count_changed_cb), visualizer);
  g_signal_connect(peak_hold, "value-changed",
                   G_CALLBACK(peak_hold_changed_cb), visualizer);
  g_signal_connect(peak_fall, "value-changed",
                   G_CALLBACK(peak_fall_changed_cb), visualizer);
  g_signal_connect(display_threshold, "value-changed",
                   G_CALLBACK(display_threshold_changed_cb), visualizer);

  gtk_box_append(GTK_BOX(box), bars);
  gtk_box_append(GTK_BOX(box), peak);
  gtk_box_append(GTK_BOX(box), flash);
  gtk_box_append(GTK_BOX(box), control_row("Bars", bar_count));
  gtk_box_append(GTK_BOX(box), control_row("Peak hold", peak_hold));
  gtk_box_append(GTK_BOX(box), control_row("Peak fall speed", peak_fall));
  gtk_box_append(GTK_BOX(box),
                 control_row("Display threshold", display_threshold));
  return box;
}

static GtkWidget *build_style_tab(PwvizVisualizer *visualizer) {
  GtkWidget *box = tab_box();
  GtkWidget *block_height = gtk_spin_button_new_with_range(1, 16, 1);
  GtkWidget *block_gap = gtk_spin_button_new_with_range(0, 12, 1);
  GtkWidget *alpha =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
  GtkWidget *border = gtk_check_button_new_with_label("Show analyzer border");

  gtk_spin_button_set_value(GTK_SPIN_BUTTON(block_height),
                            visualizer->config.block_height);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(block_gap),
                            visualizer->config.block_gap);
  gtk_range_set_value(GTK_RANGE(alpha), visualizer->config.background_alpha);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(border),
                              visualizer->config.show_border);

  g_signal_connect(block_height, "value-changed",
                   G_CALLBACK(block_height_changed_cb), visualizer);
  g_signal_connect(block_gap, "value-changed", G_CALLBACK(block_gap_changed_cb),
                   visualizer);
  g_signal_connect(alpha, "value-changed", G_CALLBACK(alpha_changed_cb),
                   visualizer);
  g_signal_connect(border, "toggled", G_CALLBACK(show_border_toggled_cb),
                   visualizer);

  gtk_box_append(GTK_BOX(box), control_row("Block height", block_height));
  gtk_box_append(GTK_BOX(box), control_row("Block gap", block_gap));
  gtk_box_append(GTK_BOX(box), control_row("Background alpha", alpha));
  gtk_box_append(GTK_BOX(box), border);
  return box;
}

static GtkWidget *build_colour_tab(PwvizVisualizer *visualizer) {
  GtkWidget *box = tab_box();

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
  gtk_box_append(GTK_BOX(box),
                 control_row("Background",
                             color_control(visualizer, COLOR_BACKGROUND,
                                           &visualizer->config.background_color,
                                           "Background Colour")));
  return box;
}

static void profile_row_activated_cb(GtkListBox *box, GtkListBoxRow *row,
                                     gpointer data) {
  (void)box;

  PwvizVisualizer *visualizer = data;
  const char *profile = g_object_get_data(G_OBJECT(row), "profile");

  if (profile) {
    pwviz_app_config_apply_profile(&visualizer->config, profile);
    queue_visualizer_draw(visualizer);
  }
}

static GtkWidget *build_profiles_tab(PwvizVisualizer *visualizer) {
  static const char *profiles[] = {
      "Default Red & Yellow",
      "Classic",
      "Classic LED",
      "Blue Flames",
      "Blue on Grey",
      "Flames",
      "Lavender Pink Tips",
      "LCD",
      "Northern Lights",
      "Purple Neon",
  };
  GtkWidget *box = tab_box();
  GtkWidget *list = gtk_list_box_new();

  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
  gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);

  for (guint i = 0; i < G_N_ELEMENTS(profiles); i++) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *label = gtk_label_new(profiles[i]);

    gtk_widget_set_margin_top(label, 4);
    gtk_widget_set_margin_bottom(label, 4);
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_end(label, 6);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    g_object_set_data(G_OBJECT(row), "profile", (gpointer)profiles[i]);
    gtk_list_box_append(GTK_LIST_BOX(list), row);

    if (g_strcmp0(visualizer->config.profile_name, profiles[i]) == 0)
      gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(row));
  }

  g_signal_connect(list, "row-activated", G_CALLBACK(profile_row_activated_cb),
                   visualizer);

  gtk_box_append(GTK_BOX(box), gtk_label_new("Saved Profiles"));
  gtk_box_append(GTK_BOX(box), list);
  gtk_box_append(GTK_BOX(box),
                 gtk_label_new("Select a profile to load its colours. Use Save "
                               "or OK to persist the current settings."));
  return box;
}

static gboolean config_close_request_cb(GtkWindow *window, gpointer data) {
  (void)window;

  PwvizVisualizer *visualizer = data;

  visualizer->config = visualizer->config_snapshot;
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
  gtk_window_set_default_size(GTK_WINDOW(window), 460, 360);
  gtk_window_set_transient_for(GTK_WINDOW(window), visualizer->window);

  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                           build_analyzer_tab(visualizer),
                           gtk_label_new("Analyzer"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_style_tab(visualizer),
                           gtk_label_new("Style"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_colour_tab(visualizer),
                           gtk_label_new("Colour Factory"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                           build_profiles_tab(visualizer),
                           gtk_label_new("Profiles"));

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

static void show_config_window_cb(gpointer data) {
  show_config_window(data);
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

  return FALSE;
}

static void apply_layer_position(PwvizVisualizer *visualizer) {
  gtk_layer_set_margin(visualizer->window, GTK_LAYER_SHELL_EDGE_RIGHT,
                       visualizer->x);
  gtk_layer_set_margin(visualizer->window, GTK_LAYER_SHELL_EDGE_BOTTOM,
                       visualizer->y);
}

static void install_transparent_window_css(void) {
  GtkCssProvider *provider = gtk_css_provider_new();

  gtk_css_provider_load_from_string(
      provider,
      "window.pipewire-visualizer-window, .pipewire-visualizer-overlay { "
      "background: transparent; }");
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
  visualizer->width = 900;
  visualizer->height = 240;
  visualizer->x = 0;
  visualizer->y = 0;

  pwviz_app_config_load(&visualizer->config);
  visualizer->config_snapshot = visualizer->config;
  pwviz_fft_init(&visualizer->fft);
  pwviz_binner_init(&visualizer->binner);
}

static void visualizer_free(PwvizVisualizer *visualizer) {
  if (!visualizer)
    return;

  pwviz_global_shortcut_free(visualizer->global_shortcut);
  g_free(visualizer);
}

static void activate(GtkApplication *app, gpointer user_data) {
  PwvizAudioBuffer *audio_buffer = user_data;
  GtkWidget *window = gtk_application_window_new(app);
  PwvizVisualizer *visualizer = g_new0(PwvizVisualizer, 1);

  visualizer_init(visualizer, audio_buffer, window);

  gtk_window_set_title(GTK_WINDOW(window), "pipewire-visualizer");
  gtk_widget_add_css_class(window, "pipewire-visualizer-window");
  gtk_window_set_default_size(GTK_WINDOW(window), visualizer->width,
                              visualizer->height);
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

  gtk_layer_init_for_window(GTK_WINDOW(window));
  gtk_layer_set_namespace(GTK_WINDOW(window), "pipewire-visualizer");
  gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
  gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window),
                              GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  gtk_layer_set_exclusive_zone(GTK_WINDOW(window), 0);
  apply_layer_position(visualizer);

  install_transparent_window_css();

  GtkWidget *overlay = gtk_overlay_new();
  gtk_widget_add_css_class(overlay, "pipewire-visualizer-overlay");

  GtkWidget *area = gtk_drawing_area_new();
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
      pwviz_global_shortcut_register(show_config_window_cb, visualizer);

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
