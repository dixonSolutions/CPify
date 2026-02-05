srcs = files(
  'main.c',
  'cpify_app.c',
  'media_scanner.c',
  'player.c',
  'settings.c',
  'sound_effects.c',
  'splash_screen.c',
)

incs = include_directories('.')

# Include directory for config.h
config_inc = include_directories('..')

executable(
  'cpify',
  srcs,
  include_directories: [incs, config_inc],
  dependencies: [gtk, adw, gst, gst_video, gst_audio, gio, glib, gdk_pixbuf, libm],
  install: false,
)
