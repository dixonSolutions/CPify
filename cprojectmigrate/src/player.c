#include "player.h"

#include <gio/gio.h>
#include <gst/video/videooverlay.h>

// playbin "flags" is a bitmask
#define PYPIFY_PLAY_FLAG_VIDEO (1u << 0)
#define PYPIFY_PLAY_FLAG_AUDIO (1u << 1)
#define PYPIFY_PLAY_FLAG_DOWNLOAD (1u << 7)

static gboolean on_bus_message(GstBus *bus, GstMessage *msg, gpointer user_data) {
  (void)bus;
  PypifyPlayer *p = (PypifyPlayer *)user_data;
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
      if (p->eos_cb) p->eos_cb(p->eos_cb_data);
      break;
    case GST_MESSAGE_ERROR: {
      GError *err = NULL;
      gchar *dbg = NULL;
      gst_message_parse_error(msg, &err, &dbg);
      g_printerr("GStreamer error: %s\n", err ? err->message : "unknown");
      if (dbg) g_printerr("Debug: %s\n", dbg);
      if (err) g_error_free(err);
      g_free(dbg);
      break;
    }
    default:
      break;
  }
  return G_SOURCE_CONTINUE;
}

static GtkWidget *try_create_gtk4_sink_widget(GstElement *playbin, GdkPaintable **out_paintable) {
  g_print("[DEBUG] try_create_gtk4_sink_widget: attempting gtk4paintablesink...\n");
  
  // Try gtk4paintablesink first (from gst-plugins-good or gst-plugin-gtk4)
  GstElement *sink = gst_element_factory_make("gtk4paintablesink", NULL);
  if (!sink) {
    g_print("[DEBUG] gtk4paintablesink not available, trying gtksink...\n");
    // Fallback to gtksink if available (older approach)
    sink = gst_element_factory_make("gtksink", NULL);
    if (!sink) {
      g_print("[DEBUG] No GTK video sink available!\n");
      return NULL;
    }
    // gtksink provides widget directly
    g_object_set(playbin, "video-sink", sink, NULL);
    GtkWidget *widget = NULL;
    g_object_get(sink, "widget", &widget, NULL);
    *out_paintable = NULL;
    return widget;
  }
  
  // gtk4paintablesink provides a GdkPaintable
  g_print("[DEBUG] gtk4paintablesink created successfully\n");
  
  // Create a glsinkbin to wrap the paintable sink for OpenGL support
  GstElement *glsinkbin = gst_element_factory_make("glsinkbin", NULL);
  if (glsinkbin) {
    g_object_set(glsinkbin, "sink", sink, NULL);
    g_object_set(playbin, "video-sink", glsinkbin, NULL);
  } else {
    g_object_set(playbin, "video-sink", sink, NULL);
  }
  
  // Get the paintable from the sink
  GdkPaintable *paintable = NULL;
  g_object_get(sink, "paintable", &paintable, NULL);
  
  if (!paintable) {
    g_print("[DEBUG] Failed to get paintable from sink\n");
    return NULL;
  }
  
  *out_paintable = paintable;
  
  // Create a GtkPicture to display the paintable
  GtkWidget *picture = gtk_picture_new_for_paintable(paintable);
  gtk_widget_set_hexpand(picture, TRUE);
  gtk_widget_set_vexpand(picture, TRUE);
  gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
  
  g_print("[DEBUG] Created GtkPicture for video display\n");
  return picture;
}

PypifyPlayer *pypify_player_new(void) {
  g_print("[DEBUG] pypify_player_new: called\n");
  PypifyPlayer *p = g_new0(PypifyPlayer, 1);
  
  p->playbin = gst_element_factory_make("playbin", "pypify-playbin");
  if (!p->playbin) {
    g_printerr("[DEBUG] pypify_player_new: Unable to create GStreamer playbin!\n");
    g_free(p);
    return NULL;
  }
  g_print("[DEBUG] pypify_player_new: playbin created\n");

  p->audio_enabled = TRUE;
  p->video_enabled = TRUE;
  p->volume = 0.8;
  p->rate = 1.0;

  // Create GTK4-compatible video widget
  p->video_widget = try_create_gtk4_sink_widget(p->playbin, &p->paintable);
  if (p->video_widget) {
    g_print("[DEBUG] pypify_player_new: GTK4 video widget created\n");
    g_object_get(p->playbin, "video-sink", &p->video_sink, NULL);
  } else {
    // Fallback: create a simple drawing area with message
    g_print("[DEBUG] pypify_player_new: No video sink, using placeholder\n");
    p->video_widget = gtk_label_new("Video playback unavailable\n(install gstreamer1-plugin-gtk4)");
    gtk_widget_set_hexpand(p->video_widget, TRUE);
    gtk_widget_set_vexpand(p->video_widget, TRUE);
  }

  p->bus = gst_element_get_bus(p->playbin);
  p->bus_watch_id = gst_bus_add_watch(p->bus, on_bus_message, p);
  g_object_set(p->playbin, "volume", p->volume, NULL);
  
  g_print("[DEBUG] pypify_player_new: done\n");
  return p;
}

void pypify_player_free(PypifyPlayer *p) {
  if (!p) return;
  pypify_player_stop(p);
  if (p->bus_watch_id) {
    g_source_remove(p->bus_watch_id);
    p->bus_watch_id = 0;
  }
  if (p->bus) {
    gst_object_unref(p->bus);
    p->bus = NULL;
  }
  if (p->playbin) {
    gst_object_unref(p->playbin);
    p->playbin = NULL;
  }
  if (p->video_sink) {
    gst_object_unref(p->video_sink);
    p->video_sink = NULL;
  }
  if (p->paintable) {
    g_object_unref(p->paintable);
    p->paintable = NULL;
  }
  // video_widget is owned by GTK containers; don't unref here
  p->video_widget = NULL;
  g_free(p);
}

GtkWidget *pypify_player_get_video_widget(PypifyPlayer *p) {
  return p ? p->video_widget : NULL;
}

void pypify_player_set_eos_callback(PypifyPlayer *p, PypifyPlayerEosCallback cb, gpointer user_data) {
  if (!p) return;
  p->eos_cb = cb;
  p->eos_cb_data = user_data;
}

gboolean pypify_player_set_path(PypifyPlayer *p, const gchar *abs_path, GError **error) {
  g_print("[DEBUG] pypify_player_set_path: path='%s'\n", abs_path ? abs_path : "(null)");
  if (!p || !abs_path || abs_path[0] == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid path");
    return FALSE;
  }

  gst_element_set_state(p->playbin, GST_STATE_NULL);

  gchar *uri = gst_filename_to_uri(abs_path, NULL);
  if (!uri) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to build URI from path");
    return FALSE;
  }
  g_print("[DEBUG] pypify_player_set_path: URI='%s'\n", uri);

  // Apply audio/video flags
  guint flags = 0;
  g_object_get(p->playbin, "flags", &flags, NULL);
  flags |= PYPIFY_PLAY_FLAG_DOWNLOAD;
  if (p->audio_enabled) {
    flags |= PYPIFY_PLAY_FLAG_AUDIO;
  } else {
    flags &= ~PYPIFY_PLAY_FLAG_AUDIO;
  }
  if (p->video_enabled) {
    flags |= PYPIFY_PLAY_FLAG_VIDEO;
  } else {
    flags &= ~PYPIFY_PLAY_FLAG_VIDEO;
  }
  g_object_set(p->playbin, "flags", flags, "volume", p->volume, NULL);
  g_object_set(p->playbin, "uri", uri, NULL);
  g_free(uri);
  
  return TRUE;
}

void pypify_player_play(PypifyPlayer *p) {
  if (!p) return;
  gst_element_set_state(p->playbin, GST_STATE_PLAYING);
}

void pypify_player_pause(PypifyPlayer *p) {
  if (!p) return;
  gst_element_set_state(p->playbin, GST_STATE_PAUSED);
}

void pypify_player_stop(PypifyPlayer *p) {
  if (!p) return;
  gst_element_set_state(p->playbin, GST_STATE_NULL);
}

void pypify_player_set_volume(PypifyPlayer *p, gdouble volume_0_to_1) {
  if (!p) return;
  if (volume_0_to_1 < 0.0) volume_0_to_1 = 0.0;
  if (volume_0_to_1 > 1.0) volume_0_to_1 = 1.0;
  p->volume = volume_0_to_1;
  g_object_set(p->playbin, "volume", p->volume, NULL);
}

void pypify_player_set_audio_enabled(PypifyPlayer *p, gboolean enabled) {
  if (!p) return;
  p->audio_enabled = enabled ? TRUE : FALSE;
}

void pypify_player_set_video_enabled(PypifyPlayer *p, gboolean enabled) {
  if (!p) return;
  p->video_enabled = enabled ? TRUE : FALSE;
}

static gboolean do_seek_with_rate(PypifyPlayer *p, gint64 start_ns) {
  if (!p) return FALSE;
  GstSeekFlags flags = (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);
  gboolean ok = gst_element_seek(
      p->playbin,
      p->rate,
      GST_FORMAT_TIME,
      flags,
      GST_SEEK_TYPE_SET,
      start_ns,
      GST_SEEK_TYPE_NONE,
      GST_CLOCK_TIME_NONE
  );
  return ok ? TRUE : FALSE;
}

void pypify_player_set_rate(PypifyPlayer *p, gdouble rate) {
  if (!p) return;
  if (rate < 0.25) rate = 0.25;
  if (rate > 4.0) rate = 4.0;
  p->rate = rate;
  gint64 pos = 0;
  if (pypify_player_query_position(p, &pos)) {
    do_seek_with_rate(p, pos);
  }
}

gboolean pypify_player_seek_to(PypifyPlayer *p, gdouble position_seconds) {
  if (!p) return FALSE;
  if (position_seconds < 0.0) position_seconds = 0.0;
  gint64 ns = (gint64)(position_seconds * (gdouble)GST_SECOND);
  return do_seek_with_rate(p, ns);
}

gboolean pypify_player_seek_relative(PypifyPlayer *p, gdouble delta_seconds) {
  if (!p) return FALSE;
  gint64 pos = 0;
  if (!pypify_player_query_position(p, &pos)) return FALSE;
  gint64 delta = (gint64)(delta_seconds * (gdouble)GST_SECOND);
  gint64 target = pos + delta;
  if (target < 0) target = 0;
  return do_seek_with_rate(p, target);
}

gboolean pypify_player_query_position(PypifyPlayer *p, gint64 *out_position_ns) {
  if (!p || !out_position_ns) return FALSE;
  gint64 pos = 0;
  if (!gst_element_query_position(p->playbin, GST_FORMAT_TIME, &pos)) return FALSE;
  *out_position_ns = pos;
  return TRUE;
}

gboolean pypify_player_query_duration(PypifyPlayer *p, gint64 *out_duration_ns) {
  if (!p || !out_duration_ns) return FALSE;
  gint64 dur = 0;
  if (!gst_element_query_duration(p->playbin, GST_FORMAT_TIME, &dur)) return FALSE;
  *out_duration_ns = dur;
  return TRUE;
}
