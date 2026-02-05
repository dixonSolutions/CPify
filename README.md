# Pypify (C/GTK migration)

This directory is the start of the **C + GTK** rewrite of **Pypify**.

## What this build does (today)

- Prompts for a **folder selection on app launch**
- Recursively scans for common **audio + video** files (no JSON)
- Lists the files and lets you **play / pause / next / prev**
- Provides **shuffle** and **repeat** toggles
- Uses **GStreamer** for playback

## Dev setup (Fedora)

See `docs/DEV_SETUP.md`.

## Build + run (Meson)

```bash
cd cprojectmigrate
meson setup build
meson compile -C build
./build/pypify
```

