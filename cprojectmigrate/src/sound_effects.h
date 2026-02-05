#pragma once

#include <glib.h>

/**
 * Initialize the sound effects system.
 * Call this once at application startup.
 */
void pypify_sound_effects_init(void);

/**
 * Cleanup the sound effects system.
 * Call this at application shutdown.
 */
void pypify_sound_effects_cleanup(void);

/**
 * Play the UI click sound effect.
 * This is non-blocking and fires-and-forgets the sound.
 */
void pypify_play_click_sound(void);
