"""Where the generator reads and writes.

generate.py sets these environment variables before anything else is imported, so the
render processes it starts inherit them:

  TGMUSIC_WORK   the work folder: the audio extracted from the player's ROM (MIDI, sheets,
                 samples) and the note lists made from it. Never inside the repository.
  TGMUSIC_LIBS   the folder with the instrument libraries, one sub-folder per library
                 (see libraries.py), plus the instrument calibration caches.
"""
import json
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
SR = 48000


def _env_dir(name):
    v = os.environ.get(name)
    if not v:
        raise RuntimeError(f"{name} is not set: run tools/music/generate.py")
    return Path(v)


def work_dir():
    """The work folder (TGMUSIC_WORK)."""
    return _env_dir("TGMUSIC_WORK")


def libs_dir():
    """The instrument library folder (TGMUSIC_LIBS)."""
    return _env_dir("TGMUSIC_LIBS")


def lib(name):
    """The folder of one instrument library (libraries.LIBRARIES key)."""
    return libs_dir() / name


def audio_dir():
    """tools/audio's output for the player's ROM (rebuild/<song>/<song>.mid, sheet.json, samples/)."""
    return work_dir() / "audio"


def events_dir():
    """The note lists made from the MIDI (parse_midi.py)."""
    return work_dir() / "events"


def rel_key(path):
    """A library file as 'library:relative/path', so caches do not depend on where the libraries are."""
    p = str(path)
    root = str(libs_dir()) + os.sep
    if p.startswith(root):
        name, _, rest = p[len(root):].partition(os.sep)
        return f"{name}:{rest.replace(os.sep, '/')}"
    return p


class JsonCache:
    """A small JSON table: shipped values (data/<name>) first, then values measured on this
    machine (<libs>/calibration/<name>). Several render processes may add entries at once,
    so writes re-read the file and replace it atomically."""

    def __init__(self, name):
        self.name = name
        self._shipped = None
        self._local = None

    def _paths(self):
        return DATA / self.name, libs_dir() / "calibration" / self.name

    def _read(self, p):
        try:
            return json.loads(p.read_text())
        except (OSError, ValueError):
            return {}

    def get(self, key):
        shipped, local = self._paths()
        if self._shipped is None:
            self._shipped = self._read(shipped)
        if key in self._shipped:
            return self._shipped[key]
        if self._local is None or key not in self._local:
            self._local = self._read(local)
        return self._local.get(key)

    def put(self, key, value):
        _, local = self._paths()
        local.parent.mkdir(parents=True, exist_ok=True)
        cur = self._read(local)
        cur[key] = value
        tmp = local.with_suffix(f".{os.getpid()}.tmp")
        tmp.write_text(json.dumps(cur, indent=1, sort_keys=True))
        os.replace(tmp, local)
        self._local = cur
