# Building CPify as a Flatpak

This guide documents how to build and distribute CPify as a Flatpak application.

## Prerequisites

Install Flatpak and flatpak-builder:

### Fedora

```bash
sudo dnf install -y flatpak flatpak-builder
```

### Ubuntu/Debian

```bash
sudo apt install -y flatpak flatpak-builder
```

### Arch Linux

```bash
sudo pacman -S flatpak flatpak-builder
```

## Set Up Flathub

Add the Flathub repository (required for GNOME runtime):

```bash
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
```

## Install GNOME SDK

Install the GNOME Platform and SDK (required to build):

```bash
flatpak install flathub org.gnome.Platform//47 org.gnome.Sdk//47
```

## Build the Flatpak

### Local Build (Development)

Build and install locally for testing:

```bash
cd CPify

# Build the Flatpak
flatpak-builder --force-clean build-flatpak com.github.dixonsolutions.CPify.yml

# Install locally (user-only)
flatpak-builder --user --install --force-clean build-flatpak com.github.dixonsolutions.CPify.yml
```

### Run the Installed Flatpak

```bash
flatpak run com.github.dixonsolutions.CPify
```

### Create a Distributable Bundle

Create a single-file bundle for distribution:

```bash
# First, build to a repo
flatpak-builder --repo=repo --force-clean build-flatpak com.github.dixonsolutions.CPify.yml

# Create a bundle file
flatpak build-bundle repo cpify.flatpak com.github.dixonsolutions.CPify
```

The `cpify.flatpak` file can be shared and installed on any system with:

```bash
flatpak install cpify.flatpak
```

## Project Structure

The Flatpak build requires these files:

```
CPify/
├── com.github.dixonsolutions.CPify.yml   # Flatpak manifest
├── data/
│   ├── com.github.dixonsolutions.CPify.desktop      # Desktop entry
│   ├── com.github.dixonsolutions.CPify.metainfo.xml # AppStream metadata
│   ├── icons/
│   │   └── hicolor/
│   │       └── scalable/
│   │           └── apps/
│   │               └── com.github.dixonsolutions.CPify.svg  # App icon
│   └── meson.build
├── meson.build
└── src/
    └── ...
```

## File Naming Conventions

### Application ID

The application ID follows reverse DNS notation:

```
com.github.dixonsolutions.CPify
```

This ID is used consistently across:
- Flatpak manifest filename
- Desktop file
- Metainfo file
- Icon filename
- D-Bus name

### Desktop File

**Filename:** `com.github.dixonsolutions.CPify.desktop`

Required fields:
- `Name`: Application display name
- `Exec`: Command to run (just `cpify`)
- `Icon`: Must match app ID (without extension)
- `Categories`: `AudioVideo;Audio;Video;Player;`

### MetaInfo File

**Filename:** `com.github.dixonsolutions.CPify.metainfo.xml`

This file provides:
- Application description for app stores
- Screenshots (optional)
- Release history
- Content rating

### Icon

**Filename:** `com.github.dixonsolutions.CPify.svg`

**Location:** `data/icons/hicolor/scalable/apps/`

Requirements:
- SVG format for scalable icons
- Must be named exactly as the app ID
- Will appear in dock, app launcher, and app stores

## Manifest Reference

The Flatpak manifest (`com.github.dixonsolutions.CPify.yml`) defines:

| Field | Value | Description |
|-------|-------|-------------|
| `app-id` | `com.github.dixonsolutions.CPify` | Unique application identifier |
| `runtime` | `org.gnome.Platform` | GNOME runtime for GTK4/libadwaita |
| `runtime-version` | `47` | GNOME platform version |
| `sdk` | `org.gnome.Sdk` | Build SDK |
| `command` | `cpify` | Executable to run |

### Permissions (finish-args)

| Permission | Purpose |
|------------|---------|
| `--share=ipc` | Shared memory for X11 |
| `--socket=fallback-x11` | X11 display (fallback) |
| `--socket=wayland` | Wayland display |
| `--socket=pulseaudio` | Audio playback |
| `--filesystem=home:ro` | Read access to home directory |
| `--filesystem=xdg-music:ro` | Read access to Music folder |
| `--filesystem=xdg-videos:ro` | Read access to Videos folder |
| `--share=network` | Network for update checking |
| `--device=dri` | GPU access for video acceleration |

## Updating the Flatpak

When releasing a new version:

1. Update version in `meson.build`
2. Add release entry to `metainfo.xml`
3. Rebuild the Flatpak
4. Create and distribute bundle or push to repository

### Adding a Release to MetaInfo

Edit `data/com.github.dixonsolutions.CPify.metainfo.xml`:

```xml
<releases>
  <release version="0.1.0" date="2026-03-01">
    <description>
      <p>New features and improvements</p>
      <ul>
        <li>Feature 1</li>
        <li>Feature 2</li>
      </ul>
    </description>
  </release>
  <!-- Previous releases below -->
</releases>
```

## Troubleshooting

### Build Fails with Missing Dependencies

Ensure the GNOME SDK is installed:

```bash
flatpak list --runtime | grep org.gnome.Sdk
```

### Icon Not Showing

1. Verify icon filename matches app ID exactly
2. Check icon is installed to correct path
3. Run `gtk-update-icon-cache` if needed

### App Won't Start

Check logs:

```bash
flatpak run --command=sh com.github.dixonsolutions.CPify
# Then run cpify manually to see errors
./cpify
```

## Publishing to Flathub

To publish on Flathub:

1. Fork the [Flathub repository](https://github.com/flathub/flathub)
2. Create a new branch with your app ID
3. Add your manifest file
4. Submit a pull request
5. Follow the [Flathub submission guidelines](https://github.com/flathub/flathub/wiki/App-Submission)

## Quick Reference

| Task | Command |
|------|---------|
| Build | `flatpak-builder --force-clean build-flatpak com.github.dixonsolutions.CPify.yml` |
| Install | `flatpak-builder --user --install --force-clean build-flatpak com.github.dixonsolutions.CPify.yml` |
| Run | `flatpak run com.github.dixonsolutions.CPify` |
| Uninstall | `flatpak uninstall com.github.dixonsolutions.CPify` |
| Bundle | `flatpak build-bundle repo cpify.flatpak com.github.dixonsolutions.CPify` |
