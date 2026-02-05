#pragma once

#include <glib.h>
#include <adwaita.h>

typedef enum {
  CPIFY_THEME_SYSTEM,
  CPIFY_THEME_LIGHT,
  CPIFY_THEME_DARK
} CPifyTheme;

typedef struct {
  CPifyTheme theme;
  gdouble volume;       // 0-100
  gdouble speed;        // 25-200 (as percentage)
  gboolean audio_enabled;
  gboolean video_enabled;
  gint layout;          // 0 = sidebar, 1 = gallery
  gchar *last_folder;
} CPifySettings;

// Initialize settings system and load from disk
void cpify_settings_init(void);

// Get the global settings instance
CPifySettings *cpify_settings_get(void);

// Save settings to disk
void cpify_settings_save(void);

// Free settings resources
void cpify_settings_cleanup(void);

// Apply theme to the application
void cpify_settings_apply_theme(AdwApplication *app, CPifyTheme theme);

// Get whether dark mode is active
gboolean cpify_settings_is_dark_mode(void);
