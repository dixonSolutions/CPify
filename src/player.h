#pragma once

#include <gtk/gtk.h>
#include <gst/gst.h>

typedef void (*PypifyPlayerEosCallback)(gpointer user_data);

typedef struct {
  // GStreamer playbin (used for audio control and seeking)
  GstElement *playbin;
  GstElement *video_sink;
  GstBus *bus;
  guint bus_watch_id;

  // GTK4 video display
  GtkWidget *video_widget;  // GtkVideo or GtkPicture
  GdkPaintable *paintable;  // From gtk4paintablesink
  GtkMediaStream *media_stream;  // Alternative: GTK4 native media

  PypifyPlayerEosCallback eos_cb;
  gpointer eos_cb_data;

  gboolean audio_enabled;
  gboolean video_enabled;
  gboolean use_gtk_media;  // TRUE if using GtkMediaFile instead of playbin
  gdouble volume;  // 0..1
  gdouble rate;    // playback rate (1.0 = normal)
} PypifyPlayer;

PypifyPlayer *pypify_player_new(void);
void pypify_player_free(PypifyPlayer *player);

// Returns a widget that can be packed into the UI to display video
GtkWidget *pypify_player_get_video_widget(PypifyPlayer *player);

void pypify_player_set_eos_callback(PypifyPlayer *player, PypifyPlayerEosCallback cb, gpointer user_data);

gboolean pypify_player_set_path(PypifyPlayer *player, const gchar *abs_path, GError **error);
void pypify_player_play(PypifyPlayer *player);
void pypify_player_pause(PypifyPlayer *player);
void pypify_player_stop(PypifyPlayer *player);

void pypify_player_set_volume(PypifyPlayer *player, gdouble volume_0_to_1);
void pypify_player_set_audio_enabled(PypifyPlayer *player, gboolean enabled);
void pypify_player_set_video_enabled(PypifyPlayer *player, gboolean enabled);
void pypify_player_set_rate(PypifyPlayer *player, gdouble rate);

gboolean pypify_player_seek_relative(PypifyPlayer *player, gdouble delta_seconds);
gboolean pypify_player_seek_to(PypifyPlayer *player, gdouble position_seconds);

gboolean pypify_player_query_position(PypifyPlayer *player, gint64 *out_position_ns);
gboolean pypify_player_query_duration(PypifyPlayer *player, gint64 *out_duration_ns);
