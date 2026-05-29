#include "global_shortcut.h"

#include <gio/gio.h>

#define PORTAL_BUS "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_GLOBAL_SHORTCUTS "org.freedesktop.portal.GlobalShortcuts"
#define PORTAL_REQUEST "org.freedesktop.portal.Request"
#define SHORTCUT_ID "open_settings_ctrl_shift_alt_f12"

struct PwvizGlobalShortcut {
  GDBusConnection *connection;
  PwvizGlobalShortcutCallback callback;
  gpointer user_data;
  char *session_handle;
  guint activated_subscription;
  guint response_subscription;
  gboolean waiting_for_create;
  gboolean waiting_for_bind;
};

static void subscribe_activated(PwvizGlobalShortcut *shortcut);
static void bind_shortcut(PwvizGlobalShortcut *shortcut);

static char *token(const char *prefix) {
  return g_strdup_printf("%s_%u", prefix, g_random_int());
}

static void request_response_cb(GDBusConnection *connection,
                                const char *sender_name,
                                const char *object_path,
                                const char *interface_name,
                                const char *signal_name,
                                GVariant *parameters, gpointer user_data) {
  (void)connection;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;

  PwvizGlobalShortcut *shortcut = user_data;
  guint response = 1;
  GVariant *results = NULL;

  g_variant_get(parameters, "(u@a{sv})", &response, &results);
  if (response != 0) {
    g_warning("Global shortcut portal request failed with response %u",
              response);
    g_variant_unref(results);
    return;
  }

  if (shortcut->waiting_for_create) {
    GVariant *session =
        g_variant_lookup_value(results, "session_handle", G_VARIANT_TYPE_STRING);

    shortcut->waiting_for_create = FALSE;
    if (session) {
      shortcut->session_handle = g_variant_dup_string(session, NULL);
      g_variant_unref(session);
      g_message("Global shortcut portal session created");
      bind_shortcut(shortcut);
    } else {
      g_warning("Global shortcut portal did not return a session handle");
    }
  } else if (shortcut->waiting_for_bind) {
    GVariant *bound =
        g_variant_lookup_value(results, "shortcuts", G_VARIANT_TYPE("a(sa{sv})"));

    shortcut->waiting_for_bind = FALSE;
    if (bound) {
      if (g_variant_n_children(bound) == 0)
        g_warning("Global shortcut was not bound by the portal");
      else {
        GVariantIter iter;
        const char *id = NULL;
        GVariant *props = NULL;

        g_message("Global shortcut bound through portal");
        g_variant_iter_init(&iter, bound);
        while (g_variant_iter_next(&iter, "(&s@a{sv})", &id, &props)) {
          GVariant *trigger = g_variant_lookup_value(
              props, "trigger_description", G_VARIANT_TYPE_STRING);

          if (trigger) {
            g_message("Global shortcut %s trigger: %s", id,
                      g_variant_get_string(trigger, NULL));
            g_variant_unref(trigger);
          }

          g_variant_unref(props);
        }
      }
      g_variant_unref(bound);
    }
    subscribe_activated(shortcut);
  }

  g_variant_unref(results);
}

static void activated_cb(GDBusConnection *connection, const char *sender_name,
                         const char *object_path, const char *interface_name,
                         const char *signal_name, GVariant *parameters,
                         gpointer user_data) {
  (void)connection;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;

  PwvizGlobalShortcut *shortcut = user_data;
  const char *session_handle = NULL;
  const char *shortcut_id = NULL;
  guint64 timestamp = 0;
  GVariant *options = NULL;

  g_variant_get(parameters, "(&o&st@a{sv})", &session_handle, &shortcut_id,
                &timestamp, &options);
  if (g_strcmp0(shortcut_id, SHORTCUT_ID) == 0 && shortcut->callback)
    shortcut->callback(shortcut->user_data);
  g_variant_unref(options);
}

static void subscribe_activated(PwvizGlobalShortcut *shortcut) {
  if (shortcut->activated_subscription)
    return;

  shortcut->activated_subscription = g_dbus_connection_signal_subscribe(
      shortcut->connection, PORTAL_BUS, PORTAL_GLOBAL_SHORTCUTS, "Activated",
      PORTAL_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE, activated_cb, shortcut,
      NULL);
}

static void bind_shortcut(PwvizGlobalShortcut *shortcut) {
  GVariantBuilder shortcuts;
  GVariantBuilder shortcut_properties;
  GVariantBuilder options;
  g_autofree char *handle_token = token("bind");
  g_autoptr(GError) error = NULL;
  GVariant *reply = NULL;

  g_variant_builder_init(&shortcut_properties, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&shortcut_properties, "{sv}", "description",
                        g_variant_new_string("Open pwviz settings"));
  g_variant_builder_add(&shortcut_properties, "{sv}", "preferred_trigger",
                        g_variant_new_string("CTRL+SHIFT+ALT+F12"));

  g_variant_builder_init(&shortcuts, G_VARIANT_TYPE("a(sa{sv})"));
  g_variant_builder_add(&shortcuts, "(sa{sv})", SHORTCUT_ID,
                        &shortcut_properties);

  g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string(handle_token));

  shortcut->waiting_for_bind = TRUE;
  reply = g_dbus_connection_call_sync(
      shortcut->connection, PORTAL_BUS, PORTAL_PATH, PORTAL_GLOBAL_SHORTCUTS,
      "BindShortcuts",
      g_variant_new("(oa(sa{sv})sa{sv})", shortcut->session_handle, &shortcuts,
                    "", &options),
      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (!reply) {
    g_warning("Global shortcut BindShortcuts failed: %s",
              error ? error->message : "unknown error");
    shortcut->waiting_for_bind = FALSE;
    return;
  }

  g_variant_unref(reply);
}

PwvizGlobalShortcut *
pwviz_global_shortcut_register(PwvizGlobalShortcutCallback callback,
                               gpointer user_data) {
  PwvizGlobalShortcut *shortcut = g_new0(PwvizGlobalShortcut, 1);
  GVariantBuilder options;
  g_autofree char *handle_token = token("create");
  g_autofree char *session_token = token("session");
  g_autoptr(GError) error = NULL;
  GVariant *reply = NULL;

  shortcut->callback = callback;
  shortcut->user_data = user_data;
  shortcut->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (!shortcut->connection)
    return shortcut;

  shortcut->response_subscription = g_dbus_connection_signal_subscribe(
      shortcut->connection, PORTAL_BUS, PORTAL_REQUEST, "Response", NULL, NULL,
      G_DBUS_SIGNAL_FLAGS_NONE, request_response_cb, shortcut, NULL);

  g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string(handle_token));
  g_variant_builder_add(&options, "{sv}", "session_handle_token",
                        g_variant_new_string(session_token));

  shortcut->waiting_for_create = TRUE;
  reply = g_dbus_connection_call_sync(
      shortcut->connection, PORTAL_BUS, PORTAL_PATH, PORTAL_GLOBAL_SHORTCUTS,
      "CreateSession", g_variant_new("(a{sv})", &options),
      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (!reply) {
    g_warning("Global shortcut CreateSession failed: %s",
              error ? error->message : "unknown error");
    shortcut->waiting_for_create = FALSE;
    return shortcut;
  }

  g_variant_unref(reply);
  return shortcut;
}

void pwviz_global_shortcut_free(PwvizGlobalShortcut *shortcut) {
  if (!shortcut)
    return;

  if (shortcut->connection) {
    if (shortcut->activated_subscription)
      g_dbus_connection_signal_unsubscribe(shortcut->connection,
                                           shortcut->activated_subscription);
    if (shortcut->response_subscription)
      g_dbus_connection_signal_unsubscribe(shortcut->connection,
                                           shortcut->response_subscription);
    g_object_unref(shortcut->connection);
  }

  g_free(shortcut->session_handle);
  g_free(shortcut);
}
