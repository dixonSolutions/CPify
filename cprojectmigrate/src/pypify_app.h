#pragma once

#include <gtk/gtk.h>

typedef struct _PypifyApp PypifyApp;

PypifyApp *pypify_app_new(GtkApplication *app);
void pypify_app_show(PypifyApp *p);

