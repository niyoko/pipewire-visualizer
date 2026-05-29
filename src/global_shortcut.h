#pragma once

#include <glib.h>

typedef enum {
  PWVIZ_GLOBAL_SHORTCUT_OPEN_SETTINGS,
  PWVIZ_GLOBAL_SHORTCUT_LYRICS_OFFSET_BACK,
  PWVIZ_GLOBAL_SHORTCUT_LYRICS_OFFSET_FORWARD,
} PwvizGlobalShortcutAction;

typedef void (*PwvizGlobalShortcutCallback)(PwvizGlobalShortcutAction action,
                                            gpointer user_data);

typedef struct PwvizGlobalShortcut PwvizGlobalShortcut;

PwvizGlobalShortcut *
pwviz_global_shortcut_register(PwvizGlobalShortcutCallback callback,
                               gpointer user_data);
void pwviz_global_shortcut_free(PwvizGlobalShortcut *shortcut);
