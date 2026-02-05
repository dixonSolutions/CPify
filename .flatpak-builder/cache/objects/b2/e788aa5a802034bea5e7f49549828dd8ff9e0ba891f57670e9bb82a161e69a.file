#pragma once

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct {
  gchar *path;         // absolute file path
  gchar *title;        // display name
  GdkPixbuf *thumbnail; // video thumbnail (can be NULL)
  gboolean is_video;   // TRUE if this is a video file
} CPifyTrack;

CPifyTrack *cpify_track_new(const gchar *abs_path);
void cpify_track_free(CPifyTrack *track);

// Returns a GPtrArray of CPifyTrack* (free with g_ptr_array_unref)
GPtrArray *cpify_scan_folder(const gchar *folder_path, GError **error);

// Generate thumbnail for a track (call asynchronously)
void cpify_track_generate_thumbnail(CPifyTrack *track);

// Check if file is a video based on extension
gboolean cpify_is_video_file(const gchar *path);
