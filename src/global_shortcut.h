#pragma once

#include <glib.h>

typedef void (*PwvizGlobalShortcutCallback)(gpointer user_data);

typedef struct PwvizGlobalShortcut PwvizGlobalShortcut;

PwvizGlobalShortcut *
pwviz_global_shortcut_register(PwvizGlobalShortcutCallback callback,
                               gpointer user_data);
void pwviz_global_shortcut_free(PwvizGlobalShortcut *shortcut);
