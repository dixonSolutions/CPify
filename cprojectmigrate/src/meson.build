srcs = files(
  'main.c',
  'pypify_app.c',
  'media_scanner.c',
  'player.c',
  'sound_effects.c',
)

incs = include_directories('.')

# Include directory for config.h
config_inc = include_directories('..')

executable(
  'pypify',
  srcs,
  include_directories: [incs, config_inc],
  dependencies: [gtk, adw, gst, gst_video, gst_audio, gio, glib, libm],
  install: false,
)
