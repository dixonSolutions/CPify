#pragma once

#include <gtk/gtk.h>
#include <gst/gst.h>

typedef void (*CPifyPlayerEosCallback)(gpointer user_data);

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

  CPifyPlayerEosCallback eos_cb;
  gpointer eos_cb_data;

  gboolean audio_enabled;
  gboolean video_enabled;
  gboolean use_gtk_media;  // TRUE if using GtkMediaFile instead of playbin
  gdouble volume;  // 0..1
  gdouble rate;    // playback rate (1.0 = normal)
} CPifyPlayer;

CPifyPlayer *cpify_player_new(void);
void cpify_player_free(CPifyPlayer *player);

// Returns a widget that can be packed into the UI to display video
GtkWidget *cpify_player_get_video_widget(CPifyPlayer *player);

void cpify_player_set_eos_callback(CPifyPlayer *player, CPifyPlayerEosCallback cb, gpointer user_data);

gboolean cpify_player_set_path(CPifyPlayer *player, const gchar *abs_path, GError **error);
void cpify_player_play(CPifyPlayer *player);
void cpify_player_pause(CPifyPlayer *player);
void cpify_player_stop(CPifyPlayer *player);

void cpify_player_set_volume(CPifyPlayer *player, gdouble volume_0_to_1);
void cpify_player_set_audio_enabled(CPifyPlayer *player, gboolean enabled);
void cpify_player_set_video_enabled(CPifyPlayer *player, gboolean enabled);
void cpify_player_set_rate(CPifyPlayer *player, gdouble rate);

gboolean cpify_player_seek_relative(CPifyPlayer *player, gdouble delta_seconds);
gboolean cpify_player_seek_to(CPifyPlayer *player, gdouble position_seconds);

gboolean cpify_player_query_position(CPifyPlayer *player, gint64 *out_position_ns);
gboolean cpify_player_query_duration(CPifyPlayer *player, gint64 *out_duration_ns);
