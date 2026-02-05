# Desktop file installation
desktop_file = 'com.github.dixonsolutions.CPify.desktop'
install_data(
  desktop_file,
  install_dir: get_option('datadir') / 'applications'
)

# Validate desktop file if desktop-file-validate is available
desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils, args: [files(desktop_file)])
endif

# AppStream metainfo installation
metainfo_file = 'com.github.dixonsolutions.CPify.metainfo.xml'
install_data(
  metainfo_file,
  install_dir: get_option('datadir') / 'metainfo'
)

# Validate metainfo if appstream-util is available
appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  test('Validate metainfo file', appstreamcli,
    args: ['validate', '--no-net', files(metainfo_file)])
endif

# Icon installation
install_data(
  'icons/hicolor/scalable/apps/com.github.dixonsolutions.CPify.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps'
)

# Install application assets (sounds, logos, etc.)
install_data(
  '../assets/ClickSoundEffect.wav',
  '../assets/CPify Dark Mode Logo.svg',
  '../assets/CPify Light Mode Logo.svg',
  '../assets/CPify Official Logo.svg',
  install_dir: get_option('datadir') / 'cpify'
)

# Update icon cache after installation
gnome = import('gnome')
gnome.post_install(
  gtk_update_icon_cache: true,
  update_desktop_database: true,
)
