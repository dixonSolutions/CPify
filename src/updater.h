#pragma once

#include <glib.h>
#include <adwaita.h>

// Release information from GitHub
typedef struct {
  gchar *tag_name;      // e.g., "V0.0.2"
  gchar *title;         // e.g., "CPify V0.0.2 Release"
  gchar *description;   // Release body/description
  gchar *download_url;  // URL to the binary for current OS
  gchar *published_at;  // Publication date
} CPifyReleaseInfo;

// Callback for when update check completes
// release will be NULL if no update available or on error
typedef void (*CPifyUpdateCallback)(CPifyReleaseInfo *release, GError *error, gpointer user_data);

// Initialize the updater system
void cpify_updater_init(void);

// Cleanup updater resources
void cpify_updater_cleanup(void);

// Get the current app version string (e.g., "0.0.1")
const gchar *cpify_updater_get_current_version(void);

// Get the OS identifier for binary matching (e.g., "linux", "windows", "macos")
const gchar *cpify_updater_get_os_identifier(void);

// Check for updates asynchronously
// callback will be called with release info if update available, NULL otherwise
void cpify_updater_check_async(CPifyUpdateCallback callback, gpointer user_data);

// Show the update dialog with release info
// parent_window: The parent window for the dialog
// release: The release info to display
// Returns: The created dialog widget
GtkWidget *cpify_updater_show_dialog(GtkWindow *parent_window, CPifyReleaseInfo *release);

// Download and install the update
// This will download the binary and replace the current executable
void cpify_updater_install_async(CPifyReleaseInfo *release, 
                                  GtkWindow *parent_window,
                                  GCallback on_complete, 
                                  gpointer user_data);

// Free a release info struct
void cpify_release_info_free(CPifyReleaseInfo *info);

// Compare version strings
// Returns: -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
gint cpify_version_compare(const gchar *v1, const gchar *v2);
