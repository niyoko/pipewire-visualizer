#include "pipewire_sink.h"

#include "config.h"

#include <glib.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

typedef struct {
  PwvizAudioBuffer *buffer;
  struct pw_main_loop *loop;
  struct pw_stream *stream;
} PwvizPipewireSink;

static void on_process(void *userdata) {
  PwvizPipewireSink *sink = userdata;
  struct pw_buffer *b = pw_stream_dequeue_buffer(sink->stream);

  if (!b)
    return;

  struct spa_buffer *buf = b->buffer;
  if (!buf->datas[0].data) {
    pw_stream_queue_buffer(sink->stream, b);
    return;
  }

  float *data = buf->datas[0].data;
  uint32_t n_bytes = buf->datas[0].chunk->size;
  uint32_t n_floats = n_bytes / sizeof(float);

  pwviz_audio_buffer_push_interleaved_stereo(sink->buffer, data, n_floats);

  pw_stream_queue_buffer(sink->stream, b);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

static gpointer pipewire_thread(gpointer data) {
  PwvizPipewireSink *sink = data;

  pw_init(NULL, NULL);

  sink->loop = pw_main_loop_new(NULL);
  sink->stream = pw_stream_new_simple(
      pw_main_loop_get_loop(sink->loop), "pipewire-visualizer-input",
      pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
                        "Capture", PW_KEY_MEDIA_ROLE, "Music",
                        PW_KEY_STREAM_CAPTURE_SINK, "true", PW_KEY_NODE_NAME,
                        "pipewire-visualizer", NULL),
      &stream_events, sink);

  uint8_t buffer[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  const struct spa_pod *params[1];
  params[0] = spa_format_audio_raw_build(
      &b, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32,
                               .rate = PWVIZ_SAMPLE_RATE,
                               .channels = PWVIZ_CHANNELS));

  pw_stream_connect(sink->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                        PW_STREAM_FLAG_RT_PROCESS,
                    params, 1);

  pw_main_loop_run(sink->loop);
  return NULL;
}

void pwviz_pipewire_sink_start_async(PwvizAudioBuffer *buffer) {
  PwvizPipewireSink *sink = g_new0(PwvizPipewireSink, 1);
  sink->buffer = buffer;

  g_thread_new("pipewire", pipewire_thread, sink);
}
