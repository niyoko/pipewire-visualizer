#include <fftw3.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <math.h>
#include <pthread.h>
#include <string.h>

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define FFT_SIZE 2048
#define BAR_COUNT 64
#define RING_SIZE (SAMPLE_RATE * 4)
#define HANDLE_HEIGHT 22
#define RESIZE_GRIP_SIZE 28
#define MIN_WINDOW_WIDTH 240
#define MIN_WINDOW_HEIGHT 90
#define SPECTRUM_TOP_PADDING 28
#define SPECTRUM_BOTTOM_PADDING 6
#define PEAK_HOLD_FRAMES 8
#define PEAK_FALL_PER_FRAME 0.012f

static float ring[RING_SIZE];
static int ring_write = 0;
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;

static float bars[BAR_COUNT];
static float peak_caps[BAR_COUNT];
static int peak_holds[BAR_COUNT];

static struct pw_stream *stream;

typedef struct {
  GtkWindow *window;
  int x;
  int y;
  int width;
  int height;
} AppState;

static void push_sample(float s) {
  pthread_mutex_lock(&ring_lock);
  ring[ring_write] = s;
  ring_write = (ring_write + 1) % RING_SIZE;
  pthread_mutex_unlock(&ring_lock);
}

static void on_process(void *userdata) {
  (void)userdata;

  struct pw_buffer *b = pw_stream_dequeue_buffer(stream);
  if (!b)
    return;

  struct spa_buffer *buf = b->buffer;
  if (!buf->datas[0].data) {
    pw_stream_queue_buffer(stream, b);
    return;
  }

  float *data = buf->datas[0].data;
  uint32_t n_bytes = buf->datas[0].chunk->size;
  uint32_t n_floats = n_bytes / sizeof(float);

  for (uint32_t i = 0; i + 1 < n_floats; i += 2) {
    float mono = (data[i] + data[i + 1]) * 0.5f;
    push_sample(mono);
  }

  pw_stream_queue_buffer(stream, b);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

static void start_pipewire(void) {
  pw_init(NULL, NULL);

  struct pw_main_loop *loop = pw_main_loop_new(NULL);

  stream = pw_stream_new_simple(
      pw_main_loop_get_loop(loop), "pwviz-input",
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
                        "Capture", PW_KEY_MEDIA_ROLE, "Music",
                        PW_KEY_STREAM_CAPTURE_SINK, "true", PW_KEY_NODE_NAME,
                        "pwviz", NULL),
      &stream_events, NULL);

  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  const struct spa_pod *params[1];
  params[0] = spa_format_audio_raw_build(
      &b, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32,
                               .rate = SAMPLE_RATE, .channels = CHANNELS));

  pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                        PW_STREAM_FLAG_RT_PROCESS,
                    params, 1);

  pw_main_loop_run(loop);
}

static gpointer pipewire_thread(gpointer data) {
  (void)data;

  start_pipewire();
  return NULL;
}

static void calculate_fft(void) {
  static float input[FFT_SIZE];
  static fftwf_complex output[FFT_SIZE / 2 + 1];
  static fftwf_plan plan;
  static int initialized = 0;

  if (!initialized) {
    plan = fftwf_plan_dft_r2c_1d(FFT_SIZE, input, output, FFTW_MEASURE);
    initialized = 1;
  }

  pthread_mutex_lock(&ring_lock);

  int start = ring_write - FFT_SIZE;
  if (start < 0)
    start += RING_SIZE;

  for (int i = 0; i < FFT_SIZE; i++) {
    int idx = (start + i) % RING_SIZE;

    float hann = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
    input[i] = ring[idx] * hann;
  }

  pthread_mutex_unlock(&ring_lock);

  fftwf_execute(plan);

  for (int b = 0; b < BAR_COUNT; b++) {
    int start_bin = 1 + b * ((FFT_SIZE / 2) / BAR_COUNT);
    int end_bin = 1 + (b + 1) * ((FFT_SIZE / 2) / BAR_COUNT);

    float sum = 0.0f;

    for (int i = start_bin; i < end_bin; i++) {
      float re = output[i][0];
      float im = output[i][1];
      sum += sqrtf(re * re + im * im);
    }

    float value = logf(1.0f + sum) / 5.0f;
    if (value > 1.0f)
      value = 1.0f;

    if (value > bars[b])
      bars[b] = bars[b] * 0.4f + value * 0.6f;
    else
      bars[b] = bars[b] * 0.85f + value * 0.15f;
  }
}

static void update_input_region(AppState *state) {
  GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(state->window));
  if (!surface)
    return;

  int width = gtk_widget_get_width(GTK_WIDGET(state->window));
  int height = gtk_widget_get_height(GTK_WIDGET(state->window));

  if (width <= 0)
    width = state->width;
  if (height <= 0)
    height = state->height;

  cairo_region_t *region = cairo_region_create();
  cairo_rectangle_int_t drag_rect = {0, 0, width, HANDLE_HEIGHT};
  cairo_rectangle_int_t resize_rect = {
      width - RESIZE_GRIP_SIZE,
      height - RESIZE_GRIP_SIZE,
      RESIZE_GRIP_SIZE,
      RESIZE_GRIP_SIZE,
  };

  cairo_region_union_rectangle(region, &drag_rect);
  cairo_region_union_rectangle(region, &resize_rect);
  gdk_surface_set_input_region(surface, region);
  cairo_region_destroy(region);
}

static void draw_cb(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                    gpointer data) {
  (void)area;

  AppState *state = data;

  state->width = width;
  state->height = height;
  update_input_region(state);

  calculate_fft();

  cairo_set_source_rgba(cr, 0.015, 0.010, 0.008, 0.46);
  cairo_paint(cr);

  double visual_top = SPECTRUM_TOP_PADDING;
  double visual_bottom = height - SPECTRUM_BOTTOM_PADDING;
  double visual_height = visual_bottom - visual_top;
  double bar_w = (double)width / BAR_COUNT;
  double block_h = 4.0;
  double block_gap = 2.0;

  if (visual_height <= block_h)
    return;

  cairo_set_source_rgba(cr, 1.0, 0.78, 0.18, 0.32);
  cairo_set_line_width(cr, 1.0);
  cairo_rectangle(cr, 0.5, visual_top - 0.5, width - 1.0,
                  visual_height + 1.0);
  cairo_stroke(cr);

  for (int i = 0; i < BAR_COUNT; i++) {
    if (bars[i] > peak_caps[i]) {
      peak_caps[i] = bars[i];
      peak_holds[i] = PEAK_HOLD_FRAMES;
    } else if (peak_holds[i] > 0) {
      peak_holds[i]--;
    } else {
      peak_caps[i] = MAX(0.0f, peak_caps[i] - PEAK_FALL_PER_FRAME);
    }

    double h = bars[i] * visual_height;
    double x = i * bar_w;
    double lit_top = visual_bottom - h;
    double block_w = MAX(1.0, bar_w - 2.0);

    for (double y = visual_bottom - block_h; y >= visual_top;
         y -= block_h + block_gap) {
      double level = (visual_bottom - y) / visual_height;
      gboolean lit = y >= lit_top;

      double red = 0.40 + 0.60 * level;
      double green = 0.02 + 0.84 * level;
      double blue = 0.0;
      double alpha = lit ? 0.94 : 0.14;

      cairo_set_source_rgba(cr, red, green, blue, alpha);
      cairo_rectangle(cr, x + 1.0, y, block_w, block_h);
      cairo_fill(cr);
    }

    double peak_y = visual_bottom - peak_caps[i] * visual_height;
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

static void apply_layer_position(AppState *state) {
  gtk_layer_set_margin(state->window, GTK_LAYER_SHELL_EDGE_LEFT, state->x);
  gtk_layer_set_margin(state->window, GTK_LAYER_SHELL_EDGE_TOP, state->y);
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

  AppState *state = data;

  state->x = MAX(0, state->x + (int)round(offset_x));
  state->y = MAX(0, state->y + (int)round(offset_y));
  apply_layer_position(state);
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

  AppState *state = data;

  state->width = MAX(MIN_WINDOW_WIDTH, state->width + (int)round(offset_x));
  state->height = MAX(MIN_WINDOW_HEIGHT, state->height + (int)round(offset_y));

  gtk_window_set_default_size(state->window, state->width, state->height);
  update_input_region(state);
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

static void activate(GtkApplication *app, gpointer user_data) {
  (void)user_data;

  GtkWidget *window = gtk_application_window_new(app);
  AppState *state = g_new0(AppState, 1);

  state->window = GTK_WINDOW(window);
  state->width = 900;
  state->height = 240;
  state->x = 24;
  state->y = 24;

  gtk_window_set_title(GTK_WINDOW(window), "PipeWire Visualizer");
  gtk_window_set_default_size(GTK_WINDOW(window), state->width, state->height);
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
  apply_layer_position(state);

  install_transparent_window_css();

  GtkWidget *overlay = gtk_overlay_new();
  gtk_widget_add_css_class(overlay, "pwviz-overlay");

  GtkWidget *area = gtk_drawing_area_new();
  gtk_widget_set_can_target(area, FALSE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_cb, state, NULL);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), area);

  GtkWidget *drag_handle = gtk_drawing_area_new();
  gtk_widget_set_size_request(drag_handle, -1, HANDLE_HEIGHT);
  gtk_widget_set_halign(drag_handle, GTK_ALIGN_FILL);
  gtk_widget_set_valign(drag_handle, GTK_ALIGN_START);
  gtk_widget_set_cursor_from_name(drag_handle, "move");
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drag_handle),
                                 drag_handle_draw_cb, NULL, NULL);

  GtkGesture *drag_gesture = gtk_gesture_drag_new();
  g_signal_connect(drag_gesture, "drag-begin", G_CALLBACK(drag_begin_cb),
                   state);
  g_signal_connect(drag_gesture, "drag-update", G_CALLBACK(drag_update_cb),
                   state);
  gtk_widget_add_controller(drag_handle, GTK_EVENT_CONTROLLER(drag_gesture));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), drag_handle);

  GtkWidget *resize_grip = gtk_drawing_area_new();
  gtk_widget_set_size_request(resize_grip, RESIZE_GRIP_SIZE, RESIZE_GRIP_SIZE);
  gtk_widget_set_halign(resize_grip, GTK_ALIGN_END);
  gtk_widget_set_valign(resize_grip, GTK_ALIGN_END);
  gtk_widget_set_cursor_from_name(resize_grip, "nwse-resize");
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(resize_grip),
                                 resize_grip_draw_cb, NULL, NULL);

  GtkGesture *resize_gesture = gtk_gesture_drag_new();
  g_signal_connect(resize_gesture, "drag-begin", G_CALLBACK(resize_begin_cb),
                   state);
  g_signal_connect(resize_gesture, "drag-update", G_CALLBACK(resize_update_cb),
                   state);
  gtk_widget_add_controller(resize_grip, GTK_EVENT_CONTROLLER(resize_gesture));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), resize_grip);

  gtk_window_set_child(GTK_WINDOW(window), overlay);

  gtk_widget_add_tick_callback(area, tick_cb, NULL, NULL);
  g_signal_connect_swapped(window, "map", G_CALLBACK(update_input_region),
                           state);
  g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_free), state);

  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
  g_thread_new("pipewire", pipewire_thread, NULL);

  GtkApplication *app =
      gtk_application_new("local.pwviz", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

  int status = g_application_run(G_APPLICATION(app), argc, argv);

  g_object_unref(app);
  return status;
}
