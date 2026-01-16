# Dev setup (Fedora)

Your system needs the *development* headers for GTK and GStreamer (the runtime alone is not enough).

## Install build tools + deps

```bash
sudo dnf install -y \
  meson ninja-build gcc pkgconf-pkg-config \
  gtk3-devel \
  gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-good \
  gstreamer1-plugins-bad-free gstreamer1-libav
```

Notes:
- If you prefer **GTK4**, we can switch later; this initial migration targets **GTK3** because it’s broadly available and keeps the code simple.
- For in-window video embedding, GStreamer’s **`gtksink`** is recommended. If it’s missing, Pypify will still play video via the default video sink (often a separate window).

## Verify install

```bash
pkg-config --modversion gtk+-3.0 gstreamer-1.0 gstreamer-video-1.0
gst-inspect-1.0 --version
```

