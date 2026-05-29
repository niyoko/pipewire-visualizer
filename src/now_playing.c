#include "now_playing.h"

#include <gio/gio.h>
#include <string.h>

#define MPRIS_PREFIX "org.mpris.MediaPlayer2."
#define MPRIS_PLAYER_IFACE "org.mpris.MediaPlayer2.Player"
#define MPRIS_ROOT_IFACE "org.mpris.MediaPlayer2"
#define DBUS_PROPERTIES_IFACE "org.freedesktop.DBus.Properties"

typedef struct {
  gboolean found;
  gboolean playing;
  PwvizNowPlaying now_playing;
} PlayerCandidate;

void pwviz_now_playing_clear(PwvizNowPlaying *now_playing) {
  memset(now_playing, 0, sizeof(*now_playing));
}

static gboolean get_string_property(GDBusConnection *connection,
                                    const char *bus_name,
                                    const char *interface,
                                    const char *property, char *buffer,
                                    gsize buffer_size) {
  GVariant *result = g_dbus_connection_call_sync(
      connection, bus_name, "/org/mpris/MediaPlayer2", DBUS_PROPERTIES_IFACE,
      "Get", g_variant_new("(ss)", interface, property), G_VARIANT_TYPE("(v)"),
      G_DBUS_CALL_FLAGS_NONE, 100, NULL, NULL);

  if (!result)
    return FALSE;

  GVariant *value = NULL;
  g_variant_get(result, "(v)", &value);
  if (value && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
    g_strlcpy(buffer, g_variant_get_string(value, NULL), buffer_size);

  g_clear_pointer(&value, g_variant_unref);
  g_variant_unref(result);
  return buffer[0] != '\0';
}

static void set_bus_fallback_app(const char *bus_name, char *buffer,
                                 gsize buffer_size) {
  const char *name = g_str_has_prefix(bus_name, MPRIS_PREFIX)
                         ? bus_name + strlen(MPRIS_PREFIX)
                         : bus_name;

  g_strlcpy(buffer, name, buffer_size);
}

static void copy_metadata_string(GVariant *metadata, const char *key,
                                 char *buffer, gsize buffer_size) {
  GVariant *value = g_variant_lookup_value(metadata, key,
                                           G_VARIANT_TYPE_STRING);

  if (!value)
    return;

  g_strlcpy(buffer, g_variant_get_string(value, NULL), buffer_size);
  g_variant_unref(value);
}

static void copy_metadata_string_array(GVariant *metadata, const char *key,
                                       char *buffer, gsize buffer_size) {
  GVariant *value = g_variant_lookup_value(metadata, key, G_VARIANT_TYPE("as"));

  if (!value)
    return;

  gsize length = 0;
  const char **items = g_variant_get_strv(value, &length);

  buffer[0] = '\0';
  for (gsize i = 0; i < length; i++) {
    if (i > 0)
      g_strlcat(buffer, ", ", buffer_size);
    g_strlcat(buffer, items[i], buffer_size);
  }

  g_free(items);
  g_variant_unref(value);
}

static gboolean get_metadata(GDBusConnection *connection, const char *bus_name,
                             PlayerCandidate *candidate) {
  GVariant *result = g_dbus_connection_call_sync(
      connection, bus_name, "/org/mpris/MediaPlayer2", DBUS_PROPERTIES_IFACE,
      "Get", g_variant_new("(ss)", MPRIS_PLAYER_IFACE, "Metadata"),
      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 100, NULL, NULL);

  if (!result)
    return FALSE;

  GVariant *value = NULL;
  g_variant_get(result, "(v)", &value);
  if (value && g_variant_is_of_type(value, G_VARIANT_TYPE("a{sv}"))) {
    copy_metadata_string(value, "xesam:title", candidate->now_playing.title,
                         sizeof(candidate->now_playing.title));
    copy_metadata_string_array(value, "xesam:artist",
                               candidate->now_playing.artist,
                               sizeof(candidate->now_playing.artist));
    copy_metadata_string(value, "xesam:album", candidate->now_playing.album,
                         sizeof(candidate->now_playing.album));
  }

  g_clear_pointer(&value, g_variant_unref);
  g_variant_unref(result);
  return candidate->now_playing.title[0] != '\0' ||
         candidate->now_playing.artist[0] != '\0' ||
         candidate->now_playing.album[0] != '\0';
}

static gboolean read_player(GDBusConnection *connection, const char *bus_name,
                            PlayerCandidate *candidate) {
  char playback_status[32] = {0};

  pwviz_now_playing_clear(&candidate->now_playing);
  candidate->found = FALSE;
  candidate->playing = FALSE;

  get_string_property(connection, bus_name, MPRIS_ROOT_IFACE, "Identity",
                      candidate->now_playing.app,
                      sizeof(candidate->now_playing.app));
  if (candidate->now_playing.app[0] == '\0')
    set_bus_fallback_app(bus_name, candidate->now_playing.app,
                         sizeof(candidate->now_playing.app));

  get_string_property(connection, bus_name, MPRIS_PLAYER_IFACE,
                      "PlaybackStatus", playback_status,
                      sizeof(playback_status));
  candidate->playing = g_strcmp0(playback_status, "Playing") == 0;

  if (!get_metadata(connection, bus_name, candidate))
    return FALSE;

  candidate->found = TRUE;
  candidate->now_playing.available = TRUE;
  return TRUE;
}

gboolean pwviz_now_playing_refresh(PwvizNowPlaying *now_playing) {
  GDBusConnection *connection =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);

  if (!connection) {
    pwviz_now_playing_clear(now_playing);
    return FALSE;
  }

  GVariant *names_result = g_dbus_connection_call_sync(
      connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "ListNames", NULL, G_VARIANT_TYPE("(as)"),
      G_DBUS_CALL_FLAGS_NONE, 100, NULL, NULL);

  if (!names_result) {
    g_object_unref(connection);
    pwviz_now_playing_clear(now_playing);
    return FALSE;
  }

  PlayerCandidate best = {0};
  GVariantIter *iter = NULL;
  const char *name = NULL;

  g_variant_get(names_result, "(as)", &iter);
  while (g_variant_iter_loop(iter, "&s", &name)) {
    if (!g_str_has_prefix(name, MPRIS_PREFIX))
      continue;

    PlayerCandidate candidate = {0};
    if (!read_player(connection, name, &candidate))
      continue;

    if (!best.found || candidate.playing) {
      best = candidate;
      if (candidate.playing)
        break;
    }
  }

  g_variant_iter_free(iter);
  g_variant_unref(names_result);
  g_object_unref(connection);

  if (!best.found) {
    pwviz_now_playing_clear(now_playing);
    return FALSE;
  }

  *now_playing = best.now_playing;
  return TRUE;
}
