#include "player.h"

#include <gio/gio.h>
#include <gst/video/videooverlay.h>

// playbin "flags" is a bitmask
#define CPIFY_PLAY_FLAG_VIDEO (1u << 0)
#define CPIFY_PLAY_FLAG_AUDIO (1u << 1)
#define CPIFY_PLAY_FLAG_DOWNLOAD (1u << 7)

static gboolean on_bus_message(GstBus *bus, GstMessage *msg, gpointer user_data) {
  (void)bus;
  CPifyPlayer *p = (CPifyPlayer *)user_data;
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

static GtkWidget *try_create_gtk4_sink_widget(GstElement *playbin, GdkPaintable **out_paintable, gboolean *out_has_video_sink) {
  g_print("[DEBUG] try_create_gtk4_sink_widget: creating gtk4paintablesink...\n");
  *out_has_video_sink = FALSE;
  *out_paintable = NULL;
  
  GstElement *gtksink = gst_element_factory_make("gtk4paintablesink", NULL);
  if (!gtksink) {
    g_print("[DEBUG] gtk4paintablesink not available, using autovideosink\n");
    GstElement *autosink = gst_element_factory_make("autovideosink", NULL);
    if (autosink) {
      g_object_set(playbin, "video-sink", autosink, NULL);
    }
    return NULL;
  }
  
  g_print("[DEBUG] gtk4paintablesink created\n");
  
  // Set up simple pipeline: just gtksink directly on playbin
  g_object_set(playbin, "video-sink", gtksink, NULL);
  
  // Get paintable
  GdkPaintable *paintable = NULL;
  g_object_get(gtksink, "paintable", &paintable, NULL);
  
  if (!paintable) {
    g_print("[DEBUG] Failed to get paintable\n");
    return NULL;
  }
  
  g_print("[DEBUG] Got paintable, creating GtkPicture\n");
  *out_paintable = paintable;
  *out_has_video_sink = TRUE;
  
  // Create GtkPicture
  GtkWidget *picture = gtk_picture_new_for_paintable(paintable);
  gtk_widget_set_hexpand(picture, TRUE);
  gtk_widget_set_vexpand(picture, TRUE);
  gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
  
  return picture;
}

CPifyPlayer *cpify_player_new(void) {
  g_print("[DEBUG] cpify_player_new: called\n");
  CPifyPlayer *p = g_new0(CPifyPlayer, 1);

  p->audio_enabled = TRUE;
  p->video_enabled = TRUE;
  p->volume = 0.8;
  p->rate = 1.0;
  
  // Use GtkMediaFile by default for embedded video in GTK4
  // GtkMediaFile uses the best available backend (usually GStreamer)
  // and displays video natively within our GTK4 interface
  p->use_gtk_media = TRUE;
  
  // Create a GtkPicture - we'll set its paintable from the media stream
  p->video_widget = gtk_picture_new();
  gtk_widget_set_hexpand(p->video_widget, TRUE);
  gtk_widget_set_vexpand(p->video_widget, TRUE);
  gtk_picture_set_content_fit(GTK_PICTURE(p->video_widget), GTK_CONTENT_FIT_CONTAIN);
  
  g_print("[DEBUG] cpify_player_new: created GtkPicture widget for embedded video\n");
  g_print("[DEBUG] cpify_player_new: done\n");
  return p;
}

void cpify_player_free(CPifyPlayer *p) {
  if (!p) return;
  cpify_player_stop(p);
  
  if (!p->use_gtk_media) {
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
  }
  
  if (p->media_stream) {
    g_object_unref(p->media_stream);
    p->media_stream = NULL;
  }
  
  // video_widget is owned by GTK containers; don't unref here
  p->video_widget = NULL;
  g_free(p);
}

GtkWidget *cpify_player_get_video_widget(CPifyPlayer *p) {
  return p ? p->video_widget : NULL;
}

void cpify_player_set_eos_callback(CPifyPlayer *p, CPifyPlayerEosCallback cb, gpointer user_data) {
  if (!p) return;
  p->eos_cb = cb;
  p->eos_cb_data = user_data;
}

static void on_media_stream_ended(GtkMediaStream *stream, GParamSpec *pspec, gpointer user_data) {
  (void)stream;
  (void)pspec;
  CPifyPlayer *p = (CPifyPlayer *)user_data;
  
  // Check if stream has ended
  if (p->media_stream && gtk_media_stream_get_ended(p->media_stream)) {
    g_print("[DEBUG] Media stream ended\n");
    if (p->eos_cb) {
      p->eos_cb(p->eos_cb_data);
    }
  }
}

gboolean cpify_player_set_path(CPifyPlayer *p, const gchar *abs_path, GError **error) {
  g_print("[DEBUG] cpify_player_set_path: path='%s'\n", abs_path ? abs_path : "(null)");
  if (!p || !abs_path || abs_path[0] == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid path");
    return FALSE;
  }

  if (p->use_gtk_media) {
    // Using GtkPicture with GtkMediaFile's paintable
    GFile *file = g_file_new_for_path(abs_path);
    
    // Create new media file
    if (p->media_stream) {
      g_signal_handlers_disconnect_by_data(p->media_stream, p);
      g_object_unref(p->media_stream);
    }
    
    p->media_stream = GTK_MEDIA_STREAM(gtk_media_file_new_for_file(file));
    g_object_unref(file);
    
    if (!p->media_stream) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to create media stream");
      return FALSE;
    }
    
    // Connect to ended signal
    g_signal_connect(p->media_stream, "notify::ended", G_CALLBACK(on_media_stream_ended), p);
    
    // Set volume and mute state
    gtk_media_stream_set_volume(p->media_stream, p->volume);
    gtk_media_stream_set_muted(p->media_stream, !p->audio_enabled);
    
    // Set the paintable on the GtkPicture
    // GtkMediaStream is a GdkPaintable, so we can use it directly
    gtk_picture_set_paintable(GTK_PICTURE(p->video_widget), GDK_PAINTABLE(p->media_stream));
    
    g_print("[DEBUG] cpify_player_set_path: set media stream paintable on GtkPicture\n");
    return TRUE;
  }
  
  // Legacy playbin path
  gst_element_set_state(p->playbin, GST_STATE_NULL);

  gchar *uri = gst_filename_to_uri(abs_path, NULL);
  if (!uri) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to build URI from path");
    return FALSE;
  }
  g_print("[DEBUG] cpify_player_set_path: URI='%s'\n", uri);

  // Apply audio/video flags
  guint flags = 0;
  g_object_get(p->playbin, "flags", &flags, NULL);
  flags |= CPIFY_PLAY_FLAG_DOWNLOAD;
  if (p->audio_enabled) {
    flags |= CPIFY_PLAY_FLAG_AUDIO;
  } else {
    flags &= ~CPIFY_PLAY_FLAG_AUDIO;
  }
  if (p->video_enabled) {
    flags |= CPIFY_PLAY_FLAG_VIDEO;
  } else {
    flags &= ~CPIFY_PLAY_FLAG_VIDEO;
  }
  g_object_set(p->playbin, "flags", flags, "volume", p->volume, NULL);
  g_object_set(p->playbin, "uri", uri, NULL);
  g_free(uri);
  
  return TRUE;
}

void cpify_player_play(CPifyPlayer *p) {
  if (!p) return;
  if (p->use_gtk_media && p->media_stream) {
    gtk_media_stream_play(p->media_stream);
    return;
  }
  if (p->playbin) {
    gst_element_set_state(p->playbin, GST_STATE_PLAYING);
  }
}

void cpify_player_pause(CPifyPlayer *p) {
  if (!p) return;
  if (p->use_gtk_media && p->media_stream) {
    gtk_media_stream_pause(p->media_stream);
    return;
  }
  if (p->playbin) {
    gst_element_set_state(p->playbin, GST_STATE_PAUSED);
  }
}

void cpify_player_stop(CPifyPlayer *p) {
  if (!p) return;
  if (p->use_gtk_media && p->media_stream) {
    gtk_media_stream_pause(p->media_stream);
    gtk_media_stream_seek(p->media_stream, 0);
    return;
  }
  if (p->playbin) {
    gst_element_set_state(p->playbin, GST_STATE_NULL);
  }
}

void cpify_player_set_volume(CPifyPlayer *p, gdouble volume_0_to_1) {
  if (!p) return;
  if (volume_0_to_1 < 0.0) volume_0_to_1 = 0.0;
  if (volume_0_to_1 > 1.0) volume_0_to_1 = 1.0;
  p->volume = volume_0_to_1;
  
  if (p->use_gtk_media && p->media_stream) {
    gtk_media_stream_set_volume(p->media_stream, p->volume);
    return;
  }
  if (p->playbin) {
    g_object_set(p->playbin, "volume", p->volume, NULL);
  }
}

void cpify_player_set_audio_enabled(CPifyPlayer *p, gboolean enabled) {
  if (!p) return;
  p->audio_enabled = enabled ? TRUE : FALSE;
  
  if (p->use_gtk_media && p->media_stream) {
    gtk_media_stream_set_muted(p->media_stream, !p->audio_enabled);
  }
}

void cpify_player_set_video_enabled(CPifyPlayer *p, gboolean enabled) {
  if (!p) return;
  p->video_enabled = enabled ? TRUE : FALSE;
  // GtkVideo doesn't support disabling video separately
}

static gboolean do_seek_with_rate(CPifyPlayer *p, gint64 start_ns) {
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

void cpify_player_set_rate(CPifyPlayer *p, gdouble rate) {
  if (!p) return;
  if (rate < 0.25) rate = 0.25;
  if (rate > 4.0) rate = 4.0;
  p->rate = rate;
  
  // GtkMediaStream doesn't support rate changes
  if (p->use_gtk_media) {
    return;
  }
  
  if (p->playbin) {
    gint64 pos = 0;
    if (cpify_player_query_position(p, &pos)) {
      do_seek_with_rate(p, pos);
    }
  }
}

gboolean cpify_player_seek_to(CPifyPlayer *p, gdouble position_seconds) {
  if (!p) return FALSE;
  if (position_seconds < 0.0) position_seconds = 0.0;
  
  if (p->use_gtk_media && p->media_stream) {
    gint64 pos_us = (gint64)(position_seconds * 1000000.0);  // microseconds
    gtk_media_stream_seek(p->media_stream, pos_us);
    return TRUE;
  }
  
  if (p->playbin) {
    gint64 ns = (gint64)(position_seconds * (gdouble)GST_SECOND);
    return do_seek_with_rate(p, ns);
  }
  return FALSE;
}

gboolean cpify_player_seek_relative(CPifyPlayer *p, gdouble delta_seconds) {
  if (!p) return FALSE;
  gint64 pos = 0;
  if (!cpify_player_query_position(p, &pos)) return FALSE;
  
  if (p->use_gtk_media && p->media_stream) {
    gint64 delta_us = (gint64)(delta_seconds * 1000000.0);
    gint64 target = pos + delta_us;  // pos is already in microseconds for gtk_media
    if (target < 0) target = 0;
    gtk_media_stream_seek(p->media_stream, target);
    return TRUE;
  }
  
  if (p->playbin) {
    gint64 delta = (gint64)(delta_seconds * (gdouble)GST_SECOND);
    gint64 target = pos + delta;
    if (target < 0) target = 0;
    return do_seek_with_rate(p, target);
  }
  return FALSE;
}

gboolean cpify_player_query_position(CPifyPlayer *p, gint64 *out_position_ns) {
  if (!p || !out_position_ns) return FALSE;
  
  if (p->use_gtk_media && p->media_stream) {
    // GtkMediaStream uses microseconds, we return nanoseconds for consistency
    gint64 pos_us = gtk_media_stream_get_timestamp(p->media_stream);
    *out_position_ns = pos_us * 1000;  // Convert to nanoseconds
    return TRUE;
  }
  
  if (p->playbin) {
    gint64 pos = 0;
    if (!gst_element_query_position(p->playbin, GST_FORMAT_TIME, &pos)) return FALSE;
    *out_position_ns = pos;
    return TRUE;
  }
  return FALSE;
}

gboolean cpify_player_query_duration(CPifyPlayer *p, gint64 *out_duration_ns) {
  if (!p || !out_duration_ns) return FALSE;
  
  if (p->use_gtk_media && p->media_stream) {
    // GtkMediaStream uses microseconds
    gint64 dur_us = gtk_media_stream_get_duration(p->media_stream);
    *out_duration_ns = dur_us * 1000;  // Convert to nanoseconds
    return TRUE;
  }
  
  if (p->playbin) {
    gint64 dur = 0;
    if (!gst_element_query_duration(p->playbin, GST_FORMAT_TIME, &dur)) return FALSE;
    *out_duration_ns = dur;
    return TRUE;
  }
  return FALSE;
}
