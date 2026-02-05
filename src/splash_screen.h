#pragma once

#include <adwaita.h>

typedef struct _PypifySplash PypifySplash;

/**
 * Callback invoked when the user clicks "Add Folder"
 * or when the splash animation completes and the user proceeds.
 */
typedef void (*PypifySplashCallback)(gpointer user_data);

/**
 * Create a new splash screen widget.
 * The splash shows the Pypify logo animating from right to center,
 * with "PyPify" and "Offline Vibes" text fading in.
 * 
 * @param on_add_folder Callback when user clicks "Add Folder"
 * @param user_data Data passed to callbacks
 * @return A GtkWidget* containing the splash screen
 */
GtkWidget *pypify_splash_new(PypifySplashCallback on_add_folder, gpointer user_data);

/**
 * Start the splash animation (call after widget is mapped).
 */
void pypify_splash_start_animation(GtkWidget *splash);
