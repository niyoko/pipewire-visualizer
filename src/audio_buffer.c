#include "audio_buffer.h"

void pwviz_audio_buffer_init(PwvizAudioBuffer *buffer) {
  buffer->write_index = 0;
  pthread_mutex_init(&buffer->lock, NULL);

  for (int i = 0; i < PWVIZ_RING_SIZE; i++)
    buffer->samples[i] = 0.0f;
}

void pwviz_audio_buffer_push(PwvizAudioBuffer *buffer, float sample) {
  pthread_mutex_lock(&buffer->lock);
  buffer->samples[buffer->write_index] = sample;
  buffer->write_index = (buffer->write_index + 1) % PWVIZ_RING_SIZE;
  pthread_mutex_unlock(&buffer->lock);
}

void pwviz_audio_buffer_copy_latest(PwvizAudioBuffer *buffer, float *output,
                                    int count) {
  pthread_mutex_lock(&buffer->lock);

  int start = buffer->write_index - count;
  if (start < 0)
    start += PWVIZ_RING_SIZE;

  for (int i = 0; i < count; i++) {
    int idx = (start + i) % PWVIZ_RING_SIZE;
    output[i] = buffer->samples[idx];
  }

  pthread_mutex_unlock(&buffer->lock);
}
