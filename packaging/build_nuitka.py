from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

try:
    import nuitka.__main__  # type: ignore
except ImportError as exc:
    raise SystemExit(
        "Nuitka is required to build the Windows executable. "
        "Install it with: pip install nuitka ordered-set zstandard"
    ) from exc

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DIST_DIR = PROJECT_ROOT / "dist" / "windows-nuitka"
APP_NAME = "Pypify"
ICON_DIR = PROJECT_ROOT / "packaging" / "windows"
ICON_PATH = ICON_DIR / "pypify.ico"
ICON_SIZES = [256, 128, 64, 48, 32, 16]
COLOR_PRIMARY_TOP = (0x40, 0x67, 0xFF)
COLOR_PRIMARY_BOTTOM = (0x7F, 0x9B, 0xFF)


def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def ensure_icon() -> Path | None:
    if ICON_PATH.exists():
        return ICON_PATH

    try:
        from PIL import Image, ImageDraw  # type: ignore
    except ImportError:
        print(
            "Pillow is required to generate the application icon. "
            "Install it with: pip install pillow",
            file=sys.stderr,
        )
        return None

    ICON_DIR.mkdir(parents=True, exist_ok=True)
    size = 512
    base = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    gradient = Image.new("RGBA", (size, size))
    gradient_draw = ImageDraw.Draw(gradient)
    for y in range(size):
        t = y / (size - 1)
        color = (
            int(_lerp(COLOR_PRIMARY_TOP[0], COLOR_PRIMARY_BOTTOM[0], t)),
            int(_lerp(COLOR_PRIMARY_TOP[1], COLOR_PRIMARY_BOTTOM[1], t)),
            int(_lerp(COLOR_PRIMARY_TOP[2], COLOR_PRIMARY_BOTTOM[2], t)),
            255,
        )
        gradient_draw.line([(0, y), (size, y)], fill=color)

    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [(16, 16), (size - 16, size - 16)], radius=128, fill=255
    )
    base.paste(gradient, (0, 0), mask)

    draw = ImageDraw.Draw(base)

    draw.ellipse(
        [(96, 92), (368, 364)],
        fill=(255, 255, 255, 60),
    )
    draw.ellipse(
        [(260, 112), (436, 288)],
        fill=(255, 255, 255, 48),
    )

    wave_points: list[tuple[float, float]] = []
    for i in range(33):
        t = i / 32
        x = _lerp(64, 448, t)
        y = 320 + 48 * t - 72 * (t**2)
        wave_points.append((x, y))
    wave_points.append((448, 392))
    wave_points.append((64, 392))
    draw.polygon(
        wave_points,
        fill=(255, 255, 255, 120),
    )

    triangle = [(210, 188), (356, 256), (210, 324)]
    draw.polygon(triangle, fill=(11, 19, 46, 230))
    inset_triangle = [(244, 212), (328, 256), (244, 300)]
    draw.polygon(inset_triangle, fill=(96, 123, 255, 230))

    icon_sizes = [(s, s) for s in ICON_SIZES]
    base.convert("RGBA").save(ICON_PATH, format="ICO", sizes=icon_sizes)
    return ICON_PATH


def build_windows_executable() -> None:
    if DIST_DIR.exists():
        shutil.rmtree(DIST_DIR)
    DIST_DIR.mkdir(parents=True, exist_ok=True)

    icon_path = ensure_icon()

    command = [
        sys.executable,
        "-m",
        "nuitka",
        str(PROJECT_ROOT / "main.py"),
        "--standalone",
        "--onefile",
        "--mingw64",
        "--windows-target=mingw64",
        "--output-dir",
        str(DIST_DIR),
        "--output-filename",
        f"{APP_NAME}.exe",
        "--include-data-file",
        f"{PROJECT_ROOT / 'song_data.json'}=song_data.json",
        "--include-data-dir",
        f"{PROJECT_ROOT / 'songs'}=songs",
        "--enable-plugin=pygame",
        "--include-package=moviepy",
        "--include-package=imageio_ffmpeg",
        "--remove-output",
        "--assume-yes-for-downloads",
    ]

    if icon_path and icon_path.exists():
        command.append(f"--windows-icon-from-file={icon_path}")

    print("Running Nuitka to produce Windows executable...")
    print(" ".join(command))
    result = subprocess.run(command, cwd=PROJECT_ROOT, check=False)
    if result.returncode != 0:
        raise SystemExit(result.returncode)

    executable = DIST_DIR / f"{APP_NAME}.exe"
    if not executable.exists():
        raise SystemExit(
            "Executable was not produced. Check Nuitka output for details."
        )

    print(f"Executable created at: {executable}")


if __name__ == "__main__":
    build_windows_executable()


