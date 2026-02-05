#include "updater.h"
#include "config.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <gio/gio.h>
#include <string.h>

// GitHub API configuration
#define GITHUB_API_BASE "https://api.github.com"
#define GITHUB_REPO_OWNER "dixonSolutions"
#define GITHUB_REPO_NAME "CPify"
#define GITHUB_RELEASES_URL GITHUB_API_BASE "/repos/" GITHUB_REPO_OWNER "/" GITHUB_REPO_NAME "/releases/latest"

// Static soup session for reuse
static SoupSession *g_soup_session = NULL;

// Update check state
typedef struct {
  CPifyUpdateCallback callback;
  gpointer user_data;
} UpdateCheckData;

// Install state
typedef struct {
  CPifyReleaseInfo *release;
  GtkWindow *parent_window;
  GCallback on_complete;
  gpointer user_data;
  GtkWidget *progress_dialog;
  GtkWidget *progress_bar;
  GtkWidget *status_label;
} InstallData;

void cpify_updater_init(void) {
  if (!g_soup_session) {
    g_soup_session = soup_session_new();
    // Set a user agent as required by GitHub API
    soup_session_set_user_agent(g_soup_session, "CPify-Updater/1.0");
  }
}

void cpify_updater_cleanup(void) {
  if (g_soup_session) {
    g_object_unref(g_soup_session);
    g_soup_session = NULL;
  }
}

const gchar *cpify_updater_get_current_version(void) {
  return CPIFY_VERSION;
}

const gchar *cpify_updater_get_os_identifier(void) {
#if defined(__linux__)
  return "linux";
#elif defined(_WIN32) || defined(_WIN64)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "unknown";
#endif
}

static gchar *get_arch_identifier(void) {
#if defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
  return "i386";
#elif defined(__aarch64__) || defined(_M_ARM64)
  return "aarch64";
#elif defined(__arm__) || defined(_M_ARM)
  return "arm";
#else
  return "unknown";
#endif
}

gint cpify_version_compare(const gchar *v1, const gchar *v2) {
  if (!v1 && !v2) return 0;
  if (!v1) return -1;
  if (!v2) return 1;

  // Skip leading 'v' or 'V' if present
  if (*v1 == 'v' || *v1 == 'V') v1++;
  if (*v2 == 'v' || *v2 == 'V') v2++;

  gchar **parts1 = g_strsplit(v1, ".", -1);
  gchar **parts2 = g_strsplit(v2, ".", -1);

  gint result = 0;
  guint i = 0;

  while (parts1[i] || parts2[i]) {
    gint64 n1 = parts1[i] ? g_ascii_strtoll(parts1[i], NULL, 10) : 0;
    gint64 n2 = parts2[i] ? g_ascii_strtoll(parts2[i], NULL, 10) : 0;

    if (n1 < n2) {
      result = -1;
      break;
    } else if (n1 > n2) {
      result = 1;
      break;
    }
    i++;
  }

  g_strfreev(parts1);
  g_strfreev(parts2);
  return result;
}

void cpify_release_info_free(CPifyReleaseInfo *info) {
  if (!info) return;
  g_free(info->tag_name);
  g_free(info->title);
  g_free(info->description);
  g_free(info->download_url);
  g_free(info->published_at);
  g_free(info);
}

static gchar *find_asset_url_for_os(JsonArray *assets) {
  const gchar *os = cpify_updater_get_os_identifier();
  const gchar *arch = get_arch_identifier();

  guint len = json_array_get_length(assets);
  for (guint i = 0; i < len; i++) {
    JsonObject *asset = json_array_get_object_element(assets, i);
    const gchar *name = json_object_get_string_member(asset, "name");
    
    if (!name) continue;

    // Look for binary matching OS and architecture
    // Expected naming: cpify-linux-x86_64, cpify-windows-x86_64.exe, cpify-macos-aarch64
    gchar *name_lower = g_utf8_strdown(name, -1);
    gboolean os_match = g_strstr_len(name_lower, -1, os) != NULL;
    gboolean arch_match = g_strstr_len(name_lower, -1, arch) != NULL;
    g_free(name_lower);

    if (os_match && arch_match) {
      return g_strdup(json_object_get_string_member(asset, "browser_download_url"));
    }
  }

  // Fallback: try to find any matching OS
  for (guint i = 0; i < len; i++) {
    JsonObject *asset = json_array_get_object_element(assets, i);
    const gchar *name = json_object_get_string_member(asset, "name");
    
    if (!name) continue;

    gchar *name_lower = g_utf8_strdown(name, -1);
    gboolean os_match = g_strstr_len(name_lower, -1, os) != NULL;
    g_free(name_lower);

    if (os_match) {
      return g_strdup(json_object_get_string_member(asset, "browser_download_url"));
    }
  }

  return NULL;
}

static CPifyReleaseInfo *parse_release_json(const gchar *json_data, gsize length) {
  JsonParser *parser = json_parser_new();
  GError *error = NULL;

  if (!json_parser_load_from_data(parser, json_data, length, &error)) {
    g_printerr("[UPDATER] Failed to parse JSON: %s\n", error->message);
    g_error_free(error);
    g_object_unref(parser);
    return NULL;
  }

  JsonNode *root = json_parser_get_root(parser);
  if (!JSON_NODE_HOLDS_OBJECT(root)) {
    g_printerr("[UPDATER] Invalid JSON structure\n");
    g_object_unref(parser);
    return NULL;
  }

  JsonObject *obj = json_node_get_object(root);

  CPifyReleaseInfo *info = g_new0(CPifyReleaseInfo, 1);
  info->tag_name = g_strdup(json_object_get_string_member(obj, "tag_name"));
  info->title = g_strdup(json_object_get_string_member(obj, "name"));
  info->description = g_strdup(json_object_get_string_member(obj, "body"));
  info->published_at = g_strdup(json_object_get_string_member(obj, "published_at"));

  // Find appropriate asset for this OS
  if (json_object_has_member(obj, "assets")) {
    JsonArray *assets = json_object_get_array_member(obj, "assets");
    info->download_url = find_asset_url_for_os(assets);
  }

  g_object_unref(parser);
  return info;
}

static void on_update_check_complete(GObject *source, GAsyncResult *result, gpointer user_data) {
  UpdateCheckData *data = (UpdateCheckData *)user_data;
  SoupSession *session = SOUP_SESSION(source);
  GError *error = NULL;
  GBytes *bytes = NULL;

  bytes = soup_session_send_and_read_finish(session, result, &error);

  if (error) {
    g_printerr("[UPDATER] HTTP request failed: %s\n", error->message);
    if (data->callback) {
      data->callback(NULL, error, data->user_data);
    }
    g_error_free(error);
    g_free(data);
    return;
  }

  gsize size;
  const gchar *body = g_bytes_get_data(bytes, &size);

  CPifyReleaseInfo *release = parse_release_json(body, size);
  g_bytes_unref(bytes);

  if (!release) {
    GError *parse_error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_INVALID_DATA, 
                                               "Failed to parse release data");
    if (data->callback) {
      data->callback(NULL, parse_error, data->user_data);
    }
    g_error_free(parse_error);
    g_free(data);
    return;
  }

  // Compare versions
  const gchar *current = cpify_updater_get_current_version();
  gint cmp = cpify_version_compare(current, release->tag_name);

  g_print("[UPDATER] Current version: %s, Latest version: %s, Comparison: %d\n", 
          current, release->tag_name, cmp);

  if (cmp >= 0) {
    // Current version is same or newer
    g_print("[UPDATER] No update available (current is up to date)\n");
    cpify_release_info_free(release);
    if (data->callback) {
      data->callback(NULL, NULL, data->user_data);
    }
  } else {
    // Update available
    g_print("[UPDATER] Update available: %s\n", release->title);
    if (data->callback) {
      data->callback(release, NULL, data->user_data);
    }
  }

  g_free(data);
}

void cpify_updater_check_async(CPifyUpdateCallback callback, gpointer user_data) {
  if (!g_soup_session) {
    cpify_updater_init();
  }

  UpdateCheckData *data = g_new0(UpdateCheckData, 1);
  data->callback = callback;
  data->user_data = user_data;

  SoupMessage *msg = soup_message_new("GET", GITHUB_RELEASES_URL);
  
  // Add Accept header for GitHub API
  soup_message_headers_append(soup_message_get_request_headers(msg), 
                               "Accept", "application/vnd.github.v3+json");

  g_print("[UPDATER] Checking for updates at %s\n", GITHUB_RELEASES_URL);

  soup_session_send_and_read_async(g_soup_session, msg, G_PRIORITY_DEFAULT, 
                                    NULL, on_update_check_complete, data);
  g_object_unref(msg);
}

// Update dialog callbacks
static void on_update_dialog_close_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  AdwDialog *dialog = ADW_DIALOG(user_data);
  adw_dialog_close(dialog);
}

static void on_install_complete(GObject *source, GAsyncResult *result, gpointer user_data);

static void on_update_dialog_install_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  InstallData *install_data = (InstallData *)user_data;
  
  // Disable install button and show progress
  gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
  gtk_button_set_label(btn, "Downloading...");
  
  cpify_updater_install_async(install_data->release, 
                               install_data->parent_window,
                               G_CALLBACK(on_install_complete), 
                               install_data);
}

GtkWidget *cpify_updater_show_dialog(GtkWindow *parent_window, CPifyReleaseInfo *release) {
  if (!release) return NULL;

  // Create dialog window
  AdwDialog *dialog = adw_dialog_new();
  adw_dialog_set_title(dialog, "Update Available");
  adw_dialog_set_content_width(dialog, 480);
  adw_dialog_set_content_height(dialog, 400);

  // Main content box
  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_margin_start(content, 24);
  gtk_widget_set_margin_end(content, 24);
  gtk_widget_set_margin_top(content, 24);
  gtk_widget_set_margin_bottom(content, 24);

  // Header with icon
  GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_widget_set_halign(header_box, GTK_ALIGN_CENTER);
  
  GtkWidget *icon = gtk_image_new_from_icon_name("software-update-available-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
  gtk_widget_add_css_class(icon, "accent");
  gtk_box_append(GTK_BOX(header_box), icon);

  // Title
  GtkWidget *title_label = gtk_label_new(NULL);
  gchar *title_markup = g_strdup_printf("<span size='x-large' weight='bold'>%s</span>", 
                                         release->title ? release->title : "New Update Available");
  gtk_label_set_markup(GTK_LABEL(title_label), title_markup);
  gtk_label_set_wrap(GTK_LABEL(title_label), TRUE);
  g_free(title_markup);
  gtk_box_append(GTK_BOX(header_box), title_label);
  gtk_box_append(GTK_BOX(content), header_box);

  // Version info
  GtkWidget *version_label = gtk_label_new(NULL);
  gchar *version_text = g_strdup_printf("Version %s → %s", 
                                         cpify_updater_get_current_version(),
                                         release->tag_name);
  gtk_label_set_text(GTK_LABEL(version_label), version_text);
  gtk_widget_add_css_class(version_label, "dim-label");
  g_free(version_text);
  gtk_box_append(GTK_BOX(content), version_label);

  // Description in scrolled window
  GtkWidget *desc_scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(desc_scroll), 
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(desc_scroll, TRUE);
  gtk_widget_set_size_request(desc_scroll, -1, 150);

  GtkWidget *desc_label = gtk_label_new(release->description ? release->description : "No release notes available.");
  gtk_label_set_wrap(GTK_LABEL(desc_label), TRUE);
  gtk_label_set_xalign(GTK_LABEL(desc_label), 0.0f);
  gtk_label_set_yalign(GTK_LABEL(desc_label), 0.0f);
  gtk_label_set_selectable(GTK_LABEL(desc_label), TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(desc_scroll), desc_label);

  GtkWidget *desc_frame = gtk_frame_new(NULL);
  gtk_widget_add_css_class(desc_frame, "view");
  gtk_frame_set_child(GTK_FRAME(desc_frame), desc_scroll);
  gtk_box_append(GTK_BOX(content), desc_frame);

  // Download availability notice
  if (!release->download_url) {
    GtkWidget *notice = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(notice), 
      "<span color='orange'>⚠ No binary available for your system. Please build from source.</span>");
    gtk_label_set_wrap(GTK_LABEL(notice), TRUE);
    gtk_box_append(GTK_BOX(content), notice);
  }

  // Button box
  GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_halign(button_box, GTK_ALIGN_END);
  gtk_widget_set_margin_top(button_box, 8);

  GtkWidget *close_btn = gtk_button_new_with_label("Later");
  g_signal_connect(close_btn, "clicked", G_CALLBACK(on_update_dialog_close_clicked), dialog);

  GtkWidget *install_btn = gtk_button_new_with_label("Install Update");
  gtk_widget_add_css_class(install_btn, "suggested-action");

  // Store install data
  InstallData *install_data = g_new0(InstallData, 1);
  install_data->release = release;
  install_data->parent_window = parent_window;
  
  if (release->download_url) {
    g_signal_connect(install_btn, "clicked", G_CALLBACK(on_update_dialog_install_clicked), install_data);
  } else {
    gtk_widget_set_sensitive(install_btn, FALSE);
    gtk_widget_set_tooltip_text(install_btn, "No binary available for your OS");
  }

  gtk_box_append(GTK_BOX(button_box), close_btn);
  gtk_box_append(GTK_BOX(button_box), install_btn);
  gtk_box_append(GTK_BOX(content), button_box);

  adw_dialog_set_child(dialog, content);
  
  // Present the dialog
  adw_dialog_present(dialog, GTK_WIDGET(parent_window));

  return GTK_WIDGET(dialog);
}

static void on_download_progress(goffset current_bytes, goffset total_bytes, gpointer user_data) {
  InstallData *data = (InstallData *)user_data;
  
  if (data->progress_bar && total_bytes > 0) {
    gdouble fraction = (gdouble)current_bytes / (gdouble)total_bytes;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(data->progress_bar), fraction);
    
    gchar *status = g_strdup_printf("Downloading... %.1f%%", fraction * 100.0);
    gtk_label_set_text(GTK_LABEL(data->status_label), status);
    g_free(status);
  }
}

static void on_install_complete(GObject *source, GAsyncResult *result, gpointer user_data) {
  (void)source;
  (void)result;
  InstallData *data = (InstallData *)user_data;
  
  // Show completion message
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
    "Update Installed",
    "The update has been downloaded. Please restart CPify to apply the update."));
  
  adw_alert_dialog_add_response(dialog, "ok", "OK");
  adw_alert_dialog_set_default_response(dialog, "ok");
  
  adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(data->parent_window));
  
  g_free(data);
}

static void on_download_complete(GObject *source, GAsyncResult *result, gpointer user_data) {
  GFile *file = G_FILE(source);
  InstallData *data = (InstallData *)user_data;
  GError *error = NULL;

  if (!g_file_copy_finish(file, result, &error)) {
    g_printerr("[UPDATER] Download failed: %s\n", error->message);
    
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
      "Download Failed",
      error->message));
    
    adw_alert_dialog_add_response(dialog, "ok", "OK");
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(data->parent_window));
    
    g_error_free(error);
    g_free(data);
    return;
  }

  g_print("[UPDATER] Download complete\n");

  // Get the executable path and the downloaded file path
  gchar *exe_path = g_file_get_path(file);
  
  // Make the downloaded file executable
  GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_UNIX_MODE, 
                                       G_FILE_QUERY_INFO_NONE, NULL, NULL);
  if (info) {
    guint32 mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE);
    mode |= 0755;  // Add executable permissions
    g_file_set_attribute_uint32(file, G_FILE_ATTRIBUTE_UNIX_MODE, mode, 
                                 G_FILE_QUERY_INFO_NONE, NULL, NULL);
    g_object_unref(info);
  }

  g_free(exe_path);
  on_install_complete(NULL, NULL, data);
}

void cpify_updater_install_async(CPifyReleaseInfo *release, 
                                  GtkWindow *parent_window,
                                  GCallback on_complete, 
                                  gpointer user_data) {
  if (!release || !release->download_url) {
    g_printerr("[UPDATER] No download URL available\n");
    return;
  }

  InstallData *data = g_new0(InstallData, 1);
  data->release = release;
  data->parent_window = parent_window;
  data->on_complete = on_complete;
  data->user_data = user_data;

  // Determine download destination
  // We'll download to a temporary location, then the user can replace manually
  // or we can implement auto-replacement based on permissions
  
  const gchar *cache_dir = g_get_user_cache_dir();
  gchar *download_dir = g_build_filename(cache_dir, "cpify", "updates", NULL);
  g_mkdir_with_parents(download_dir, 0755);

  // Extract filename from URL
  gchar *basename = g_path_get_basename(release->download_url);
  gchar *dest_path = g_build_filename(download_dir, basename, NULL);
  g_free(basename);
  g_free(download_dir);

  g_print("[UPDATER] Downloading %s to %s\n", release->download_url, dest_path);

  GFile *source = g_file_new_for_uri(release->download_url);
  GFile *dest = g_file_new_for_path(dest_path);
  g_free(dest_path);

  // Download with progress
  g_file_copy_async(source, dest, 
                     G_FILE_COPY_OVERWRITE,
                     G_PRIORITY_DEFAULT,
                     NULL,  // cancellable
                     on_download_progress, data,
                     on_download_complete, data);

  g_object_unref(source);
  g_object_unref(dest);
}
