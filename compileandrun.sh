#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cd "${ROOT_DIR}"

if ! command -v meson >/dev/null 2>&1; then
  echo "meson not found. Install it (e.g. 'sudo dnf install -y meson ninja-build')." >&2
  exit 1
fi

if ! command -v pkg-config >/dev/null 2>&1; then
  echo "pkg-config not found. Install it (e.g. 'sudo dnf install -y pkgconf-pkg-config')." >&2
  exit 1
fi

if ! pkg-config --exists gtk4; then
  echo "GTK4 development files not found (gtk4.pc missing)." >&2
  echo "Install: sudo dnf install -y gtk4-devel" >&2
  exit 1
fi

if ! pkg-config --exists libadwaita-1; then
  echo "libadwaita development files not found (libadwaita-1.pc missing)." >&2
  echo "Install: sudo dnf install -y libadwaita-devel" >&2
  exit 1
fi

if ! pkg-config --exists gstreamer-1.0; then
  echo "GStreamer development files not found (gstreamer-1.0.pc missing)." >&2
  echo "Install: sudo dnf install -y gstreamer1-devel gstreamer1-plugins-base-devel" >&2
  exit 1
fi

if [[ ! -d "${BUILD_DIR}" ]]; then
  meson setup "${BUILD_DIR}"
elif [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
  # Previous setup may have failed (or the directory is incomplete). Wipe and reconfigure.
  meson setup "${BUILD_DIR}" --wipe
else
  meson setup "${BUILD_DIR}" --reconfigure
fi

meson compile -C "${BUILD_DIR}"

BIN_CANDIDATES=(
  "${BUILD_DIR}/cpify"
  "${BUILD_DIR}/src/cpify"
)

for bin in "${BIN_CANDIDATES[@]}"; do
  if [[ -x "${bin}" ]]; then
    exec "${bin}" "$@"
  fi
done

echo "Built successfully, but could not find an executable to run." >&2
echo "Tried:" >&2
printf '  - %s\n' "${BIN_CANDIDATES[@]}" >&2
exit 1

