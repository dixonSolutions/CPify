project(
  'pypify',
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

gtk = dependency('gtk+-3.0', version: '>=3.24')
gst = dependency('gstreamer-1.0', version: '>=1.18')
gst_video = dependency('gstreamer-video-1.0', version: '>=1.18')
gio = dependency('gio-2.0')
glib = dependency('glib-2.0')

subdir('src')

