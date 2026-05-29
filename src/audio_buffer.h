#pragma once

#include "config.h"

#include <pthread.h>

typedef struct {
  float samples[PWVIZ_RING_SIZE];
  int write_index;
  pthread_mutex_t lock;
} PwvizAudioBuffer;

void pwviz_audio_buffer_init(PwvizAudioBuffer *buffer);
void pwviz_audio_buffer_push(PwvizAudioBuffer *buffer, float sample);
void pwviz_audio_buffer_copy_latest(PwvizAudioBuffer *buffer, float *output,
                                    int count);
