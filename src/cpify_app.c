#include "cpify_app.h"
#include "config.h"

#include <gst/gst.h>
#include <math.h>
#include <stdlib.h>

#include "media_scanner.h"
#include "player.h"
#include "settings.h"
#include "sound_effects.h"
#include "splash_screen.h"
#include "updater.h"

typedef enum {
  LAYOUT_SIDEBAR,
  LAYOUT_GALLERY
} CPifyLayout;

struct _CPifyApp {
  AdwApplication *app;
  AdwApplicationWindow *window;
  AdwToastOverlay *toast_overlay;

  // Main content stack (splash vs player)
  GtkWidget *content_stack;
  GtkWidget *splash_screen;

  // Header bar
  AdwHeaderBar *header_bar;
  GtkWidget *sidebar_toggle;
  GtkWidget *open_folder_button;
  GtkWidget *layout_dropdown;
  GtkWidget *theme_dropdown;
  GtkWidget *settings_button;
  GtkWidget *settings_popover;

  // Layout state
  CPifyLayout current_layout;
  GtkWidget *layout_stack;  // Stack for switching between sidebar and gallery layouts
  
  // Sidebar layout widgets
  GtkWidget *sidebar_layout;
  GtkWidget *sidebar;
  GtkWidget *search_entry;
  GtkWidget *listbox;

  // Gallery layout widgets
  GtkWidget *gallery_layout;
  GtkWidget *gallery_search_entry;
  GtkWidget *gallery_grid;
  GtkWidget *gallery_scroll;

  // Video overlay (for gallery fullscreen with minimize)
  GtkWidget *video_overlay;
  GtkWidget *video_container;
  gboolean video_minimized;
  GtkWidget *minimize_button;

  // Center (shared)
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

  CPifyPlayer *player;
};

// Forward declarations
static void on_player_eos(gpointer user_data);
static void play_track_index(CPifyApp *p, gint track_index);
static void on_search_changed(GtkEditable *editable, gpointer user_data);
static void open_folder_dialog(CPifyApp *p);
static void switch_to_player_view(CPifyApp *p);
static void populate_gallery(CPifyApp *p);
static void switch_layout(CPifyApp *p, CPifyLayout layout);
static void update_video_for_layout(CPifyApp *p);

static void switch_to_player_view(CPifyApp *p) {
  if (!p || !p->content_stack) return;
  gtk_stack_set_visible_child_name(GTK_STACK(p->content_stack), "player");
  gtk_widget_set_visible(GTK_WIDGET(p->header_bar), TRUE);
}

// Flag to track if we're coming from splash (used in folder callback)
static gboolean g_from_splash = FALSE;

static void on_splash_add_folder(gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  g_from_splash = TRUE;
  open_folder_dialog(p);
}

static void show_toast(CPifyApp *p, const gchar *message) {
  if (!p || !p->toast_overlay) return;
  AdwToast *toast = adw_toast_new(message);
  adw_toast_set_timeout(toast, 3);
  adw_toast_overlay_add_toast(p->toast_overlay, toast);
}

static void set_status(CPifyApp *p, const gchar *text) {
  if (!p || !p->status_label) return;
  gtk_label_set_text(GTK_LABEL(p->status_label), text ? text : "");
}

static void update_play_button(CPifyApp *p) {
  if (!p || !p->play_pause_button) return;
  gtk_button_set_icon_name(GTK_BUTTON(p->play_pause_button), 
    p->is_playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic");
}

static gboolean get_shuffle(CPifyApp *p) {
  if (!p || !p->shuffle_toggle) return FALSE;
  return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->shuffle_toggle));
}

static gboolean get_repeat(CPifyApp *p) {
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

static guint visible_len(CPifyApp *p) {
  return (p && p->visible_tracks) ? p->visible_tracks->len : 0;
}

static gint visible_get_track_index(CPifyApp *p, guint visible_pos) {
  if (!p || !p->visible_tracks) return -1;
  if (visible_pos >= p->visible_tracks->len) return -1;
  return g_array_index(p->visible_tracks, gint, visible_pos);
}

static gint visible_find_pos(CPifyApp *p, gint track_index) {
  if (!p || !p->visible_tracks) return -1;
  for (guint i = 0; i < p->visible_tracks->len; i++) {
    if (g_array_index(p->visible_tracks, gint, i) == track_index) return (gint)i;
  }
  return -1;
}

static void clear_listbox(CPifyApp *p) {
  if (!p || !p->listbox) return;
  // In GTK 4.12+, use gtk_list_box_remove_all
  gtk_list_box_remove_all(GTK_LIST_BOX(p->listbox));
}

static void update_list_playing_icons(CPifyApp *p) {
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

static void set_now_playing(CPifyApp *p, const gchar *title) {
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

static void populate_listbox(CPifyApp *p) {
  if (!p) return;
  clear_listbox(p);
  if (!p->tracks || !p->visible_tracks || !p->listbox) return;

  for (guint i = 0; i < p->visible_tracks->len; i++) {
    gint track_index = visible_get_track_index(p, i);
    if (track_index < 0 || track_index >= (gint)p->tracks->len) continue;
    CPifyTrack *t = g_ptr_array_index(p->tracks, (guint)track_index);

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

static void visible_reset_all(CPifyApp *p) {
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

static void visible_apply_search(CPifyApp *p, const gchar *query) {
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
    CPifyTrack *t = g_ptr_array_index(p->tracks, i);
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

// ============ Gallery Layout Functions ============

static void clear_gallery(CPifyApp *p) {
  if (!p || !p->gallery_grid) return;
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(p->gallery_grid)) != NULL) {
    gtk_flow_box_remove(GTK_FLOW_BOX(p->gallery_grid), child);
  }
}

static void on_gallery_item_activated(GtkFlowBox *box, GtkFlowBoxChild *child, gpointer user_data) {
  (void)box;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  gint track_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "track-index"));
  play_track_index(p, track_index);
}

static void populate_gallery(CPifyApp *p) {
  if (!p) return;
  clear_gallery(p);
  if (!p->tracks || !p->visible_tracks || !p->gallery_grid) return;

  for (guint i = 0; i < p->visible_tracks->len; i++) {
    gint track_index = visible_get_track_index(p, i);
    if (track_index < 0 || track_index >= (gint)p->tracks->len) continue;
    CPifyTrack *t = g_ptr_array_index(p->tracks, (guint)track_index);

    // Create a card-like item for the gallery
    GtkWidget *item = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(item, 180, 180);
    gtk_widget_add_css_class(item, "card");
    gtk_widget_set_margin_top(item, 8);
    gtk_widget_set_margin_bottom(item, 8);
    gtk_widget_set_margin_start(item, 8);
    gtk_widget_set_margin_end(item, 8);

    // Thumbnail - use actual thumbnail if available, otherwise icon
    GtkWidget *thumb;
    
    // Thread-safe thumbnail access
    g_mutex_lock(&t->thumbnail_mutex);
    gboolean has_thumbnail = (t && t->thumbnail != NULL);
    GdkPixbuf *thumbnail_copy = NULL;
    if (has_thumbnail) {
      thumbnail_copy = g_object_ref(t->thumbnail);
    }
    g_mutex_unlock(&t->thumbnail_mutex);
    
    if (has_thumbnail && thumbnail_copy) {
      // Use the actual video thumbnail
      GdkTexture *texture = gdk_texture_new_for_pixbuf(thumbnail_copy);
      thumb = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
      gtk_picture_set_content_fit(GTK_PICTURE(thumb), GTK_CONTENT_FIT_COVER);
      gtk_widget_set_size_request(thumb, 160, 100);
      g_object_unref(texture);
      g_object_unref(thumbnail_copy);
    } else {
      // Use icon based on file type
      const char *icon_name = (t && t->is_video) ? 
        "video-x-generic-symbolic" : "audio-x-generic-symbolic";
      thumb = gtk_image_new_from_icon_name(icon_name);
      gtk_image_set_pixel_size(GTK_IMAGE(thumb), 64);
      gtk_widget_add_css_class(thumb, "dim-label");
    }
    gtk_widget_set_hexpand(thumb, TRUE);
    gtk_widget_set_vexpand(thumb, TRUE);
    gtk_widget_set_valign(thumb, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(thumb, GTK_ALIGN_CENTER);

    // Title label
    GtkWidget *label = gtk_label_new(t && t->title ? t->title : "(unknown)");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 20);
    gtk_label_set_lines(GTK_LABEL(label), 2);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(label, 8);
    gtk_widget_set_margin_end(label, 8);
    gtk_widget_set_margin_bottom(label, 8);

    gtk_box_append(GTK_BOX(item), thumb);
    gtk_box_append(GTK_BOX(item), label);

    // Wrap in FlowBoxChild and store track index
    GtkWidget *flow_child = gtk_flow_box_child_new();
    g_object_set_data(G_OBJECT(flow_child), "track-index", GINT_TO_POINTER(track_index));
    gtk_flow_box_child_set_child(GTK_FLOW_BOX_CHILD(flow_child), item);
    gtk_flow_box_append(GTK_FLOW_BOX(p->gallery_grid), flow_child);
  }
}

static void on_gallery_search_changed(GtkEditable *editable, gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  const gchar *q = gtk_editable_get_text(editable);
  visible_apply_search(p, q);
  populate_gallery(p);
}

static void on_video_minimize_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  if (!p || !p->video_container) return;
  
  p->video_minimized = !p->video_minimized;
  
  if (p->video_minimized) {
    // Minimize to corner - small size, positioned in corner
    gtk_widget_set_size_request(p->video_container, 320, 180);
    gtk_widget_set_halign(p->video_container, GTK_ALIGN_END);
    gtk_widget_set_valign(p->video_container, GTK_ALIGN_START);
    gtk_widget_set_margin_top(p->video_container, 12);
    gtk_widget_set_margin_end(p->video_container, 12);
    gtk_button_set_icon_name(GTK_BUTTON(p->minimize_button), "view-fullscreen-symbolic");
    gtk_widget_set_tooltip_text(p->minimize_button, "Maximize Video");
    // Show the gallery behind
    gtk_widget_set_visible(p->gallery_scroll, TRUE);
  } else {
    // Maximize - full size
    gtk_widget_set_size_request(p->video_container, -1, -1);
    gtk_widget_set_halign(p->video_container, GTK_ALIGN_FILL);
    gtk_widget_set_valign(p->video_container, GTK_ALIGN_FILL);
    gtk_widget_set_margin_top(p->video_container, 0);
    gtk_widget_set_margin_end(p->video_container, 0);
    gtk_button_set_icon_name(GTK_BUTTON(p->minimize_button), "window-minimize-symbolic");
    gtk_widget_set_tooltip_text(p->minimize_button, "Minimize Video");
    // Hide gallery when video is fullscreen
    gtk_widget_set_visible(p->gallery_scroll, FALSE);
  }
}

static void switch_layout(CPifyApp *p, CPifyLayout layout) {
  if (!p || !p->layout_stack) return;
  if (p->current_layout == layout) return;
  
  p->current_layout = layout;
  
  if (layout == LAYOUT_SIDEBAR) {
    gtk_stack_set_visible_child_name(GTK_STACK(p->layout_stack), "sidebar");
    gtk_widget_set_visible(p->sidebar_toggle, TRUE);
  } else {
    gtk_stack_set_visible_child_name(GTK_STACK(p->layout_stack), "gallery");
    gtk_widget_set_visible(p->sidebar_toggle, FALSE);
    // Sync search and populate gallery
    if (p->search_entry && p->gallery_search_entry) {
      const gchar *q = gtk_editable_get_text(GTK_EDITABLE(p->search_entry));
      gtk_editable_set_text(GTK_EDITABLE(p->gallery_search_entry), q);
    }
    populate_gallery(p);
  }
  
  // Update video widget position for the new layout
  update_video_for_layout(p);
}

static void on_layout_dropdown_changed(GObject *dropdown, GParamSpec *pspec, gpointer user_data) {
  (void)pspec;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
  switch_layout(p, selected == 0 ? LAYOUT_SIDEBAR : LAYOUT_GALLERY);
  
  // Save layout preference
  CPifySettings *settings = cpify_settings_get();
  settings->layout = (gint)selected;
  cpify_settings_save();
}

static void on_theme_dropdown_changed(GObject *dropdown, GParamSpec *pspec, gpointer user_data) {
  (void)pspec;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
  
  CPifyTheme theme;
  switch (selected) {
    case 1: theme = CPIFY_THEME_LIGHT; break;
    case 2: theme = CPIFY_THEME_DARK; break;
    default: theme = CPIFY_THEME_SYSTEM; break;
  }
  
  cpify_settings_apply_theme(p->app, theme);
  
  // Save theme preference
  CPifySettings *settings = cpify_settings_get();
  settings->theme = theme;
  cpify_settings_save();
}

static void update_video_for_layout(CPifyApp *p) {
  if (!p || !p->video_widget) return;
  
  GtkWidget *current_parent = gtk_widget_get_parent(p->video_widget);
  
  if (p->current_layout == LAYOUT_GALLERY) {
    // Move video to gallery overlay container
    if (p->video_container && current_parent != p->video_container) {
      // Remove from current parent
      if (current_parent) {
        if (GTK_IS_STACK(current_parent)) {
          // Don't unparent from stack - just hide it there
        } else {
          g_object_ref(p->video_widget);
          gtk_box_remove(GTK_BOX(current_parent), p->video_widget);
        }
      }
      
      // Remove placeholder if exists
      GtkWidget *child = gtk_widget_get_last_child(p->video_container);
      while (child) {
        GtkWidget *prev = gtk_widget_get_prev_sibling(child);
        if (GTK_IS_LABEL(child)) {
          gtk_box_remove(GTK_BOX(p->video_container), child);
        }
        child = prev;
      }
      
      // Add video widget to container
      gtk_box_append(GTK_BOX(p->video_container), p->video_widget);
      if (current_parent && !GTK_IS_STACK(current_parent)) {
        g_object_unref(p->video_widget);
      }
      
      gtk_widget_set_visible(p->video_container, p->is_playing);
    }
  } else {
    // Move video to sidebar's video stack
    if (p->video_stack) {
      // The video_stack already has the video, just make it visible
      gtk_stack_set_visible_child(GTK_STACK(p->video_stack), p->video_widget);
      
      // Hide gallery video container
      if (p->video_container) {
        gtk_widget_set_visible(p->video_container, FALSE);
      }
    }
  }
}

// ============ End Gallery Layout Functions ============

static gboolean ensure_player(CPifyApp *p) {
  if (!p) return FALSE;
  if (p->player) return TRUE;

  p->player = cpify_player_new();
  if (!p->player) {
    show_toast(p, "Playback unavailable (GStreamer failed to initialize)");
    return FALSE;
  }
  cpify_player_set_eos_callback(p->player, on_player_eos, p);

  // Add the video widget to the stack
  if (!p->video_widget && p->video_stack) {
    GtkWidget *vw = cpify_player_get_video_widget(p->player);
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

// Apply volume setting in real-time (respects audio mute state)
static void apply_volume_setting(CPifyApp *p) {
  if (!p || !p->player || !p->volume_scale) return;
  
  // Only apply volume if audio is enabled (not muted)
  gboolean audio_on = p->audio_switch ?
    adw_switch_row_get_active(ADW_SWITCH_ROW(p->audio_switch)) : TRUE;
  
  if (audio_on) {
    gdouble vol = gtk_range_get_value(GTK_RANGE(p->volume_scale)) / 100.0;
    cpify_player_set_volume(p->player, vol);
  }
  // If audio is off, keep it muted (volume stays at 0)
}

// Apply speed/rate setting in real-time (no reload needed)
static void apply_speed_setting(CPifyApp *p) {
  if (!p || !p->player || !p->speed_scale) return;
  gdouble rate = gtk_range_get_value(GTK_RANGE(p->speed_scale)) / 100.0;
  cpify_player_set_rate(p->player, rate);
}

// Update video stack visibility based on video switch
static void update_video_visibility(CPifyApp *p) {
  if (!p || !p->video_stack) return;
  gboolean video_on = p->video_switch ? 
    adw_switch_row_get_active(ADW_SWITCH_ROW(p->video_switch)) : TRUE;
  
  GtkWidget *vw = p->video_widget ? p->video_widget : 
    (p->player ? cpify_player_get_video_widget(p->player) : NULL);
  
  g_print("[DEBUG] update_video_visibility: video_on=%d, video_widget=%p\n", video_on, (void*)vw);
  
  if (!video_on || !vw) {
    gtk_stack_set_visible_child(GTK_STACK(p->video_stack), p->video_disabled_label);
  } else {
    gtk_stack_set_visible_child(GTK_STACK(p->video_stack), vw);
    g_print("[DEBUG] update_video_visibility: set video widget as visible child\n");
  }
}

// Apply audio toggle in real-time (mute/unmute without restart)
static void apply_audio_toggle(CPifyApp *p) {
  if (!p || !p->player) return;
  
  gboolean audio_on = p->audio_switch ?
    adw_switch_row_get_active(ADW_SWITCH_ROW(p->audio_switch)) : TRUE;
  
  if (audio_on) {
    // Restore volume from slider
    gdouble vol = p->volume_scale ? 
      gtk_range_get_value(GTK_RANGE(p->volume_scale)) / 100.0 : 0.8;
    cpify_player_set_volume(p->player, vol);
  } else {
    // Mute by setting volume to 0
    cpify_player_set_volume(p->player, 0.0);
  }
}

// Apply video toggle in real-time (show/hide without restart)
static void apply_video_toggle(CPifyApp *p) {
  if (!p) return;
  // Just update visibility - video keeps playing in background
  update_video_visibility(p);
}

// Initialize player settings without reload (called when starting playback)
static void init_player_settings(CPifyApp *p) {
  if (!p || !p->player) return;
  
  gdouble rate = p->speed_scale ?
    gtk_range_get_value(GTK_RANGE(p->speed_scale)) / 100.0 : 1.0;
  
  cpify_player_set_rate(p->player, rate);
  
  // Apply audio toggle (handles mute state and volume)
  apply_audio_toggle(p);
  
  // Apply video toggle (handles visibility)
  apply_video_toggle(p);
}

static void play_track_index(CPifyApp *p, gint track_index) {
  if (!p || !p->tracks) return;
  if (track_index < 0 || track_index >= (gint)p->tracks->len) return;
  if (!ensure_player(p)) return;

  CPifyTrack *t = g_ptr_array_index(p->tracks, (guint)track_index);
  if (!t || !t->path) return;

  // Stop current playback before loading new track
  cpify_player_stop(p->player);

  // Set guard to prevent settings callbacks from reloading during init
  p->is_loading_track = TRUE;
  init_player_settings(p);

  GError *err = NULL;
  if (!cpify_player_set_path(p->player, t->path, &err)) {
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
  cpify_player_play(p->player);

  update_list_playing_icons(p);

  gint visible_pos = visible_find_pos(p, track_index);
  if (visible_pos >= 0) {
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(p->listbox), visible_pos);
    if (row) gtk_list_box_select_row(GTK_LIST_BOX(p->listbox), row);
  }
  
  // Update video position based on current layout
  update_video_for_layout(p);
}

static gint choose_next_visible_pos(CPifyApp *p) {
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

static void play_next(CPifyApp *p) {
  gint next_pos = choose_next_visible_pos(p);
  if (next_pos < 0) {
    p->is_playing = FALSE;
    update_play_button(p);
    if (p->player) cpify_player_stop(p->player);
    set_status(p, "Reached end of list.");
    update_list_playing_icons(p);
    return;
  }
  gint track_index = visible_get_track_index(p, (guint)next_pos);
  play_track_index(p, track_index);
}

static void play_prev(CPifyApp *p) {
  guint n = visible_len(p);
  if (n == 0) return;
  gint cur_pos = visible_find_pos(p, p->current_track_index);
  gint prev_pos = (cur_pos <= 0) ? 0 : (cur_pos - 1);
  gint track_index = visible_get_track_index(p, (guint)prev_pos);
  play_track_index(p, track_index);
}

static void on_player_eos(gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  if (!p) return;
  if (get_repeat(p) && p->current_track_index >= 0) {
    play_track_index(p, p->current_track_index);
    return;
  }
  play_next(p);
}

// Thumbnail generation callback with debounced gallery refresh
static guint gallery_refresh_timer = 0;

static gboolean refresh_gallery_idle(gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  gallery_refresh_timer = 0;
  
  if (p && p->current_layout == LAYOUT_GALLERY) {
    populate_gallery(p);
  }
  
  return G_SOURCE_REMOVE;
}

static void on_thumbnail_generated(CPifyTrack *track, gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  if (!p) return;
  
  (void)track; // unused
  
  // Debounce gallery refresh - only refresh after 200ms of no updates
  if (p->current_layout == LAYOUT_GALLERY) {
    if (gallery_refresh_timer != 0) {
      g_source_remove(gallery_refresh_timer);
    }
    gallery_refresh_timer = g_timeout_add(200, refresh_gallery_idle, p);
  }
}

static void start_thumbnail_generation(CPifyApp *p) {
  if (!p || !p->tracks) return;
  
  g_print("[DEBUG] Starting async thumbnail generation for %u tracks\n", p->tracks->len);
  
  // Use new batch async API for parallel multicore processing
  cpify_generate_thumbnails_batch(p->tracks, on_thumbnail_generated, p);
}

static void load_folder(CPifyApp *p, const gchar *folder) {
  g_print("[DEBUG] load_folder: called with folder='%s'\n", folder ? folder : "(null)");
  if (!p || !folder) return;

  g_free(p->current_folder);
  p->current_folder = g_strdup(folder);
  
  // Save last folder to settings
  CPifySettings *settings = cpify_settings_get();
  g_free(settings->last_folder);
  settings->last_folder = g_strdup(folder);
  cpify_settings_save();

  if (p->tracks) {
    g_ptr_array_unref(p->tracks);
    p->tracks = NULL;
  }

  set_status(p, "Scanning folder…");

  g_print("[DEBUG] load_folder: scanning...\n");
  GError *err = NULL;
  p->tracks = cpify_scan_folder(folder, &err);
  if (!p->tracks) {
    gchar *msg = g_strdup_printf("Scan failed: %s", err ? err->message : "unknown error");
    show_toast(p, msg);
    g_free(msg);
    if (err) g_error_free(err);
    return;
  }
  g_print("[DEBUG] load_folder: found %u tracks\n", p->tracks->len);

  // Block search signal while we update the entries
  g_print("[DEBUG] load_folder: clearing search entries...\n");
  if (p->search_entry) {
    g_signal_handlers_block_by_func(p->search_entry, on_search_changed, p);
    gtk_editable_set_text(GTK_EDITABLE(p->search_entry), "");
    g_signal_handlers_unblock_by_func(p->search_entry, on_search_changed, p);
  }
  if (p->gallery_search_entry) {
    g_signal_handlers_block_by_func(p->gallery_search_entry, on_gallery_search_changed, p);
    gtk_editable_set_text(GTK_EDITABLE(p->gallery_search_entry), "");
    g_signal_handlers_unblock_by_func(p->gallery_search_entry, on_gallery_search_changed, p);
  }
  
  g_print("[DEBUG] load_folder: visible_reset_all...\n");
  visible_reset_all(p);
  
  g_print("[DEBUG] load_folder: populate views...\n");
  populate_listbox(p);
  populate_gallery(p);

  p->current_track_index = -1;
  p->is_playing = FALSE;
  
  g_print("[DEBUG] load_folder: updating UI...\n");
  update_play_button(p);
  set_now_playing(p, NULL);
  update_list_playing_icons(p);

  gchar *status = g_strdup_printf("Loaded %u media file(s)", p->tracks->len);
  set_status(p, status);
  g_free(status);
  
  // Start generating thumbnails in background
  start_thumbnail_generation(p);
  
  g_print("[DEBUG] load_folder: done\n");
}

static void on_folder_selected(GObject *source, GAsyncResult *result, gpointer user_data) {
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  CPifyApp *p = (CPifyApp *)user_data;
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

static void open_folder_dialog(CPifyApp *p) {
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
static void on_sidebar_toggle_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  if (!p || !p->sidebar) return;
  
  gboolean visible = gtk_widget_get_visible(p->sidebar);
  gtk_widget_set_visible(p->sidebar, !visible);
  
  // Update button icon
  gtk_button_set_icon_name(GTK_BUTTON(p->sidebar_toggle), 
    visible ? "sidebar-show-symbolic" : "sidebar-hide-symbolic");
}

static void on_open_folder_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  cpify_play_click_sound();
  open_folder_dialog((CPifyApp *)user_data);
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
  (void)box;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  gint track_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "track-index"));
  play_track_index(p, track_index);
}

static void on_search_changed(GtkEditable *editable, gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  const gchar *q = gtk_editable_get_text(editable);
  visible_apply_search(p, q);
  populate_listbox(p);
  update_list_playing_icons(p);
}

static void on_play_pause_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
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
    cpify_player_pause(p->player);
  } else {
    p->is_playing = TRUE;
    cpify_player_play(p->player);
  }
  update_play_button(p);
  update_list_playing_icons(p);
}

static void on_next_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  cpify_play_click_sound();
  play_next((CPifyApp *)user_data);
}

static void on_prev_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  cpify_play_click_sound();
  play_prev((CPifyApp *)user_data);
}

static void on_skip_back_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  if (!p || !p->player || p->current_track_index < 0) return;
  cpify_player_seek_relative(p->player, -10.0);
}

static void on_skip_forward_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  if (!p || !p->player || p->current_track_index < 0) return;
  cpify_player_seek_relative(p->player, 10.0);
}

static void on_settings_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  gtk_popover_popup(GTK_POPOVER(p->settings_popover));
}

static void on_shuffle_toggled(GtkToggleButton *btn, gpointer user_data) {
  (void)btn;
  (void)user_data;
  cpify_play_click_sound();
}

static void on_repeat_toggled(GtkToggleButton *btn, gpointer user_data) {
  (void)btn;
  (void)user_data;
  cpify_play_click_sound();
}

// Progress bar drag handling using GtkGestureClick
static void on_progress_drag_begin(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer user_data) {
  (void)gesture;
  (void)x;
  (void)y;
  ((CPifyApp *)user_data)->progress_dragging = TRUE;
}

static void on_progress_drag_end(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer user_data) {
  (void)gesture;
  (void)x;
  (void)y;
  CPifyApp *p = (CPifyApp *)user_data;
  p->progress_dragging = FALSE;
  if (!p->player || p->current_track_index < 0) return;
  gdouble seconds = gtk_range_get_value(GTK_RANGE(p->progress_scale));
  cpify_player_seek_to(p->player, seconds);
}

static void on_volume_changed(GtkRange *range, gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  apply_volume_setting(p);
  
  // Save to settings
  CPifySettings *settings = cpify_settings_get();
  settings->volume = gtk_range_get_value(range);
  cpify_settings_save();
}

static void on_speed_changed(GtkRange *range, gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  apply_speed_setting(p);
  
  // Save to settings
  CPifySettings *settings = cpify_settings_get();
  settings->speed = gtk_range_get_value(range);
  cpify_settings_save();
}

static void on_audio_switch_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
  (void)pspec;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  apply_audio_toggle(p);
  
  // Save to settings
  CPifySettings *settings = cpify_settings_get();
  settings->audio_enabled = adw_switch_row_get_active(ADW_SWITCH_ROW(obj));
  cpify_settings_save();
}

static void on_video_switch_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
  (void)pspec;
  cpify_play_click_sound();
  CPifyApp *p = (CPifyApp *)user_data;
  apply_video_toggle(p);
  
  // Save to settings
  CPifySettings *settings = cpify_settings_get();
  settings->video_enabled = adw_switch_row_get_active(ADW_SWITCH_ROW(obj));
  cpify_settings_save();
}

static gboolean on_tick(gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  if (!p || !p->player || p->current_track_index < 0) return G_SOURCE_CONTINUE;

  gint64 pos_ns = 0;
  gint64 dur_ns = 0;
  gboolean have_pos = cpify_player_query_position(p->player, &pos_ns);
  gboolean have_dur = cpify_player_query_duration(p->player, &dur_ns);
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

static GtkWidget *build_settings_popover(CPifyApp *p) {
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

static GtkWidget *build_sidebar(CPifyApp *p) {
  p->sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_size_request(p->sidebar, 320, -1);
  gtk_widget_set_margin_start(p->sidebar, 12);
  gtk_widget_set_margin_end(p->sidebar, 0);
  gtk_widget_set_margin_top(p->sidebar, 12);
  gtk_widget_set_margin_bottom(p->sidebar, 12);

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

  gtk_box_append(GTK_BOX(p->sidebar), p->search_entry);
  gtk_box_append(GTK_BOX(p->sidebar), list_scroller);
  return p->sidebar;
}

static GtkWidget *build_center(CPifyApp *p) {
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

static GtkWidget *build_controls(CPifyApp *p) {
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

static GtkWidget *build_gallery_layout(CPifyApp *p) {
  // Main container with overlay for minimized video
  p->video_overlay = gtk_overlay_new();
  gtk_widget_set_hexpand(p->video_overlay, TRUE);
  gtk_widget_set_vexpand(p->video_overlay, TRUE);
  
  // Gallery content (base layer)
  GtkWidget *gallery_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start(gallery_box, 12);
  gtk_widget_set_margin_end(gallery_box, 12);
  gtk_widget_set_margin_top(gallery_box, 12);
  gtk_widget_set_margin_bottom(gallery_box, 12);
  
  // Search entry for gallery
  p->gallery_search_entry = gtk_search_entry_new();
  gtk_widget_set_hexpand(p->gallery_search_entry, TRUE);
  g_object_set(p->gallery_search_entry, "placeholder-text", "Search songs…", NULL);
  g_signal_connect(p->gallery_search_entry, "changed", G_CALLBACK(on_gallery_search_changed), p);
  
  // Scrolled window for the grid
  p->gallery_scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(p->gallery_scroll), 
    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(p->gallery_scroll, TRUE);
  
  // Flow box for grid of items
  p->gallery_grid = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(p->gallery_grid), GTK_SELECTION_NONE);
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(p->gallery_grid), TRUE);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(p->gallery_grid), 10);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(p->gallery_grid), 2);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(p->gallery_grid), 8);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(p->gallery_grid), 8);
  gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(p->gallery_grid), TRUE);
  g_signal_connect(p->gallery_grid, "child-activated", G_CALLBACK(on_gallery_item_activated), p);
  
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(p->gallery_scroll), p->gallery_grid);
  
  gtk_box_append(GTK_BOX(gallery_box), p->gallery_search_entry);
  gtk_box_append(GTK_BOX(gallery_box), p->gallery_scroll);
  
  gtk_overlay_set_child(GTK_OVERLAY(p->video_overlay), gallery_box);
  
  // Video container (overlay layer) - starts hidden, shown when playing
  p->video_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(p->video_container, "card");
  gtk_widget_set_visible(p->video_container, FALSE);  // Hidden until playback starts
  
  // Minimize button inside video container
  GtkWidget *video_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign(video_header, GTK_ALIGN_END);
  gtk_widget_set_margin_top(video_header, 8);
  gtk_widget_set_margin_end(video_header, 8);
  
  p->minimize_button = gtk_button_new_from_icon_name("window-minimize-symbolic");
  gtk_widget_set_tooltip_text(p->minimize_button, "Minimize Video");
  gtk_widget_add_css_class(p->minimize_button, "circular");
  gtk_widget_add_css_class(p->minimize_button, "osd");
  g_signal_connect(p->minimize_button, "clicked", G_CALLBACK(on_video_minimize_clicked), p);
  
  gtk_box_append(GTK_BOX(video_header), p->minimize_button);
  gtk_box_append(GTK_BOX(p->video_container), video_header);
  
  // The actual video will be added here when playing
  // For now, add a placeholder
  GtkWidget *video_placeholder = gtk_label_new("Video will appear here");
  gtk_widget_set_hexpand(video_placeholder, TRUE);
  gtk_widget_set_vexpand(video_placeholder, TRUE);
  gtk_widget_add_css_class(video_placeholder, "dim-label");
  gtk_box_append(GTK_BOX(p->video_container), video_placeholder);
  
  gtk_overlay_add_overlay(GTK_OVERLAY(p->video_overlay), p->video_container);
  
  p->video_minimized = TRUE;  // Start minimized (video in corner when playing)
  
  return p->video_overlay;
}

static GtkWidget *build_main(CPifyApp *p) {
  // Create layout stack for switching between sidebar and gallery
  p->layout_stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(p->layout_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_set_transition_duration(GTK_STACK(p->layout_stack), 200);
  gtk_widget_set_hexpand(p->layout_stack, TRUE);
  gtk_widget_set_vexpand(p->layout_stack, TRUE);
  
  // Sidebar layout (default)
  p->sidebar_layout = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  GtkWidget *sidebar = build_sidebar(p);
  GtkWidget *center = build_center(p);
  gtk_paned_set_start_child(GTK_PANED(p->sidebar_layout), sidebar);
  gtk_paned_set_end_child(GTK_PANED(p->sidebar_layout), center);
  gtk_paned_set_shrink_start_child(GTK_PANED(p->sidebar_layout), FALSE);
  gtk_paned_set_shrink_end_child(GTK_PANED(p->sidebar_layout), FALSE);
  
  // Gallery layout
  p->gallery_layout = build_gallery_layout(p);
  
  // Add to stack
  gtk_stack_add_named(GTK_STACK(p->layout_stack), p->sidebar_layout, "sidebar");
  gtk_stack_add_named(GTK_STACK(p->layout_stack), p->gallery_layout, "gallery");
  
  // Default to sidebar layout
  p->current_layout = LAYOUT_SIDEBAR;
  gtk_stack_set_visible_child_name(GTK_STACK(p->layout_stack), "sidebar");
  
  return p->layout_stack;
}

static gboolean on_window_close_request(GtkWindow *window, gpointer user_data) {
  (void)window;
  CPifyApp *p = (CPifyApp *)user_data;
  if (!p) return FALSE;
  
  // Clean up popover before button is destroyed
  if (p->settings_popover) {
    gtk_widget_unparent(p->settings_popover);
    p->settings_popover = NULL;
  }
  
  if (p->tick_id) {
    g_source_remove(p->tick_id);
    p->tick_id = 0;
  }
  if (gallery_refresh_timer != 0) {
    g_source_remove(gallery_refresh_timer);
    gallery_refresh_timer = 0;
  }
  if (p->player) {
    cpify_player_free(p->player);
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
  
  cpify_thumbnail_cleanup();
  cpify_sound_effects_cleanup();
  cpify_settings_cleanup();
  cpify_updater_cleanup();
  g_free(p);
  
  return FALSE;  // Allow window to close
}

static void on_splash_mapped(GtkWidget *widget, gpointer user_data) {
  (void)user_data;
  cpify_splash_start_animation(widget);
}

// Update check callback - called when update check completes
static void on_update_check_complete(CPifyReleaseInfo *release, GError *error, gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  
  if (error) {
    g_print("[UPDATE] Check failed: %s\n", error->message);
    // Silently fail - don't bother user with update check errors
    return;
  }
  
  if (release) {
    g_print("[UPDATE] New version available: %s\n", release->title);
    // Show update dialog
    cpify_updater_show_dialog(GTK_WINDOW(p->window), release);
    // Note: release will be freed when dialog is closed
  } else {
    g_print("[UPDATE] App is up to date (version %s)\n", cpify_updater_get_current_version());
  }
}

// Delayed update check - triggered after app is fully loaded
static gboolean trigger_update_check(gpointer user_data) {
  CPifyApp *p = (CPifyApp *)user_data;
  cpify_updater_check_async(on_update_check_complete, p);
  return G_SOURCE_REMOVE;  // Don't repeat
}

CPifyApp *cpify_app_new(AdwApplication *app) {
  gst_init(NULL, NULL);
  cpify_sound_effects_init();
  cpify_settings_init();
  cpify_updater_init();

  // Apply saved theme immediately
  CPifySettings *settings = cpify_settings_get();
  cpify_settings_apply_theme(app, settings->theme);

  CPifyApp *p = g_new0(CPifyApp, 1);
  p->app = app;
  p->current_track_index = -1;
  p->is_playing = FALSE;
  p->progress_dragging = FALSE;
  p->player = NULL;

  // Create the main window with AdwApplicationWindow
  p->window = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(app)));
  gtk_window_set_title(GTK_WINDOW(p->window), "CPify");
  gtk_window_set_default_size(GTK_WINDOW(p->window), 1200, 760);
  g_signal_connect(p->window, "close-request", G_CALLBACK(on_window_close_request), p);

  // Root container
  GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  // Create header bar with libadwaita styling (hidden initially for splash)
  p->header_bar = ADW_HEADER_BAR(adw_header_bar_new());
  adw_header_bar_set_title_widget(p->header_bar, 
    adw_window_title_new("CPify", "Offline Vibes"));
  gtk_widget_set_visible(GTK_WIDGET(p->header_bar), FALSE);

  // Sidebar toggle button
  p->sidebar_toggle = gtk_button_new_from_icon_name("sidebar-hide-symbolic");
  gtk_widget_set_tooltip_text(p->sidebar_toggle, "Toggle Sidebar");
  g_signal_connect(p->sidebar_toggle, "clicked", G_CALLBACK(on_sidebar_toggle_clicked), p);
  adw_header_bar_pack_start(p->header_bar, p->sidebar_toggle);

  // Open folder button
  p->open_folder_button = gtk_button_new_with_label("Open Folder");
  gtk_widget_add_css_class(p->open_folder_button, "suggested-action");
  g_signal_connect(p->open_folder_button, "clicked", G_CALLBACK(on_open_folder_clicked), p);
  adw_header_bar_pack_start(p->header_bar, p->open_folder_button);

  // Layout dropdown (Sidebar / Gallery)
  const char *layout_options[] = {"Sidebar", "Gallery", NULL};
  p->layout_dropdown = gtk_drop_down_new_from_strings(layout_options);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(p->layout_dropdown), 0);
  gtk_widget_set_tooltip_text(p->layout_dropdown, "Layout");
  g_signal_connect(p->layout_dropdown, "notify::selected", G_CALLBACK(on_layout_dropdown_changed), p);
  adw_header_bar_pack_start(p->header_bar, p->layout_dropdown);

  // Theme dropdown (System / Light / Dark)
  const char *theme_options[] = {"System", "Light", "Dark", NULL};
  p->theme_dropdown = gtk_drop_down_new_from_strings(theme_options);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(p->theme_dropdown), 0);
  gtk_widget_set_tooltip_text(p->theme_dropdown, "Theme");
  g_signal_connect(p->theme_dropdown, "notify::selected", G_CALLBACK(on_theme_dropdown_changed), p);
  adw_header_bar_pack_end(p->header_bar, p->theme_dropdown);

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
  p->splash_screen = cpify_splash_new(on_splash_add_folder, p);
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
  
  // Apply saved settings to UI
  {
    CPifySettings *s = cpify_settings_get();
    
    // Apply theme dropdown selection
    guint theme_idx = 0;
    switch (s->theme) {
      case CPIFY_THEME_LIGHT: theme_idx = 1; break;
      case CPIFY_THEME_DARK: theme_idx = 2; break;
      default: theme_idx = 0; break;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(p->theme_dropdown), theme_idx);
    
    // Apply layout
    gtk_drop_down_set_selected(GTK_DROP_DOWN(p->layout_dropdown), (guint)s->layout);
    if (s->layout == 1) {
      switch_layout(p, LAYOUT_GALLERY);
    }
    
    // Apply volume and speed to settings popover (they're already built)
    if (p->volume_scale) {
      gtk_range_set_value(GTK_RANGE(p->volume_scale), s->volume);
    }
    if (p->speed_scale) {
      gtk_range_set_value(GTK_RANGE(p->speed_scale), s->speed);
    }
    if (p->audio_switch) {
      adw_switch_row_set_active(ADW_SWITCH_ROW(p->audio_switch), s->audio_enabled);
    }
    if (p->video_switch) {
      adw_switch_row_set_active(ADW_SWITCH_ROW(p->video_switch), s->video_enabled);
    }
    
    // Load last folder if available
    if (s->last_folder && g_file_test(s->last_folder, G_FILE_TEST_IS_DIR)) {
      load_folder(p, s->last_folder);
    }
  }
  
  return p;
}

void cpify_app_show(CPifyApp *p) {
  gtk_window_present(GTK_WINDOW(p->window));
  
  // Check for updates after a 3 second delay (let the UI settle first)
  g_timeout_add_seconds(3, trigger_update_check, p);
}
