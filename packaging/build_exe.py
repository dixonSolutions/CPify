from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path

try:
    import PyInstaller.__main__  # type: ignore
except ImportError as exc:
    raise SystemExit(
        "PyInstaller is required to build the Windows executable. Install it with: pip install pyinstaller"
    ) from exc


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DIST_DIR = PROJECT_ROOT / "dist"
BUILD_DIR = PROJECT_ROOT / "build"
APP_NAME = "Pypify"
ICON_DIR = PROJECT_ROOT / "packaging" / "windows"
ICON_PATH = ICON_DIR / "pypify.ico"
ICON_SIZES = [256, 128, 64, 48, 32, 16]
COLOR_PRIMARY_TOP = (0x40, 0x67, 0xFF)
COLOR_PRIMARY_BOTTOM = (0x7F, 0x9B, 0xFF)


def _format_data(source: Path, destination: Path) -> str:
    separator = ";" if os.name == "nt" else ":"
    return f"{source}{separator}{destination}"


def clean_previous_artifacts() -> None:
    for path in (DIST_DIR, BUILD_DIR):
        if path.exists():
            shutil.rmtree(path)


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

    # Create gradient background with rounded corners.
    gradient = Image.new("RGBA", (size, size))
    for y in range(size):
        t = y / (size - 1)
        color = (
            int(_lerp(COLOR_PRIMARY_TOP[0], COLOR_PRIMARY_BOTTOM[0], t)),
            int(_lerp(COLOR_PRIMARY_TOP[1], COLOR_PRIMARY_BOTTOM[1], t)),
            int(_lerp(COLOR_PRIMARY_TOP[2], COLOR_PRIMARY_BOTTOM[2], t)),
            255,
        )
        ImageDraw.Draw(gradient).line([(0, y), (size, y)], fill=color)

    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [(16, 16), (size - 16, size - 16)], radius=128, fill=255
    )
    base.paste(gradient, (0, 0), mask)

    draw = ImageDraw.Draw(base)

    # Add soft highlight circles.
    draw.ellipse(
        [(96, 92), (368, 364)],
        fill=(255, 255, 255, 60),
    )
    draw.ellipse(
        [(260, 112), (436, 288)],
        fill=(255, 255, 255, 48),
    )

    # Draw stylised wave across the bottom.
    wave_points: list[tuple[float, float]] = []
    for i in range(33):
        t = i / 32
        x = _lerp(64, 448, t)
        y = (
            320
            + 48 * (t)
            - 72 * (t**2)
        )
        wave_points.append((x, y))
    wave_points.append((448, 392))
    wave_points.append((64, 392))
    draw.polygon(
        wave_points,
        fill=(255, 255, 255, 120),
    )

    # Draw play triangle.
    triangle = [(210, 188), (356, 256), (210, 324)]
    draw.polygon(triangle, fill=(11, 19, 46, 230))
    inset_triangle = [(244, 212), (328, 256), (244, 300)]
    draw.polygon(inset_triangle, fill=(96, 123, 255, 230))

    icon_sizes = [(s, s) for s in ICON_SIZES]
    base.convert("RGBA").save(ICON_PATH, format="ICO", sizes=icon_sizes)
    return ICON_PATH


def build_windows_executable() -> None:
    clean_previous_artifacts()
    icon_path = ensure_icon()

    add_data_args = [
        "--add-data",
        _format_data(PROJECT_ROOT / "song_data.json", Path("song_data.json")),
        "--add-data",
        _format_data(PROJECT_ROOT / "songs", Path("songs")),
    ]

    pyinstaller_args = [
        str(PROJECT_ROOT / "main.py"),
        "--noconfirm",
        "--clean",
        "--windowed",
        "--name",
        APP_NAME,
        "--collect-all",
        "pygame",
        "--collect-all",
        "moviepy",
        *add_data_args,
    ]
    if icon_path and icon_path.exists():
        pyinstaller_args.extend(["--icon", str(icon_path)])

    PyInstaller.__main__.run(pyinstaller_args)

    executable = DIST_DIR / f"{APP_NAME}.exe"
    if not executable.exists():
        raise SystemExit("Executable was not produced. Check PyInstaller output for details.")

    print(f"Executable created at: {executable}")


if __name__ == "__main__":
    build_windows_executable()

