# Dev Setup (Fedora)

Your system needs the *development* headers for GTK 4, libadwaita, and GStreamer.

## Install build tools + deps

```bash
sudo dnf install -y \
  meson ninja-build gcc pkgconf-pkg-config \
  gtk4-devel libadwaita-devel \
  gstreamer1-devel gstreamer1-plugins-base-devel \
  gstreamer1-plugins-good gstreamer1-plugins-good-gtk4 \
  gstreamer1-plugins-bad-free gstreamer1-libav
```

### Ubuntu/Debian

```bash
sudo apt install -y \
  meson ninja-build gcc pkg-config \
  libgtk-4-dev libadwaita-1-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-good gstreamer1.0-gtk4 \
  gstreamer1.0-plugins-bad gstreamer1.0-libav
```

### Arch Linux

```bash
sudo pacman -S \
  meson ninja gcc pkgconf \
  gtk4 libadwaita \
  gstreamer gst-plugins-base gst-plugins-good gst-plugin-gtk \
  gst-plugins-bad gst-libav
```

## Notes

- This build uses **GTK 4** with **libadwaita** for modern GNOME design guidelines
- For video display, `gtk4paintablesink` from `gstreamer1-plugins-good-gtk4` is preferred
- If GTK4 sink isn't available, it falls back to legacy video sinks

## Verify install

```bash
pkg-config --modversion gtk4 libadwaita-1 gstreamer-1.0 gstreamer-video-1.0
gst-inspect-1.0 gtk4paintablesink  # Should show the GTK4 video sink element
```

## Build and Run

```bash
cd cprojectmigrate
meson setup build
cd build
ninja
./src/pypify
```

Or use the convenience script:

```bash
./compileandrun.sh
```