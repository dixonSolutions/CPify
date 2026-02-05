#include "media_scanner.h"

#include <gio/gio.h>
#include <gst/gst.h>
#include <string.h>

static const gchar *VIDEO_EXTS[] = {
  ".mp4", ".mkv", ".webm", ".mov", ".avi", ".mpg", ".mpeg", ".m4v", ".wmv",
};

static const gchar *AUDIO_EXTS[] = {
  ".mp3", ".flac", ".ogg", ".opus", ".wav", ".m4a", ".aac", ".wma",
};

gboolean cpify_is_video_file(const gchar *path) {
  if (!path) return FALSE;
  gchar *lower = g_ascii_strdown(path, -1);
  gboolean is_video = FALSE;
  for (guint i = 0; i < G_N_ELEMENTS(VIDEO_EXTS); i++) {
    if (g_str_has_suffix(lower, VIDEO_EXTS[i])) {
      is_video = TRUE;
      break;
    }
  }
  g_free(lower);
  return is_video;
}

static gboolean has_supported_extension(const gchar *path) {
  if (!path) return FALSE;
  gchar *lower = g_ascii_strdown(path, -1);
  gboolean ok = FALSE;
  
  for (guint i = 0; i < G_N_ELEMENTS(VIDEO_EXTS); i++) {
    if (g_str_has_suffix(lower, VIDEO_EXTS[i])) {
      ok = TRUE;
      break;
    }
  }
  if (!ok) {
    for (guint i = 0; i < G_N_ELEMENTS(AUDIO_EXTS); i++) {
      if (g_str_has_suffix(lower, AUDIO_EXTS[i])) {
        ok = TRUE;
        break;
      }
    }
  }
  g_free(lower);
  return ok;
}

CPifyTrack *cpify_track_new(const gchar *abs_path) {
  if (!abs_path) return NULL;
  CPifyTrack *t = g_new0(CPifyTrack, 1);
  t->path = g_strdup(abs_path);
  t->title = g_path_get_basename(abs_path);
  t->is_video = cpify_is_video_file(abs_path);
  t->thumbnail = NULL;
  return t;
}

void cpify_track_free(CPifyTrack *track) {
  if (!track) return;
  g_free(track->path);
  g_free(track->title);
  if (track->thumbnail) {
    g_object_unref(track->thumbnail);
    track->thumbnail = NULL;
  }
  g_free(track);
}

void cpify_track_generate_thumbnail(CPifyTrack *track) {
  if (!track || !track->path || !track->is_video) return;
  if (track->thumbnail) return;  // Already have one
  
  // Create a pipeline to extract a frame
  GError *err = NULL;
  gchar *uri = g_filename_to_uri(track->path, NULL, &err);
  if (!uri) {
    if (err) g_error_free(err);
    return;
  }
  
  // Build pipeline: uridecodebin ! videoconvert ! videoscale ! gdkpixbufsink
  gchar *pipeline_str = g_strdup_printf(
    "uridecodebin uri=\"%s\" ! videoconvert ! videoscale ! "
    "video/x-raw,width=180,height=120 ! gdkpixbufsink name=sink",
    uri
  );
  g_free(uri);
  
  GstElement *pipeline = gst_parse_launch(pipeline_str, &err);
  g_free(pipeline_str);
  
  if (!pipeline) {
    if (err) g_error_free(err);
    return;
  }
  
  // Seek to 10% of the video for a good frame
  gst_element_set_state(pipeline, GST_STATE_PAUSED);
  
  // Wait for state change
  GstStateChangeReturn ret = gst_element_get_state(pipeline, NULL, NULL, 2 * GST_SECOND);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return;
  }
  
  // Query duration and seek to 10%
  gint64 duration = 0;
  if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &duration) && duration > 0) {
    gint64 seek_pos = duration / 10;  // 10% into the video
    gst_element_seek_simple(pipeline, GST_FORMAT_TIME, 
                            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, seek_pos);
    gst_element_get_state(pipeline, NULL, NULL, GST_SECOND);
  }
  
  // Get the pixbuf from the sink
  GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
  if (sink) {
    GdkPixbuf *pixbuf = NULL;
    g_object_get(sink, "last-pixbuf", &pixbuf, NULL);
    if (pixbuf) {
      track->thumbnail = pixbuf;  // Transfer ownership
    }
    gst_object_unref(sink);
  }
  
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
}

static void scan_dir_recursive(GPtrArray *out, GFile *dir, GCancellable *cancellable) {
  GError *err = NULL;
  GFileEnumerator *en = g_file_enumerate_children(
      dir,
      "standard::name,standard::type,standard::is-symlink,standard::size",
      G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
      cancellable,
      &err
  );
  if (!en) {
    if (err) g_error_free(err);
    return;
  }

  for (;;) {
    GFileInfo *info = g_file_enumerator_next_file(en, cancellable, &err);
    if (!info) break;

    GFileType type = g_file_info_get_file_type(info);
    const gchar *name = g_file_info_get_name(info);
    if (!name || name[0] == '\0') {
      g_object_unref(info);
      continue;
    }

    GFile *child = g_file_get_child(dir, name);

    if (type == G_FILE_TYPE_DIRECTORY) {
      scan_dir_recursive(out, child, cancellable);
      g_object_unref(child);
      g_object_unref(info);
      continue;
    }

    if (type == G_FILE_TYPE_REGULAR) {
      gchar *path = g_file_get_path(child);
      if (path && has_supported_extension(path)) {
        CPifyTrack *t = cpify_track_new(path);
        if (t) g_ptr_array_add(out, t);
      }
      g_free(path);
    }

    g_object_unref(child);
    g_object_unref(info);
  }

  if (err) g_error_free(err);
  g_object_unref(en);
}

static gint track_title_cmp(gconstpointer a, gconstpointer b) {
  // g_ptr_array_sort passes pointers to array elements (CPifyTrack **)
  const CPifyTrack *ta = *(const CPifyTrack **)a;
  const CPifyTrack *tb = *(const CPifyTrack **)b;
  if (!ta || !tb) return 0;
  return g_ascii_strcasecmp(ta->title ? ta->title : "", tb->title ? tb->title : "");
}

GPtrArray *cpify_scan_folder(const gchar *folder_path, GError **error) {
  if (!folder_path || folder_path[0] == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Empty folder path");
    return NULL;
  }

  GFile *root = g_file_new_for_path(folder_path);
  if (!root) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to open folder");
    return NULL;
  }

  GPtrArray *tracks = g_ptr_array_new_with_free_func((GDestroyNotify)cpify_track_free);
  scan_dir_recursive(tracks, root, NULL);
  g_object_unref(root);

  g_ptr_array_sort(tracks, track_title_cmp);
  return tracks;
}

