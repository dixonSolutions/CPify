srcs = files(
  'main.c',
  'pypify_app.c',
  'media_scanner.c',
  'player.c',
)

incs = include_directories('.')

executable(
  'pypify',
  srcs,
  include_directories: incs,
  dependencies: [gtk, gst, gst_video, gio, glib, libm],
  install: false,
)

