#include "pypify_app.h"

#include <gst/gst.h>
#include <math.h>
#include <stdlib.h>

#include "media_scanner.h"
#include "player.h"

enum {
  ROW_DATA_TRACK_INDEX = 1,
};

struct _PypifyApp {
  GtkApplication *app;
  GtkWidget *window;

  // Header
  GtkWidget *open_folder_button;
  GtkWidget *settings_button;
  GtkWidget *settings_popover;

  // Sidebar
  GtkWidget *search_entry;
  GtkWidget *listbox;

  // Center
  GtkWidget *now_playing_label;
  GtkWidget *video_stack;
  GtkWidget *video_widget; // added lazily when player initializes
  GtkWidget *video_disabled_label;
  GtkWidget *time_label;
  GtkWidget *progress_scale;
  gboolean progress_dragging;

  // Controls
  GtkWidget *prev_button;
  GtkWidget *back_button;
  GtkWidget *play_pause_button;
  GtkWidget *forward_button;
  GtkWidget *next_button;
  GtkWidget *shuffle_toggle;
  GtkWidget *repeat_toggle;

  // Settings widgets
  GtkWidget *volume_scale;
  GtkWidget *speed_scale;
  GtkWidget *audio_switch;
  GtkWidget *video_switch;

  // Footer/status
  GtkWidget *status_label;

  gchar *current_folder;
  GPtrArray *tracks;      // PypifyTrack*
  GArray *visible_tracks; // gint track_index (filters/search)
  gint current_track_index;
  gboolean is_playing;

  guint tick_id;

  PypifyPlayer *player;
};

// Forward declarations (needed for lazy-init paths)
static void on_player_eos(gpointer user_data);

static void set_status(PypifyApp *p, const gchar *text) {
  gtk_label_set_text(GTK_LABEL(p->status_label), text ? text : "");
}

static void update_play_button(PypifyApp *p) {
  gtk_button_set_label(GTK_BUTTON(p->play_pause_button), p->is_playing ? "Pause" : "Play");
}

static gboolean get_shuffle(PypifyApp *p) {
  return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->shuffle_toggle));
}

static gboolean get_repeat(PypifyApp *p) {
  return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->repeat_toggle));
}

static gchar *format_time_seconds(gdouble seconds) {
  if (seconds < 0) seconds = 0;
  gint total = (gint)floor(seconds + 0.5);
  gint mm = total / 60;
  gint ss = total % 60;
  return g_strdup_printf("%02d:%02d", mm, ss);
}

static guint visible_len(PypifyApp *p) {
  return (p && p->visible_tracks) ? p->visible_tracks->len : 0;
}

static gint visible_get_track_index(PypifyApp *p, guint visible_pos) {
  if (!p || !p->visible_tracks) return -1;
  if (visible_pos >= p->visible_tracks->len) return -1;
  return g_array_index(p->visible_tracks, gint, visible_pos);
}

static gint visible_find_pos(PypifyApp *p, gint track_index) {
  if (!p || !p->visible_tracks) return -1;
  for (guint i = 0; i < p->visible_tracks->len; i++) {
    if (g_array_index(p->visible_tracks, gint, i) == track_index) return (gint)i;
  }
  return -1;
}

static void clear_listbox(PypifyApp *p) {
  GList *children = gtk_container_get_children(GTK_CONTAINER(p->listbox));
  for (GList *l = children; l; l = l->next) {
    gtk_widget_destroy(GTK_WIDGET(l->data));
  }
  g_list_free(children);
}

static void update_list_playing_icons(PypifyApp *p) {
  GList *rows = gtk_container_get_children(GTK_CONTAINER(p->listbox));
  for (GList *l = rows; l; l = l->next) {
    GtkWidget *row = GTK_WIDGET(l->data);
    GtkWidget *img = g_object_get_data(G_OBJECT(row), "play-icon");
    gint idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "track-index"));
    if (!img) continue;
    if (idx == p->current_track_index && p->is_playing) {
      gtk_image_set_from_icon_name(GTK_IMAGE(img), "media-playback-start-symbolic", GTK_ICON_SIZE_MENU);
    } else {
      gtk_image_clear(GTK_IMAGE(img));
    }
  }
  g_list_free(rows);
}

static void set_now_playing(PypifyApp *p, const gchar *title) {
  if (!title || title[0] == '\0') {
    gtk_label_set_markup(GTK_LABEL(p->now_playing_label), "<span size='x-large' weight='bold'>Choose a song</span>");
    return;
  }
  gchar *escaped = g_markup_escape_text(title, -1);
  gchar *markup = g_strdup_printf("<span size='x-large' weight='bold'>%s</span>", escaped);
  gtk_label_set_markup(GTK_LABEL(p->now_playing_label), markup);
  g_free(markup);
  g_free(escaped);
}

static void populate_listbox(PypifyApp *p) {
  clear_listbox(p);
  if (!p->tracks || !p->visible_tracks) return;

  for (guint i = 0; i < p->visible_tracks->len; i++) {
    gint track_index = visible_get_track_index(p, i);
    if (track_index < 0 || track_index >= (gint)p->tracks->len) continue;
    PypifyTrack *t = g_ptr_array_index(p->tracks, (guint)track_index);

    GtkWidget *row = gtk_list_box_row_new();
    g_object_set_data(G_OBJECT(row), "track-index", GINT_TO_POINTER(track_index));

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);

    GtkWidget *icon = gtk_image_new();
    gtk_widget_set_size_request(icon, 18, -1);
    g_object_set_data(G_OBJECT(row), "play-icon", icon);

    GtkWidget *label = gtk_label_new(t && t->title ? t->title : "(unknown)");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);

    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(row), box);
    gtk_widget_show_all(row);
    gtk_container_add(GTK_CONTAINER(p->listbox), row);
  }

  update_list_playing_icons(p);
}

static void visible_reset_all(PypifyApp *p) {
  if (!p) return;
  if (p->visible_tracks) {
    g_array_unref(p->visible_tracks);
    p->visible_tracks = NULL;
  }
  p->visible_tracks = g_array_new(FALSE, FALSE, sizeof(gint));
  if (!p->tracks) return;
  for (guint i = 0; i < p->tracks->len; i++) {
    gint idx = (gint)i;
    g_array_append_val(p->visible_tracks, idx);
  }
}

static void visible_apply_search(PypifyApp *p, const gchar *query) {
  visible_reset_all(p);
  if (!p || !p->tracks || !p->visible_tracks) return;
  if (!query) return;
  gchar *q = g_utf8_strdown(query, -1);
  if (!q || q[0] == '\0') {
    g_free(q);
    return;
  }

  GArray *filtered = g_array_new(FALSE, FALSE, sizeof(gint));
  for (guint i = 0; i < p->tracks->len; i++) {
    PypifyTrack *t = g_ptr_array_index(p->tracks, i);
    const gchar *title = (t && t->title) ? t->title : "";
    gchar *lower = g_utf8_strdown(title, -1);
    gboolean match = (lower && g_strstr_len(lower, -1, q) != NULL);
    g_free(lower);
    if (match) {
      gint idx = (gint)i;
      g_array_append_val(filtered, idx);
    }
  }
  g_free(q);

  g_array_unref(p->visible_tracks);
  p->visible_tracks = filtered;
}

static gboolean ensure_player(PypifyApp *p) {
  if (!p) return FALSE;
  if (p->player) return TRUE;

  // Lazy-init the player to avoid startup crashes on systems with quirky video sinks.
  p->player = pypify_player_new();
  if (!p->player) {
    set_status(p, "Playback unavailable (GStreamer playbin failed to initialize).");
    return FALSE;
  }
  pypify_player_set_eos_callback(p->player, on_player_eos, p);

  // Lazily add the video widget to the stack once we have a player.
  if (!p->video_widget && p->video_stack) {
    GtkWidget *vw = pypify_player_get_video_widget(p->player);
    if (vw) {
      p->video_widget = vw;
      gtk_stack_add_named(GTK_STACK(p->video_stack), p->video_widget, "video");
      gtk_widget_show_all(p->video_stack);
    }
  }

  return TRUE;
}

static void apply_settings_to_player(PypifyApp *p) {
  if (!p) return;
  if (!p->player) {
    // No player: keep UI stable and avoid dereferencing NULL.
    gtk_stack_set_visible_child(GTK_STACK(p->video_stack), p->video_disabled_label);
    return;
  }
  gdouble vol = gtk_range_get_value(GTK_RANGE(p->volume_scale)) / 100.0;
  gdouble rate = gtk_range_get_value(GTK_RANGE(p->speed_scale)) / 100.0;
  gboolean audio_on = gtk_switch_get_active(GTK_SWITCH(p->audio_switch));
  gboolean video_on = gtk_switch_get_active(GTK_SWITCH(p->video_switch));

  pypify_player_set_volume(p->player, vol);
  pypify_player_set_audio_enabled(p->player, audio_on);
  pypify_player_set_video_enabled(p->player, video_on);
  pypify_player_set_rate(p->player, rate);

  GtkWidget *vw = p->video_widget ? p->video_widget : pypify_player_get_video_widget(p->player);
  if (!video_on || !vw) {
    gtk_stack_set_visible_child(GTK_STACK(p->video_stack), p->video_disabled_label);
  } else {
    gtk_stack_set_visible_child(GTK_STACK(p->video_stack), vw);
  }

  // Make audio/video toggles effective immediately during playback by reloading the URI
  // while preserving position. (Player flags are always applied on set_path.)
  if (p->tracks && p->current_track_index >= 0 && p->current_track_index < (gint)p->tracks->len) {
    PypifyTrack *t = g_ptr_array_index(p->tracks, (guint)p->current_track_index);
    if (t && t->path) {
      gint64 pos_ns = 0;
      gboolean have_pos = pypify_player_query_position(p->player, &pos_ns);
      gdouble pos_s = have_pos ? ((gdouble)pos_ns / (gdouble)GST_SECOND) : 0.0;
      gboolean should_play = p->is_playing;

      GError *err = NULL;
      if (pypify_player_set_path(p->player, t->path, &err)) {
        if (have_pos) pypify_player_seek_to(p->player, pos_s);
        if (should_play) {
          pypify_player_play(p->player);
        } else {
          pypify_player_pause(p->player);
        }
      } else if (err) {
        g_error_free(err);
      }
    }
  }
}

static void play_track_index(PypifyApp *p, gint track_index) {
  if (!p || !p->tracks) return;
  if (track_index < 0 || track_index >= (gint)p->tracks->len) return;
  if (!ensure_player(p)) return;

  PypifyTrack *t = g_ptr_array_index(p->tracks, (guint)track_index);
  if (!t || !t->path) return;

  apply_settings_to_player(p);

  GError *err = NULL;
  if (!pypify_player_set_path(p->player, t->path, &err)) {
    gchar *msg = g_strdup_printf("Unable to play: %s", err ? err->message : "unknown error");
    set_status(p, msg);
    g_free(msg);
    if (err) g_error_free(err);
    return;
  }

  p->current_track_index = track_index;
  p->is_playing = TRUE;
  update_play_button(p);
  set_now_playing(p, t->title ? t->title : t->path);
  pypify_player_play(p->player);

  update_list_playing_icons(p);

  gint visible_pos = visible_find_pos(p, track_index);
  if (visible_pos >= 0) {
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(p->listbox), visible_pos);
    if (row) gtk_list_box_select_row(GTK_LIST_BOX(p->listbox), row);
  }
}

static gint choose_next_visible_pos(PypifyApp *p) {
  guint n = visible_len(p);
  if (n == 0) return -1;

  gint cur_pos = visible_find_pos(p, p->current_track_index);
  gboolean shuffle = get_shuffle(p);
  if (!shuffle) {
    if (cur_pos < 0) return 0;
    gint next = cur_pos + 1;
    if (next >= (gint)n) return -1;
    return next;
  }

  if (n == 1) return 0;
  for (gint attempts = 0; attempts < 12; attempts++) {
    gint idx = (gint)g_random_int_range(0, (gint)n);
    if (idx != cur_pos) return idx;
  }
  return (cur_pos + 1) % (gint)n;
}

static void play_next(PypifyApp *p) {
  gint next_pos = choose_next_visible_pos(p);
  if (next_pos < 0) {
    p->is_playing = FALSE;
    update_play_button(p);
    if (p->player) pypify_player_stop(p->player);
    set_status(p, "Reached end of list.");
    update_list_playing_icons(p);
    return;
  }
  gint track_index = visible_get_track_index(p, (guint)next_pos);
  play_track_index(p, track_index);
}

static void play_prev(PypifyApp *p) {
  guint n = visible_len(p);
  if (n == 0) return;
  gint cur_pos = visible_find_pos(p, p->current_track_index);
  gint prev_pos = (cur_pos <= 0) ? 0 : (cur_pos - 1);
  gint track_index = visible_get_track_index(p, (guint)prev_pos);
  play_track_index(p, track_index);
}

static void on_player_eos(gpointer user_data) {
  PypifyApp *p = (PypifyApp *)user_data;
  if (!p) return;
  if (get_repeat(p) && p->current_track_index >= 0) {
    play_track_index(p, p->current_track_index);
    return;
  }
  play_next(p);
}

static void load_folder(PypifyApp *p, const gchar *folder) {
  if (!p || !folder) return;

  g_free(p->current_folder);
  p->current_folder = g_strdup(folder);

  if (p->tracks) {
    g_ptr_array_unref(p->tracks);
    p->tracks = NULL;
  }

  set_status(p, "Scanning folder…");
  while (gtk_events_pending()) gtk_main_iteration();

  GError *err = NULL;
  p->tracks = pypify_scan_folder(folder, &err);
  if (!p->tracks) {
    gchar *msg = g_strdup_printf("Scan failed: %s", err ? err->message : "unknown error");
    set_status(p, msg);
    g_free(msg);
    if (err) g_error_free(err);
    return;
  }

  gtk_entry_set_text(GTK_ENTRY(p->search_entry), "");
  visible_reset_all(p);
  populate_listbox(p);

  p->current_track_index = -1;
  p->is_playing = FALSE;
  update_play_button(p);
  set_now_playing(p, NULL);
  update_list_playing_icons(p);

  gchar *status = g_strdup_printf("Loaded %u media file(s) from: %s", p->tracks->len, folder);
  set_status(p, status);
  g_free(status);
}

static void open_folder_dialog(PypifyApp *p) {
  GtkWidget *dlg = gtk_file_chooser_dialog_new(
      "Select a media folder",
      GTK_WINDOW(p->window),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
      "_Cancel",
      GTK_RESPONSE_CANCEL,
      "_Open",
      GTK_RESPONSE_ACCEPT,
      NULL
  );

  gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dlg), TRUE);
  gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dlg), FALSE);
  if (p->current_folder) {
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), p->current_folder);
  }

  gint res = gtk_dialog_run(GTK_DIALOG(dlg));
  if (res == GTK_RESPONSE_ACCEPT) {
    gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    gtk_widget_destroy(dlg);
    if (folder) {
      load_folder(p, folder);
      g_free(folder);
    }
    return;
  }
  gtk_widget_destroy(dlg);
}

static gboolean open_folder_on_startup(gpointer user_data) {
  PypifyApp *p = (PypifyApp *)user_data;
  open_folder_dialog(p);
  if (!p->current_folder) {
    g_application_quit(G_APPLICATION(p->app));
  }
  return G_SOURCE_REMOVE;
}

static void on_open_folder_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  open_folder_dialog((PypifyApp *)user_data);
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
  (void)box;
  PypifyApp *p = (PypifyApp *)user_data;
  gint track_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "track-index"));
  play_track_index(p, track_index);
}

static void on_search_changed(GtkEditable *editable, gpointer user_data) {
  (void)editable;
  PypifyApp *p = (PypifyApp *)user_data;
  const gchar *q = gtk_entry_get_text(GTK_ENTRY(p->search_entry));
  visible_apply_search(p, q);
  populate_listbox(p);
  update_list_playing_icons(p);
}

static void on_play_pause_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  PypifyApp *p = (PypifyApp *)user_data;
  if (!p->tracks || p->tracks->len == 0) return;
  if (!ensure_player(p)) return;

  if (p->current_track_index < 0) {
    if (visible_len(p) > 0) {
      play_track_index(p, visible_get_track_index(p, 0));
    }
    return;
  }

  if (p->is_playing) {
    p->is_playing = FALSE;
    pypify_player_pause(p->player);
  } else {
    p->is_playing = TRUE;
    pypify_player_play(p->player);
  }
  update_play_button(p);
  update_list_playing_icons(p);
}

static void on_next_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  play_next((PypifyApp *)user_data);
}

static void on_prev_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  play_prev((PypifyApp *)user_data);
}

static void on_skip_back_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  PypifyApp *p = (PypifyApp *)user_data;
  if (!p || !p->player || p->current_track_index < 0) return;
  pypify_player_seek_relative(p->player, -10.0);
}

static void on_skip_forward_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  PypifyApp *p = (PypifyApp *)user_data;
  if (!p || !p->player || p->current_track_index < 0) return;
  pypify_player_seek_relative(p->player, 10.0);
}

static gboolean on_progress_button_press(GtkWidget *w, GdkEventButton *ev, gpointer user_data) {
  (void)w;
  (void)ev;
  ((PypifyApp *)user_data)->progress_dragging = TRUE;
  return FALSE;
}

static gboolean on_progress_button_release(GtkWidget *w, GdkEventButton *ev, gpointer user_data) {
  (void)w;
  (void)ev;
  PypifyApp *p = (PypifyApp *)user_data;
  p->progress_dragging = FALSE;
  if (!p->player || p->current_track_index < 0) return FALSE;
  gdouble seconds = gtk_range_get_value(GTK_RANGE(p->progress_scale));
  pypify_player_seek_to(p->player, seconds);
  return FALSE;
}

static void on_range_value_changed(GtkRange *range, gpointer user_data) {
  (void)range;
  apply_settings_to_player((PypifyApp *)user_data);
}

static void on_switch_active_notify(GObject *obj, GParamSpec *pspec, gpointer user_data) {
  (void)obj;
  (void)pspec;
  apply_settings_to_player((PypifyApp *)user_data);
}

static gboolean on_tick(gpointer user_data) {
  PypifyApp *p = (PypifyApp *)user_data;
  if (!p || !p->player || p->current_track_index < 0) return G_SOURCE_CONTINUE;

  gint64 pos_ns = 0;
  gint64 dur_ns = 0;
  gboolean have_pos = pypify_player_query_position(p->player, &pos_ns);
  gboolean have_dur = pypify_player_query_duration(p->player, &dur_ns);
  if (!have_pos || !have_dur || dur_ns <= 0) return G_SOURCE_CONTINUE;

  gdouble pos_s = (gdouble)pos_ns / (gdouble)GST_SECOND;
  gdouble dur_s = (gdouble)dur_ns / (gdouble)GST_SECOND;

  gtk_range_set_range(GTK_RANGE(p->progress_scale), 0.0, dur_s);
  if (!p->progress_dragging) {
    gtk_range_set_value(GTK_RANGE(p->progress_scale), pos_s);
  }

  gchar *left = format_time_seconds(pos_s);
  gchar *right = format_time_seconds(dur_s);
  gchar *combined = g_strdup_printf("%s / %s", left, right);
  gtk_label_set_text(GTK_LABEL(p->time_label), combined);
  g_free(combined);
  g_free(left);
  g_free(right);
  return G_SOURCE_CONTINUE;
}

static GtkWidget *build_settings_popover(PypifyApp *p) {
  GtkWidget *popover = gtk_popover_new(p->settings_button);
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_widget_set_margin_start(grid, 12);
  gtk_widget_set_margin_end(grid, 12);
  gtk_widget_set_margin_top(grid, 12);
  gtk_widget_set_margin_bottom(grid, 12);

  GtkWidget *vol_label = gtk_label_new("Volume");
  gtk_label_set_xalign(GTK_LABEL(vol_label), 0.0f);
  p->volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_range_set_value(GTK_RANGE(p->volume_scale), 80);
  gtk_scale_set_draw_value(GTK_SCALE(p->volume_scale), FALSE);

  GtkWidget *speed_label = gtk_label_new("Speed");
  gtk_label_set_xalign(GTK_LABEL(speed_label), 0.0f);
  p->speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 50, 200, 5);
  gtk_range_set_value(GTK_RANGE(p->speed_scale), 100);
  gtk_scale_set_draw_value(GTK_SCALE(p->speed_scale), FALSE);

  GtkWidget *audio_label = gtk_label_new("Audio");
  gtk_label_set_xalign(GTK_LABEL(audio_label), 0.0f);
  p->audio_switch = gtk_switch_new();
  gtk_switch_set_active(GTK_SWITCH(p->audio_switch), TRUE);

  GtkWidget *video_label = gtk_label_new("Video");
  gtk_label_set_xalign(GTK_LABEL(video_label), 0.0f);
  p->video_switch = gtk_switch_new();
  gtk_switch_set_active(GTK_SWITCH(p->video_switch), TRUE);

  gtk_grid_attach(GTK_GRID(grid), vol_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), p->volume_scale, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), speed_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), p->speed_scale, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), audio_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), p->audio_switch, 1, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), video_label, 0, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), p->video_switch, 1, 3, 1, 1);

  g_signal_connect(p->volume_scale, "value-changed", G_CALLBACK(on_range_value_changed), p);
  g_signal_connect(p->speed_scale, "value-changed", G_CALLBACK(on_range_value_changed), p);
  g_signal_connect(p->audio_switch, "notify::active", G_CALLBACK(on_switch_active_notify), p);
  g_signal_connect(p->video_switch, "notify::active", G_CALLBACK(on_switch_active_notify), p);

  gtk_container_add(GTK_CONTAINER(popover), grid);
  return popover;
}

static GtkWidget *build_header(PypifyApp *p) {
  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_start(bar, 12);
  gtk_widget_set_margin_end(bar, 12);
  gtk_widget_set_margin_top(bar, 12);
  gtk_widget_set_margin_bottom(bar, 8);

  GtkWidget *title = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(title), "<b>Pypify</b>");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

  GtkWidget *spacer = gtk_label_new("");
  gtk_widget_set_hexpand(spacer, TRUE);

  p->open_folder_button = gtk_button_new_with_label("Open Folder");
  g_signal_connect(p->open_folder_button, "clicked", G_CALLBACK(on_open_folder_clicked), p);

  p->settings_button = gtk_button_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_button_set_relief(GTK_BUTTON(p->settings_button), GTK_RELIEF_NONE);

  p->settings_popover = build_settings_popover(p);
  g_signal_connect_swapped(p->settings_button, "clicked", G_CALLBACK(gtk_popover_popup), p->settings_popover);

  gtk_box_pack_start(GTK_BOX(bar), title, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(bar), spacer, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(bar), p->open_folder_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(bar), p->settings_button, FALSE, FALSE, 0);
  return bar;
}

static GtkWidget *build_sidebar(PypifyApp *p) {
  GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_size_request(left, 330, -1);
  gtk_widget_set_margin_start(left, 10);
  gtk_widget_set_margin_end(left, 0);
  gtk_widget_set_margin_top(left, 10);
  gtk_widget_set_margin_bottom(left, 10);

  p->search_entry = gtk_search_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(p->search_entry), "Search songs…");
  g_signal_connect(p->search_entry, "changed", G_CALLBACK(on_search_changed), p);

  GtkWidget *list_scroller = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  p->listbox = gtk_list_box_new();
  gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(p->listbox), TRUE);
  g_signal_connect(p->listbox, "row-activated", G_CALLBACK(on_row_activated), p);
  gtk_container_add(GTK_CONTAINER(list_scroller), p->listbox);

  gtk_box_pack_start(GTK_BOX(left), p->search_entry, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(left), list_scroller, TRUE, TRUE, 0);
  return left;
}

static GtkWidget *build_center(PypifyApp *p) {
  GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_hexpand(right, TRUE);
  gtk_widget_set_vexpand(right, TRUE);
  gtk_widget_set_margin_start(right, 12);
  gtk_widget_set_margin_end(right, 12);
  gtk_widget_set_margin_top(right, 12);
  gtk_widget_set_margin_bottom(right, 12);

  p->now_playing_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(p->now_playing_label), 0.0f);
  set_now_playing(p, NULL);

  p->video_stack = gtk_stack_new();
  gtk_widget_set_hexpand(p->video_stack, TRUE);
  gtk_widget_set_vexpand(p->video_stack, TRUE);

  // Player/video widget are created lazily at first playback.
  GtkWidget *video_widget = NULL;
  p->video_widget = NULL;
  p->video_disabled_label = gtk_label_new(
      "Pick a song to start playback.\nYou can disable video in Settings."
  );
  gtk_label_set_justify(GTK_LABEL(p->video_disabled_label), GTK_JUSTIFY_CENTER);

  if (video_widget) {
    gtk_stack_add_named(GTK_STACK(p->video_stack), video_widget, "video");
  }
  gtk_stack_add_named(GTK_STACK(p->video_stack), p->video_disabled_label, "disabled");
  gtk_stack_set_visible_child(GTK_STACK(p->video_stack), video_widget ? video_widget : p->video_disabled_label);

  p->progress_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_draw_value(GTK_SCALE(p->progress_scale), FALSE);
  gtk_widget_add_events(p->progress_scale, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
  g_signal_connect(p->progress_scale, "button-press-event", G_CALLBACK(on_progress_button_press), p);
  g_signal_connect(p->progress_scale, "button-release-event", G_CALLBACK(on_progress_button_release), p);

  p->time_label = gtk_label_new("00:00 / 00:00");
  gtk_label_set_xalign(GTK_LABEL(p->time_label), 1.0f);

  GtkWidget *progress_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_box_pack_start(GTK_BOX(progress_row), p->progress_scale, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(progress_row), p->time_label, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(right), p->now_playing_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(right), p->video_stack, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(right), progress_row, FALSE, FALSE, 0);
  return right;
}

static GtkWidget *build_controls(PypifyApp *p) {
  GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_start(controls, 12);
  gtk_widget_set_margin_end(controls, 12);
  gtk_widget_set_margin_top(controls, 0);
  gtk_widget_set_margin_bottom(controls, 10);

  p->prev_button = gtk_button_new_with_label("Prev");
  p->back_button = gtk_button_new_with_label("-10s");
  p->play_pause_button = gtk_button_new_with_label("Play");
  p->forward_button = gtk_button_new_with_label("+10s");
  p->next_button = gtk_button_new_with_label("Next");

  p->shuffle_toggle = gtk_toggle_button_new_with_label("Shuffle");
  p->repeat_toggle = gtk_toggle_button_new_with_label("Repeat");

  g_signal_connect(p->prev_button, "clicked", G_CALLBACK(on_prev_clicked), p);
  g_signal_connect(p->back_button, "clicked", G_CALLBACK(on_skip_back_clicked), p);
  g_signal_connect(p->play_pause_button, "clicked", G_CALLBACK(on_play_pause_clicked), p);
  g_signal_connect(p->forward_button, "clicked", G_CALLBACK(on_skip_forward_clicked), p);
  g_signal_connect(p->next_button, "clicked", G_CALLBACK(on_next_clicked), p);

  gtk_box_pack_start(GTK_BOX(controls), p->prev_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(controls), p->back_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(controls), p->play_pause_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(controls), p->forward_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(controls), p->next_button, FALSE, FALSE, 0);

  GtkWidget *spacer = gtk_label_new("");
  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(controls), spacer, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(controls), p->shuffle_toggle, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(controls), p->repeat_toggle, FALSE, FALSE, 0);
  return controls;
}

static GtkWidget *build_main(PypifyApp *p) {
  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  GtkWidget *sidebar = build_sidebar(p);
  GtkWidget *center = build_center(p);
  gtk_paned_pack1(GTK_PANED(paned), sidebar, FALSE, TRUE);
  gtk_paned_pack2(GTK_PANED(paned), center, TRUE, TRUE);
  return paned;
}

static void on_window_destroy(GtkWidget *w, gpointer user_data) {
  (void)w;
  PypifyApp *p = (PypifyApp *)user_data;
  if (!p) return;
  if (p->tick_id) {
    g_source_remove(p->tick_id);
    p->tick_id = 0;
  }
  if (p->player) {
    pypify_player_free(p->player);
    p->player = NULL;
  }
  if (p->tracks) {
    g_ptr_array_unref(p->tracks);
    p->tracks = NULL;
  }
  if (p->visible_tracks) {
    g_array_unref(p->visible_tracks);
    p->visible_tracks = NULL;
  }
  g_free(p->current_folder);
  p->current_folder = NULL;
  g_free(p);
}

PypifyApp *pypify_app_new(GtkApplication *app) {
  gst_init(NULL, NULL);

  PypifyApp *p = g_new0(PypifyApp, 1);
  p->app = app;
  p->current_track_index = -1;
  p->is_playing = FALSE;
  p->progress_dragging = FALSE;

  // Lazy-init player on first playback to reduce startup crash surface area.
  p->player = NULL;

  p->window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(p->window), "Pypify");
  gtk_window_set_default_size(GTK_WINDOW(p->window), 1200, 760);
  g_signal_connect(p->window, "destroy", G_CALLBACK(on_window_destroy), p);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(p->window), root);

  GtkWidget *header = build_header(p);
  GtkWidget *content = build_main(p);
  GtkWidget *controls = build_controls(p);

  p->status_label = gtk_label_new("Choose a folder to begin.");
  gtk_label_set_xalign(GTK_LABEL(p->status_label), 0.0f);
  gtk_widget_set_margin_start(p->status_label, 12);
  gtk_widget_set_margin_end(p->status_label, 12);
  gtk_widget_set_margin_top(p->status_label, 0);
  gtk_widget_set_margin_bottom(p->status_label, 12);

  gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(root), content, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(root), controls, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(root), p->status_label, FALSE, FALSE, 0);

  update_play_button(p);
  visible_reset_all(p);
  p->tick_id = g_timeout_add(250, on_tick, p);
  return p;
}

void pypify_app_show(PypifyApp *p) {
  gtk_widget_show_all(p->window);
  g_idle_add(open_folder_on_startup, p);
}

