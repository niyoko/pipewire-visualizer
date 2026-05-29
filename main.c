#include <fftw3.h>
#include <gtk/gtk.h>
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

static float ring[RING_SIZE];
static int ring_write = 0;
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;

static float bars[BAR_COUNT];

static struct pw_stream *stream;

static void push_sample(float s) {
  pthread_mutex_lock(&ring_lock);
  ring[ring_write] = s;
  ring_write = (ring_write + 1) % RING_SIZE;
  pthread_mutex_unlock(&ring_lock);
}

static void on_process(void *userdata) {
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

static void draw_cb(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                    gpointer data) {
  calculate_fft();

  cairo_set_source_rgb(cr, 0.02, 0.02, 0.02);
  cairo_paint(cr);

  double bar_w = (double)width / BAR_COUNT;

  for (int i = 0; i < BAR_COUNT; i++) {
    double h = bars[i] * height;
    double x = i * bar_w;
    double y = height - h;

    cairo_set_source_rgb(cr, 0.0, 0.9, 1.0);
    cairo_rectangle(cr, x + 1, y, bar_w - 2, h);
    cairo_fill(cr);
  }
}

static gboolean tick_cb(GtkWidget *widget, GdkFrameClock *clock,
                        gpointer data) {
  gtk_widget_queue_draw(widget);
  return G_SOURCE_CONTINUE;
}

static void activate(GtkApplication *app, gpointer user_data) {
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "PipeWire Visualizer");
  gtk_window_set_default_size(GTK_WINDOW(window), 900, 240);

  GtkWidget *area = gtk_drawing_area_new();
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_cb, NULL, NULL);

  gtk_window_set_child(GTK_WINDOW(window), area);

  gtk_widget_add_tick_callback(area, tick_cb, NULL, NULL);

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
