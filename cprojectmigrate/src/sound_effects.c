#include "sound_effects.h"
#include "config.h"

#include <gst/gst.h>
#include <gio/gio.h>

// Sound effect file path
static gchar *click_sound_path = NULL;

// We use a simple playbin for sound effects
// For rapid successive clicks, we create lightweight pipelines on demand
static GstElement *sound_pipeline = NULL;
static GstBus *sound_bus = NULL;
static guint bus_watch_id = 0;

static gboolean on_sound_bus_message(GstBus *bus, GstMessage *msg, gpointer user_data) {
  (void)bus;
  (void)user_data;
  
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
      // Sound finished, reset pipeline for next play
      if (sound_pipeline) {
        gst_element_set_state(sound_pipeline, GST_STATE_NULL);
      }
      break;
    case GST_MESSAGE_ERROR: {
      GError *err = NULL;
      gchar *dbg = NULL;
      gst_message_parse_error(msg, &err, &dbg);
      g_printerr("Sound effect error: %s\n", err ? err->message : "unknown");
      if (err) g_error_free(err);
      g_free(dbg);
      if (sound_pipeline) {
        gst_element_set_state(sound_pipeline, GST_STATE_NULL);
      }
      break;
    }
    default:
      break;
  }
  return G_SOURCE_CONTINUE;
}

void pypify_sound_effects_init(void) {
  // Build the path to the click sound effect
  // Try multiple locations for flexibility
  const gchar *search_paths[] = {
    PYPIFY_ASSETS_DIR "/ClickSoundEffectForPypify.wav",
    "assets/ClickSoundEffectForPypify.wav",
    "../assets/ClickSoundEffectForPypify.wav",
    "../../assets/ClickSoundEffectForPypify.wav",
    NULL
  };
  
  for (int i = 0; search_paths[i] != NULL; i++) {
    if (g_file_test(search_paths[i], G_FILE_TEST_EXISTS)) {
      click_sound_path = g_strdup(search_paths[i]);
      g_print("[SFX] Found click sound at: %s\n", click_sound_path);
      break;
    }
  }
  
  if (!click_sound_path) {
    g_printerr("[SFX] Warning: Click sound effect file not found\n");
    return;
  }
  
  // Create the playbin for sound effects
  sound_pipeline = gst_element_factory_make("playbin", "sfx-playbin");
  if (!sound_pipeline) {
    g_printerr("[SFX] Warning: Unable to create sound effect pipeline\n");
    return;
  }
  
  // Set up bus watch for cleanup
  sound_bus = gst_element_get_bus(sound_pipeline);
  bus_watch_id = gst_bus_add_watch(sound_bus, on_sound_bus_message, NULL);
  
  // Pre-configure with low latency settings
  // Set volume to a reasonable level for UI feedback (not too loud)
  g_object_set(sound_pipeline, "volume", 0.5, NULL);
  
  g_print("[SFX] Sound effects system initialized\n");
}

void pypify_sound_effects_cleanup(void) {
  if (bus_watch_id) {
    g_source_remove(bus_watch_id);
    bus_watch_id = 0;
  }
  
  if (sound_bus) {
    gst_object_unref(sound_bus);
    sound_bus = NULL;
  }
  
  if (sound_pipeline) {
    gst_element_set_state(sound_pipeline, GST_STATE_NULL);
    gst_object_unref(sound_pipeline);
    sound_pipeline = NULL;
  }
  
  g_free(click_sound_path);
  click_sound_path = NULL;
  
  g_print("[SFX] Sound effects system cleaned up\n");
}

void pypify_play_click_sound(void) {
  if (!click_sound_path || !sound_pipeline) {
    return;
  }
  
  // Reset pipeline to NULL first for rapid successive plays
  gst_element_set_state(sound_pipeline, GST_STATE_NULL);
  
  // Convert path to URI
  gchar *uri = gst_filename_to_uri(click_sound_path, NULL);
  if (!uri) {
    g_printerr("[SFX] Failed to create URI for sound file\n");
    return;
  }
  
  // Set the URI and play
  g_object_set(sound_pipeline, "uri", uri, NULL);
  gst_element_set_state(sound_pipeline, GST_STATE_PLAYING);
  
  g_free(uri);
}
