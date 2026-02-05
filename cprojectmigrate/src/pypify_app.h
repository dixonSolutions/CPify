#pragma once

#include <adwaita.h>

typedef struct _PypifyApp PypifyApp;

PypifyApp *pypify_app_new(AdwApplication *app);
void pypify_app_show(PypifyApp *p);
