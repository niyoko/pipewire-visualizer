#include "lyrics.h"

#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <stdio.h>
#include <string.h>

#define LRCLIB_ENDPOINT "https://lrclib.net/api/get"
#define PWVIZ_LYRICS_OFFSET_KEY "pwvizOffsetMs"
#define PWVIZ_LYRICS_MAX_OFFSET_MS 600000
#define PWVIZ_LYRICS_DOTS_WINDOW_MS 3000

static void lyric_line_free(gpointer data) {
  PwvizLyricLine *line = data;

  if (!line)
    return;

  g_free(line->text);
  g_free(line);
}

void pwviz_lyrics_clear(PwvizLyrics *lyrics) {
  if (!lyrics)
    return;

  lyrics->available = FALSE;
  lyrics->synced = FALSE;
  lyrics->offset_ms = 0;
  lyrics->key[0] = '\0';
  if (lyrics->lines)
    g_ptr_array_set_size(lyrics->lines, 0);
}

void pwviz_lyrics_free(PwvizLyrics *lyrics) {
  if (!lyrics)
    return;

  if (lyrics->lines)
    g_ptr_array_unref(lyrics->lines);
  g_free(lyrics);
}

void pwviz_lyrics_key_for_track(const PwvizNowPlaying *now_playing,
                                char *buffer, gsize buffer_size) {
  gint64 duration_s = now_playing->duration_us > 0
                         ? (now_playing->duration_us + 500000) / 1000000
                         : 0;

  g_snprintf(buffer, buffer_size, "%s|%s|%s|%" G_GINT64_FORMAT,
             now_playing->artist, now_playing->title, now_playing->album,
             duration_s);
}

static char *cache_path_for_key(const char *key) {
  char *checksum = g_compute_checksum_for_string(G_CHECKSUM_SHA256, key, -1);
  char *dir = g_build_filename(g_get_user_cache_dir(), "pipewire-visualizer",
                               "lyrics", NULL);
  char *filename = g_strdup_printf("%s.json", checksum);
  char *path = g_build_filename(dir, filename, NULL);

  g_mkdir_with_parents(dir, 0700);
  g_free(filename);
  g_free(dir);
  g_free(checksum);
  return path;
}

static char *escape_query_value(const char *value) {
  return g_uri_escape_string(value ? value : "", NULL, FALSE);
}

static char *request_uri_for_track(const PwvizNowPlaying *now_playing) {
  char *artist = escape_query_value(now_playing->artist);
  char *title = escape_query_value(now_playing->title);
  char *album = escape_query_value(now_playing->album);
  char *uri = NULL;

  if (now_playing->duration_us > 0) {
    gint64 duration_s = (now_playing->duration_us + 500000) / 1000000;
    uri = g_strdup_printf("%s?artist_name=%s&track_name=%s&album_name=%s&"
                          "duration=%" G_GINT64_FORMAT,
                          LRCLIB_ENDPOINT, artist, title, album, duration_s);
  } else {
    uri = g_strdup_printf("%s?artist_name=%s&track_name=%s&album_name=%s",
                          LRCLIB_ENDPOINT, artist, title, album);
  }

  g_free(album);
  g_free(title);
  g_free(artist);
  return uri;
}

static gboolean parse_lrc_time(const char *stamp, gint64 *time_ms) {
  int minutes = 0;
  double seconds = 0.0;

  if (sscanf(stamp, "%d:%lf", &minutes, &seconds) != 2)
    return FALSE;

  *time_ms = (gint64)minutes * 60000 + (gint64)(seconds * 1000.0 + 0.5);
  return TRUE;
}

static gint compare_lyric_lines(gconstpointer a, gconstpointer b) {
  const PwvizLyricLine *left = *(PwvizLyricLine *const *)a;
  const PwvizLyricLine *right = *(PwvizLyricLine *const *)b;

  if (left->time_ms < right->time_ms)
    return -1;
  if (left->time_ms > right->time_ms)
    return 1;
  return 0;
}

static gboolean lyric_line_has_text(const PwvizLyricLine *line) {
  return line && line->text && line->text[0] != '\0';
}

static PwvizLyricLine *next_text_line(const PwvizLyrics *lyrics,
                                      guint start_index) {
  for (guint i = start_index; i < lyrics->lines->len; i++) {
    PwvizLyricLine *line = g_ptr_array_index(lyrics->lines, i);
    if (lyric_line_has_text(line))
      return line;
  }

  return NULL;
}

static const char *dots_countdown(gint64 remaining_ms) {
  if (remaining_ms < 0 || remaining_ms > PWVIZ_LYRICS_DOTS_WINDOW_MS)
    return "";
  if (remaining_ms > 2000)
    return "...";
  if (remaining_ms > 1000)
    return "..";
  return ".";
}

static void add_lrc_line(PwvizLyrics *lyrics, gint64 time_ms,
                         const char *text) {
  PwvizLyricLine *line = g_new0(PwvizLyricLine, 1);

  line->time_ms = time_ms;
  line->text = g_strdup(text);
  g_strstrip(line->text);
  g_ptr_array_add(lyrics->lines, line);
}

static void parse_synced_lyrics(PwvizLyrics *lyrics, const char *synced) {
  char **rows = g_strsplit(synced, "\n", -1);

  for (guint i = 0; rows[i]; i++) {
    char *row = g_strstrip(rows[i]);
    char *cursor = row;
    GArray *times = g_array_new(FALSE, FALSE, sizeof(gint64));

    while (cursor[0] == '[') {
      char *end = strchr(cursor, ']');
      if (!end)
        break;

      *end = '\0';
      gint64 time_ms = 0;
      if (parse_lrc_time(cursor + 1, &time_ms))
        g_array_append_val(times, time_ms);
      cursor = end + 1;
    }

    cursor = g_strstrip(cursor);
    if (times->len > 0) {
      for (guint t = 0; t < times->len; t++) {
        gint64 time_ms = g_array_index(times, gint64, t);
        add_lrc_line(lyrics, time_ms, cursor);
      }
    }

    g_array_unref(times);
  }

  g_strfreev(rows);
  if (lyrics->lines->len > 0) {
    g_ptr_array_sort(lyrics->lines, compare_lyric_lines);
    lyrics->synced = TRUE;
  }
}

static PwvizLyrics *parse_lyrics_json(const char *json_data, const char *key) {
  JsonParser *parser = json_parser_new();
  GError *error = NULL;

  if (!json_parser_load_from_data(parser, json_data, -1, &error)) {
    g_clear_error(&error);
    g_object_unref(parser);
    return NULL;
  }

  JsonNode *root = json_parser_get_root(parser);
  if (!JSON_NODE_HOLDS_OBJECT(root)) {
    g_object_unref(parser);
    return NULL;
  }

  JsonObject *object = json_node_get_object(root);
  const char *synced = json_object_get_string_member_with_default(
      object, "syncedLyrics", NULL);

  if (!synced || synced[0] == '\0') {
    g_object_unref(parser);
    return NULL;
  }

  PwvizLyrics *lyrics = g_new0(PwvizLyrics, 1);
  lyrics->lines = g_ptr_array_new_with_free_func(lyric_line_free);
  lyrics->available = TRUE;
  if (json_object_has_member(object, PWVIZ_LYRICS_OFFSET_KEY))
    lyrics->offset_ms =
        CLAMP(json_object_get_int_member(object, PWVIZ_LYRICS_OFFSET_KEY),
              -PWVIZ_LYRICS_MAX_OFFSET_MS, PWVIZ_LYRICS_MAX_OFFSET_MS);
  g_strlcpy(lyrics->key, key, sizeof(lyrics->key));

  if (synced && synced[0] != '\0')
    parse_synced_lyrics(lyrics, synced);

  g_object_unref(parser);
  if (!lyrics->synced || !lyrics->lines || lyrics->lines->len == 0) {
    pwviz_lyrics_free(lyrics);
    return NULL;
  }

  return lyrics;
}

static char *read_cached_json(const char *path) {
  char *contents = NULL;

  if (g_file_get_contents(path, &contents, NULL, NULL))
    return contents;

  return NULL;
}

static gboolean write_cached_offset(const char *key, gint64 offset_ms) {
  char *path = cache_path_for_key(key);
  char *contents = read_cached_json(path);
  JsonParser *parser = NULL;
  JsonGenerator *generator = NULL;
  GError *error = NULL;
  gboolean written = FALSE;

  if (!contents)
    goto done;

  parser = json_parser_new();
  if (!json_parser_load_from_data(parser, contents, -1, &error))
    goto done;

  JsonNode *root = json_parser_get_root(parser);
  if (!JSON_NODE_HOLDS_OBJECT(root))
    goto done;

  JsonObject *object = json_node_get_object(root);
  json_object_set_int_member(object, PWVIZ_LYRICS_OFFSET_KEY, offset_ms);

  generator = json_generator_new();
  json_generator_set_root(generator, root);

  char *data = json_generator_to_data(generator, NULL);
  written = g_file_set_contents(path, data, -1, NULL);
  g_free(data);

done:
  g_clear_error(&error);
  if (generator)
    g_object_unref(generator);
  if (parser)
    g_object_unref(parser);
  g_free(contents);
  g_free(path);
  return written;
}

static char *fetch_json(const PwvizNowPlaying *now_playing) {
  SoupSession *session = soup_session_new();
  char *uri = request_uri_for_track(now_playing);
  SoupMessage *message = soup_message_new("GET", uri);
  GError *error = NULL;
  GBytes *bytes = NULL;
  char *data = NULL;

  if (message) {
    soup_message_headers_append(soup_message_get_request_headers(message),
                                "User-Agent", "pipewire-visualizer");
    bytes = soup_session_send_and_read(session, message, NULL, &error);
  }

  if (bytes && soup_message_get_status(message) == SOUP_STATUS_OK) {
    gsize size = 0;
    const char *raw = g_bytes_get_data(bytes, &size);
    if (raw && size > 0)
      data = g_strndup(raw, size);
  }

  if (bytes)
    g_bytes_unref(bytes);

  g_clear_error(&error);
  g_clear_object(&message);
  g_object_unref(session);
  g_free(uri);
  return data;
}

PwvizLyrics *pwviz_lyrics_fetch(const PwvizNowPlaying *now_playing) {
  if (!now_playing->available || now_playing->title[0] == '\0')
    return NULL;

  char key[512];
  pwviz_lyrics_key_for_track(now_playing, key, sizeof(key));
  char *path = cache_path_for_key(key);
  char *json_data = read_cached_json(path);

  if (!json_data) {
    json_data = fetch_json(now_playing);
    if (json_data)
      g_file_set_contents(path, json_data, -1, NULL);
  }

  PwvizLyrics *lyrics = json_data ? parse_lyrics_json(json_data, key) : NULL;

  g_free(json_data);
  g_free(path);
  return lyrics;
}

gboolean pwviz_lyrics_cached_text_for_track(const PwvizNowPlaying *now_playing,
                                            char **synced_lyrics,
                                            gint64 *offset_ms) {
  if (synced_lyrics)
    *synced_lyrics = NULL;
  if (offset_ms)
    *offset_ms = 0;
  if (!now_playing->available || now_playing->title[0] == '\0')
    return FALSE;

  char key[512];
  pwviz_lyrics_key_for_track(now_playing, key, sizeof(key));
  char *path = cache_path_for_key(key);
  char *json_data = read_cached_json(path);
  JsonParser *parser = NULL;
  GError *error = NULL;
  gboolean loaded = FALSE;

  if (!json_data)
    goto done;

  parser = json_parser_new();
  if (!json_parser_load_from_data(parser, json_data, -1, &error))
    goto done;

  JsonNode *root = json_parser_get_root(parser);
  if (!JSON_NODE_HOLDS_OBJECT(root))
    goto done;

  JsonObject *object = json_node_get_object(root);
  const char *synced = json_object_get_string_member_with_default(
      object, "syncedLyrics", "");
  if (synced_lyrics)
    *synced_lyrics = g_strdup(synced ? synced : "");
  if (offset_ms && json_object_has_member(object, PWVIZ_LYRICS_OFFSET_KEY))
    *offset_ms =
        CLAMP(json_object_get_int_member(object, PWVIZ_LYRICS_OFFSET_KEY),
              -PWVIZ_LYRICS_MAX_OFFSET_MS, PWVIZ_LYRICS_MAX_OFFSET_MS);
  loaded = TRUE;

done:
  g_clear_error(&error);
  if (parser)
    g_object_unref(parser);
  g_free(json_data);
  g_free(path);
  return loaded;
}

gboolean pwviz_lyrics_save_text_for_track(const PwvizNowPlaying *now_playing,
                                          const char *synced_lyrics,
                                          gint64 offset_ms) {
  if (!now_playing->available || now_playing->title[0] == '\0')
    return FALSE;

  char key[512];
  pwviz_lyrics_key_for_track(now_playing, key, sizeof(key));
  char *path = cache_path_for_key(key);
  JsonBuilder *builder = json_builder_new();
  JsonGenerator *generator = json_generator_new();
  gboolean written = FALSE;

  offset_ms = CLAMP(offset_ms, -PWVIZ_LYRICS_MAX_OFFSET_MS,
                    PWVIZ_LYRICS_MAX_OFFSET_MS);

  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "syncedLyrics");
  json_builder_add_string_value(builder, synced_lyrics ? synced_lyrics : "");
  json_builder_set_member_name(builder, PWVIZ_LYRICS_OFFSET_KEY);
  json_builder_add_int_value(builder, offset_ms);
  json_builder_end_object(builder);

  JsonNode *root = json_builder_get_root(builder);
  json_generator_set_root(generator, root);
  char *data = json_generator_to_data(generator, NULL);
  written = g_file_set_contents(path, data, -1, NULL);

  g_free(data);
  json_node_unref(root);
  g_object_unref(generator);
  g_object_unref(builder);
  g_free(path);
  return written;
}

void pwviz_lyrics_current_lines(const PwvizLyrics *lyrics, gint64 position_us,
                                const char **current, const char **next) {
  *current = NULL;
  *next = NULL;

  if (!lyrics || !lyrics->available)
    return;

  if (lyrics->synced && lyrics->lines && lyrics->lines->len > 0) {
    gint64 position_ms = MAX(0, position_us / 1000 - lyrics->offset_ms);
    gint active = -1;

    for (guint i = 0; i < lyrics->lines->len; i++) {
      PwvizLyricLine *line = g_ptr_array_index(lyrics->lines, i);
      if (line->time_ms > position_ms)
        break;
      active = (gint)i;
    }

    if (active < 0) {
      PwvizLyricLine *upcoming = next_text_line(lyrics, 0);
      *current = upcoming ? dots_countdown(upcoming->time_ms - position_ms)
                          : "";
      *next = upcoming ? upcoming->text : NULL;
      return;
    }

    PwvizLyricLine *line = g_ptr_array_index(lyrics->lines, active);
    if (!lyric_line_has_text(line)) {
      PwvizLyricLine *upcoming = next_text_line(lyrics, active + 1);
      *current = upcoming ? dots_countdown(upcoming->time_ms - position_ms)
                          : "";
      *next = upcoming ? upcoming->text : NULL;
      return;
    }

    *current = line->text;
    if ((guint)active + 1 < lyrics->lines->len) {
      line = g_ptr_array_index(lyrics->lines, active + 1);
      *next = line->text;
    }
    return;
  }

}

gboolean pwviz_lyrics_adjust_offset(PwvizLyrics *lyrics, gint64 delta_ms) {
  if (!lyrics || !lyrics->available || lyrics->key[0] == '\0')
    return FALSE;

  lyrics->offset_ms =
      CLAMP(lyrics->offset_ms + delta_ms, -PWVIZ_LYRICS_MAX_OFFSET_MS,
            PWVIZ_LYRICS_MAX_OFFSET_MS);
  return write_cached_offset(lyrics->key, lyrics->offset_ms);
}
