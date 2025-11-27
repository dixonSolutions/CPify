# Packaging Pypify

This guide covers building distributable artifacts for Windows (`.exe`) and Linux (Flatpak), bundling the default assets, and preparing files for GitHub Releases.

## Prerequisites
- Python 3.10+
- `pip install pygame moviepy pillow`
- Packaging tools:
  - Windows executable: `pip install pyinstaller`
  - Flatpak bundle: `flatpak install org.freedesktop.Platform//23.08 org.freedesktop.Sdk//23.08` and `flatpak install org.freedesktop.Sdk.Extension.python3//23.08`
- Git LFS (optional) if you intend to ship large media assets through releases.

## Runtime data layout

Pypify now copies `song_data.json` and the entire `songs/` directory into a writable data directory on first launch:

- Source/dev runs: assets remain in the project directory.
- Frozen binaries (PyInstaller) and Flatpak builds: assets are copied to the appropriate user data root (e.g. `~/.local/share/Pypify` on Linux, `%LOCALAPPDATA%\Pypify` on Windows).

Users can edit or replace thumbnails/audio/video directly in that writable location without modifying the bundled default files.

## Windows `.exe` build (no console window)

### Option A — cross-compile from Fedora with Nuitka (current approach)

1. Layer the MinGW-w64 toolchain that Nuitka relies on and reboot once so the deployment goes live:

   ```bash
   sudo rpm-ostree install mingw64-gcc mingw64-gcc-c++ mingw64-binutils
   sudo systemctl reboot
   ```

2. Install the Python dependencies in your (host) environment:

   ```bash
   pip install nuitka ordered-set zstandard pillow pygame moviepy imageio-ffmpeg
   ```

3. From the project root, run the helper. It generates the `.ico` if needed and invokes Nuitka with the proper data/include flags:

   ```bash
   cd /var/home/ratrad/Projects/Other/Pypify
   python packaging/build_nuitka.py
   ```

   Nuitka emits status logs during compilation. Expect the first build to take a few minutes while it caches C++ objects.

4. The finished executable is written to `dist/windows-nuitka/Pypify.exe`. Zip it for distribution:

   ```bash
   cd dist/windows-nuitka
   zip -r ../Pypify-win64-nuitka.zip Pypify.exe
   ```

5. (Optional) Smoke-test under Wine:

   ```bash
   wine ../windows-nuitka/Pypify.exe
   ```

### Option B — PyInstaller via Windows container

1. Install Docker or Podman on the host.
2. Ensure `packaging/windows/pypify.ico` exists (generate it with Pillow if necessary).
3. Run:

   ```bash
   docker run --rm -v "$PWD":/src -w /src cdrx/pyinstaller-windows:python3 \
     python packaging/build_exe.py
   ```

4. The executable lands at `dist/Pypify.exe`; create `Pypify-win64.zip` as usual.

### Option C — PyInstaller on a native Windows machine

1. Install Python, PyInstaller, Pillow, pygame, and moviepy.
2. Execute:

   ```bash
   python packaging/build_exe.py
   ```

3. Package the resulting `dist/` directory.

## Flatpak bundle (no terminal window)

1. From the repository root, build the manifest:

   ```bash
   cd packaging/flatpak
   flatpak-builder --force-clean build-dir com.github.ratrad.Pypify.yml
   ```

2. Export a Flatpak bundle ready for distribution:

   ```bash
   flatpak-builder --repo=pypify-repo --force-clean build-dir com.github.ratrad.Pypify.yml
   flatpak build-bundle pypify-repo ../Pypify.flatpak com.github.ratrad.Pypify
   ```

   Notes:
   - `packaging/flatpak/pypify.sh` launches the app without a console.
   - Default assets are installed under `/app/pypify/` and copied to the user data directory on first run.
   - Finish arguments grant access to audio, graphics, and the user’s home directory so new media can be imported or edited.

3. (Optional) Test locally:

   ```bash
   flatpak install --user Pypify.flatpak
   flatpak run com.github.ratrad.Pypify
   ```

## GitHub Releases workflow

1. Build the Windows and Flatpak artifacts as described above.
2. Confirm `song_data.json` and bundled songs behave correctly on a clean machine (edits should land in the user data directory).
3. Create release notes that mention:
   - Main changes.
   - Data directory location for editing assets.
   - Known limitations (e.g. bundled sample media size).
4. Upload:
   - `Pypify-win64.zip` (or equivalent).
   - `Pypify.flatpak`.
   - Optional: checksum files generated with `shasum -a 256 <filename>`.

## Troubleshooting

- **Missing DLLs on Windows**: ensure PyInstaller is executed on the target architecture and the host has Visual C++ Redistributable installed.
- **Nuitka build errors**: confirm the MinGW-w64 toolchain packages are layered and that `nuitka`, `ordered-set`, and `zstandard` are installed in the invoking Python environment.
- **Flatpak audio issues**: verify PulseAudio/PipeWire sockets are available; adjust `finish-args` if a different audio stack is needed.
- **Default assets not appearing**: delete the writable data folder (`~/.local/share/Pypify` or `%LOCALAPPDATA%\Pypify`) to trigger a fresh copy from packaged defaults.

