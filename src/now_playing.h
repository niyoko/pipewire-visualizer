#pragma once

#include <glib.h>

typedef struct {
  gboolean available;
  char app[128];
  char title[256];
  char artist[256];
  char album[256];
} PwvizNowPlaying;

void pwviz_now_playing_clear(PwvizNowPlaying *now_playing);
gboolean pwviz_now_playing_refresh(PwvizNowPlaying *now_playing);
