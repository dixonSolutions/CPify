project(
  'cpify',
  'c',
  version: '0.1.0',
  meson_version: '>=0.59.0',
  default_options: [
    'warning_level=2',
    'c_std=c11',
  ],
)

cc = meson.get_compiler('c')
libm = cc.find_library('m', required: true)

# GTK 4 + libadwaita for modern GNOME UI
gtk = dependency('gtk4', version: '>=4.10')
adw = dependency('libadwaita-1', version: '>=1.4')

# GStreamer for media playback and sound effects
gst = dependency('gstreamer-1.0', version: '>=1.18')
gst_video = dependency('gstreamer-video-1.0', version: '>=1.18')
gst_audio = dependency('gstreamer-audio-1.0', version: '>=1.18')
gio = dependency('gio-2.0')
glib = dependency('glib-2.0')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')

# Asset installation path
assets_dir = meson.current_source_dir() / 'assets'

conf_data = configuration_data()
conf_data.set_quoted('CPIFY_ASSETS_DIR', assets_dir)

config_h = configure_file(
  output: 'config.h',
  configuration: conf_data,
)

subdir('src')
