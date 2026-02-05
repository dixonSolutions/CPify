# Dev Setup (Fedora)

Your system needs the *development* headers for GTK 4, libadwaita, GStreamer, libsoup3 and json-glib.

## Install build tools + deps

```bash
sudo dnf install -y \
  meson ninja-build gcc pkgconf-pkg-config \
  gtk4-devel libadwaita-devel \
  gstreamer1-devel gstreamer1-plugins-base-devel \
  gstreamer1-plugins-good gstreamer1-plugins-good-gtk4 \
  gstreamer1-plugins-bad-free gstreamer1-libav \
  libsoup3-devel json-glib-devel
```

### Ubuntu/Debian

```bash
sudo apt install -y \
  meson ninja-build gcc pkg-config \
  libgtk-4-dev libadwaita-1-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-good gstreamer1.0-gtk4 \
  gstreamer1.0-plugins-bad gstreamer1.0-libav \
  libsoup-3.0-dev libjson-glib-dev
```

### Arch Linux

```bash
sudo pacman -S \
  meson ninja gcc pkgconf \
  gtk4 libadwaita \
  gstreamer gst-plugins-base gst-plugins-good gst-plugin-gtk \
  gst-plugins-bad gst-libav \
  libsoup3 json-glib
```

## Notes

- This build uses **GTK 4** with **libadwaita** for modern GNOME design guidelines
- For video display, CPify uses **GtkMediaFile** which provides native GTK4 video embedding
- GtkMediaFile uses the best available GStreamer backend internally
- **Auto-Update**: CPify includes automatic update checking via the GitHub API
  - Uses **libsoup3** for HTTP requests
  - Uses **json-glib** for parsing GitHub API responses
  - Checks for updates on startup and prompts users when a new version is available

## Verify install

```bash
pkg-config --modversion gtk4 libadwaita-1 gstreamer-1.0 gstreamer-video-1.0 libsoup-3.0 json-glib-1.0
```

## Build and Run

```bash
cd CPify
meson setup build
meson compile -C build
./build/src/cpify
```

Or use the convenience script:

```bash
./compileandrun.sh
```