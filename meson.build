project(
  'cpify',
  'c',
  version: '0.0.2',
  meson_version: '>=0.59.0',
  default_options: [
    'warning_level=2',
    'c_std=c11',
  ],
)

# Application ID (reverse DNS notation)
app_id = 'com.github.dixonsolutions.CPify'

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

# HTTP requests for auto-update (using libsoup 3.x)
libsoup = dependency('libsoup-3.0', version: '>=3.0')

# JSON parsing for GitHub API responses
json_glib = dependency('json-glib-1.0', version: '>=1.6')

# Asset paths - use installed path for release, source path for development
assets_source_dir = meson.current_source_dir() / 'assets'
assets_install_dir = join_paths(get_option('datadir'), 'cpify')

conf_data = configuration_data()
# Use installed path - will be /app/share/cpify in Flatpak, /usr/share/cpify otherwise
conf_data.set_quoted('CPIFY_ASSETS_DIR', join_paths(get_option('prefix'), assets_install_dir))
conf_data.set_quoted('CPIFY_VERSION', meson.project_version())
conf_data.set_quoted('CPIFY_APP_ID', app_id)

config_h = configure_file(
  output: 'config.h',
  configuration: conf_data,
)

# Create a dependency that includes the build directory where config.h lives
config_dep = declare_dependency(
  sources: config_h,
  include_directories: include_directories('.')
)

subdir('src')
subdir('data')