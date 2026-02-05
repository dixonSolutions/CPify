#include "splash_screen.h"
#include "config.h"
#include "sound_effects.h"

#include <math.h>

typedef struct {
  GtkWidget *overlay;
  GtkWidget *logo_image;
  GtkWidget *title_label;
  GtkWidget *subtitle_label;
  GtkWidget *add_folder_button;
  GtkWidget *main_box;

  PypifySplashCallback on_add_folder;
  gpointer user_data;

  // Animation state
  guint animation_tick_id;
  gint64 animation_start_time;
  gboolean animation_complete;

  // Track dark mode preference
  gboolean is_dark_mode;
} SplashData;

// Animation timing (in milliseconds)
#define LOGO_SLIDE_DURATION 800
#define LOGO_SLIDE_DELAY 200
#define TITLE_FADE_DELAY 300
#define TITLE_FADE_DURATION 600
#define SUBTITLE_FADE_DELAY 600
#define SUBTITLE_FADE_DURATION 600
#define BUTTON_FADE_DELAY 1000
#define BUTTON_FADE_DURATION 400

// Easing function for smooth animation
static gdouble ease_out_cubic(gdouble t) {
  return 1.0 - pow(1.0 - t, 3.0);
}

static gdouble ease_out_expo(gdouble t) {
  return t >= 1.0 ? 1.0 : 1.0 - pow(2.0, -10.0 * t);
}

// Forward declarations
static void update_splash_background_class(SplashData *data);

static void update_logo_for_theme(SplashData *data) {
  AdwStyleManager *style_manager = adw_style_manager_get_default();
  gboolean dark = adw_style_manager_get_dark(style_manager);

  if (dark != data->is_dark_mode || !gtk_image_get_paintable(GTK_IMAGE(data->logo_image))) {
    data->is_dark_mode = dark;

    gchar *logo_path;
    if (dark) {
      logo_path = g_build_filename(PYPIFY_ASSETS_DIR, "Pypify Dark Mode Logo copy.svg", NULL);
    } else {
      logo_path = g_build_filename(PYPIFY_ASSETS_DIR, "Pypify Light Mode Logo.svg", NULL);
    }

    GdkTexture *texture = gdk_texture_new_from_filename(logo_path, NULL);
    if (texture) {
      gtk_image_set_from_paintable(GTK_IMAGE(data->logo_image), GDK_PAINTABLE(texture));
      g_object_unref(texture);
    } else {
      // Fallback: try using file icon
      GFile *file = g_file_new_for_path(logo_path);
      GtkWidget *picture = gtk_picture_new_for_file(file);
      g_object_unref(file);
      if (picture) {
        // Can't easily swap, so just log
        g_printerr("Warning: Could not load logo as texture: %s\n", logo_path);
      }
    }
    g_free(logo_path);
  }
}

static void on_style_changed(AdwStyleManager *style_manager, GParamSpec *pspec, gpointer user_data) {
  (void)style_manager;
  (void)pspec;
  SplashData *data = (SplashData *)user_data;
  update_logo_for_theme(data);
  update_splash_background_class(data);
}

static gboolean on_animation_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer user_data) {
  (void)widget;
  SplashData *data = (SplashData *)user_data;

  gint64 now = gdk_frame_clock_get_frame_time(clock);
  if (data->animation_start_time == 0) {
    data->animation_start_time = now;
  }

  gdouble elapsed_ms = (gdouble)(now - data->animation_start_time) / 1000.0;

  // Logo slide animation: from right (offset 300px) to center (offset 0)
  gdouble logo_progress = 0.0;
  if (elapsed_ms >= LOGO_SLIDE_DELAY) {
    gdouble logo_elapsed = elapsed_ms - LOGO_SLIDE_DELAY;
    logo_progress = CLAMP(logo_elapsed / (gdouble)LOGO_SLIDE_DURATION, 0.0, 1.0);
    logo_progress = ease_out_expo(logo_progress);
  }
  gdouble logo_offset = (1.0 - logo_progress) * 300.0;
  gtk_widget_set_margin_start(data->logo_image, (gint)logo_offset);
  gtk_widget_set_opacity(data->logo_image, logo_progress);

  // Title fade in
  gdouble title_progress = 0.0;
  if (elapsed_ms >= TITLE_FADE_DELAY) {
    gdouble title_elapsed = elapsed_ms - TITLE_FADE_DELAY;
    title_progress = CLAMP(title_elapsed / (gdouble)TITLE_FADE_DURATION, 0.0, 1.0);
    title_progress = ease_out_cubic(title_progress);
  }
  gtk_widget_set_opacity(data->title_label, title_progress);

  // Subtitle fade in
  gdouble subtitle_progress = 0.0;
  if (elapsed_ms >= SUBTITLE_FADE_DELAY) {
    gdouble subtitle_elapsed = elapsed_ms - SUBTITLE_FADE_DELAY;
    subtitle_progress = CLAMP(subtitle_elapsed / (gdouble)SUBTITLE_FADE_DURATION, 0.0, 1.0);
    subtitle_progress = ease_out_cubic(subtitle_progress);
  }
  gtk_widget_set_opacity(data->subtitle_label, subtitle_progress);

  // Button fade in
  gdouble button_progress = 0.0;
  if (elapsed_ms >= BUTTON_FADE_DELAY) {
    gdouble button_elapsed = elapsed_ms - BUTTON_FADE_DELAY;
    button_progress = CLAMP(button_elapsed / (gdouble)BUTTON_FADE_DURATION, 0.0, 1.0);
    button_progress = ease_out_cubic(button_progress);
  }
  gtk_widget_set_opacity(data->add_folder_button, button_progress);

  // Check if animation is complete
  gdouble total_duration = BUTTON_FADE_DELAY + BUTTON_FADE_DURATION + 100;
  if (elapsed_ms >= total_duration) {
    data->animation_complete = TRUE;
    data->animation_tick_id = 0;
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

static void on_add_folder_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  SplashData *data = (SplashData *)user_data;
  pypify_play_click_sound();
  if (data->on_add_folder) {
    data->on_add_folder(data->user_data);
  }
}

static void splash_data_free(gpointer user_data) {
  SplashData *data = (SplashData *)user_data;
  if (data->animation_tick_id) {
    g_source_remove(data->animation_tick_id);
  }

  // Disconnect style manager signal
  AdwStyleManager *style_manager = adw_style_manager_get_default();
  g_signal_handlers_disconnect_by_data(style_manager, data);

  g_free(data);
}

static void setup_splash_css(void) {
  static gboolean css_loaded = FALSE;
  if (css_loaded) return;
  css_loaded = TRUE;

  GtkCssProvider *provider = gtk_css_provider_new();
  const char *css =
    ".splash-background {"
    "  background: linear-gradient(180deg, "
    "    alpha(@window_bg_color, 0.95) 0%, "
    "    shade(@window_bg_color, 0.85) 100%);"
    "}"
    ".splash-background-dark {"
    "  background: linear-gradient(180deg, "
    "    #0d0d0d 0%, "
    "    #1a1a1a 50%, "
    "    #0d0d0d 100%);"
    "}"
    ".splash-title {"
    "  font-weight: 800;"
    "  font-size: 42px;"
    "  letter-spacing: 4px;"
    "}"
    ".splash-subtitle {"
    "  font-style: italic;"
    "  opacity: 0.7;"
    "  font-size: 18px;"
    "}";

  gtk_css_provider_load_from_string(provider, css);
  gtk_style_context_add_provider_for_display(
    gdk_display_get_default(),
    GTK_STYLE_PROVIDER(provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
  );
  g_object_unref(provider);
}

static void update_splash_background_class(SplashData *data) {
  AdwStyleManager *style_manager = adw_style_manager_get_default();
  gboolean dark = adw_style_manager_get_dark(style_manager);

  gtk_widget_remove_css_class(data->overlay, "splash-background");
  gtk_widget_remove_css_class(data->overlay, "splash-background-dark");

  if (dark) {
    gtk_widget_add_css_class(data->overlay, "splash-background-dark");
  } else {
    gtk_widget_add_css_class(data->overlay, "splash-background");
  }
}

GtkWidget *pypify_splash_new(PypifySplashCallback on_add_folder, gpointer user_data) {
  setup_splash_css();

  SplashData *data = g_new0(SplashData, 1);
  data->on_add_folder = on_add_folder;
  data->user_data = user_data;
  data->animation_complete = FALSE;
  data->is_dark_mode = FALSE;

  // Main container - center everything
  data->overlay = gtk_overlay_new();
  gtk_widget_set_hexpand(data->overlay, TRUE);
  gtk_widget_set_vexpand(data->overlay, TRUE);

  // Store data on widget for cleanup
  g_object_set_data_full(G_OBJECT(data->overlay), "splash-data", data, splash_data_free);

  // Main vertical box to hold everything
  data->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_halign(data->main_box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(data->main_box, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(data->main_box, 48);
  gtk_widget_set_margin_end(data->main_box, 48);
  gtk_widget_set_margin_top(data->main_box, 48);
  gtk_widget_set_margin_bottom(data->main_box, 48);

  // Title label "PyPify" at top
  data->title_label = gtk_label_new("PyPify");
  gtk_widget_add_css_class(data->title_label, "splash-title");
  gtk_widget_set_opacity(data->title_label, 0.0);
  gtk_widget_set_margin_bottom(data->title_label, 8);

  // Subtitle "Offline Vibes"
  data->subtitle_label = gtk_label_new("Offline Vibes");
  gtk_widget_add_css_class(data->subtitle_label, "splash-subtitle");
  gtk_widget_set_opacity(data->subtitle_label, 0.0);
  gtk_widget_set_margin_bottom(data->subtitle_label, 32);

  // Logo image - will be loaded based on theme
  data->logo_image = gtk_image_new();
  gtk_image_set_pixel_size(GTK_IMAGE(data->logo_image), 280);
  gtk_widget_set_opacity(data->logo_image, 0.0);
  gtk_widget_set_margin_bottom(data->logo_image, 48);

  // "Add Folder" button
  data->add_folder_button = gtk_button_new_with_label("Add Folder");
  gtk_widget_add_css_class(data->add_folder_button, "suggested-action");
  gtk_widget_add_css_class(data->add_folder_button, "pill");
  gtk_widget_set_size_request(data->add_folder_button, 200, 48);
  gtk_widget_set_halign(data->add_folder_button, GTK_ALIGN_CENTER);
  gtk_widget_set_opacity(data->add_folder_button, 0.0);
  g_signal_connect(data->add_folder_button, "clicked", G_CALLBACK(on_add_folder_clicked), data);

  // Pack everything
  gtk_box_append(GTK_BOX(data->main_box), data->title_label);
  gtk_box_append(GTK_BOX(data->main_box), data->subtitle_label);
  gtk_box_append(GTK_BOX(data->main_box), data->logo_image);
  gtk_box_append(GTK_BOX(data->main_box), data->add_folder_button);

  gtk_overlay_set_child(GTK_OVERLAY(data->overlay), data->main_box);

  // Listen for theme changes
  AdwStyleManager *style_manager = adw_style_manager_get_default();
  g_signal_connect(style_manager, "notify::dark", G_CALLBACK(on_style_changed), data);

  // Initial theme setup
  update_logo_for_theme(data);
  update_splash_background_class(data);

  return data->overlay;
}

void pypify_splash_start_animation(GtkWidget *splash) {
  SplashData *data = g_object_get_data(G_OBJECT(splash), "splash-data");
  if (!data || data->animation_tick_id) return;

  data->animation_start_time = 0;
  data->animation_tick_id = gtk_widget_add_tick_callback(
    splash, on_animation_tick, data, NULL);
}
