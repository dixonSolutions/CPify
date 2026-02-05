#include "pypify_app.h"
#include "config.h"

#include <gst/gst.h>
#include <math.h>
#include <stdlib.h>

#include "media_scanner.h"
#include "player.h"
#include "sound_effects.h"
#include "splash_screen.h"

struct _PypifyApp {
  AdwApplication *app;
  AdwApplicationWindow *window;
  AdwToastOverlay *toast_overlay;

  // Main content stack (splash vs player)
  GtkWidget *content_stack;
  GtkWidget *splash_screen;

  // Header bar
  AdwHeaderBar *header_bar;
  GtkWidget *open_folder_button;
  GtkWidget *settings_button;
  GtkWidget *settings_popover;

  // Sidebar
  GtkWidget *search_entry;
  GtkWidget *listbox;

  // Center
  GtkWidget *now_playing_label;
  GtkWidget *video_stack;
  GtkWidget *video_widget;
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
  GPtrArray *tracks;
  GArray *visible_tracks;
  gint current_track_index;
  gboolean is_playing;
  gboolean is_loading_track;  // Guard to prevent settings reload during track load

  guint tick_id;

  PypifyPlayer *player;
};

// Forward declarations
static void on_player_eos(gpointer user_data);
static void play_track_index(PypifyApp *p, gint track_index);
static void on_search_changed(GtkEditable *editable, gpointer user_data);
static void open_folder_dialog(PypifyApp *p);
static void switch_to_player_view(PypifyApp *p);

static void switch_to_player_view(PypifyApp *p) {
  if (!p || !p->content_stack) return;
  gtk_stack_set_visible_child_name(GTK_STACK(p->content_stack), "player");
  gtk_widget_set_visible(GTK_WIDGET(p->header_bar), TRUE);
}

// Flag to track if we're coming from splash (used in folder callback)
static gboolean g_from_splash = FALSE;

static void on_splash_add_folder(gpointer user_data) {
  PypifyApp *p = (PypifyApp *)user_data;
  g_from_splash = TRUE;
  open_folder_dialog(p);
}

static void show_toast(PypifyApp *p, const gchar *message) {
  if (!p || !p->toast_overlay) return;
  AdwToast *toast = adw_toast_new(message);
  adw_toast_set_timeout(toast, 3);
  adw_toast_overlay_add_toast(p->toast_overlay, toast);
}

static void set_status(PypifyApp *p, const gchar *text) {
  if (!p || !p->status_label) return;
  gtk_label_set_text(GTK_LABEL(p->status_label), text ? text : "");
}

static void update_play_button(PypifyApp *p) {
  if (!p || !p->play_pause_button) return;
  gtk_button_set_icon_name(GTK_BUTTON(p->play_pause_button), 
    p->is_playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic");
}

static gboolean get_shuffle(PypifyApp *p) {
  if (!p || !p->shuffle_toggle) return FALSE;
  return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->shuffle_toggle));
}

static gboolean get_repeat(PypifyApp *p) {
  if (!p || !p->repeat_toggle) return FALSE;
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
  if (!p || !p->listbox) return;
  // In GTK 4.12+, use gtk_list_box_remove_all
  gtk_list_box_remove_all(GTK_LIST_BOX(p->listbox));
}

static void update_list_playing_icons(PypifyApp *p) {
  if (!p || !p->listbox) return;
  // Iterate using gtk_list_box_get_row_at_index for GTK 4 compatibility
  for (gint i = 0; ; i++) {
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(p->listbox), i);
    if (!row) break;
    
    GtkWidget *img = g_object_get_data(G_OBJECT(row), "play-icon");
    gint idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "track-index"));
    if (img && GTK_IS_IMAGE(img)) {
      if (idx == p->current_track_index && p->is_playing) {
        gtk_image_set_from_icon_name(GTK_IMAGE(img), "media-playback-start-symbolic");
      } else {
        gtk_image_clear(GTK_IMAGE(img));
      }
    }
  }
}

static void set_now_playing(PypifyApp *p, const gchar *title) {
  if (!p || !p->now_playing_label) return;
  if (!title || title[0] == '\0') {
    gtk_label_set_markup(GTK_LABEL(p->now_playing_label), 
      "<span size='x-large' weight='bold'>Choose a song</span>");
    return;
  }
  gchar *escaped = g_markup_escape_text(title, -1);
  gchar *markup = g_strdup_printf("<span size='x-large' weight='bold'>%s</span>", escaped);
  gtk_label_set_markup(GTK_LABEL(p->now_playing_label), markup);
  g_free(markup);
  g_free(escaped);
}

static void populate_listbox(PypifyApp *p) {
  if (!p) return;
  clear_listbox(p);
  if (!p->tracks || !p->visible_tracks || !p->listbox) return;

  for (guint i = 0; i < p->visible_tracks->len; i++) {
    gint track_index = visible_get_track_index(p, i);
    if (track_index < 0 || track_index >= (gint)p->tracks->len) continue;
    PypifyTrack *t = g_ptr_array_index(p->tracks, (guint)track_index);

    GtkWidget *row = gtk_list_box_row_new();
    g_object_set_data(G_OBJECT(row), "track-index", GINT_TO_POINTER(track_index));

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    GtkWidget *icon = gtk_image_new();
    gtk_widget_set_size_request(icon, 18, -1);
    g_object_set_data(G_OBJECT(row), "play-icon", icon);

    GtkWidget *label = gtk_label_new(t && t->title ? t->title : "(unknown)");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(label, TRUE);

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(p->listbox), row);
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

  p->player = pypify_player_new();
  if (!p->player) {
    show_toast(p, "Playback unavailable (GStreamer failed to initialize)");
    return FALSE;
  }
  pypify_player_set_eos_callback(p->player, on_player_eos, p);

  // Add the video widget to the stack
  if (!p->video_widget && p->video_stack) {
    GtkWidget *vw = pypify_player_get_video_widget(p->player);
    g_print("[DEBUG] ensure_player: got video widget %p\n", (void*)vw);
    if (vw) {
      p->video_widget = vw;
      gtk_widget_set_size_request(p->video_widget, 320, 180);
      gtk_widget_set_hexpand(p->video_widget, TRUE);
      gtk_widget_set_vexpand(p->video_widget, TRUE);
      gtk_stack_add_named(GTK_STACK(p->video_stack), p->video_widget, "video");
      gtk_stack_set_visible_child(GTK_STACK(p->video_stack), p->video_widget);
      g_print("[DEBUG] ensure_player: video widget added to stack and set visible\n");
    }
  }

  return TRUE;
}

// Apply volume setting in real-time (no reload needed)
static void apply_volume_setting(PypifyApp *p) {
  if (!p || !p->player || !p->volume_scale) return;
  gdouble vol = gtk_range_get_value(GTK_RANGE(p->volume_scale)) / 100.0;
  pypify_player_set_volume(p->player, vol);
}

// Apply speed/rate setting in real-time (no reload needed)
static void apply_speed_setting(PypifyApp *p) {
  if (!p || !p->player || !p->speed_scale) return;
  gdouble rate = gtk_range_get_value(GTK_RANGE(p->speed_scale)) / 100.0;
  pypify_player_set_rate(p->player, rate);
}

// Update video stack visibility based on video switch
static void update_video_visibility(PypifyApp *p) {
  if (!p || !p->video_stack) return;
  gboolean video_on = p->video_switch ? 
    adw_switch_row_get_active(ADW_SWITCH_ROW(p->video_switch)) : TRUE;
  
  GtkWidget *vw = p->video_widget ? p->video_widget : 
    (p->player ? pypify_player_get_video_widget(p->player) : NULL);
  
  g_print("[DEBUG] update_video_visibility: video_on=%d, video_widget=%p\n", video_on, (void*)vw);
  
  if (!video_on || !vw) {
    gtk_stack_set_visible_child(GTK_STACK(p->video_stack), p->video_disabled_label);
  } else {
    gtk_stack_set_visible_child(GTK_STACK(p->video_stack), vw);
    g_print("[DEBUG] update_video_visibility: set video widget as visible child\n");
  }
}

// Apply audio/video toggle - these require pipeline reload
static void apply_av_toggle_settings(PypifyApp *p) {
  if (!p || !p->player) return;
  
  // Don't reload during track loading - it will be set up properly by play_track_index
  if (p->is_loading_track) {
    g_print("[DEBUG] apply_av_toggle_settings: skipped during track load\n");
    return;
  }
  
  gboolean audio_on = p->audio_switch ?
    adw_switch_row_get_active(ADW_SWITCH_ROW(p->audio_switch)) : TRUE;
  gboolean video_on = p->video_switch ?
    adw_switch_row_get_active(ADW_SWITCH_ROW(p->video_switch)) : TRUE;

  pypify_player_set_audio_enabled(p->player, audio_on);
  pypify_player_set_video_enabled(p->player, video_on);
  update_video_visibility(p);

  // Only reload if we have a track playing (to apply audio/video flags)
  if (p->tracks && p->current_track_index >= 0 && 
      p->current_track_index < (gint)p->tracks->len) {
    PypifyTrack *t = g_ptr_array_index(p->tracks, (guint)p->current_track_index);
    if (t && t->path) {
      gint64 pos_ns = 0;
      gboolean have_pos = pypify_player_query_position(p->player, &pos_ns);
      gdouble pos_s = have_pos ? ((gdouble)pos_ns / (gdouble)GST_SECOND) : 0.0;
      gboolean should_play = p->is_playing;

      g_print("[DEBUG] apply_av_toggle_settings: reloading track\n");
      GError *err = NULL;
      if (pypify_player_set_path(p->player, t->path, &err)) {
        if (have_pos) pypify_player_seek_to(p->player, pos_s);
        if (should_play) pypify_player_play(p->player);
        else pypify_player_pause(p->player);
      } else if (err) {
        g_error_free(err);
      }
    }
  }
}

// Initialize player settings without reload (called when starting playback)
static void init_player_settings(PypifyApp *p) {
  if (!p || !p->player) return;
  
  gdouble vol = p->volume_scale ? 
    gtk_range_get_value(GTK_RANGE(p->volume_scale)) / 100.0 : 0.8;
  gdouble rate = p->speed_scale ?
    gtk_range_get_value(GTK_RANGE(p->speed_scale)) / 100.0 : 1.0;
  gboolean audio_on = p->audio_switch ?
    adw_switch_row_get_active(ADW_SWITCH_ROW(p->audio_switch)) : TRUE;
  gboolean video_on = p->video_switch ?
    adw_switch_row_get_active(ADW_SWITCH_ROW(p->video_switch)) : TRUE;

  pypify_player_set_volume(p->player, vol);
  pypify_player_set_rate(p->player, rate);
  pypify_player_set_audio_enabled(p->player, audio_on);
  pypify_player_set_video_enabled(p->player, video_on);
  update_video_visibility(p);
}

static void play_track_index(PypifyApp *p, gint track_index) {
  if (!p || !p->tracks) return;
  if (track_index < 0 || track_index >= (gint)p->tracks->len) return;
  if (!ensure_player(p)) return;

  PypifyTrack *t = g_ptr_array_index(p->tracks, (guint)track_index);
  if (!t || !t->path) return;

  // Set guard to prevent settings callbacks from reloading during init
  p->is_loading_track = TRUE;
  init_player_settings(p);

  GError *err = NULL;
  if (!pypify_player_set_path(p->player, t->path, &err)) {
    p->is_loading_track = FALSE;
    gchar *msg = g_strdup_printf("Unable to play: %s", err ? err->message : "unknown error");
    show_toast(p, msg);
    g_free(msg);
    if (err) g_error_free(err);
    return;
  }

  p->current_track_index = track_index;
  p->is_playing = TRUE;
  p->is_loading_track = FALSE;  // Clear guard after track is loaded
  
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
  g_print("[DEBUG] load_folder: called with folder='%s'\n", folder ? folder : "(null)");
  if (!p || !folder) return;

  g_free(p->current_folder);
  p->current_folder = g_strdup(folder);

  if (p->tracks) {
    g_ptr_array_unref(p->tracks);
    p->tracks = NULL;
  }

  set_status(p, "Scanning folder…");

  g_print("[DEBUG] load_folder: scanning...\n");
  GError *err = NULL;
  p->tracks = pypify_scan_folder(folder, &err);
  if (!p->tracks) {
    gchar *msg = g_strdup_printf("Scan failed: %s", err ? err->message : "unknown error");
    show_toast(p, msg);
    g_free(msg);
    if (err) g_error_free(err);
    return;
  }
  g_print("[DEBUG] load_folder: found %u tracks\n", p->tracks->len);

  // Block search signal while we update the entry
  g_print("[DEBUG] load_folder: clearing search entry...\n");
  if (p->search_entry) {
    g_signal_handlers_block_by_func(p->search_entry, on_search_changed, p);
    gtk_editable_set_text(GTK_EDITABLE(p->search_entry), "");
    g_signal_handlers_unblock_by_func(p->search_entry, on_search_changed, p);
  }
  
  g_print("[DEBUG] load_folder: visible_reset_all...\n");
  visible_reset_all(p);
  
  g_print("[DEBUG] load_folder: populate_listbox...\n");
  populate_listbox(p);

  p->current_track_index = -1;
  p->is_playing = FALSE;
  
  g_print("[DEBUG] load_folder: updating UI...\n");
  update_play_button(p);
  set_now_playing(p, NULL);
  update_list_playing_icons(p);

  gchar *status = g_strdup_printf("Loaded %u media file(s)", p->tracks->len);
  set_status(p, status);
  g_free(status);
  g_print("[DEBUG] load_folder: done\n");
}

static void on_folder_selected(GObject *source, GAsyncResult *result, gpointer user_data) {
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  PypifyApp *p = (PypifyApp *)user_data;
  gboolean from_splash = g_from_splash;
  g_from_splash = FALSE;
  
  GError *error = NULL;
  GFile *folder = gtk_file_dialog_select_folder_finish(dialog, result, &error);
  
  if (folder) {
    gchar *path = g_file_get_path(folder);
    if (path) {
      load_folder(p, path);
      g_free(path);
      // Only switch to player view if folder was loaded successfully
      if (from_splash && p->tracks && p->tracks->len > 0) {
        switch_to_player_view(p);
      } else if (from_splash) {
        // Folder had no media - still switch but show message
        switch_to_player_view(p);
      }
    }
    g_object_unref(folder);
  } else if (error && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
    show_toast(p, error->message);
    g_error_free(error);
  }
  // If dialog was dismissed and we came from splash, stay on splash
}

static void open_folder_dialog(PypifyApp *p) {
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Select a media folder");
  gtk_file_dialog_set_modal(dialog, TRUE);
  
  if (p->current_folder) {
    GFile *initial = g_file_new_for_path(p->current_folder);
    gtk_file_dialog_set_initial_folder(dialog, initial);
    g_object_unref(initial);
  }
  
  gtk_file_dialog_select_folder(dialog, GTK_WINDOW(p->window), NULL, on_folder_selected, p);
  g_object_unref(dialog);
}

// Button click handlers with sound effects
static void on_open_folder_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  pypify_play_click_sound();
  open_folder_dialog((PypifyApp *)user_data);
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
  (void)box;
  pypify_play_click_sound();
  PypifyApp *p = (PypifyApp *)user_data;
  gint track_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "track-index"));
  play_track_index(p, track_index);
}

static void on_search_changed(GtkEditable *editable, gpointer user_data) {
  PypifyApp *p = (PypifyApp *)user_data;
  const gchar *q = gtk_editable_get_text(editable);
  visible_apply_search(p, q);
  populate_listbox(p);
  update_list_playing_icons(p);
}

static void on_play_pause_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  pypify_play_click_sound();
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
  pypify_play_click_sound();
  play_next((PypifyApp *)user_data);
}

static void on_prev_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  pypify_play_click_sound();
  play_prev((PypifyApp *)user_data);
}

static void on_skip_back_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  pypify_play_click_sound();
  PypifyApp *p = (PypifyApp *)user_data;
  if (!p || !p->player || p->current_track_index < 0) return;
  pypify_player_seek_relative(p->player, -10.0);
}

static void on_skip_forward_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  pypify_play_click_sound();
  PypifyApp *p = (PypifyApp *)user_data;
  if (!p || !p->player || p->current_track_index < 0) return;
  pypify_player_seek_relative(p->player, 10.0);
}

static void on_settings_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  pypify_play_click_sound();
  PypifyApp *p = (PypifyApp *)user_data;
  gtk_popover_popup(GTK_POPOVER(p->settings_popover));
}

static void on_shuffle_toggled(GtkToggleButton *btn, gpointer user_data) {
  (void)btn;
  (void)user_data;
  pypify_play_click_sound();
}

static void on_repeat_toggled(GtkToggleButton *btn, gpointer user_data) {
  (void)btn;
  (void)user_data;
  pypify_play_click_sound();
}

// Progress bar drag handling using GtkGestureClick
static void on_progress_drag_begin(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer user_data) {
  (void)gesture;
  (void)x;
  (void)y;
  ((PypifyApp *)user_data)->progress_dragging = TRUE;
}

static void on_progress_drag_end(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer user_data) {
  (void)gesture;
  (void)x;
  (void)y;
  PypifyApp *p = (PypifyApp *)user_data;
  p->progress_dragging = FALSE;
  if (!p->player || p->current_track_index < 0) return;
  gdouble seconds = gtk_range_get_value(GTK_RANGE(p->progress_scale));
  pypify_player_seek_to(p->player, seconds);
}

static void on_volume_changed(GtkRange *range, gpointer user_data) {
  (void)range;
  // Volume applies in real-time, no reload needed
  apply_volume_setting((PypifyApp *)user_data);
}

static void on_speed_changed(GtkRange *range, gpointer user_data) {
  (void)range;
  // Speed applies in real-time, no reload needed
  apply_speed_setting((PypifyApp *)user_data);
}

static void on_audio_switch_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
  (void)obj;
  (void)pspec;
  pypify_play_click_sound();
  // Audio toggle requires pipeline reload to apply flags
  apply_av_toggle_settings((PypifyApp *)user_data);
}

static void on_video_switch_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
  (void)obj;
  (void)pspec;
  pypify_play_click_sound();
  // Video toggle requires pipeline reload to apply flags
  apply_av_toggle_settings((PypifyApp *)user_data);
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

static gchar *format_scale_percent(GtkScale *scale, gdouble value, gpointer user_data) {
  (void)scale;
  (void)user_data;
  return g_strdup_printf("%.0f%%", value);
}

static GtkWidget *build_settings_popover(PypifyApp *p) {
  GtkWidget *popover = gtk_popover_new();
  gtk_widget_set_parent(popover, p->settings_button);
  
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(box, "boxed-list");
  gtk_widget_set_margin_start(box, 12);
  gtk_widget_set_margin_end(box, 12);
  gtk_widget_set_margin_top(box, 12);
  gtk_widget_set_margin_bottom(box, 12);

  // Volume row with scale
  GtkWidget *vol_row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(vol_row), "Volume");
  p->volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 5);
  gtk_widget_set_size_request(p->volume_scale, 150, -1);
  gtk_widget_set_valign(p->volume_scale, GTK_ALIGN_CENTER);
  gtk_range_set_value(GTK_RANGE(p->volume_scale), 80);
  gtk_scale_set_draw_value(GTK_SCALE(p->volume_scale), TRUE);
  gtk_scale_set_value_pos(GTK_SCALE(p->volume_scale), GTK_POS_RIGHT);
  gtk_scale_set_format_value_func(GTK_SCALE(p->volume_scale), format_scale_percent, NULL, NULL);
  g_signal_connect(p->volume_scale, "value-changed", G_CALLBACK(on_volume_changed), p);
  adw_action_row_add_suffix(ADW_ACTION_ROW(vol_row), p->volume_scale);

  // Speed row with scale
  GtkWidget *speed_row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(speed_row), "Speed");
  p->speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 25, 200, 25);
  gtk_widget_set_size_request(p->speed_scale, 150, -1);
  gtk_widget_set_valign(p->speed_scale, GTK_ALIGN_CENTER);
  gtk_range_set_value(GTK_RANGE(p->speed_scale), 100);
  gtk_scale_set_draw_value(GTK_SCALE(p->speed_scale), TRUE);
  gtk_scale_set_value_pos(GTK_SCALE(p->speed_scale), GTK_POS_RIGHT);
  gtk_scale_set_format_value_func(GTK_SCALE(p->speed_scale), format_scale_percent, NULL, NULL);
  g_signal_connect(p->speed_scale, "value-changed", G_CALLBACK(on_speed_changed), p);
  gtk_scale_add_mark(GTK_SCALE(p->speed_scale), 100, GTK_POS_BOTTOM, "1x");
  adw_action_row_add_suffix(ADW_ACTION_ROW(speed_row), p->speed_scale);

  // Audio switch row
  p->audio_switch = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->audio_switch), "Audio");
  adw_switch_row_set_active(ADW_SWITCH_ROW(p->audio_switch), TRUE);
  g_signal_connect(p->audio_switch, "notify::active", G_CALLBACK(on_audio_switch_changed), p);

  // Video switch row
  p->video_switch = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->video_switch), "Show Video");
  adw_switch_row_set_active(ADW_SWITCH_ROW(p->video_switch), TRUE);
  g_signal_connect(p->video_switch, "notify::active", G_CALLBACK(on_video_switch_changed), p);

  // Use a ListBox for proper styling
  GtkWidget *list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class(list, "boxed-list");
  gtk_list_box_append(GTK_LIST_BOX(list), vol_row);
  gtk_list_box_append(GTK_LIST_BOX(list), speed_row);
  gtk_list_box_append(GTK_LIST_BOX(list), p->audio_switch);
  gtk_list_box_append(GTK_LIST_BOX(list), p->video_switch);
  
  gtk_box_append(GTK_BOX(box), list);
  gtk_popover_set_child(GTK_POPOVER(popover), box);
  
  return popover;
}

static GtkWidget *build_sidebar(PypifyApp *p) {
  GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_size_request(left, 320, -1);
  gtk_widget_set_margin_start(left, 12);
  gtk_widget_set_margin_end(left, 0);
  gtk_widget_set_margin_top(left, 12);
  gtk_widget_set_margin_bottom(left, 12);

  p->search_entry = gtk_search_entry_new();
  gtk_widget_set_hexpand(p->search_entry, TRUE);
  g_object_set(p->search_entry, "placeholder-text", "Search songs…", NULL);
  g_signal_connect(p->search_entry, "changed", G_CALLBACK(on_search_changed), p);

  GtkWidget *list_scroller = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroller), 
    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(list_scroller, TRUE);

  p->listbox = gtk_list_box_new();
  gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(p->listbox), TRUE);
  gtk_widget_add_css_class(p->listbox, "navigation-sidebar");
  g_signal_connect(p->listbox, "row-activated", G_CALLBACK(on_row_activated), p);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scroller), p->listbox);

  gtk_box_append(GTK_BOX(left), p->search_entry);
  gtk_box_append(GTK_BOX(left), list_scroller);
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

  // Video stack with rounded corners and background
  p->video_stack = gtk_stack_new();
  gtk_widget_set_hexpand(p->video_stack, TRUE);
  gtk_widget_set_vexpand(p->video_stack, TRUE);
  gtk_widget_add_css_class(p->video_stack, "card");

  p->video_disabled_label = gtk_label_new(
      "Click 'Open Folder' to select your music folder\nthen pick a song to start playback.");
  gtk_label_set_justify(GTK_LABEL(p->video_disabled_label), GTK_JUSTIFY_CENTER);
  gtk_widget_set_hexpand(p->video_disabled_label, TRUE);
  gtk_widget_set_vexpand(p->video_disabled_label, TRUE);
  gtk_widget_add_css_class(p->video_disabled_label, "dim-label");

  gtk_stack_add_named(GTK_STACK(p->video_stack), p->video_disabled_label, "disabled");
  gtk_stack_set_visible_child(GTK_STACK(p->video_stack), p->video_disabled_label);

  // Progress bar with drag gesture
  p->progress_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_draw_value(GTK_SCALE(p->progress_scale), FALSE);
  gtk_widget_set_hexpand(p->progress_scale, TRUE);
  
  GtkGesture *drag = gtk_gesture_drag_new();
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_progress_drag_begin), p);
  g_signal_connect(drag, "drag-end", G_CALLBACK(on_progress_drag_end), p);
  gtk_widget_add_controller(p->progress_scale, GTK_EVENT_CONTROLLER(drag));

  p->time_label = gtk_label_new("00:00 / 00:00");
  gtk_label_set_xalign(GTK_LABEL(p->time_label), 1.0f);
  gtk_widget_add_css_class(p->time_label, "caption");

  GtkWidget *progress_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(progress_row), p->progress_scale);
  gtk_box_append(GTK_BOX(progress_row), p->time_label);

  gtk_box_append(GTK_BOX(right), p->now_playing_label);
  gtk_box_append(GTK_BOX(right), p->video_stack);
  gtk_box_append(GTK_BOX(right), progress_row);
  return right;
}

static GtkWidget *build_controls(PypifyApp *p) {
  GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(controls, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(controls, 12);
  gtk_widget_set_margin_end(controls, 12);
  gtk_widget_set_margin_top(controls, 8);
  gtk_widget_set_margin_bottom(controls, 12);

  // Media control buttons with icons
  p->prev_button = gtk_button_new_from_icon_name("media-skip-backward-symbolic");
  p->back_button = gtk_button_new_from_icon_name("media-seek-backward-symbolic");
  p->play_pause_button = gtk_button_new_from_icon_name("media-playback-start-symbolic");
  p->forward_button = gtk_button_new_from_icon_name("media-seek-forward-symbolic");
  p->next_button = gtk_button_new_from_icon_name("media-skip-forward-symbolic");

  // Make play button larger
  gtk_widget_add_css_class(p->play_pause_button, "circular");
  gtk_widget_add_css_class(p->play_pause_button, "suggested-action");

  // Shuffle and repeat toggle buttons
  p->shuffle_toggle = gtk_toggle_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(p->shuffle_toggle), "media-playlist-shuffle-symbolic");
  gtk_widget_set_tooltip_text(p->shuffle_toggle, "Shuffle");
  
  p->repeat_toggle = gtk_toggle_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(p->repeat_toggle), "media-playlist-repeat-symbolic");
  gtk_widget_set_tooltip_text(p->repeat_toggle, "Repeat");

  g_signal_connect(p->prev_button, "clicked", G_CALLBACK(on_prev_clicked), p);
  g_signal_connect(p->back_button, "clicked", G_CALLBACK(on_skip_back_clicked), p);
  g_signal_connect(p->play_pause_button, "clicked", G_CALLBACK(on_play_pause_clicked), p);
  g_signal_connect(p->forward_button, "clicked", G_CALLBACK(on_skip_forward_clicked), p);
  g_signal_connect(p->next_button, "clicked", G_CALLBACK(on_next_clicked), p);
  g_signal_connect(p->shuffle_toggle, "toggled", G_CALLBACK(on_shuffle_toggled), p);
  g_signal_connect(p->repeat_toggle, "toggled", G_CALLBACK(on_repeat_toggled), p);

  gtk_box_append(GTK_BOX(controls), p->shuffle_toggle);
  gtk_box_append(GTK_BOX(controls), p->prev_button);
  gtk_box_append(GTK_BOX(controls), p->back_button);
  gtk_box_append(GTK_BOX(controls), p->play_pause_button);
  gtk_box_append(GTK_BOX(controls), p->forward_button);
  gtk_box_append(GTK_BOX(controls), p->next_button);
  gtk_box_append(GTK_BOX(controls), p->repeat_toggle);
  
  return controls;
}

static GtkWidget *build_main(PypifyApp *p) {
  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  GtkWidget *sidebar = build_sidebar(p);
  GtkWidget *center = build_center(p);
  gtk_paned_set_start_child(GTK_PANED(paned), sidebar);
  gtk_paned_set_end_child(GTK_PANED(paned), center);
  gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
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
  
  pypify_sound_effects_cleanup();
  g_free(p);
}

static void on_splash_mapped(GtkWidget *widget, gpointer user_data) {
  (void)user_data;
  pypify_splash_start_animation(widget);
}

PypifyApp *pypify_app_new(AdwApplication *app) {
  gst_init(NULL, NULL);
  pypify_sound_effects_init();

  PypifyApp *p = g_new0(PypifyApp, 1);
  p->app = app;
  p->current_track_index = -1;
  p->is_playing = FALSE;
  p->progress_dragging = FALSE;
  p->player = NULL;

  // Create the main window with AdwApplicationWindow
  p->window = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(app)));
  gtk_window_set_title(GTK_WINDOW(p->window), "Pypify");
  gtk_window_set_default_size(GTK_WINDOW(p->window), 1200, 760);
  g_signal_connect(p->window, "destroy", G_CALLBACK(on_window_destroy), p);

  // Root container
  GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  // Create header bar with libadwaita styling (hidden initially for splash)
  p->header_bar = ADW_HEADER_BAR(adw_header_bar_new());
  adw_header_bar_set_title_widget(p->header_bar, 
    adw_window_title_new("Pypify", "Media Player"));
  gtk_widget_set_visible(GTK_WIDGET(p->header_bar), FALSE);

  // Open folder button
  p->open_folder_button = gtk_button_new_with_label("Open Folder");
  gtk_widget_add_css_class(p->open_folder_button, "suggested-action");
  g_signal_connect(p->open_folder_button, "clicked", G_CALLBACK(on_open_folder_clicked), p);
  adw_header_bar_pack_start(p->header_bar, p->open_folder_button);

  // Settings button
  p->settings_button = gtk_button_new_from_icon_name("emblem-system-symbolic");
  gtk_widget_set_tooltip_text(p->settings_button, "Settings");
  g_signal_connect(p->settings_button, "clicked", G_CALLBACK(on_settings_clicked), p);
  adw_header_bar_pack_end(p->header_bar, p->settings_button);
  
  // Build settings popover after settings button exists
  p->settings_popover = build_settings_popover(p);

  // Create content stack (splash vs player)
  p->content_stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(p->content_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_set_transition_duration(GTK_STACK(p->content_stack), 400);
  gtk_widget_set_hexpand(p->content_stack, TRUE);
  gtk_widget_set_vexpand(p->content_stack, TRUE);

  // Create splash screen
  p->splash_screen = pypify_splash_new(on_splash_add_folder, p);
  g_signal_connect(p->splash_screen, "map", G_CALLBACK(on_splash_mapped), NULL);
  gtk_stack_add_named(GTK_STACK(p->content_stack), p->splash_screen, "splash");

  // Toast overlay for notifications (wraps player content)
  p->toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());

  // Main player content
  GtkWidget *player_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *main_content = build_main(p);
  GtkWidget *controls = build_controls(p);

  // Status bar
  p->status_label = gtk_label_new("Click 'Open Folder' to select a music folder.");
  gtk_label_set_xalign(GTK_LABEL(p->status_label), 0.0f);
  gtk_widget_set_margin_start(p->status_label, 12);
  gtk_widget_set_margin_end(p->status_label, 12);
  gtk_widget_set_margin_top(p->status_label, 4);
  gtk_widget_set_margin_bottom(p->status_label, 8);
  gtk_widget_add_css_class(p->status_label, "caption");
  gtk_widget_add_css_class(p->status_label, "dim-label");

  gtk_box_append(GTK_BOX(player_content), main_content);
  gtk_box_append(GTK_BOX(player_content), controls);
  gtk_box_append(GTK_BOX(player_content), p->status_label);

  adw_toast_overlay_set_child(p->toast_overlay, player_content);
  gtk_stack_add_named(GTK_STACK(p->content_stack), GTK_WIDGET(p->toast_overlay), "player");

  // Start with splash screen
  gtk_stack_set_visible_child_name(GTK_STACK(p->content_stack), "splash");

  gtk_box_append(GTK_BOX(root_box), GTK_WIDGET(p->header_bar));
  gtk_box_append(GTK_BOX(root_box), p->content_stack);

  adw_application_window_set_content(p->window, root_box);

  update_play_button(p);
  visible_reset_all(p);
  p->tick_id = g_timeout_add(250, on_tick, p);
  return p;
}

void pypify_app_show(PypifyApp *p) {
  gtk_window_present(GTK_WINDOW(p->window));
}
