#include "player.h"

#include <gio/gio.h>
#include <gst/video/videooverlay.h>

#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

// playbin "flags" is a bitmask. We only need a small subset, and we define it
// locally to avoid relying on optional playback headers (which can be missing
// depending on distro packaging).
//
// Values match GStreamer's GstPlayFlags:
// - video:    (1 << 0)
// - audio:    (1 << 1)
// - download: (1 << 7)
#define PYPIFY_PLAY_FLAG_VIDEO (1u << 0)
#define PYPIFY_PLAY_FLAG_AUDIO (1u << 1)
#define PYPIFY_PLAY_FLAG_DOWNLOAD (1u << 7)

static guintptr get_window_handle_from_widget(GtkWidget *widget) {
  if (!widget) return 0;
  GdkWindow *window = gtk_widget_get_window(widget);
  if (!window) return 0;
  if (!gdk_window_ensure_native(window)) return 0;

#ifdef GDK_WINDOWING_X11
  if (GDK_IS_X11_WINDOW(window)) {
    return (guintptr)GDK_WINDOW_XID(window);
  }
#endif
#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND_WINDOW(window)) {
    return (guintptr)gdk_wayland_window_get_wl_surface(window);
  }
#endif

  return 0;
}

static void overlay_apply_render_rect(PypifyPlayer *p) {
  if (!p || !p->video_sink || !p->window_handle) return;
  if (!GST_IS_VIDEO_OVERLAY(p->video_sink)) return;

  GtkAllocation a;
  gtk_widget_get_allocation(p->video_widget, &a);
  gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(p->video_sink), 0, 0, a.width, a.height);
}

static void overlay_attach_if_possible(PypifyPlayer *p) {
  if (!p || !p->video_sink || !p->window_handle) return;
  if (!GST_IS_VIDEO_OVERLAY(p->video_sink)) return;

  gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(p->video_sink), p->window_handle);
  gst_video_overlay_handle_events(GST_VIDEO_OVERLAY(p->video_sink), TRUE);
  overlay_apply_render_rect(p);
}

static void on_video_realize(GtkWidget *widget, gpointer user_data) {
  PypifyPlayer *p = (PypifyPlayer *)user_data;
  if (!p) return;
  p->window_handle = get_window_handle_from_widget(widget);
  overlay_attach_if_possible(p);
}

static void on_video_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data) {
  (void)widget;
  (void)allocation;
  overlay_apply_render_rect((PypifyPlayer *)user_data);
}

static gboolean on_video_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
  (void)widget;
  (void)user_data;
  // Clear the background on expose/resize to avoid "ghosted" frames when the
  // video sink is drawing via an overlay.
  cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
  cairo_paint(cr);
  return FALSE;
}

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
    case GST_MESSAGE_ELEMENT: {
      const GstStructure *s = gst_message_get_structure(msg);
      if (s && gst_structure_has_name(s, "prepare-window-handle")) {
        overlay_attach_if_possible(p);
      }
      break;
    }
    default:
      break;
  }
  return G_SOURCE_CONTINUE;
}

static GstElement *make_overlay_sink(void) {
  const gchar *candidates[] = {"glimagesink", "ximagesink", "waylandsink"};
  for (guint i = 0; i < G_N_ELEMENTS(candidates); i++) {
    GstElement *sink = gst_element_factory_make(candidates[i], NULL);
    if (sink) return sink;
  }
  return NULL;
}

static GtkWidget *try_create_gtksink_widget(GstElement *playbin) {
  GstElement *sink = gst_element_factory_make("gtksink", NULL);
  if (!sink) return NULL;

  g_object_set(playbin, "video-sink", sink, NULL);

  GtkWidget *widget = NULL;
  g_object_get(sink, "widget", &widget, NULL);
  return widget;
}

PypifyPlayer *pypify_player_new(void) {
  PypifyPlayer *p = g_new0(PypifyPlayer, 1);
  p->playbin = gst_element_factory_make("playbin", "pypify-playbin");
  if (!p->playbin) {
    g_printerr("Unable to create GStreamer playbin.\n");
    g_free(p);
    return NULL;
  }

  p->audio_enabled = TRUE;
  p->video_enabled = TRUE;
  p->volume = 0.8;
  p->rate = 1.0;

  // Preferred: gtksink provides a ready-to-pack widget.
  p->video_widget = try_create_gtksink_widget(p->playbin);
  if (p->video_widget) {
    // gtksink is set as video-sink by try_create_gtksink_widget.
    g_object_get(p->playbin, "video-sink", &p->video_sink, NULL);
  } else {
    // Fallback: embed via GstVideoOverlay into a drawing area.
    p->video_widget = gtk_drawing_area_new();
    gtk_widget_set_hexpand(p->video_widget, TRUE);
    gtk_widget_set_vexpand(p->video_widget, TRUE);

    p->video_sink = make_overlay_sink();
    if (p->video_sink) {
      g_object_set(p->playbin, "video-sink", p->video_sink, NULL);
    }

    g_signal_connect(p->video_widget, "realize", G_CALLBACK(on_video_realize), p);
    g_signal_connect(p->video_widget, "size-allocate", G_CALLBACK(on_video_size_allocate), p);
    g_signal_connect(p->video_widget, "draw", G_CALLBACK(on_video_draw), p);
  }

  p->bus = gst_element_get_bus(p->playbin);
  p->bus_watch_id = gst_bus_add_watch(p->bus, on_bus_message, p);
  g_object_set(p->playbin, "volume", p->volume, NULL);
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
  // video_widget is owned by GTK containers; don't unref here
  p->video_widget = NULL;
  p->window_handle = 0;
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
  if (!p || !abs_path || abs_path[0] == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid path");
    return FALSE;
  }

  // Stop any existing playback cleanly before swapping the URI.
  gst_element_set_state(p->playbin, GST_STATE_NULL);

  gchar *uri = gst_filename_to_uri(abs_path, NULL);
  if (!uri) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to build URI from path");
    return FALSE;
  }

  // Apply audio/video flags.
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
  overlay_attach_if_possible(p);
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
