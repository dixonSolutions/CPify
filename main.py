from __future__ import annotations

import json
import os
import random
import shutil
import sys
import webbrowser
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Literal

import pygame


def _is_frozen() -> bool:
    return bool(getattr(sys, "frozen", False)) and hasattr(sys, "_MEIPASS")


def _resolve_app_root() -> Path:
    if _is_frozen():
        return Path(sys._MEIPASS)  # type: ignore[attr-defined]
    return Path(__file__).resolve().parent


def _platform_data_root() -> Path:
    if sys.platform == "win32":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    elif sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support"
    else:
        base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    return base / "Pypify"


APP_ROOT = _resolve_app_root()
DEFAULT_DATA_ROOT = _platform_data_root() if _is_frozen() else APP_ROOT
DATA_ROOT = Path(os.environ.get("PYPIFY_DATA_DIR", str(DEFAULT_DATA_ROOT))).expanduser()
DATA_ROOT.mkdir(parents=True, exist_ok=True)
DATA_PATH = DATA_ROOT / "song_data.json"
CACHE_DIR = DATA_ROOT / ".cache"
SONGS_DIR = DATA_ROOT / "songs"
DEFAULT_DATA_PATH = APP_ROOT / "song_data.json"
DEFAULT_SONGS_DIR = APP_ROOT / "songs"

WINDOW_WIDTH = 1280
WINDOW_HEIGHT = 720
BACKGROUND_COLOR = (10, 12, 24)
CARD_COLOR = (34, 37, 54)
CARD_BORDER = (24, 26, 40)
CARD_HIGHLIGHT = (88, 140, 255)
PRIMARY_COLOR = (64, 123, 255)
PRIMARY_HOVER = (92, 148, 255)
PRIMARY_ACTIVE = (138, 180, 255)
ACCENT_GREEN = (76, 198, 140)
DELETE_COLOR = (210, 80, 92)
DELETE_HOVER = (230, 108, 120)
TEXT_COLOR = (230, 232, 245)
MUTED_TEXT = (160, 165, 195)
HEADER_HEIGHT = 160
CARD_WIDTH = 320
CARD_HEIGHT = 300
CARD_MARGIN_X = 32
CARD_MARGIN_Y = 20
CARD_GAP = 24
THUMB_PADDING = 12
VOLUME_SLIDER_WIDTH = 160
VOLUME_SLIDER_HEIGHT = 6
VOLUME_KNOB_RADIUS = 10
PROGRESS_HEIGHT = 8
PAGE_SIZE = 5
VALID_VIDEO_EXTENSIONS = {
    ".mp4",
    ".mkv",
    ".mov",
    ".webm",
    ".avi",
    ".mpg",
    ".mpeg",
    ".m4v",
    ".wmv",
}
MUSIC_END_EVENT = pygame.USEREVENT + 1

_VIDEO_FILE_CLIP: Optional[type] = None


def resolve_media_path(path_value: str) -> Path:
    """
    Resolve a media path string to an existing Path, falling back to bundled assets or user songs.
    """
    raw = Path(path_value).expanduser()
    search_order: List[Path] = []
    seen: set[str] = set()

    def add_candidate(candidate: Path) -> None:
        key = str(candidate)
        if key and key not in seen:
            seen.add(key)
            search_order.append(candidate)

    if str(path_value).strip():
        if raw.is_absolute():
            add_candidate(raw)
        else:
            add_candidate(SONGS_DIR / raw)
            if raw.parts and raw.parts[0].lower() == "songs":
                stripped = Path(*raw.parts[1:]) if len(raw.parts) > 1 else Path()
                if stripped.parts:
                    add_candidate(SONGS_DIR / stripped)
            add_candidate(APP_ROOT / raw)
            add_candidate(DEFAULT_SONGS_DIR / raw)
        filename = raw.name
        if filename:
            add_candidate(SONGS_DIR / filename)
            add_candidate(DEFAULT_SONGS_DIR / filename)
    else:
        add_candidate(SONGS_DIR)

    for candidate in search_order:
        try:
            resolved = candidate.expanduser()
        except Exception:
            resolved = candidate
        if resolved.exists():
            return resolved

    if raw.is_absolute():
        return raw
    return SONGS_DIR / raw.name if raw.name else SONGS_DIR


def _normalize_song_data_file(data_path: Path) -> None:
    """
    Ensure all song entries point to valid, reachable media paths.
    """
    if not data_path.exists():
        return
    try:
        with data_path.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
    except Exception:
        return

    songs = payload.get("songs")
    if not isinstance(songs, list):
        return

    changed = False
    for entry in songs:
        if not isinstance(entry, dict):
            continue
        original = entry.get("pathtovideo")
        if not isinstance(original, str):
            continue
        resolved = resolve_media_path(original)
        resolved_str = _serialize_media_path(resolved)
        if entry.get("pathtovideo") != resolved_str and resolved.exists():
            entry["pathtovideo"] = resolved_str
            changed = True

    if changed:
        try:
            with data_path.open("w", encoding="utf-8") as handle:
                json.dump(payload, handle, indent=4)
        except Exception:
            pass


def prepare_user_environment() -> None:
    """
    Materialize default assets into the writable data directory when missing.
    """
    SONGS_DIR.mkdir(parents=True, exist_ok=True)

    if DEFAULT_DATA_PATH.exists() and not DATA_PATH.exists():
        try:
            shutil.copy2(DEFAULT_DATA_PATH, DATA_PATH)
        except Exception:
            pass

    if DEFAULT_SONGS_DIR.exists():
        for item in DEFAULT_SONGS_DIR.iterdir():
            target = SONGS_DIR / item.name
            if target.exists():
                continue
            try:
                if item.is_dir():
                    shutil.copytree(item, target)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(item, target)
            except Exception:
                continue

    _normalize_song_data_file(DATA_PATH)


def _serialize_media_path(path: Path) -> str:
    """
    Convert an absolute media Path into a portable string suitable for song_data.json.
    """
    candidate = path
    try:
        candidate = path.expanduser()
    except Exception:
        pass
    try:
        relative = candidate.resolve(strict=False).relative_to(SONGS_DIR.resolve(strict=False))
        return str(Path("songs") / relative)
    except Exception:
        return str(candidate)


def resolve_video_file_clip() -> type:
    """
    Locate the VideoFileClip class across supported moviepy versions.
    """
    global _VIDEO_FILE_CLIP
    if _VIDEO_FILE_CLIP is not None:
        return _VIDEO_FILE_CLIP
    try:
        from moviepy.video.io.VideoFileClip import VideoFileClip as clip_cls
    except ModuleNotFoundError:
        try:
            from moviepy.editor import VideoFileClip as clip_cls  # type: ignore
        except ModuleNotFoundError as exc:
            raise ImportError("moviepy VideoFileClip module unavailable") from exc
    _VIDEO_FILE_CLIP = clip_cls
    return clip_cls


def ensure_dependencies() -> None:
    """
    Validate that optional dependencies required for media processing are available.
    """
    missing = []
    try:
        import moviepy  # noqa: F401
    except ImportError:
        missing.append("moviepy")
    else:
        try:
            resolve_video_file_clip()
        except ImportError:
            missing.append("moviepy (VideoFileClip)")

    if missing:
        pkg_list = ", ".join(missing)
        print(f"Missing required package(s): {pkg_list}", file=sys.stderr)
        print("Install them with: pip install pygame moviepy", file=sys.stderr)
        raise SystemExit(1)


def load_song_data(path: Path) -> List[dict]:
    if not path.exists():
        raise FileNotFoundError(f"Could not find song data file at {path}")
    with path.open("r", encoding="utf-8") as f:
        payload = json.load(f)
    songs = payload.get("songs", [])
    if not songs:
        raise ValueError("No songs defined in song_data.json")
    return songs


def save_song_data(path: Path, songs: List[Song]) -> None:
    payload = {
        "songs": [
            {
                "name": song.name,
                "artist": song.artist,
                "pathtovideo": _serialize_media_path(song.video_path),
            }
            for song in songs
        ]
    }
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=4)


def slugify(value: str) -> str:
    """
    Create a filesystem friendly slug for caching derived media files.
    """
    safe = []
    for ch in value.lower():
        if ch.isalnum():
            safe.append(ch)
        else:
            safe.append("-")
    slug = "".join(safe).strip("-")
    slug = "-".join(filter(None, slug.split("-")))
    return slug or "song"


@dataclass
class Song:
    name: str
    artist: str
    video_path: Path
    cache_key: str = field(init=False)
    thumbnail_path: Path = field(init=False)
    audio_path: Path = field(init=False)
    search_key: str = field(init=False, repr=False)
    _thumbnail_surface: Optional[pygame.Surface] = field(default=None, init=False, repr=False)
    _scaled_cache: Dict[Tuple[int, int], pygame.Surface] = field(default_factory=dict, init=False, repr=False)
    loop_enabled: bool = field(default=False, init=False)
    duration: float = field(default=0.0, init=False)
    video_size: Tuple[int, int] = field(default=(0, 0), init=False)
    assets_prepared: bool = field(default=False, init=False, repr=False)

    def __post_init__(self) -> None:
        self.video_path = self.video_path.expanduser()
        if not self.video_path.exists():
            raise FileNotFoundError(f"Missing media file: {self.video_path}")
        base = f"{slugify(self.artist)}-{slugify(self.name)}"
        self.cache_key = base or slugify(self.video_path.stem)
        self.thumbnail_path = CACHE_DIR / f"{self.cache_key}.jpg"
        self.audio_path = CACHE_DIR / f"{self.cache_key}.ogg"
        self.search_key = self.name.casefold()

    def ensure_assets(self) -> None:
        """
        Ensure that the thumbnail image and audio track are materialized in the cache.
        """
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        needs_thumb = not self.thumbnail_path.exists()
        needs_audio = not self.audio_path.exists()

        force_thumb_refresh = not self.assets_prepared

        if not needs_thumb and not needs_audio and not force_thumb_refresh and self.duration > 0.0:
            return

        VideoFileClip = resolve_video_file_clip()

        with VideoFileClip(str(self.video_path)) as clip:
            clip_duration = float(getattr(clip, "duration", 0.0) or 0.0)
            self.duration = clip_duration
            if hasattr(clip, "size") and clip.size:
                self.video_size = (int(clip.size[0]), int(clip.size[1]))
            frame_time = 0.0
            if clip_duration > 0.0:
                midpoint = clip_duration / 2.0
                safe_tail = max(0.0, clip_duration - 0.04)
                frame_time = max(0.0, min(midpoint, safe_tail))
            if needs_thumb or force_thumb_refresh:
                clip.save_frame(str(self.thumbnail_path), t=frame_time)
                self._thumbnail_surface = None
                self._scaled_cache.clear()
            if needs_audio:
                if clip.audio is None:
                    raise RuntimeError(f"No audio track found in {self.video_path}")
                clip.audio.write_audiofile(
                    str(self.audio_path),
                    fps=44100,
                    nbytes=2,
                    codec="libvorbis",
                    logger=None,
                )
        if self.duration <= 0.0:
            self.duration = 1.0
        self.assets_prepared = True

    def get_thumbnail(self, size: Tuple[int, int]) -> pygame.Surface:
        """
        Return a pygame surface for the thumbnail scaled to the requested size.
        """
        if self._thumbnail_surface is None:
            self._thumbnail_surface = pygame.image.load(str(self.thumbnail_path)).convert()
        target_w, target_h = size
        original_w, original_h = self._thumbnail_surface.get_size()
        if original_w == 0 or original_h == 0:
            return self._thumbnail_surface
        scale = min(target_w / original_w, target_h / original_h)
        scaled_size = (max(1, int(original_w * scale)), max(1, int(original_h * scale)))
        if scaled_size not in self._scaled_cache:
            self._scaled_cache[scaled_size] = pygame.transform.smoothscale(self._thumbnail_surface, scaled_size)
        return self._scaled_cache[scaled_size]

class SongCard:
    def __init__(self, song: Song, rect: pygame.Rect, title_font: pygame.font.Font, body_font: pygame.font.Font) -> None:
        self.song = song
        self.rect = rect
        self.title_font = title_font
        self.body_font = body_font
        self.control_rects: Dict[str, pygame.Rect] = {}
        self.progress_rect: Optional[pygame.Rect] = None

    def update_rect(self, rect: pygame.Rect) -> None:
        self.rect = rect

    def _truncate(self, text: str, font: pygame.font.Font, max_width: int) -> str:
        if font.size(text)[0] <= max_width:
            return text
        ellipsis = "..."
        base = text
        while base and font.size(base + ellipsis)[0] > max_width:
            base = base[:-1]
        return (base + ellipsis) if base else text[: max(1, len(text) - 3)] + ellipsis

    def draw(
        self,
        surface: pygame.Surface,
        mouse_pos: Tuple[int, int],
        *,
        is_current: bool = False,
        is_playing: bool = False,
        progress: float = 0.0,
        is_fullscreen: bool = False,
    ) -> None:
        pygame.draw.rect(surface, CARD_COLOR, self.rect, border_radius=18)
        border_color = CARD_HIGHLIGHT if is_current else CARD_BORDER
        pygame.draw.rect(surface, border_color, self.rect, width=3, border_radius=18)

        self.control_rects.clear()

        thumb_area = pygame.Rect(
            self.rect.x + THUMB_PADDING,
            self.rect.y + THUMB_PADDING,
            self.rect.width - THUMB_PADDING * 2,
            int(self.rect.height * 0.5),
        )

        thumbnail = self.song.get_thumbnail((thumb_area.width, thumb_area.height))
        thumb_rect = thumbnail.get_rect(center=thumb_area.center)
        thumb_rect.clamp_ip(thumb_area)
        surface.blit(thumbnail, thumb_rect)

        text_area_top = thumb_area.bottom + 14
        text_width = self.rect.width - THUMB_PADDING * 2

        title_text = self._truncate(self.song.name, self.title_font, text_width)
        title_surface = self.title_font.render(title_text, True, TEXT_COLOR)
        title_rect = title_surface.get_rect(midtop=(self.rect.centerx, text_area_top))
        surface.blit(title_surface, title_rect)

        artist_text = self._truncate(self.song.artist, self.body_font, text_width)
        artist_surface = self.body_font.render(artist_text, True, MUTED_TEXT)
        artist_rect = artist_surface.get_rect(midtop=(self.rect.centerx, title_rect.bottom + 8))
        surface.blit(artist_surface, artist_rect)

        content_bottom = artist_rect.bottom
        progress_top = max(content_bottom + 20, thumb_area.bottom + 16)
        button_height = 44
        upper_button_top = self.rect.bottom - THUMB_PADDING - button_height
        if progress_top + PROGRESS_HEIGHT + 16 > upper_button_top:
            progress_top = max(thumb_area.bottom + 16, upper_button_top - (PROGRESS_HEIGHT + 16))

        progress_rect = pygame.Rect(
            self.rect.x + THUMB_PADDING,
            progress_top,
            self.rect.width - THUMB_PADDING * 2,
            PROGRESS_HEIGHT,
        )
        self.progress_rect = progress_rect
        if is_current:
            pygame.draw.rect(surface, (40, 45, 65), progress_rect, border_radius=PROGRESS_HEIGHT // 2)
            progress_width = int(progress_rect.width * max(0.0, min(progress, 1.0)))
            if progress_width > 0:
                fill_rect = pygame.Rect(progress_rect.x, progress_rect.y, progress_width, progress_rect.height)
                pygame.draw.rect(surface, PRIMARY_ACTIVE, fill_rect, border_radius=PROGRESS_HEIGHT // 2)

        button_top = max(progress_rect.bottom + 16, thumb_area.bottom + 32)
        button_top = min(button_top, upper_button_top)

        button_area = pygame.Rect(
            self.rect.x + THUMB_PADDING,
            button_top,
            self.rect.width - THUMB_PADDING * 2,
            44,
        )

        gap = 8
        control_order = ["play", "loop", "fullscreen"]
        button_count = len(control_order)
        button_width = (button_area.width - gap * (button_count - 1)) // max(1, button_count)
        button_height = button_area.height

        labels = {
            "play": "Pause" if (is_current and is_playing) else "Play",
            "loop": "Loop",
            "fullscreen": "Fullscreen",
        }

        for index, control in enumerate(control_order):
            x = button_area.x + index * (button_width + gap)
            rect = pygame.Rect(x, button_area.y, button_width, button_height)
            hovered = rect.collidepoint(mouse_pos)
            is_active = False
            border_color: Optional[Tuple[int, int, int]] = None
            if control == "play" and is_current and is_playing:
                border_color = ACCENT_GREEN
            if control == "loop":
                is_active = self.song.loop_enabled
            if control == "fullscreen":
                is_active = is_fullscreen
                if is_fullscreen:
                    border_color = CARD_HIGHLIGHT
            base_color = PRIMARY_ACTIVE if is_active else PRIMARY_COLOR
            hover_color = PRIMARY_HOVER if not is_active else PRIMARY_ACTIVE
            pygame.draw.rect(
                surface,
                hover_color if hovered else base_color,
                rect,
                border_radius=14,
            )
            if border_color:
                pygame.draw.rect(surface, border_color, rect, width=3, border_radius=14)
            label_surface = self.body_font.render(labels[control], True, TEXT_COLOR)
            surface.blit(label_surface, label_surface.get_rect(center=rect.center))
            self.control_rects[control] = rect

    def control_at(self, position: Tuple[int, int]) -> Optional[str]:
        for control, rect in self.control_rects.items():
            if rect.collidepoint(position):
                return control
        return None

    def contains(self, position: Tuple[int, int]) -> bool:
        return self.rect.collidepoint(position)

    def progress_ratio_at(self, position: Tuple[int, int]) -> Optional[float]:
        if not self.progress_rect:
            return None
        rect = self.progress_rect
        vertical_tolerance = 12
        if position[1] < rect.top - vertical_tolerance or position[1] > rect.bottom + vertical_tolerance:
            return None
        ratio = (position[0] - rect.left) / max(1, rect.width)
        return max(0.0, min(1.0, ratio))


class VideoSession:
    def __init__(self) -> None:
        self.clip: Optional[object] = None
        self.song: Optional[Song] = None
        self.cached_surface: Optional[pygame.Surface] = None
        self.last_timestamp: float = -1.0

    def load(self, song: Song) -> None:
        if self.song == song and self.clip is not None:
            return
        self.unload()
        VideoFileClip = resolve_video_file_clip()
        self.clip = VideoFileClip(str(song.video_path))
        self.song = song
        self.cached_surface = None
        self.last_timestamp = -1.0

    def unload(self) -> None:
        if self.clip is not None:
            try:
                self.clip.close()
            except Exception:
                pass
        self.clip = None
        self.song = None
        self.cached_surface = None
        self.last_timestamp = -1.0

    def get_surface_at(self, timestamp: float) -> Optional[pygame.Surface]:
        if self.clip is None or self.song is None:
            return None
        duration = max(self.song.duration, 0.001)
        timestamp = max(0.0, min(timestamp, duration - 0.001))
        if self.cached_surface is not None and abs(timestamp - self.last_timestamp) < 1 / 30:
            return self.cached_surface
        frame = self.clip.get_frame(timestamp)
        frame_bytes = frame.astype("uint8").tobytes()
        width, height = frame.shape[1], frame.shape[0]
        surface = pygame.image.frombuffer(frame_bytes, (width, height), "RGB")
        surface = surface.convert()
        self.cached_surface = surface
        self.last_timestamp = timestamp
        return self.cached_surface


class PypifyApp:
    def __init__(self) -> None:
        prepare_user_environment()
        ensure_dependencies()
        pygame.mixer.pre_init(44100, -16, 2, 1024)
        pygame.init()
        pygame.mixer.init()
        pygame.mixer.music.set_endevent(MUSIC_END_EVENT)

        self.display = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT), pygame.RESIZABLE)
        pygame.display.set_caption("Pypify — Offline Vibes")

        self.header_font = pygame.font.SysFont("Montserrat", 36, bold=True)
        self.status_font = pygame.font.SysFont("Montserrat", 22)
        self.button_font = pygame.font.SysFont("Montserrat", 20, bold=True)
        self.card_title_font = pygame.font.SysFont("Montserrat", 22, bold=True)
        self.card_body_font = pygame.font.SysFont("Montserrat", 18)
        self.small_font = pygame.font.SysFont("Montserrat", 16)

        self.clock = pygame.time.Clock()

        raw_songs = load_song_data(DATA_PATH)
        self.songs: List[Song] = []
        for entry in raw_songs:
            name = entry.get("name", "").strip() if isinstance(entry, dict) else ""
            artist = entry.get("artist", "").strip() if isinstance(entry, dict) else ""
            video_value = entry.get("pathtovideo", "") if isinstance(entry, dict) else ""

            try:
                video_path = resolve_media_path(video_value)
                song = Song(
                    name=name or "Untitled Track",
                    artist=artist or "Unknown Artist",
                    video_path=video_path,
                )
            except Exception as exc:
                print(f"Skipping song '{name}' due to error: {exc}", file=sys.stderr)
                continue
            self.songs.append(song)

        self.filtered_songs: List[Song] = []
        self.display_songs: List[Song] = []
        self.visible_song_limit = PAGE_SIZE
        self.cards: List[SongCard] = []
        self.video_session = VideoSession()

        self.current_song: Optional[Song] = None
        self.is_paused = False
        self.play_start_ticks = 0
        self.play_start_position = 0.0

        self.shuffle_active = False
        self.shuffle_queue: List[Song] = []

        self.search_query = ""
        self.search_active = False
        self.search_rect = pygame.Rect(0, 0, 0, 0)
        self.search_cursor_visible = True
        self.search_cursor_tick = 0
        self.load_more_rect = pygame.Rect(0, 0, 0, 0)
        self.search_available = True

        self.add_form_active = False
        self.add_form_fields = {"path": "", "name": "", "artist": ""}
        self.add_form_order = ["path", "name", "artist"]
        self.add_form_focus = 0
        self.add_form_error = ""
        self.add_form_status = ""
        self.add_form_field_rects: Dict[str, pygame.Rect] = {}
        self.add_form_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_submit_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_cancel_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_error_field: Optional[str] = None
        self.add_form_mode: Literal["add", "edit"] = "add"
        self.edit_target_song: Optional[Song] = None

        self.edit_mode = False
        self.delete_mode = False
        self.edit_mode_rect = pygame.Rect(0, 0, 0, 0)
        self.delete_mode_rect = pygame.Rect(0, 0, 0, 0)
        self.clipboard_enabled = False
        self._scrap_module: Optional[object] = None
        self._scrap_text_formats: List[str] = []

        self.volume = 0.7
        pygame.mixer.music.set_volume(self.volume)
        self.dragging_volume = False

        self.shuffle_button_rect = pygame.Rect(0, 0, 0, 0)
        self.add_button_rect = pygame.Rect(0, 0, 0, 0)
        self.volume_slider_rect = pygame.Rect(0, 0, 0, 0)
        self.volume_knob_pos = (0, 0)
        self.fullscreen_exit_rect = pygame.Rect(0, 0, 0, 0)
        self.fullscreen_play_rect = pygame.Rect(0, 0, 0, 0)
        self.fullscreen_next_rect = pygame.Rect(0, 0, 0, 0)
        self.fullscreen_progress_rect = pygame.Rect(0, 0, 0, 0)
        self.video_fullscreen = False
        self.dragging_progress_card: Optional[SongCard] = None
        self.dragging_fullscreen_progress = False
        self.suppress_end_event = False
        self.card_area_offset = HEADER_HEIGHT
        self.external_link = "https://ytdown.to/en2/"
        self.link_rect = pygame.Rect(0, 0, 0, 0)
        self.link_font = pygame.font.SysFont("Montserrat", 26, bold=True)

        self._apply_search()
        self._init_clipboard()

    def _search_base(self) -> List[Song]:
        query = self.search_query.casefold().strip()
        if not query:
            return list(self.songs)
        return [song for song in self.songs if query in song.search_key]

    def _apply_search(self, *, reset_visible: bool = True) -> None:
        base = self._search_base()
        self.shuffle_active = False
        self.shuffle_queue.clear()
        self.filtered_songs = base
        if reset_visible or self.visible_song_limit <= 0:
            self.visible_song_limit = min(PAGE_SIZE, len(self.filtered_songs)) if self.filtered_songs else 0
        else:
            self.visible_song_limit = min(max(self.visible_song_limit, PAGE_SIZE), len(self.filtered_songs))
        self._refresh_display_songs()

    def _init_clipboard(self) -> None:
        try:
            import pygame.scrap as scrap

            scrap.init()
            self._scrap_module = scrap
            self.clipboard_enabled = True
            text_formats: List[str] = []
            for attr in ("SCRAP_TEXT", "SCRAP_TEXT_UTF8", "SCRAP_UTF8", "SCRAP_UNICODE"):
                if hasattr(scrap, attr):
                    text_formats.append(getattr(scrap, attr))
            text_formats.extend(
                [
                    "text/plain;charset=utf-8",
                    "text/plain",
                    "UTF8_STRING",
                    "STRING",
                ]
            )
            self._scrap_text_formats = text_formats
        except Exception:
            self._scrap_module = None
            self.clipboard_enabled = False
            self._scrap_text_formats = []

    def _get_clipboard_text(self) -> str:
        text_value = ""
        if self.clipboard_enabled and self._scrap_module is not None:
            scrap = self._scrap_module
            try:
                text_value = scrap.get_text()  # type: ignore[assignment]
            except Exception:
                text_value = ""
            if not text_value:
                for fmt in self._scrap_text_formats:
                    try:
                        payload = scrap.get(fmt)  # type: ignore[arg-type]
                    except Exception:
                        continue
                    if not payload:
                        continue
                    if isinstance(payload, bytes):
                        payload = payload.decode("utf-8", errors="ignore")
                    text_value = str(payload)
                    if text_value:
                        break
        if not text_value:
            try:
                import tkinter as tk

                root = tk.Tk()
                root.withdraw()
                try:
                    text_value = root.clipboard_get()
                finally:
                    root.destroy()
            except Exception:
                text_value = ""
        if not text_value:
            return ""
        return str(text_value).replace("\r\n", "\n").replace("\r", "\n")

    def _prepare_visible_assets(self) -> None:
        for song in self.display_songs:
            try:
                song.ensure_assets()
            except Exception as exc:
                print(f"Unable to prepare assets for '{song.name}': {exc}", file=sys.stderr)

    def _refresh_display_songs(self) -> None:
        if self.visible_song_limit <= 0:
            self.display_songs = []
        else:
            limit = min(self.visible_song_limit, len(self.filtered_songs))
            self.display_songs = self.filtered_songs[:limit]
        self._prepare_visible_assets()
        self._layout_cards()

    def _load_more_songs(self) -> None:
        if self.visible_song_limit >= len(self.filtered_songs):
            return
        remaining = len(self.filtered_songs) - self.visible_song_limit
        increment = min(PAGE_SIZE, remaining)
        self.visible_song_limit += increment
        self._refresh_display_songs()

    def _ensure_song_visible(self, song: Song) -> None:
        if song not in self.filtered_songs:
            return
        index = self.filtered_songs.index(song)
        if index < self.visible_song_limit:
            return
        new_limit = index + 1
        remainder = new_limit % PAGE_SIZE
        if remainder:
            new_limit += PAGE_SIZE - remainder
        self.visible_song_limit = min(new_limit, len(self.filtered_songs))
        self._refresh_display_songs()

    def _truncate_text(self, text: str, font: pygame.font.Font, max_width: int) -> str:
        if font.size(text)[0] <= max_width:
            return text
        ellipsis = "..."
        trimmed = text
        while trimmed and font.size(trimmed + ellipsis)[0] > max_width:
            trimmed = trimmed[:-1]
        return (trimmed + ellipsis) if trimmed else ellipsis

    def _set_card_area_offset(self, value: int) -> None:
        clamped = max(HEADER_HEIGHT, value)
        if clamped != getattr(self, "card_area_offset", HEADER_HEIGHT):
            self.card_area_offset = clamped
            self._layout_cards()

    def _layout_cards(self) -> None:
        available_width = WINDOW_WIDTH - CARD_MARGIN_X * 2
        cards_area_width = int(available_width)
        columns = max(1, cards_area_width // (CARD_WIDTH + CARD_GAP))
        self.cards = []
        for index, song in enumerate(self.display_songs):
            row = index // columns
            col = index % columns
            x = CARD_MARGIN_X + col * (CARD_WIDTH + CARD_GAP)
            y = self.card_area_offset + CARD_MARGIN_Y + row * (CARD_HEIGHT + CARD_GAP)
            rect = pygame.Rect(x, y, CARD_WIDTH, CARD_HEIGHT)
            self.cards.append(SongCard(song, rect, self.card_title_font, self.card_body_font))

    def _handle_key_down(self, event: pygame.event.Event) -> bool:
        if self.add_form_active:
            return self._handle_add_form_keydown(event)
        if self.search_active and not self.search_available:
            self.search_active = False
            return False
        if self.search_active:
            if event.key in (pygame.K_RETURN, pygame.K_KP_ENTER):
                self.search_active = False
                return True
            if event.key == pygame.K_ESCAPE:
                if self.search_query:
                    self.search_query = ""
                    self._apply_search()
                else:
                    self.search_active = False
                return True
            if event.key == pygame.K_BACKSPACE:
                if self.search_query:
                    self.search_query = self.search_query[:-1]
                self._apply_search()
                return True
            if event.unicode and event.unicode.isprintable() and not (event.mod & (pygame.KMOD_CTRL | pygame.KMOD_META)):
                self.search_query += event.unicode
                self._apply_search()
                return True
            if event.key == pygame.K_TAB:
                self.search_active = False
        return False

    def run(self) -> None:
        running = True
        while running:
            self.clock.tick(60)
            mouse_pos = pygame.mouse.get_pos()
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.VIDEORESIZE:
                    self._handle_resize(event.w, event.h)
                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    self._handle_mouse_down(mouse_pos)
                elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
                    self._handle_mouse_up()
                elif event.type == pygame.MOUSEMOTION:
                    self._handle_mouse_motion(mouse_pos)
                elif event.type == pygame.KEYDOWN:
                    if self._handle_key_down(event):
                        continue
                    if self.video_fullscreen and event.key in (pygame.K_ESCAPE, pygame.K_f):
                        self.toggle_fullscreen(False)
                elif event.type == MUSIC_END_EVENT:
                    self._handle_song_end()

            self._render(mouse_pos)
            pygame.display.flip()

        self.video_session.unload()
        pygame.quit()

    def _handle_resize(self, width: int, height: int) -> None:
        global WINDOW_WIDTH, WINDOW_HEIGHT
        WINDOW_WIDTH, WINDOW_HEIGHT = max(960, width), max(640, height)
        self.display = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT), pygame.RESIZABLE)
        self._layout_cards()

    def _handle_mouse_down(self, position: Tuple[int, int]) -> None:
        if self.add_form_active:
            if self._handle_add_form_mouse_down(position):
                return
        # prevent background interactions while overlay is open
        if self.add_form_active:
            return

        if self.video_fullscreen:
            if self.fullscreen_exit_rect.collidepoint(position):
                self.toggle_fullscreen(False)
                return
            if self.fullscreen_play_rect.collidepoint(position) and self.current_song:
                self._toggle_song_pause(self.current_song)
                return
            if self.fullscreen_next_rect.collidepoint(position):
                self.play_next_song()
                return
            if self.current_song:
                ratio = self._ratio_from_rect(self.fullscreen_progress_rect, position)
                if ratio is not None:
                    self.seek_song(self.current_song, ratio)
                    self.dragging_fullscreen_progress = True
                    return
            return

        if self.link_rect.collidepoint(position) and self.link_rect.width > 0 and self.link_rect.height > 0:
            try:
                webbrowser.open(self.external_link)
            except Exception as exc:
                print(f"Unable to open browser: {exc}", file=sys.stderr)
            return

        if self.search_available and self.search_rect.collidepoint(position):
            self.search_active = True
            return
        if self.search_active:
            self.search_active = False

        if self.load_more_rect.collidepoint(position):
            self._load_more_songs()
            return

        if self.edit_mode_rect.collidepoint(position) and self.edit_mode_rect.width > 0:
            self.edit_mode = not self.edit_mode
            if self.edit_mode:
                self.delete_mode = False
            return
        if self.delete_mode_rect.collidepoint(position) and self.delete_mode_rect.width > 0:
            self.delete_mode = not self.delete_mode
            if self.delete_mode:
                self.edit_mode = False
            return

        if self.shuffle_button_rect.collidepoint(position):
            self.start_shuffle()
            return
        if self.add_button_rect.collidepoint(position):
            self.open_add_song_dialog()
            return
        if self.volume_slider_rect.collidepoint(position) or (
            (position[0] - self.volume_knob_pos[0]) ** 2 + (position[1] - self.volume_knob_pos[1]) ** 2
            <= VOLUME_KNOB_RADIUS**2
        ):
            self.dragging_volume = True
            self._update_volume_from_mouse(position[0])
            return

        if self.edit_mode or self.delete_mode:
            for card in self.cards:
                if card.contains(position):
                    if self.edit_mode:
                        self.open_edit_song_dialog(card.song)
                    else:
                        self.delete_song(card.song)
                    return
            return

        for card in self.cards:
            ratio = card.progress_ratio_at(position)
            if ratio is not None:
                if self.current_song != card.song:
                    self.shuffle_active = False
                    self.shuffle_queue.clear()
                self.seek_song(card.song, ratio)
                self.dragging_progress_card = card
                return

        for card in self.cards:
            control = card.control_at(position)
            if control:
                self._handle_card_control(card.song, control)
                return
            if card.contains(position):
                self.shuffle_active = False
                self.shuffle_queue.clear()
                self.play_song(card.song)
                return

    def _handle_mouse_up(self) -> None:
        self.dragging_volume = False
        self.dragging_progress_card = None
        self.dragging_fullscreen_progress = False

    def _handle_mouse_motion(self, position: Tuple[int, int]) -> None:
        if self.add_form_active:
            return
        if self.dragging_progress_card:
            rect = self.dragging_progress_card.progress_rect
            if rect:
                ratio = self._ratio_from_rect(rect, position, tolerance=None)
                if ratio is not None:
                    self.seek_song(self.dragging_progress_card.song, ratio)
            return
        if self.dragging_fullscreen_progress and self.current_song:
            ratio = self._ratio_from_rect(self.fullscreen_progress_rect, position, tolerance=None)
            if ratio is not None:
                self.seek_song(self.current_song, ratio)
            return
        if self.dragging_volume:
            self._update_volume_from_mouse(position[0])

    def _update_volume_from_mouse(self, mouse_x: int) -> None:
        if self.volume_slider_rect.width <= 0:
            return
        left = self.volume_slider_rect.left
        right = self.volume_slider_rect.right
        ratio = (mouse_x - left) / max(1, (right - left))
        self.volume = max(0.0, min(1.0, ratio))
        pygame.mixer.music.set_volume(self.volume)

    def _ratio_from_rect(
        self,
        rect: pygame.Rect,
        position: Tuple[int, int],
        tolerance: Optional[int] = 12,
    ) -> Optional[float]:
        if rect.width <= 0 or rect.height <= 0:
            return None
        if tolerance is not None:
            if position[1] < rect.top - tolerance or position[1] > rect.bottom + tolerance:
                return None
        ratio = (position[0] - rect.left) / rect.width
        return max(0.0, min(1.0, ratio))

    def _draw_button(
        self,
        surface: pygame.Surface,
        rect: pygame.Rect,
        label: str,
        *,
        active: bool = False,
        hovered: bool = False,
    ) -> None:
        base = PRIMARY_ACTIVE if active else PRIMARY_COLOR
        color = PRIMARY_HOVER if hovered else base
        pygame.draw.rect(surface, color, rect, border_radius=16)
        text_surface = self.button_font.render(label, True, TEXT_COLOR)
        surface.blit(text_surface, text_surface.get_rect(center=rect.center))

    def _draw_mode_toggle(
        self,
        surface: pygame.Surface,
        rect: pygame.Rect,
        label: str,
        *,
        active: bool = False,
        hovered: bool = False,
    ) -> None:
        base_color = (56, 60, 84) if hovered or active else (46, 50, 72)
        border_color = PRIMARY_ACTIVE if active else CARD_BORDER
        pygame.draw.rect(surface, base_color, rect, border_radius=16)
        pygame.draw.rect(surface, border_color, rect, width=2, border_radius=16)

        box_size = 22
        box_rect = pygame.Rect(rect.left + 18, rect.centery - box_size // 2, box_size, box_size)
        pygame.draw.rect(surface, (32, 35, 52), box_rect, border_radius=6)
        pygame.draw.rect(surface, border_color, box_rect, width=2, border_radius=6)
        if active:
            check_points = [
                (box_rect.left + 5, box_rect.centery),
                (box_rect.centerx - 2, box_rect.bottom - 6),
                (box_rect.right - 6, box_rect.top + 6),
            ]
            pygame.draw.lines(surface, PRIMARY_ACTIVE, False, check_points, 3)

        label_surface = self.card_body_font.render(label, True, TEXT_COLOR)
        label_rect = label_surface.get_rect()
        label_rect.left = box_rect.right + 12
        label_rect.centery = rect.centery
        surface.blit(label_surface, label_rect)

    def _render(self, mouse_pos: Tuple[int, int]) -> None:
        if self.video_fullscreen:
            self._render_fullscreen(mouse_pos)
            return

        self.fullscreen_progress_rect = pygame.Rect(0, 0, 0, 0)
        self.display.fill(BACKGROUND_COLOR)
        ticks = pygame.time.get_ticks()
        if ticks - self.search_cursor_tick >= 500:
            self.search_cursor_visible = not self.search_cursor_visible
            self.search_cursor_tick = ticks

        header_top = 24
        header_text = "Pypify · Your Offline Mixtape"
        header_surface = self.header_font.render(header_text, True, TEXT_COLOR)
        self.display.blit(header_surface, (CARD_MARGIN_X, header_top))

        controls_top = header_top + header_surface.get_height() + 24
        button_y = controls_top
        search_height = 52
        button_width = 180
        button_height = 54
        button_gap = 18
        search_button_gap = 20

        control_order = ("edit", "delete", "shuffle", "add", "volume")
        base_widths = {
            "edit": 170.0,
            "delete": 170.0,
            "shuffle": float(button_width),
            "add": float(button_width),
            "volume": float(max(VOLUME_SLIDER_WIDTH + 140, button_width + 120)),
        }
        min_widths = {
            "edit": 130.0,
            "delete": 130.0,
            "shuffle": 150.0,
            "add": 150.0,
            "volume": float(VOLUME_SLIDER_WIDTH + 120),
        }
        min_controls_total = sum(min_widths.values())
        total_top_width = WINDOW_WIDTH - CARD_MARGIN_X * 2
        search_min_width = 240.0
        search_max_width = 480.0

        def attempt_layout(include_search: bool) -> Optional[dict]:
            if include_search and total_top_width <= min_controls_total + search_min_width + search_button_gap:
                return None
            search_width = search_max_width if include_search else 0.0
            while True:
                if include_search:
                    max_search_width = max(0.0, total_top_width - min_controls_total - search_button_gap)
                    if max_search_width < search_min_width:
                        return None
                    search_width = min(search_width, max_search_width)
                    if search_width < search_min_width:
                        return None
                controls_left = CARD_MARGIN_X + (search_width + search_button_gap if include_search else 0.0)
                controls_right = WINDOW_WIDTH - CARD_MARGIN_X
                controls_width_available = max(0.0, controls_right - controls_left)
                if controls_width_available < min_controls_total:
                    if include_search and search_width > search_min_width:
                        search_width = max(search_min_width, search_width - 40.0)
                        continue
                    return None
                widths = {key: float(base_widths[key]) for key in control_order}
                widths_total = sum(widths.values())
                if widths_total > controls_width_available:
                    while widths_total - controls_width_available > 0.5:
                        adjustable = [key for key in control_order if widths[key] - min_widths[key] > 0.5]
                        if not adjustable:
                            break
                        extra = widths_total - controls_width_available
                        share = extra / len(adjustable)
                        for key in adjustable:
                            allowed = widths[key] - min_widths[key]
                            delta = min(allowed, share)
                            if delta <= 0:
                                continue
                            widths[key] -= delta
                        widths_total = sum(widths.values())
                if widths_total > controls_width_available + 0.5:
                    if include_search and search_width > search_min_width:
                        search_width = max(search_min_width, search_width - 40.0)
                        continue
                    return None
                gap_value = 0.0
                if len(control_order) > 1:
                    gap_value = max(0.0, (controls_width_available - widths_total) / (len(control_order) - 1))
                return {
                    "search_available": include_search,
                    "search_width": search_width if include_search else 0.0,
                    "widths": widths,
                    "gap": gap_value,
                    "controls_left": controls_left,
                    "controls_right": controls_right,
                }

        layout = attempt_layout(True)
        if layout is None:
            layout = attempt_layout(False)
        if layout is None:
            controls_left = float(CARD_MARGIN_X)
            controls_right = float(WINDOW_WIDTH - CARD_MARGIN_X)
            controls_width_available = max(0.0, controls_right - controls_left)
            equal_width = controls_width_available / max(1, len(control_order))
            widths = {key: equal_width for key in control_order}
            gap_value = 0.0
            if len(control_order) > 1:
                gap_value = max(
                    0.0,
                    (controls_width_available - equal_width * len(control_order)) / (len(control_order) - 1),
                )
            layout = {
                "search_available": False,
                "search_width": 0.0,
                "widths": widths,
                "gap": gap_value,
                "controls_left": controls_left,
                "controls_right": controls_right,
            }

        if layout["search_available"]:
            search_width = max(search_min_width, layout["search_width"])
            search_rect_width = max(1, int(round(search_width)))
            self.search_available = True
            self.search_rect = pygame.Rect(CARD_MARGIN_X, button_y, search_rect_width, search_height)
            controls_left = float(self.search_rect.right + search_button_gap)
        else:
            self.search_available = False
            self.search_rect = pygame.Rect(0, 0, 0, 0)
            controls_left = float(CARD_MARGIN_X)
            if self.search_active:
                self.search_active = False

        controls_right = float(layout["controls_right"])
        gap = float(layout["gap"])
        widths = layout["widths"]
        x = controls_left
        control_rects: Dict[str, pygame.Rect] = {}
        for index, key in enumerate(control_order):
            width_value = widths[key]
            rect_left = int(round(x))
            rect_width = max(1, int(round(width_value)))
            if index == len(control_order) - 1:
                rect_right = int(round(controls_right))
                rect_width = max(1, rect_right - rect_left)
                if rect_width <= 0:
                    rect_width = max(1, int(round(width_value)))
            control_rects[key] = pygame.Rect(rect_left, button_y, rect_width, button_height)
            x += width_value + gap

        edit_rect = control_rects["edit"]
        delete_rect = control_rects["delete"]
        shuffle_rect = control_rects["shuffle"]
        add_rect = control_rects["add"]
        volume_container_rect = control_rects["volume"]

        self.edit_mode_rect = edit_rect
        self.delete_mode_rect = delete_rect
        self.shuffle_button_rect = shuffle_rect
        self.add_button_rect = add_rect

        edit_hovered = edit_rect.collidepoint(mouse_pos)
        delete_hovered = delete_rect.collidepoint(mouse_pos)
        self._draw_mode_toggle(
            self.display,
            edit_rect,
            "Edit Mode",
            active=self.edit_mode,
            hovered=edit_hovered,
        )
        self._draw_mode_toggle(
            self.display,
            delete_rect,
            "Delete Mode",
            active=self.delete_mode,
            hovered=delete_hovered,
        )

        if self.search_available:
            search_bg = (46, 50, 72)
            pygame.draw.rect(self.display, search_bg, self.search_rect, border_radius=18)
            border_color = PRIMARY_ACTIVE if self.search_active else CARD_BORDER
            pygame.draw.rect(self.display, border_color, self.search_rect, width=2, border_radius=18)

            query_text = self.search_query.strip()
            placeholder = "Search songs..."
            display_label = query_text if query_text else placeholder
            text_color = TEXT_COLOR if query_text else MUTED_TEXT
            padding = 20
            max_text_width = max(10, self.search_rect.width - padding * 2)
            truncated = self._truncate_text(display_label, self.card_body_font, max_text_width)
            text_surface = self.card_body_font.render(truncated, True, text_color)
            text_rect = text_surface.get_rect()
            text_rect.left = self.search_rect.left + padding
            text_rect.centery = self.search_rect.centery
            self.display.blit(text_surface, text_rect)

            if self.search_active and self.search_cursor_visible:
                caret_x = min(self.search_rect.right - 10, text_rect.right + 4)
                caret_top = self.search_rect.top + 10
                caret_bottom = self.search_rect.bottom - 10
                pygame.draw.line(self.display, TEXT_COLOR, (caret_x, caret_top), (caret_x, caret_bottom), 2)

        self._draw_button(
            self.display,
            shuffle_rect,
            "Shuffle",
            active=self.shuffle_active,
            hovered=shuffle_rect.collidepoint(mouse_pos),
        )
        self._draw_button(self.display, add_rect, "Add Song", hovered=add_rect.collidepoint(mouse_pos))

        pygame.draw.rect(self.display, (46, 50, 72), volume_container_rect, border_radius=18)
        pygame.draw.rect(self.display, CARD_BORDER, volume_container_rect, width=2, border_radius=18)

        label_surface = self.small_font.render("Volume", True, MUTED_TEXT)
        label_rect = label_surface.get_rect()
        label_rect.left = volume_container_rect.left + 12
        label_rect.top = volume_container_rect.top + 6
        self.display.blit(label_surface, label_rect)

        value_surface = self.small_font.render(f"{int(self.volume * 100)}%", True, TEXT_COLOR)
        value_rect = value_surface.get_rect()
        value_rect.top = volume_container_rect.top + 6
        value_rect.right = volume_container_rect.right - 12
        self.display.blit(value_surface, value_rect)

        icon_left = volume_container_rect.left + 12
        icon_right = icon_left + 20
        icon_top = volume_container_rect.centery - 12
        icon_bottom = volume_container_rect.centery + 12
        speaker_points = [
            (icon_left, volume_container_rect.centery - 10),
            (icon_left + 12, volume_container_rect.centery - 10),
            (icon_right, volume_container_rect.centery - 4),
            (icon_right, volume_container_rect.centery + 4),
            (icon_left + 12, volume_container_rect.centery + 10),
            (icon_left, volume_container_rect.centery + 10),
        ]
        pygame.draw.polygon(self.display, TEXT_COLOR, speaker_points)

        slider_left = icon_right + 12
        slider_right_limit = volume_container_rect.right - 16
        slider_width = slider_right_limit - slider_left
        if slider_width < 40:
            slider_width = 40
            slider_left = max(volume_container_rect.left + 20, slider_right_limit - slider_width)
        slider_rect = pygame.Rect(
            slider_left,
            volume_container_rect.centery - VOLUME_SLIDER_HEIGHT // 2,
            slider_width,
            VOLUME_SLIDER_HEIGHT,
        )
        self.volume_slider_rect = slider_rect
        pygame.draw.rect(self.display, (60, 65, 90), slider_rect, border_radius=VOLUME_SLIDER_HEIGHT // 2)
        knob_x = slider_rect.left + int(self.volume * slider_rect.width)
        knob_center = (knob_x, volume_container_rect.centery)
        self.volume_knob_pos = knob_center
        pygame.draw.circle(self.display, PRIMARY_ACTIVE, knob_center, VOLUME_KNOB_RADIUS)

        row_bottom = max(
            button_y + button_height,
            volume_container_rect.bottom,
            button_y + (search_height if self.search_available else 0),
        )
        status_y = row_bottom + 24
        status_text = self._status_line()
        status_surface = self.status_font.render(status_text, True, TEXT_COLOR)
        self.display.blit(status_surface, (CARD_MARGIN_X, status_y))

        self._set_card_area_offset(status_y + status_surface.get_height() + 24)

        for card in self.cards:
            is_current = card.song == self.current_song
            is_playing = is_current and not self.is_paused
            progress = self._song_progress(card.song)
            card.draw(
                self.display,
                mouse_pos,
                is_current=is_current,
                is_playing=is_playing,
                progress=progress,
                is_fullscreen=self.video_fullscreen if is_current else False,
            )

        if not self.display_songs:
            message = "No songs match your search." if self.search_query.strip() else "No songs available yet."
            msg_surface = self.card_body_font.render(message, True, MUTED_TEXT)
            msg_rect = msg_surface.get_rect(center=(WINDOW_WIDTH // 2, self.card_area_offset + CARD_MARGIN_Y + 40))
            self.display.blit(msg_surface, msg_rect)

        if self.visible_song_limit < len(self.filtered_songs):
            last_bottom = self.card_area_offset + CARD_MARGIN_Y
            if self.cards:
                last_bottom = max(card.rect.bottom for card in self.cards)
            load_more_rect = pygame.Rect(0, 0, 220, 54)
            load_more_rect.centerx = WINDOW_WIDTH // 2
            load_more_rect.y = last_bottom + CARD_GAP
            self.load_more_rect = load_more_rect
            hovered = load_more_rect.collidepoint(mouse_pos)
            self._draw_button(self.display, load_more_rect, "Load More", hovered=hovered)
        else:
            self.load_more_rect = pygame.Rect(0, 0, 0, 0)

        link_text = f"Grab more songs at {self.external_link}"
        base_surface = self.link_font.render(link_text, True, PRIMARY_ACTIVE)
        link_rect = base_surface.get_rect()
        link_rect.centerx = WINDOW_WIDTH // 2
        link_rect.bottom = WINDOW_HEIGHT - CARD_MARGIN_Y
        hovered = link_rect.collidepoint(mouse_pos)
        link_color = PRIMARY_HOVER if hovered else PRIMARY_ACTIVE
        link_surface = self.link_font.render(link_text, True, link_color)
        self.display.blit(link_surface, link_rect)
        if hovered:
            underline_y = link_rect.bottom - 4
            pygame.draw.line(self.display, link_color, (link_rect.left, underline_y), (link_rect.right, underline_y), 2)
        self.link_rect = link_rect

        if self.add_form_active:
            self._render_add_form(mouse_pos)

    def _render_add_form(self, mouse_pos: Tuple[int, int]) -> None:
        overlay = pygame.Surface(self.display.get_size(), pygame.SRCALPHA)
        overlay.fill((10, 12, 24, 220))
        self.display.blit(overlay, (0, 0))

        form_width = max(380, min(620, WINDOW_WIDTH - CARD_MARGIN_X * 2))
        form_height = max(360, min(520, WINDOW_HEIGHT - CARD_MARGIN_Y * 2))
        form_rect = pygame.Rect(0, 0, form_width, form_height)
        form_rect.center = self.display.get_rect().center
        self.add_form_rect = form_rect

        pygame.draw.rect(self.display, CARD_COLOR, form_rect, border_radius=20)
        pygame.draw.rect(self.display, CARD_HIGHLIGHT, form_rect, width=3, border_radius=20)

        title_text = "Edit Song" if self.add_form_mode == "edit" else "Add Song"
        title_surface = self.header_font.render(title_text, True, TEXT_COLOR)
        title_rect = title_surface.get_rect(midtop=(form_rect.centerx, form_rect.top + 26))
        self.display.blit(title_surface, title_rect)

        if self.add_form_mode == "edit":
            instructions = "Update the song details and press Save Changes."
        else:
        instructions = "Enter the video path, song title, and artist, then submit."
        instructions_surface = self.card_body_font.render(instructions, True, MUTED_TEXT)
        instructions_rect = instructions_surface.get_rect(midtop=(form_rect.centerx, title_rect.bottom + 18))
        self.display.blit(instructions_surface, instructions_rect)

        field_left = form_rect.left + 32
        field_width = form_rect.width - 64
        field_height = 52
        current_y = instructions_rect.bottom + 24
        field_labels = {
            "path": "Path to video file",
            "name": "Song title",
            "artist": "Artist name",
        }
        placeholders = {
            "path": "/absolute/path/to/video.mp4",
            "name": "Song name (optional)",
            "artist": "Artist (optional)",
        }

        self.add_form_field_rects.clear()
        for index, key in enumerate(self.add_form_order):
            label_surface = self.small_font.render(field_labels[key], True, MUTED_TEXT)
            label_rect = label_surface.get_rect(topleft=(field_left, current_y))
            self.display.blit(label_surface, label_rect)

            field_rect = pygame.Rect(field_left, label_rect.bottom + 6, field_width, field_height)
            self.add_form_field_rects[key] = field_rect

            is_focus = index == self.add_form_focus
            base_color = (46, 50, 72)
            pygame.draw.rect(self.display, base_color, field_rect, border_radius=12)
            if self.add_form_error_field == key:
                border_color = DELETE_COLOR
            elif is_focus:
                border_color = PRIMARY_ACTIVE
            else:
                border_color = CARD_BORDER
            pygame.draw.rect(self.display, border_color, field_rect, width=2, border_radius=12)

            value = self.add_form_fields.get(key, "")
            display_value = value if value else placeholders[key]
            text_color = TEXT_COLOR if value else MUTED_TEXT
            truncated = self._truncate_text(display_value, self.card_body_font, field_width - 32)
            text_surface = self.card_body_font.render(truncated, True, text_color)
            text_rect = text_surface.get_rect()
            text_rect.left = field_rect.left + 16
            text_rect.centery = field_rect.centery
            self.display.blit(text_surface, text_rect)

            if is_focus and self.search_cursor_visible:
                caret_x = min(field_rect.right - 10, text_rect.right + 3)
                caret_top = field_rect.top + 12
                caret_bottom = field_rect.bottom - 12
                pygame.draw.line(self.display, TEXT_COLOR, (caret_x, caret_top), (caret_x, caret_bottom), 2)

            current_y = field_rect.bottom + 20

        message_top = current_y
        if self.add_form_error:
            error_surface = self.card_body_font.render(self.add_form_error, True, DELETE_COLOR)
            error_rect = error_surface.get_rect(midtop=(form_rect.centerx, message_top))
            self.display.blit(error_surface, error_rect)
            message_top = error_rect.bottom + 12
        elif self.add_form_status:
            status_surface = self.card_body_font.render(self.add_form_status, True, ACCENT_GREEN)
            status_rect = status_surface.get_rect(midtop=(form_rect.centerx, message_top))
            self.display.blit(status_surface, status_rect)
            message_top = status_rect.bottom + 12

        buttons_top = form_rect.bottom - 72
        button_gap = 16
        button_height = 52
        button_width = (field_width - button_gap) // 2

        cancel_rect = pygame.Rect(field_left, buttons_top, button_width, button_height)
        submit_rect = pygame.Rect(cancel_rect.right + button_gap, buttons_top, button_width, button_height)

        cancel_hovered = cancel_rect.collidepoint(mouse_pos)
        pygame.draw.rect(
            self.display,
            DELETE_HOVER if cancel_hovered else DELETE_COLOR,
            cancel_rect,
            border_radius=14,
        )
        cancel_surface = self.button_font.render("Cancel", True, TEXT_COLOR)
        self.display.blit(cancel_surface, cancel_surface.get_rect(center=cancel_rect.center))

        submit_hovered = submit_rect.collidepoint(mouse_pos)
        submit_label = "Save Changes" if self.add_form_mode == "edit" else "Import Song"
        self._draw_button(self.display, submit_rect, submit_label, hovered=submit_hovered)

        self.add_form_cancel_rect = cancel_rect
        self.add_form_submit_rect = submit_rect

    def _handle_add_form_mouse_down(self, position: Tuple[int, int]) -> bool:
        if not self.add_form_active:
            return False
        if self.add_form_submit_rect.collidepoint(position):
            self._submit_add_form()
            return True
        if self.add_form_cancel_rect.collidepoint(position):
            self._close_add_form()
            return True
        for index, key in enumerate(self.add_form_order):
            rect = self.add_form_field_rects.get(key)
            if rect and rect.collidepoint(position):
                self.add_form_focus = index
                if self.add_form_error_field == key:
                    self.add_form_error_field = None
                    self.add_form_error = ""
                    self.add_form_status = ""
                return True
        if self.add_form_rect and not self.add_form_rect.collidepoint(position):
            return True
        return True

    def _handle_add_form_keydown(self, event: pygame.event.Event) -> bool:
        if event.key == pygame.K_ESCAPE:
            self._close_add_form()
            return True
        if event.key in (pygame.K_RETURN, pygame.K_KP_ENTER):
            self._submit_add_form()
            return True
        if event.key == pygame.K_TAB:
            step = -1 if event.mod & pygame.KMOD_SHIFT else 1
            count = len(self.add_form_order)
            self.add_form_focus = (self.add_form_focus + step) % max(1, count)
            return True
        if event.key == pygame.K_UP:
            self.add_form_focus = (self.add_form_focus - 1) % len(self.add_form_order)
            return True
        if event.key == pygame.K_DOWN:
            self.add_form_focus = (self.add_form_focus + 1) % len(self.add_form_order)
            return True

        key = self.add_form_order[self.add_form_focus]
        current_value = self.add_form_fields.get(key, "")

        paste_trigger = (event.key == pygame.K_v and (event.mod & (pygame.KMOD_CTRL | pygame.KMOD_META))) or (
            event.key == pygame.K_INSERT and (event.mod & pygame.KMOD_SHIFT)
        )
        if paste_trigger:
            clip_text = self._get_clipboard_text()
            if clip_text:
                snippet = clip_text.splitlines()[0]
                if snippet:
                    new_value = (current_value + snippet).strip()
                    if len(new_value) > 512:
                        new_value = new_value[:512]
                    self.add_form_fields[key] = new_value
                    self.add_form_error = ""
                    self.add_form_status = ""
                    self.add_form_error_field = None
            return True

        if event.key == pygame.K_BACKSPACE:
            if current_value:
                self.add_form_fields[key] = current_value[:-1]
            self.add_form_error = ""
            self.add_form_status = ""
            if self.add_form_error_field == key:
                self.add_form_error_field = None
            return True

        if event.unicode and event.unicode.isprintable() and not (event.mod & (pygame.KMOD_CTRL | pygame.KMOD_META)):
            if len(current_value) < 512:
                self.add_form_fields[key] = current_value + event.unicode
            self.add_form_error = ""
            self.add_form_status = ""
            if self.add_form_error_field == key:
                self.add_form_error_field = None
            return True

        return False

    def _submit_add_form(self) -> None:
        path_text = self.add_form_fields.get("path", "").strip()
        name_text = self.add_form_fields.get("name", "").strip()
        artist_text = self.add_form_fields.get("artist", "").strip()

        if not path_text:
            self.add_form_error = "Please provide a path to the video file."
            self.add_form_status = ""
            self.add_form_focus = 0
            self.add_form_error_field = "path"
            return

        normalized_path = path_text.strip()
        if (normalized_path.startswith('"') and normalized_path.endswith('"')) or (
            normalized_path.startswith("'") and normalized_path.endswith("'")
        ):
            normalized_path = normalized_path[1:-1].strip()

        expanded_path = os.path.expandvars(normalized_path)
        src_path = Path(expanded_path).expanduser()

        path_candidates: List[Path] = []

        def add_candidate(candidate: Path) -> None:
            if candidate not in path_candidates:
                path_candidates.append(candidate)

        add_candidate(src_path)
        if not src_path.is_absolute():
            add_candidate(Path.cwd() / src_path)
            add_candidate(Path.home() / src_path)

        src_resolved: Optional[Path] = None
        for candidate in path_candidates:
            try:
                resolved = candidate.resolve(strict=False)
        except OSError:
                resolved = candidate
            if resolved.exists():
                src_resolved = resolved
                break

        if src_resolved is None:
            self.add_form_error = "Could not find a file at that path."
            self.add_form_status = ""
            self.add_form_focus = 0
            self.add_form_error_field = "path"
            return

        if not src_resolved.is_file():
            self.add_form_error = "The selected path must be a file."
            self.add_form_status = ""
            self.add_form_focus = 0
            self.add_form_error_field = "path"
            return

        if not os.access(src_resolved, os.R_OK):
            self.add_form_error = "Pypify cannot read that file. Check permissions."
            self.add_form_status = ""
            self.add_form_focus = 0
            self.add_form_error_field = "path"
            return

        ext = src_resolved.suffix.lower()
        if ext not in VALID_VIDEO_EXTENSIONS:
            exts = ", ".join(sorted(VALID_VIDEO_EXTENSIONS))
            self.add_form_error = f"Unsupported format. Use one of: {exts}"
            self.add_form_status = ""
            self.add_form_focus = 0
            self.add_form_error_field = "path"
            return

        name = name_text or src_resolved.stem
        artist = artist_text or "Unknown Artist"

        songs_root = SONGS_DIR.resolve()
            dest_path = src_resolved
        copied_new_file = False
        if src_resolved.parent != songs_root:
            dest_path = songs_root / src_resolved.name
            counter = 1
            while dest_path.exists():
                dest_path = songs_root / f"{src_resolved.stem}_{counter}{src_resolved.suffix}"
                counter += 1
            try:
                shutil.copy2(src_resolved, dest_path)
                copied_new_file = True
            except Exception as exc:
                self.add_form_error = f"Unable to copy file: {exc}"
                self.add_form_status = ""
                self.add_form_error_field = None
                return

        if self.add_form_mode == "edit":
            old_song = self.edit_target_song
            if old_song is None:
                self.add_form_error = "No song selected for editing."
                self.add_form_status = ""
                self.add_form_error_field = None
                return

            old_video_path = old_song.video_path
            old_thumb = old_song.thumbnail_path
            old_audio = old_song.audio_path

            try:
                updated_song = Song(name=name, artist=artist, video_path=dest_path)
                updated_song.loop_enabled = old_song.loop_enabled
                updated_song.ensure_assets()
            except Exception as exc:
                if dest_path != old_video_path and dest_path != src_resolved:
                    try:
                        if dest_path.exists():
                            dest_path.unlink()
                    except Exception:
                        pass
                message = str(exc)
                if "No audio track found" in message:
                    self.add_form_error = "Cannot import this video because it does not contain an audio track."
                else:
                    self.add_form_error = f"Unable to prepare media: {message}"
                self.add_form_status = ""
                self.add_form_error_field = None
                return

            self._replace_song_instance(old_song, updated_song)
            self.edit_target_song = updated_song

            if old_video_path != updated_song.video_path and old_video_path.parent == songs_root:
                try:
                    if old_video_path.exists():
                        old_video_path.unlink()
                except Exception:
                    pass
            for old_asset, new_asset in (
                (old_thumb, updated_song.thumbnail_path),
                (old_audio, updated_song.audio_path),
            ):
                if old_asset != new_asset and old_asset.exists():
                    try:
                        old_asset.unlink()
                    except Exception:
                        pass

            save_song_data(DATA_PATH, self.songs)
            if self.search_query.strip() and not self.shuffle_active:
                self._apply_search(reset_visible=False)
            else:
                self._refresh_display_songs()
            self._ensure_song_visible(updated_song)
            self.add_form_status = "Song updated. Adjust fields or press Cancel."
            self.add_form_error = ""
            self.add_form_error_field = None
            self.add_form_fields = {
                "path": str(updated_song.video_path),
                "name": updated_song.name,
                "artist": updated_song.artist,
            }
            self.add_form_focus = 1
                return

        try:
            new_song = Song(name=name, artist=artist, video_path=dest_path)
            new_song.ensure_assets()
        except Exception as exc:
            if dest_path != src_resolved and copied_new_file:
                try:
                    if dest_path.exists():
                    dest_path.unlink()
                except Exception:
                    pass
            message = str(exc)
            if "No audio track found" in message:
                self.add_form_error = "Cannot import this video because it does not contain an audio track."
            else:
                self.add_form_error = f"Unable to prepare media: {message}"
            self.add_form_status = ""
            self.add_form_error_field = None
            return

        self.songs.append(new_song)
        save_song_data(DATA_PATH, self.songs)
        self.shuffle_active = False
        self.shuffle_queue.clear()
        self._apply_search(reset_visible=False)
        self._ensure_song_visible(new_song)

        self.add_form_status = "Song imported. Enter another or press Cancel."
        self.add_form_error = ""
        self.add_form_fields = {"path": "", "name": "", "artist": ""}
        self.add_form_focus = 0
        self.add_form_error_field = None

    def _replace_song_instance(self, old_song: Song, new_song: Song) -> None:
        def replace(collection: List[Song]) -> None:
            for idx, item in enumerate(collection):
                if item is old_song:
                    collection[idx] = new_song

        replace(self.songs)
        replace(self.filtered_songs)
        replace(self.display_songs)
        self.shuffle_queue = [new_song if item is old_song else item for item in self.shuffle_queue]

        if self.current_song is old_song:
            was_playing = not self.is_paused
            position = self._current_playback_time()
            self._start_song(new_song, position, play_immediately=was_playing)
        else:
            if self.video_session.song is old_song:
                try:
                    self.video_session.load(new_song)
                except Exception as exc:
                    print(f"Unable to prepare video playback: {exc}", file=sys.stderr)

        if self.dragging_progress_card and self.dragging_progress_card.song is old_song:
            self.dragging_progress_card = None

        self._layout_cards()

    def _close_add_form(self) -> None:
        self.add_form_active = False
        self.add_form_mode = "add"
        self.edit_target_song = None
        self.add_form_fields = {"path": "", "name": "", "artist": ""}
        self.add_form_focus = 0
        self.add_form_error = ""
        self.add_form_status = ""
        self.add_form_error_field = None
        self.add_form_field_rects.clear()
        self.add_form_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_submit_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_cancel_rect = pygame.Rect(0, 0, 0, 0)
        self.dragging_volume = False
        self.dragging_progress_card = None
        self.dragging_fullscreen_progress = False
        try:
            pygame.key.stop_text_input()
        except Exception:
            pass

    def _render_fullscreen(self, mouse_pos: Tuple[int, int]) -> None:
        self.display.fill((5, 6, 15))
        self.link_rect = pygame.Rect(0, 0, 0, 0)
        video_surface = None
        if self.current_song and self.video_session.song == self.current_song:
            try:
                video_surface = self.video_session.get_surface_at(self._current_playback_time())
            except Exception:
                video_surface = None

        display_rect = self.display.get_rect()

        if video_surface is not None:
            target_rect = display_rect.inflate(-80, -80)
            frame = self._scale_surface(video_surface, target_rect.size)
            self.display.blit(frame, frame.get_rect(center=target_rect.center))
        else:
            message = "Play a song to view its video"
            msg_surface = self.header_font.render(message, True, TEXT_COLOR)
            self.display.blit(msg_surface, msg_surface.get_rect(center=display_rect.center))

        exit_rect = pygame.Rect(display_rect.right - 160, 30, 120, 44)
        self.fullscreen_exit_rect = exit_rect
        self._draw_button(self.display, exit_rect, "Back", hovered=exit_rect.collidepoint(mouse_pos))

        if self.current_song:
            info_text = f"{self.current_song.name} — {self.current_song.artist}"
            info_surface = self.status_font.render(info_text, True, TEXT_COLOR)
            self.display.blit(info_surface, (CARD_MARGIN_X, display_rect.bottom - info_surface.get_height() - 40))
            progress = self._song_progress(self.current_song)
            bar_width = int(display_rect.width * 0.6)
            progress_rect = pygame.Rect(
                display_rect.centerx - bar_width // 2,
                display_rect.bottom - 140,
                bar_width,
                PROGRESS_HEIGHT,
            )
            self.fullscreen_progress_rect = progress_rect
            pygame.draw.rect(self.display, (40, 45, 65), progress_rect, border_radius=PROGRESS_HEIGHT // 2)
            fill_width = int(progress_rect.width * progress)
            if fill_width > 0:
                pygame.draw.rect(
                    self.display,
                    PRIMARY_ACTIVE,
                    pygame.Rect(progress_rect.x, progress_rect.y, fill_width, progress_rect.height),
                    border_radius=PROGRESS_HEIGHT // 2,
                )

            elapsed = self._current_playback_time()
            duration = max(self.current_song.duration, 1.0)
            time_text = f"{int(elapsed // 60):02d}:{int(elapsed % 60):02d} / {int(duration // 60):02d}:{int(duration % 60):02d}"
            time_surface = self.small_font.render(time_text, True, TEXT_COLOR)
            self.display.blit(time_surface, (progress_rect.centerx - time_surface.get_width() // 2, progress_rect.y - time_surface.get_height() - 8))

            controls_y = display_rect.bottom - 100
            play_rect = pygame.Rect(display_rect.centerx - 160, controls_y, 140, 48)
            next_rect = pygame.Rect(display_rect.centerx + 20, controls_y, 140, 48)
            self.fullscreen_play_rect = play_rect
            self.fullscreen_next_rect = next_rect
            play_label = "Play" if self.is_paused else "Pause"
            self._draw_button(
                self.display,
                play_rect,
                play_label,
                active=not self.is_paused,
                hovered=play_rect.collidepoint(mouse_pos),
            )
            self._draw_button(self.display, next_rect, "Next", hovered=next_rect.collidepoint(mouse_pos))
        else:
            self.fullscreen_play_rect = pygame.Rect(0, 0, 0, 0)
            self.fullscreen_next_rect = pygame.Rect(0, 0, 0, 0)
            self.fullscreen_progress_rect = pygame.Rect(0, 0, 0, 0)

    def _scale_surface(self, surface: pygame.Surface, target_size: Tuple[int, int]) -> pygame.Surface:
        target_w, target_h = target_size
        src_w, src_h = surface.get_size()
        if src_w == 0 or src_h == 0:
            return surface
        scale = min(target_w / src_w, target_h / src_h)
        new_size = (max(1, int(src_w * scale)), max(1, int(src_h * scale)))
        return pygame.transform.smoothscale(surface, new_size)

    def _song_progress(self, song: Optional[Song]) -> float:
        if song is None or song.duration <= 0:
            return 0.0
        if song != self.current_song:
            return 0.0
        position = self._current_playback_time()
        return max(0.0, min(1.0, position / song.duration))

    def _current_playback_time(self) -> float:
        if self.current_song is None:
            return 0.0
        if self.is_paused:
            return min(self.play_start_position, self.current_song.duration)
        elapsed = (pygame.time.get_ticks() - self.play_start_ticks) / 1000.0
        return min(self.current_song.duration, self.play_start_position + elapsed)

    def _clamp_song_position(self, song: Song, position: float) -> float:
        if song.duration <= 0:
            return 0.0
        upper_bound = max(0.0, song.duration - 0.05)
        return max(0.0, min(position, upper_bound))

    def _start_song(self, song: Song, start_position: float, play_immediately: bool) -> None:
        clamped_position = self._clamp_song_position(song, start_position)
        if not song.audio_path.exists():
            song.ensure_assets()
        try:
            if pygame.mixer.music.get_busy():
                self.suppress_end_event = True
            pygame.mixer.music.set_endevent()
            pygame.mixer.music.stop()
            pygame.event.clear(MUSIC_END_EVENT)
            pygame.mixer.music.set_endevent(MUSIC_END_EVENT)
            pygame.mixer.music.load(str(song.audio_path))
            pygame.mixer.music.play(loops=0, start=clamped_position)
        except pygame.error as exc:
            print(f"Unable to load audio: {exc}", file=sys.stderr)
            return

        pygame.mixer.music.set_volume(self.volume)
        self.current_song = song
        self.play_start_position = clamped_position
        self.play_start_ticks = pygame.time.get_ticks()
        if play_immediately:
            self.is_paused = False
        else:
            self.is_paused = True
            pygame.mixer.music.pause()

        if song in self.shuffle_queue:
            self.shuffle_queue = [item for item in self.shuffle_queue if item != song]

        try:
            self.video_session.load(song)
        except Exception as exc:
            print(f"Unable to prepare video playback: {exc}", file=sys.stderr)
        finally:
            self.suppress_end_event = False

    def seek_song(self, song: Song, ratio: float) -> None:
        ratio = max(0.0, min(1.0, ratio))
        target_position = ratio * (song.duration if song.duration > 0 else 0.0)
        if song != self.current_song:
            self.shuffle_active = False
            self.shuffle_queue.clear()
        was_paused = self.is_paused if song == self.current_song else False
        self._start_song(song, target_position, play_immediately=not was_paused)

    def _toggle_song_pause(self, song: Song) -> None:
        if self.current_song != song:
            self.shuffle_active = False
            self.shuffle_queue.clear()
            self.play_song(song)
            return
        if self.is_paused:
            pygame.mixer.music.unpause()
            self.play_start_ticks = pygame.time.get_ticks()
            self.is_paused = False
        else:
            elapsed = (pygame.time.get_ticks() - self.play_start_ticks) / 1000.0
            self.play_start_position = min(self.current_song.duration, self.play_start_position + elapsed)
            pygame.mixer.music.pause()
            self.is_paused = True

    def _handle_card_control(self, song: Song, control: str) -> None:
        if control == "play":
            self._toggle_song_pause(song)
        elif control == "loop":
            song.loop_enabled = not song.loop_enabled
        elif control == "fullscreen":
            if self.current_song != song:
                self.play_song(song)
            self.toggle_fullscreen(True)

    def play_song(self, song: Song, *, preserve_queue: bool = False) -> None:
        if not preserve_queue:
            self.shuffle_active = False
            self.shuffle_queue.clear()
        self._ensure_song_visible(song)
        self._start_song(song, 0.0, play_immediately=True)
        self.dragging_progress_card = None
        self.dragging_fullscreen_progress = False

    def stop_song(self) -> None:
        if pygame.mixer.music.get_busy():
            self.suppress_end_event = True
        pygame.mixer.music.set_endevent()
        pygame.mixer.music.stop()
        pygame.event.clear(MUSIC_END_EVENT)
        pygame.mixer.music.set_endevent(MUSIC_END_EVENT)
        self.is_paused = False
        self.current_song = None
        self.play_start_ticks = 0
        self.play_start_position = 0.0
        self.shuffle_active = False
        self.shuffle_queue.clear()
        self.video_session.unload()
        self.video_fullscreen = False
        self.fullscreen_exit_rect = pygame.Rect(0, 0, 0, 0)
        self.fullscreen_play_rect = pygame.Rect(0, 0, 0, 0)
        self.fullscreen_next_rect = pygame.Rect(0, 0, 0, 0)
        self.suppress_end_event = False

    def start_shuffle(self) -> None:
        base = self._search_base()
        if not base:
            return
        order = base[:]
        random.shuffle(order)
        self.shuffle_active = True
        self.shuffle_queue = order[1:]
        self.filtered_songs = order
        self.visible_song_limit = min(PAGE_SIZE, len(order))
        self._refresh_display_songs()
        self.play_song(order[0], preserve_queue=True)

    def _handle_song_end(self) -> None:
        if self.suppress_end_event:
            self.suppress_end_event = False
            return
        if self.current_song and self.current_song.loop_enabled:
            self.play_song(self.current_song, preserve_queue=True)
            return
        if self.shuffle_queue:
            next_song = self.shuffle_queue.pop(0)
            self.play_song(next_song, preserve_queue=True)
            return
        if self.shuffle_active:
            self.shuffle_active = False
            self._apply_search(reset_visible=False)
        self.play_next_song()

    def play_next_song(self) -> None:
        if not self.filtered_songs:
            self.stop_song()
            return
        if self.current_song is None:
            self.play_song(self.filtered_songs[0], preserve_queue=True)
            return
        try:
            idx = self.filtered_songs.index(self.current_song)
        except ValueError:
            self.play_song(self.filtered_songs[0], preserve_queue=True)
            return
        next_idx = idx + 1
        if next_idx >= len(self.filtered_songs):
            if not self.shuffle_active:
                self.stop_song()
                return
            next_idx = 0
        if next_idx < 0 or next_idx >= len(self.filtered_songs):
            self.stop_song()
            return
        self.play_song(self.filtered_songs[next_idx], preserve_queue=True)

    def delete_song(self, song: Song) -> None:
        if song == self.current_song:
            self.stop_song()
        if self.dragging_progress_card and self.dragging_progress_card.song == song:
            self.dragging_progress_card = None
        for path in [song.audio_path, song.thumbnail_path, song.video_path]:
            try:
                if path.exists():
                    path.unlink()
            except Exception:
                pass
        if song in self.songs:
            self.songs.remove(song)
        if song in self.shuffle_queue:
            self.shuffle_queue = [item for item in self.shuffle_queue if item != song]
        save_song_data(DATA_PATH, self.songs)
        self._apply_search(reset_visible=False)

    def open_add_song_dialog(self) -> None:
        if self.add_form_active:
            return
        self.add_form_active = True
        self.add_form_mode = "add"
        self.edit_target_song = None
        self.add_form_fields = {"path": "", "name": "", "artist": ""}
        self.add_form_focus = 0
        self.add_form_error = ""
        self.add_form_status = ""
        self.add_form_field_rects.clear()
        self.add_form_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_submit_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_cancel_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_error_field = None
        self.dragging_volume = False
        self.dragging_progress_card = None
        self.dragging_fullscreen_progress = False
        self.search_active = False
        try:
            pygame.key.start_text_input()
        except Exception:
            pass

    def open_edit_song_dialog(self, song: Song) -> None:
        self.add_form_active = True
        self.add_form_mode = "edit"
        self.edit_target_song = song
        self.add_form_fields = {
            "path": str(song.video_path),
            "name": song.name,
            "artist": song.artist,
        }
        self.add_form_focus = 1
        self.add_form_error = ""
        self.add_form_status = ""
        self.add_form_field_rects.clear()
        self.add_form_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_submit_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_cancel_rect = pygame.Rect(0, 0, 0, 0)
        self.add_form_error_field = None
        self.dragging_volume = False
        self.dragging_progress_card = None
        self.dragging_fullscreen_progress = False
        self.search_active = False
        self.edit_mode = False
        self.delete_mode = False
        try:
            pygame.key.start_text_input()
        except Exception:
            pass

    def toggle_fullscreen(self, enable: Optional[bool] = None) -> None:
        if enable is None:
            enable = not self.video_fullscreen
        if enable and not self.current_song:
            return
        self.video_fullscreen = enable
        if not enable:
            self.dragging_fullscreen_progress = False
        else:
            self.dragging_progress_card = None

    def _status_line(self) -> str:
        if not self.current_song:
            return "Ready — choose a song or shuffle the deck."
        state = "Paused" if self.is_paused else "Playing"
        elapsed = self._current_playback_time()
        duration = max(self.current_song.duration, 1.0)
        def format_time(value: float) -> str:
            minutes = int(value // 60)
            seconds = int(value % 60)
            return f"{minutes:02d}:{seconds:02d}"
        return f"{state}: {self.current_song.name} · {format_time(elapsed)} / {format_time(duration)}"


def main() -> None:
    app = PypifyApp()
    app.run()


if __name__ == "__main__":
    main()

