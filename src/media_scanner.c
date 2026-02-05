#include "media_scanner.h"

#include <gio/gio.h>
#include <string.h>

static gboolean has_supported_extension(const gchar *path) {
  if (!path) return FALSE;

  static const gchar *exts[] = {
      // audio
      ".mp3", ".flac", ".ogg", ".opus", ".wav", ".m4a", ".aac", ".wma",
      // video
      ".mp4", ".mkv", ".webm", ".mov", ".avi", ".mpg", ".mpeg", ".m4v", ".wmv",
  };

  gchar *lower = g_ascii_strdown(path, -1);
  gboolean ok = FALSE;
  for (guint i = 0; i < G_N_ELEMENTS(exts); i++) {
    if (g_str_has_suffix(lower, exts[i])) {
      ok = TRUE;
      break;
    }
  }
  g_free(lower);
  return ok;
}

PypifyTrack *pypify_track_new(const gchar *abs_path) {
  if (!abs_path) return NULL;
  PypifyTrack *t = g_new0(PypifyTrack, 1);
  t->path = g_strdup(abs_path);
  t->title = g_path_get_basename(abs_path);
  return t;
}

void pypify_track_free(PypifyTrack *track) {
  if (!track) return;
  g_free(track->path);
  g_free(track->title);
  g_free(track);
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
        PypifyTrack *t = pypify_track_new(path);
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
  // g_ptr_array_sort passes pointers to array elements (PypifyTrack **)
  const PypifyTrack *ta = *(const PypifyTrack **)a;
  const PypifyTrack *tb = *(const PypifyTrack **)b;
  if (!ta || !tb) return 0;
  return g_ascii_strcasecmp(ta->title ? ta->title : "", tb->title ? tb->title : "");
}

GPtrArray *pypify_scan_folder(const gchar *folder_path, GError **error) {
  if (!folder_path || folder_path[0] == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Empty folder path");
    return NULL;
  }

  GFile *root = g_file_new_for_path(folder_path);
  if (!root) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to open folder");
    return NULL;
  }

  GPtrArray *tracks = g_ptr_array_new_with_free_func((GDestroyNotify)pypify_track_free);
  scan_dir_recursive(tracks, root, NULL);
  g_object_unref(root);

  g_ptr_array_sort(tracks, track_title_cmp);
  return tracks;
}

