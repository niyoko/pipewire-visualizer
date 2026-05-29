#include "audio_buffer.h"
#include "pipewire_sink.h"
#include "visualization.h"

int main(int argc, char **argv) {
  PwvizAudioBuffer audio_buffer;

  pwviz_audio_buffer_init(&audio_buffer);
  pwviz_pipewire_sink_start_async(&audio_buffer);

  return pwviz_visualization_run(&audio_buffer, argc, argv);
}
