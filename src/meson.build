srcs = files(
  'main.c',
  'cpify_app.c',
  'media_scanner.c',
  'player.c',
  'settings.c',
  'sound_effects.c',
  'splash_screen.c',
  'updater.c',
)

executable(
  'cpify',
  srcs,
  dependencies: [
    config_dep,
    gtk,
    adw,
    gst,
    gst_video,
    gst_audio,
    gio,
    glib,
    gdk_pixbuf,
    libm,
    libsoup,
    json_glib,
  ],
  install: true,
)
