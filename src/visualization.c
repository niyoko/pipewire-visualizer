#include "visualization.h"

#include "binning.h"
#include "config.h"
#include "fft.h"

#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <math.h>

typedef struct {
  GtkWindow *window;
  PwvizAudioBuffer *audio_buffer;
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

static void update_input_region(PwvizVisualizer *visualizer) {
  GdkSurface *surface =
      gtk_native_get_surface(GTK_NATIVE(visualizer->window));
  if (!surface)
    return;

  int width = gtk_widget_get_width(GTK_WIDGET(visualizer->window));
  int height = gtk_widget_get_height(GTK_WIDGET(visualizer->window));

  if (width <= 0)
    width = visualizer->width;
  if (height <= 0)
    height = visualizer->height;

  cairo_region_t *region = cairo_region_create();
  cairo_rectangle_int_t drag_rect = {0, 0, width, PWVIZ_HANDLE_HEIGHT};
  cairo_rectangle_int_t resize_rect = {
      width - PWVIZ_RESIZE_GRIP_SIZE,
      height - PWVIZ_RESIZE_GRIP_SIZE,
      PWVIZ_RESIZE_GRIP_SIZE,
      PWVIZ_RESIZE_GRIP_SIZE,
  };

  cairo_region_union_rectangle(region, &drag_rect);
  cairo_region_union_rectangle(region, &resize_rect);
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

    if (value > visualizer->bars[i])
      visualizer->bars[i] = visualizer->bars[i] * 0.4f + value * 0.6f;
    else
      visualizer->bars[i] = visualizer->bars[i] * 0.85f + value * 0.15f;
  }
}

static void draw_spectrum(PwvizVisualizer *visualizer, cairo_t *cr, int width,
                          int height) {
  double visual_top = PWVIZ_SPECTRUM_TOP_PADDING;
  double visual_bottom = height - PWVIZ_SPECTRUM_BOTTOM_PADDING;
  double visual_height = visual_bottom - visual_top;
  double bar_w = (double)width / PWVIZ_BAR_COUNT;
  double block_h = 4.0;
  double block_gap = 2.0;

  if (visual_height <= block_h)
    return;

  cairo_set_source_rgba(cr, 1.0, 0.78, 0.18, 0.32);
  cairo_set_line_width(cr, 1.0);
  cairo_rectangle(cr, 0.5, visual_top - 0.5, width - 1.0,
                  visual_height + 1.0);
  cairo_stroke(cr);

  for (int i = 0; i < PWVIZ_BAR_COUNT; i++) {
    if (visualizer->bars[i] > visualizer->peak_caps[i]) {
      visualizer->peak_caps[i] = visualizer->bars[i];
      visualizer->peak_holds[i] = PWVIZ_PEAK_HOLD_FRAMES;
    } else if (visualizer->peak_holds[i] > 0) {
      visualizer->peak_holds[i]--;
    } else {
      visualizer->peak_caps[i] =
          MAX(0.0f, visualizer->peak_caps[i] - PWVIZ_PEAK_FALL_PER_FRAME);
    }

    double h = visualizer->bars[i] * visual_height;
    double x = i * bar_w;
    double lit_top = visual_bottom - h;
    double block_w = MAX(1.0, bar_w - 2.0);

    for (double y = visual_bottom - block_h; y >= visual_top;
         y -= block_h + block_gap) {
      double level = (visual_bottom - y) / visual_height;
      gboolean lit = y >= lit_top;

      cairo_set_source_rgba(cr, 0.40 + 0.60 * level, 0.02 + 0.84 * level,
                            0.0, lit ? 0.94 : 0.14);
      cairo_rectangle(cr, x + 1.0, y, block_w, block_h);
      cairo_fill(cr);
    }

    double peak_y = visual_bottom - visualizer->peak_caps[i] * visual_height;
    double snapped_peak_y =
        visual_bottom -
        floor((visual_bottom - peak_y) / (block_h + block_gap)) *
            (block_h + block_gap);

    cairo_set_source_rgba(cr, 1.0, 0.92, 0.20, 0.96);
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

  cairo_set_source_rgba(cr, 0.015, 0.010, 0.008, 0.46);
  cairo_paint(cr);
  draw_spectrum(visualizer, cr, width, height);
}

static void drag_handle_draw_cb(GtkDrawingArea *area, cairo_t *cr, int width,
                                int height, gpointer data) {
  (void)area;
  (void)data;

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.25);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.5);
  cairo_set_line_width(cr, 2.0);
  cairo_move_to(cr, width / 2.0 - 28.0, height / 2.0);
  cairo_line_to(cr, width / 2.0 + 28.0, height / 2.0);
  cairo_stroke(cr);
}

static void resize_grip_draw_cb(GtkDrawingArea *area, cairo_t *cr, int width,
                                int height, gpointer data) {
  (void)area;
  (void)data;

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.25);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.55);
  cairo_set_line_width(cr, 2.0);

  for (int i = 0; i < 3; i++) {
    double inset = 7.0 + i * 6.0;
    cairo_move_to(cr, width - inset, height - 4.0);
    cairo_line_to(cr, width - 4.0, height - inset);
  }

  cairo_stroke(cr);
}

static gboolean tick_cb(GtkWidget *widget, GdkFrameClock *clock,
                        gpointer data) {
  (void)clock;
  (void)data;

  gtk_widget_queue_draw(widget);
  return G_SOURCE_CONTINUE;
}

static void apply_layer_position(PwvizVisualizer *visualizer) {
  gtk_layer_set_margin(visualizer->window, GTK_LAYER_SHELL_EDGE_LEFT,
                       visualizer->x);
  gtk_layer_set_margin(visualizer->window, GTK_LAYER_SHELL_EDGE_TOP,
                       visualizer->y);
}

static void drag_begin_cb(GtkGestureDrag *gesture, double start_x,
                          double start_y, gpointer data) {
  (void)gesture;
  (void)start_x;
  (void)start_y;
  (void)data;
}

static void drag_update_cb(GtkGestureDrag *gesture, double offset_x,
                           double offset_y, gpointer data) {
  (void)gesture;

  PwvizVisualizer *visualizer = data;

  visualizer->x = MAX(0, visualizer->x + (int)round(offset_x));
  visualizer->y = MAX(0, visualizer->y + (int)round(offset_y));
  apply_layer_position(visualizer);
}

static void resize_begin_cb(GtkGestureDrag *gesture, double start_x,
                            double start_y, gpointer data) {
  (void)gesture;
  (void)start_x;
  (void)start_y;
  (void)data;
}

static void resize_update_cb(GtkGestureDrag *gesture, double offset_x,
                             double offset_y, gpointer data) {
  (void)gesture;

  PwvizVisualizer *visualizer = data;

  visualizer->width =
      MAX(PWVIZ_MIN_WINDOW_WIDTH, visualizer->width + (int)round(offset_x));
  visualizer->height =
      MAX(PWVIZ_MIN_WINDOW_HEIGHT, visualizer->height + (int)round(offset_y));

  gtk_window_set_default_size(visualizer->window, visualizer->width,
                              visualizer->height);
  update_input_region(visualizer);
}

static void install_transparent_window_css(void) {
  GtkCssProvider *provider = gtk_css_provider_new();

  gtk_css_provider_load_from_string(
      provider, "window, .pwviz-overlay { background: transparent; }");
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
  visualizer->x = 24;
  visualizer->y = 24;

  pwviz_fft_init(&visualizer->fft);
  pwviz_binner_init(&visualizer->binner);
}

static void activate(GtkApplication *app, gpointer user_data) {
  PwvizAudioBuffer *audio_buffer = user_data;
  GtkWidget *window = gtk_application_window_new(app);
  PwvizVisualizer *visualizer = g_new0(PwvizVisualizer, 1);

  visualizer_init(visualizer, audio_buffer, window);

  gtk_window_set_title(GTK_WINDOW(window), "PipeWire Visualizer");
  gtk_window_set_default_size(GTK_WINDOW(window), visualizer->width,
                              visualizer->height);
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

  gtk_layer_init_for_window(GTK_WINDOW(window));
  gtk_layer_set_namespace(GTK_WINDOW(window), "pwviz");
  gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
  gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window),
                              GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  gtk_layer_set_exclusive_zone(GTK_WINDOW(window), 0);
  apply_layer_position(visualizer);

  install_transparent_window_css();

  GtkWidget *overlay = gtk_overlay_new();
  gtk_widget_add_css_class(overlay, "pwviz-overlay");

  GtkWidget *area = gtk_drawing_area_new();
  gtk_widget_set_can_target(area, FALSE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_cb, visualizer,
                                 NULL);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), area);

  GtkWidget *drag_handle = gtk_drawing_area_new();
  gtk_widget_set_size_request(drag_handle, -1, PWVIZ_HANDLE_HEIGHT);
  gtk_widget_set_halign(drag_handle, GTK_ALIGN_FILL);
  gtk_widget_set_valign(drag_handle, GTK_ALIGN_START);
  gtk_widget_set_cursor_from_name(drag_handle, "move");
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drag_handle),
                                 drag_handle_draw_cb, NULL, NULL);

  GtkGesture *drag_gesture = gtk_gesture_drag_new();
  g_signal_connect(drag_gesture, "drag-begin", G_CALLBACK(drag_begin_cb),
                   visualizer);
  g_signal_connect(drag_gesture, "drag-update", G_CALLBACK(drag_update_cb),
                   visualizer);
  gtk_widget_add_controller(drag_handle, GTK_EVENT_CONTROLLER(drag_gesture));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), drag_handle);

  GtkWidget *resize_grip = gtk_drawing_area_new();
  gtk_widget_set_size_request(resize_grip, PWVIZ_RESIZE_GRIP_SIZE,
                              PWVIZ_RESIZE_GRIP_SIZE);
  gtk_widget_set_halign(resize_grip, GTK_ALIGN_END);
  gtk_widget_set_valign(resize_grip, GTK_ALIGN_END);
  gtk_widget_set_cursor_from_name(resize_grip, "nwse-resize");
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(resize_grip),
                                 resize_grip_draw_cb, NULL, NULL);

  GtkGesture *resize_gesture = gtk_gesture_drag_new();
  g_signal_connect(resize_gesture, "drag-begin", G_CALLBACK(resize_begin_cb),
                   visualizer);
  g_signal_connect(resize_gesture, "drag-update", G_CALLBACK(resize_update_cb),
                   visualizer);
  gtk_widget_add_controller(resize_grip,
                            GTK_EVENT_CONTROLLER(resize_gesture));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), resize_grip);

  gtk_window_set_child(GTK_WINDOW(window), overlay);

  gtk_widget_add_tick_callback(area, tick_cb, NULL, NULL);
  g_signal_connect_swapped(window, "map", G_CALLBACK(update_input_region),
                           visualizer);
  g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_free),
                           visualizer);

  gtk_window_present(GTK_WINDOW(window));
}

int pwviz_visualization_run(PwvizAudioBuffer *audio_buffer, int argc,
                            char **argv) {
  GtkApplication *app =
      gtk_application_new("local.pwviz", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), audio_buffer);

  int status = g_application_run(G_APPLICATION(app), argc, argv);

  g_object_unref(app);
  return status;
}
