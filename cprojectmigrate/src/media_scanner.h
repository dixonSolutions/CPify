#pragma once

#include <glib.h>

typedef struct {
  gchar *path;   // absolute file path
  gchar *title;  // display name
} PypifyTrack;

PypifyTrack *pypify_track_new(const gchar *abs_path);
void pypify_track_free(PypifyTrack *track);

// Returns a GPtrArray of PypifyTrack* (free with g_ptr_array_unref)
GPtrArray *pypify_scan_folder(const gchar *folder_path, GError **error);

