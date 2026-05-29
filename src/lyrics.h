#pragma once

#include "now_playing.h"

#include <glib.h>

typedef struct {
  gint64 time_ms;
  char *text;
} PwvizLyricLine;

typedef struct {
  gboolean available;
  gboolean synced;
  gint64 offset_ms;
  char key[512];
  char *plain_text;
  GPtrArray *lines;
} PwvizLyrics;

void pwviz_lyrics_clear(PwvizLyrics *lyrics);
void pwviz_lyrics_free(PwvizLyrics *lyrics);
PwvizLyrics *pwviz_lyrics_fetch(const PwvizNowPlaying *now_playing);
void pwviz_lyrics_current_lines(const PwvizLyrics *lyrics, gint64 position_us,
                                const char **current, const char **next);
gboolean pwviz_lyrics_adjust_offset(PwvizLyrics *lyrics, gint64 delta_ms);
void pwviz_lyrics_key_for_track(const PwvizNowPlaying *now_playing,
                                char *buffer, gsize buffer_size);
