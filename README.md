# CPify (C/GTK Media Player)

This is a **C + GTK4** media player application.

## What this build does

- Prompts for a **folder selection on app launch**
- Recursively scans for common **audio + video** files
- Lists the files and lets you **play / pause / next / prev**
- Provides **shuffle** and **repeat** toggles
- Uses **GStreamer** for playback
- **Embedded video** display using GTK4's GtkMediaFile

## Dev setup (Fedora)

See `docs/DEV_SETUP.md`.

## Build + run (Meson)

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