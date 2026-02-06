#include "player.h"

#include <gio/gio.h>

// playbin flags bitmask
#define CPIFY_PLAY_FLAG_VIDEO (1u << 0)
#define CPIFY_PLAY_FLAG_AUDIO (1u << 1)

static gboolean on_bus_message(GstBus *bus, GstMessage *msg, gpointer user_data) {
  (void)bus;
  CPifyPlayer *p = (CPifyPlayer *)user_data;
  
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
      g_print("[DEBUG] GStreamer: End of stream\n");
      if (p->eos_cb) {
        p->eos_cb(p->eos_cb_data);
      }
      break;
      
    case GST_MESSAGE_ERROR: {
      GError *err = NULL;
      gchar *dbg = NULL;
      gst_message_parse_error(msg, &err, &dbg);
      g_printerr("[ERROR] GStreamer error: %s\n", err ? err->message : "unknown");
      if (dbg) g_printerr("[ERROR] Debug: %s\n", dbg);
      if (err) g_error_free(err);
      g_free(dbg);
      break;
    }
    
    case GST_MESSAGE_WARNING: {
      GError *err = NULL;
      gchar *dbg = NULL;
      gst_message_parse_warning(msg, &err, &dbg);
      g_printerr("[WARNING] GStreamer warning: %s\n", err ? err->message : "unknown");
      if (dbg) g_printerr("[WARNING] Debug: %s\n", dbg);
      if (err) g_error_free(err);
      g_free(dbg);
      break;
    }
    
    case GST_MESSAGE_STATE_CHANGED: {
      if (GST_MESSAGE_SRC(msg) == GST_OBJECT(p->pipeline)) {
        GstState old_state, new_state, pending_state;
        gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
        g_print("[DEBUG] Pipeline state: %s -> %s (pending: %s)\n",
                gst_element_state_get_name(old_state),
                gst_element_state_get_name(new_state),
                gst_element_state_get_name(pending_state));
      }
      break;
    }
    
    case GST_MESSAGE_STREAM_STATUS: {
      GstStreamStatusType type;
      GstElement *owner;
      gst_message_parse_stream_status(msg, &type, &owner);
      g_print("[DEBUG] Stream status: type=%d from %s\n", 
              type, GST_ELEMENT_NAME(owner));
      break;
    }
    
    case GST_MESSAGE_ASYNC_DONE:
      g_print("[DEBUG] Async done - pipeline ready\n");
      break;
    
    default:
      break;
  }
  
  return G_SOURCE_CONTINUE;
}

static void on_paintable_invalidate(GdkPaintable *paintable, gpointer user_data) {
  (void)user_data;
  static int frame_count = 0;
  frame_count++;
  if (frame_count <= 5 || frame_count % 100 == 0) {
    g_print("[DEBUG] Paintable invalidated (frame %d), size: %dx%d\n", 
            frame_count,
            gdk_paintable_get_intrinsic_width(paintable),
            gdk_paintable_get_intrinsic_height(paintable));
  }
}

static gboolean try_create_gtk4_sink(CPifyPlayer *p) {
  // Try gtk4paintablesink (best option for GTK4)
  GstElement *gtk_sink = gst_element_factory_make("gtk4paintablesink", "gtk-sink");
  if (gtk_sink) {
    g_print("[DEBUG] Using gtk4paintablesink\n");
    
    // Get the paintable BEFORE adding to pipeline
    g_object_get(gtk_sink, "paintable", &p->paintable, NULL);
    if (!p->paintable) {
      g_printerr("[ERROR] Failed to get paintable from gtk4paintablesink\n");
      gst_object_unref(gtk_sink);
      return FALSE;
    }
    
    g_print("[DEBUG] Got paintable: %p\n", (void*)p->paintable);
    
    // Connect to invalidate-contents to see if we're receiving frames
    g_signal_connect(p->paintable, "invalidate-contents", 
                     G_CALLBACK(on_paintable_invalidate), p);
    
    // Use gtk4paintablesink directly (glsinkbin was causing issues)
    p->video_sink = gtk_sink;
    
    // Create GtkPicture for the paintable
    p->video_widget = gtk_picture_new_for_paintable(p->paintable);
    gtk_widget_set_hexpand(p->video_widget, TRUE);
    gtk_widget_set_vexpand(p->video_widget, TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(p->video_widget), GTK_CONTENT_FIT_CONTAIN);
    
    // Add CSS class for debugging (dark background)
    gtk_widget_add_css_class(p->video_widget, "card");
    
    g_print("[DEBUG] Created GtkPicture widget: %p\n", (void*)p->video_widget);
    
    return TRUE;
  }
  
  return FALSE;
}

static gboolean try_create_gtksink(CPifyPlayer *p) {
  // Try gtksink (GTK3 compatible but might work)
  GstElement *gtk_sink = gst_element_factory_make("gtksink", "gtk-sink");
  if (gtk_sink) {
    g_print("[DEBUG] Using gtksink\n");
    
    // gtksink provides a widget property
    g_object_get(gtk_sink, "widget", &p->video_widget, NULL);
    if (p->video_widget) {
      gtk_widget_set_hexpand(p->video_widget, TRUE);
      gtk_widget_set_vexpand(p->video_widget, TRUE);
      p->video_sink = gtk_sink;
      return TRUE;
    }
    
    gst_object_unref(gtk_sink);
  }
  
  return FALSE;
}

static gboolean try_create_gtkglsink(CPifyPlayer *p) {
  // Try gtkglsink wrapped in glsinkbin
  GstElement *gtkglsink = gst_element_factory_make("gtkglsink", "gtk-gl-sink");
  if (gtkglsink) {
    g_print("[DEBUG] Using gtkglsink\n");
    
    // gtkglsink provides a widget property
    g_object_get(gtkglsink, "widget", &p->video_widget, NULL);
    if (p->video_widget) {
      gtk_widget_set_hexpand(p->video_widget, TRUE);
      gtk_widget_set_vexpand(p->video_widget, TRUE);
      
      // Wrap in glsinkbin
      GstElement *glsinkbin = gst_element_factory_make("glsinkbin", "video-sink-bin");
      if (glsinkbin) {
        g_object_set(glsinkbin, "sink", gtkglsink, NULL);
        p->video_sink = glsinkbin;
      } else {
        p->video_sink = gtkglsink;
      }
      return TRUE;
    }
    
    gst_object_unref(gtkglsink);
  }
  
  return FALSE;
}

CPifyPlayer *cpify_player_new(void) {
  g_print("[DEBUG] cpify_player_new: creating player\n");
  
  CPifyPlayer *p = g_new0(CPifyPlayer, 1);
  p->audio_enabled = TRUE;
  p->video_enabled = TRUE;
  p->volume = 0.8;
  p->rate = 1.0;
  
  // Try different video sinks in order of preference
  gboolean have_sink = try_create_gtk4_sink(p);
  
  if (!have_sink) {
    have_sink = try_create_gtkglsink(p);
  }
  
  if (!have_sink) {
    have_sink = try_create_gtksink(p);
  }
  
  if (!have_sink) {
    g_printerr("[ERROR] No GTK video sink available!\n");
    g_printerr("[ERROR] Install gst-plugins-good with GTK support:\n");
    g_printerr("[ERROR]   Fedora: sudo dnf install gstreamer1-plugins-good\n");
    g_printerr("[ERROR]   Check: gst-inspect-1.0 gtksink\n");
    
    // Create a placeholder widget
    p->video_widget = gtk_label_new("Video playback not available.\nInstall GStreamer GTK plugins.");
    gtk_widget_set_hexpand(p->video_widget, TRUE);
    gtk_widget_set_vexpand(p->video_widget, TRUE);
    
    // Use autovideosink as fallback (opens separate window)
    p->video_sink = gst_element_factory_make("autovideosink", "video-sink");
    if (!p->video_sink) {
      g_free(p);
      return NULL;
    }
  }
  
  // Create playbin3 or playbin pipeline
  p->pipeline = gst_element_factory_make("playbin3", "playbin");
  if (!p->pipeline) {
    p->pipeline = gst_element_factory_make("playbin", "playbin");
  }
  
  if (!p->pipeline) {
    g_printerr("[ERROR] Failed to create playbin pipeline\n");
    if (p->paintable) g_object_unref(p->paintable);
    if (p->video_sink) gst_object_unref(p->video_sink);
    g_free(p);
    return NULL;
  }
  
  // Create audio filter bin with scaletempo for pitch preservation during speed changes
  GstElement *scaletempo = gst_element_factory_make("scaletempo", "scaletempo");
  if (scaletempo) {
    GstElement *audioconvert = gst_element_factory_make("audioconvert", "audioconvert");
    GstElement *audioresample = gst_element_factory_make("audioresample", "audioresample");
    
    if (audioconvert && audioresample) {
      GstElement *audio_bin = gst_bin_new("audio-filter-bin");
      gst_bin_add_many(GST_BIN(audio_bin), audioconvert, scaletempo, audioresample, NULL);
      gst_element_link_many(audioconvert, scaletempo, audioresample, NULL);
      
      // Add ghost pads to make the bin work as a filter
      GstPad *sink_pad = gst_element_get_static_pad(audioconvert, "sink");
      GstPad *src_pad = gst_element_get_static_pad(audioresample, "src");
      gst_element_add_pad(audio_bin, gst_ghost_pad_new("sink", sink_pad));
      gst_element_add_pad(audio_bin, gst_ghost_pad_new("src", src_pad));
      gst_object_unref(sink_pad);
      gst_object_unref(src_pad);
      
      g_object_set(p->pipeline, "audio-filter", audio_bin, NULL);
      g_print("[DEBUG] Pitch-preserving scaletempo filter enabled\n");
    } else {
      if (audioconvert) gst_object_unref(audioconvert);
      if (audioresample) gst_object_unref(audioresample);
      gst_object_unref(scaletempo);
      g_print("[WARNING] Could not create audio filter elements, pitch preservation disabled\n");
    }
  } else {
    g_print("[WARNING] scaletempo element not available, pitch preservation disabled\n");
  }
  
  // Set the video sink on playbin
  g_object_set(p->pipeline, "video-sink", p->video_sink, NULL);
  g_object_set(p->pipeline, "volume", p->volume, NULL);
  
  // Set up bus watch
  p->bus = gst_element_get_bus(p->pipeline);
  p->bus_watch_id = gst_bus_add_watch(p->bus, on_bus_message, p);
  
  g_print("[DEBUG] cpify_player_new: player created successfully\n");
  return p;
}

void cpify_player_free(CPifyPlayer *p) {
  if (!p) return;
  
  g_print("[DEBUG] cpify_player_free: cleaning up\n");
  
  if (p->pipeline) {
    gst_element_set_state(p->pipeline, GST_STATE_NULL);
  }
  
  if (p->bus_watch_id) {
    g_source_remove(p->bus_watch_id);
    p->bus_watch_id = 0;
  }
  
  if (p->bus) {
    gst_object_unref(p->bus);
    p->bus = NULL;
  }
  
  if (p->pipeline) {
    gst_object_unref(p->pipeline);
    p->pipeline = NULL;
  }
  
  if (p->paintable) {
    g_object_unref(p->paintable);
    p->paintable = NULL;
  }
  
  p->video_widget = NULL;
  p->video_sink = NULL;
  
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

gboolean cpify_player_set_path(CPifyPlayer *p, const gchar *abs_path, GError **error) {
  g_print("[DEBUG] cpify_player_set_path: path='%s'\n", abs_path ? abs_path : "(null)");
  
  if (!p || !abs_path || abs_path[0] == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid path");
    return FALSE;
  }
  
  if (!p->pipeline) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Pipeline not available");
    return FALSE;
  }
  
  // Check if file exists
  if (!g_file_test(abs_path, G_FILE_TEST_EXISTS)) {
    g_print("[ERROR] File does not exist: '%s'\n", abs_path);
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "File not found");
    return FALSE;
  }
  
  // Stop current playback
  gst_element_set_state(p->pipeline, GST_STATE_NULL);
  
  // Convert path to URI using GFile for proper handling of special characters
  GFile *file = g_file_new_for_path(abs_path);
  gchar *uri = g_file_get_uri(file);
  g_object_unref(file);
  
  if (!uri) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to build URI from path");
    return FALSE;
  }
  
  g_print("[DEBUG] cpify_player_set_path: URI='%s'\n", uri);
  g_print("[DEBUG] cpify_player_set_path: file exists=%d\n", g_file_test(abs_path, G_FILE_TEST_EXISTS));
  
  // Set flags
  guint flags = 0;
  g_object_get(p->pipeline, "flags", &flags, NULL);
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
  
  g_object_set(p->pipeline, 
               "uri", uri,
               "flags", flags,
               "volume", p->volume,
               NULL);
  
  g_free(uri);
  
  g_print("[DEBUG] cpify_player_set_path: configured pipeline\n");
  return TRUE;
}

void cpify_player_play(CPifyPlayer *p) {
  if (!p || !p->pipeline) return;
  
  GstStateChangeReturn ret = gst_element_set_state(p->pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr("[ERROR] Failed to start playback\n");
  } else {
    g_print("[DEBUG] cpify_player_play: started playback\n");
  }
}

void cpify_player_pause(CPifyPlayer *p) {
  if (!p || !p->pipeline) return;
  gst_element_set_state(p->pipeline, GST_STATE_PAUSED);
}

void cpify_player_stop(CPifyPlayer *p) {
  if (!p || !p->pipeline) return;
  gst_element_set_state(p->pipeline, GST_STATE_NULL);
}

void cpify_player_set_volume(CPifyPlayer *p, gdouble volume_0_to_1) {
  if (!p) return;
  if (volume_0_to_1 < 0.0) volume_0_to_1 = 0.0;
  if (volume_0_to_1 > 1.0) volume_0_to_1 = 1.0;
  p->volume = volume_0_to_1;
  
  if (p->pipeline) {
    g_object_set(p->pipeline, "volume", p->volume, NULL);
  }
}

void cpify_player_set_audio_enabled(CPifyPlayer *p, gboolean enabled) {
  if (!p) return;
  p->audio_enabled = enabled ? TRUE : FALSE;
  
  if (p->pipeline) {
    guint flags = 0;
    g_object_get(p->pipeline, "flags", &flags, NULL);
    if (p->audio_enabled) {
      flags |= CPIFY_PLAY_FLAG_AUDIO;
    } else {
      flags &= ~CPIFY_PLAY_FLAG_AUDIO;
    }
    g_object_set(p->pipeline, "flags", flags, NULL);
  }
}

void cpify_player_set_video_enabled(CPifyPlayer *p, gboolean enabled) {
  if (!p) return;
  p->video_enabled = enabled ? TRUE : FALSE;
}

void cpify_player_set_rate(CPifyPlayer *p, gdouble rate) {
  if (!p || !p->pipeline) return;
  if (rate < 0.25) rate = 0.25;
  if (rate > 4.0) rate = 4.0;
  p->rate = rate;
  
  gint64 position = 0;
  if (!gst_element_query_position(p->pipeline, GST_FORMAT_TIME, &position)) {
    return;
  }
  
  gst_element_seek(p->pipeline, p->rate,
                   GST_FORMAT_TIME,
                   GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                   GST_SEEK_TYPE_SET, position,
                   GST_SEEK_TYPE_NONE, 0);
}

gboolean cpify_player_seek_to(CPifyPlayer *p, gdouble position_seconds) {
  if (!p || !p->pipeline) return FALSE;
  if (position_seconds < 0.0) position_seconds = 0.0;
  
  gint64 position_ns = (gint64)(position_seconds * GST_SECOND);
  
  return gst_element_seek(p->pipeline, p->rate,
                          GST_FORMAT_TIME,
                          GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                          GST_SEEK_TYPE_SET, position_ns,
                          GST_SEEK_TYPE_NONE, 0);
}

gboolean cpify_player_seek_relative(CPifyPlayer *p, gdouble delta_seconds) {
  if (!p || !p->pipeline) return FALSE;
  
  gint64 position = 0;
  if (!gst_element_query_position(p->pipeline, GST_FORMAT_TIME, &position)) {
    return FALSE;
  }
  
  gint64 delta_ns = (gint64)(delta_seconds * GST_SECOND);
  gint64 target = position + delta_ns;
  if (target < 0) target = 0;
  
  return gst_element_seek(p->pipeline, p->rate,
                          GST_FORMAT_TIME,
                          GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                          GST_SEEK_TYPE_SET, target,
                          GST_SEEK_TYPE_NONE, 0);
}

gboolean cpify_player_query_position(CPifyPlayer *p, gint64 *out_position_ns) {
  if (!p || !p->pipeline || !out_position_ns) return FALSE;
  return gst_element_query_position(p->pipeline, GST_FORMAT_TIME, out_position_ns);
}

gboolean cpify_player_query_duration(CPifyPlayer *p, gint64 *out_duration_ns) {
  if (!p || !p->pipeline || !out_duration_ns) return FALSE;
  return gst_element_query_duration(p->pipeline, GST_FORMAT_TIME, out_duration_ns);
}
