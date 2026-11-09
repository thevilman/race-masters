#!/usr/bin/env python3
"""Install the modern-music generator's Python, pinned and hash-checked, in a per-user folder.

    python3 tools/music/setup_tools.py [--root DIR] [--progress-json]

The launcher's "Build music" runs this first; it can also be run by hand. Nothing needs to be
installed beforehand, and nothing is installed system-wide:

  Windows  the python.org embeddable Python 3.12 (pinned, SHA-256 checked) in <root>/python
           (the launcher unpacks it; run by hand, this script fetches it), with the packages
           installed into it. Needs the Microsoft Visual C++ Redistributable (x64), which most
           Windows PCs have: several packages (numba, llvmlite, pedalboard, tinysoundfont) use
           its MSVCP140.dll.
  Linux    a venv in <root>/venv from the system's Python 3.10, 3.11 or 3.12, made with
           --without-pip (pip comes from its pinned wheel, not ensurepip).

Then pip, from its pinned wheel (no get-pip.py), and every package in requirements.txt with
--require-hashes --no-deps --only-binary=:all: (wheels only, each checked against its pinned
SHA-256). <root> defaults to %LOCALAPPDATA%\\race-masters\\music-tools on Windows,
~/.local/share/race-masters/music-tools elsewhere. Prints (or, with --progress-json, sends as
{"ev":"ready","python":...}) the Python to run generate.py with. Run it again at any time: a
finished install is checked and kept (a few seconds).

Only the standard library is used here, so any Python 3.8+ can run it.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import urllib.request
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
LOCK = HERE / "requirements.txt"
UA = "tgmusic-fetch/1"  # a neutral User-Agent: no personal data in requests

PIP = dict(version="25.2",
           url="https://files.pythonhosted.org/packages/b7/3f/945ef7ab14dc4f9d7f40288d2df998d1837ee0888ec3659c813487572faa/"
               "pip-25.2-py3-none-any.whl",
           sha256="6d67a2b4e7f14d8b31b8b52648866fa717f45a1eb70e83002f4331d07e953717", bytes=1752557)
# Runs pip from the wheel given first as a module (`-c PIP_FROM_WHEEL <wheel> <pip args>`): its
# sys.argv[0] is then pip/__main__.py, as with `python -m pip`, so pip on Windows lets it
# install pip (it refuses when sys.argv[0] is named pip).
PIP_FROM_WHEEL = ("import runpy, sys; sys.path.insert(0, sys.argv.pop(1)); "
                  "runpy.run_module('pip', run_name='__main__', alter_sys=True)")
# The python.org embeddable package (Windows). The launcher (src/music_build.h) pins the same.
PYTHON_EMBED = dict(version="3.12.10",
                    url="https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip",
                    sha256="4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3", bytes=11133606)
# What its python312._pth says: the standard library, the folder, the packages, and site (for
# pip). The launcher writes the same lines.
PTH = "python312.zip\n.\nLib\\site-packages\nimport site\n"
PTH_NAME = "python312._pth"
EMBED_STAMP = "race-masters-python.txt"

# Downloads, for what the launcher shows before asking (generate.py --write-consent), in MB:
# the wheels pip picks on each system (PyPI's sizes), pip, and Windows' embeddable Python.
TOOLS_MB = {"win32-x86_64": 132 + 2 + 11, "linux-x86_64": 148 + 2}
TOOLS_INSTALLED_MB = 500
PACKAGES = 21  # in requirements.txt
PY_RANGE = {"win32": ((3, 12), (3, 12)), "other": ((3, 10), (3, 12))}  # the wheels pinned
CHECK_IMPORT = ("import numpy, scipy, numba, llvmlite, pedalboard, pysfizz, tinysoundfont, soundfile, pyloudnorm, "
                "mido, imageio_ffmpeg, pytheory")
PYFLAGS = ["-B", "-s", "-E", "-X", "utf8"]  # as tgmusic/progress.py
# Shared libraries the Linux plug-ins load (Surge XT, Dragonfly Reverb), and Ubuntu's packages.
LINUX_LIBS = {"asound": "libasound2", "freetype": "libfreetype6", "GL": "libgl1", "X11": "libx11-6",
              "Xcursor": "libxcursor1", "Xext": "libxext6", "Xrandr": "libxrandr2"}
VCREDIST_URL = "https://aka.ms/vs/17/release/vc_redist.x64.exe"


class Failure(Exception):
    def __init__(self, code, text):
        super().__init__(text)
        self.code, self.text = code, text


class Cancel(Exception):
    pass


def data_root():
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
    else:
        xdg = os.environ.get("XDG_DATA_HOME")
        base = xdg if xdg and os.path.isabs(xdg) else str(Path.home() / ".local" / "share")
    return Path(base) / "race-masters"


class Out:
    def __init__(self, json_mode, log):
        self.json, self.log = json_mode, log
        self.cancelled = threading.Event()
        self._stdout = sys.stdout

    def say(self, text):
        self.log.write(text + "\n")
        self.log.flush()
        if not self.json:
            print(text, flush=True)

    def emit(self, ev, **kw):
        if not self.json:
            return
        try:
            self._stdout.write(json.dumps({"ev": ev, **{k: v for k, v in kw.items() if v is not None}},
                                          separators=(",", ":"), ensure_ascii=True) + "\n")
            self._stdout.flush()
        except (OSError, ValueError):
            self.cancelled.set()

    def step(self, step, i, text):
        self.say(text)
        self.emit("tools", step=step, i=i, of=5, text=text)

    def check(self):
        if self.cancelled.is_set():
            raise Cancel()

    def watch_stdin(self):
        # (standard library only, as this file: the Windows pipe polled, never read while empty;
        # an embeddable Python doesn't put the script's folder on sys.path)
        if str(HERE) not in sys.path:
            sys.path.insert(0, str(HERE))
        from tgmusic.progress import wait_for_cancel

        def run():
            wait_for_cancel()
            self.cancelled.set()
        threading.Thread(target=run, daemon=True).start()


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def download(url, dest, sha256, size, out, what):
    """url -> dest, checked against sha256 (a file already there and right is kept)."""
    if dest.exists() and sha256_file(dest) == sha256:
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    part = dest.with_name(dest.name + ".part")
    h = hashlib.sha256()
    got = 0
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=60) as r, \
                open(part, "wb") as f:
            total = int(r.headers.get("Content-Length") or size)
            while True:
                out.check()
                b = r.read(1 << 18)
                if not b:
                    break
                f.write(b)
                h.update(b)
                got += len(b)
                out.emit("download", lib=what, got=got, total=total)
    except OSError as e:
        raise Failure("network", f"Couldn't download {url}: {e}") from e
    if h.hexdigest() != sha256:
        part.unlink(missing_ok=True)
        raise Failure("hash", f"{dest.name}: its SHA-256 is {h.hexdigest()}, not the pinned {sha256} (removed; try again)")
    os.replace(part, dest)
    return dest


def run(cmd, out, check=True, capture=False):
    out.say("$ " + " ".join(f'"{c}"' if " " in str(c) else str(c) for c in cmd))
    kw = {}
    if os.name == "nt":
        kw["creationflags"] = getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)
    p = subprocess.Popen([str(c) for c in cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         stdin=subprocess.DEVNULL, text=True, encoding="utf-8", errors="replace", **kw)
    lines = []
    reader = threading.Thread(target=lambda: lines.extend(_pump(p.stdout, out)), daemon=True)
    reader.start()
    while p.poll() is None:
        if out.cancelled.wait(0.25):
            p.terminate()
            try:
                p.wait(10)
            except subprocess.TimeoutExpired:
                p.kill()
            raise Cancel()
    reader.join(5)
    if check and p.returncode:
        tail = "\n".join(lines[-8:])
        raise Failure("pip" if "pip" in " ".join(map(str, cmd)) else "other",
                      f"{Path(str(cmd[0])).name} failed (exit {p.returncode}): {tail}")
    return p.returncode, lines


def _pump(stream, out):
    """Copies a child's output to the log, sending pip's per-package steps as progress."""
    n = 0
    for line in stream:
        line = line.rstrip()
        out.log.write(line + "\n")
        if line.startswith(("Collecting ", "Processing ")):
            n += 1
            name = line.split()[1].split("==")[0]
            out.emit("tools", step="packages", i=4, of=5, pkg=name, n=n, count=PACKAGES,
                     text=f"Getting Python packages ({n} of {PACKAGES}: {name})")
        elif line.startswith("Installing collected packages"):
            out.emit("tools", step="packages", i=4, of=5, n=PACKAGES, count=PACKAGES, text="Installing the Python packages")
        yield line
    out.log.flush()


def py_version(exe):
    try:
        r = subprocess.run([str(exe), "-c", "import sys; print('%d.%d' % sys.version_info[:2])"],
                           capture_output=True, text=True, timeout=30)
        major, minor = r.stdout.strip().split(".")
        return int(major), int(minor)
    except (OSError, ValueError, subprocess.SubprocessError):
        return None


def in_range(v):
    lo, hi = PY_RANGE["win32" if os.name == "nt" else "other"]
    return v is not None and lo <= v <= hi


def find_python():
    """A Python the pinned wheels are for: this one, or python3.12 / 3.11 / 3.10 on the PATH."""
    if in_range(sys.version_info[:2]):
        return Path(sys.executable)
    names = ["python3.12"] if os.name == "nt" else ["python3.12", "python3.11", "python3.10"]
    for n in names:
        exe = shutil.which(n)
        if exe and in_range(py_version(exe)):
            return Path(exe)
    return None


def embedded_here():
    """True when this script runs in the launcher's embeddable Python (packages go into it)."""
    return os.name == "nt" and (Path(sys.prefix) / PTH_NAME).is_file()


def fetch_embedded(root, out):
    """Windows, run by hand: the embeddable Python, as the launcher would unpack it."""
    dest = root / "python"
    stamp = dest / EMBED_STAMP
    if (dest / "python.exe").is_file() and stamp.is_file() and stamp.read_text().strip() == PYTHON_EMBED["sha256"]:
        return dest / "python.exe"
    out.step("python", 1, f"Getting Python {PYTHON_EMBED['version']} (python.org embeddable package)")
    z = download(PYTHON_EMBED["url"], root / "downloads" / PYTHON_EMBED["url"].rsplit("/", 1)[-1],
                 PYTHON_EMBED["sha256"], PYTHON_EMBED["bytes"], out, "python")
    tmp = root / "python.tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    with zipfile.ZipFile(z) as zf:
        zf.extractall(tmp)
    (tmp / PTH_NAME).write_text(PTH)
    (tmp / EMBED_STAMP).write_text(PYTHON_EMBED["sha256"] + "\n")
    shutil.rmtree(dest, ignore_errors=True)
    os.replace(tmp, dest)
    z.unlink(missing_ok=True)
    return dest / "python.exe"


def check_vcredist():
    if os.name != "nt":
        return
    system = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
    if not (system / "msvcp140.dll").is_file():
        raise Failure("vcredist", "The Microsoft Visual C++ Redistributable (x64) is needed (several of the music "
                                  f"tools' packages use its MSVCP140.dll). Get it from Microsoft: {VCREDIST_URL} , "
                                  "install it, then try again.")


def check_linux_libs():
    if not sys.platform.startswith("linux"):
        return
    import ctypes.util
    missing = [pkg for lib, pkg in LINUX_LIBS.items() if ctypes.util.find_library(lib) is None]
    if missing:
        raise Failure("syslibs", "The synthesizer plug-ins need these system libraries (Ubuntu / Debian package names): "
                                 f"sudo apt install {' '.join(missing)}")


def lock_hash():
    return hashlib.sha256(LOCK.read_bytes()).hexdigest()


def target_python(root, out):
    """The Python the packages go into, made if needed: (python, how)."""
    if embedded_here():
        return Path(sys.executable), "embedded"
    if os.name == "nt":
        found = find_python()
        if found is None:
            return fetch_embedded(root, out), "embedded"
    else:
        found = find_python()
        if found is None:
            raise Failure("python", "Python 3.10, 3.11 or 3.12 is needed (python3 here is "
                          f"{sys.version_info[0]}.{sys.version_info[1]}); install python3.12 and try again.")
    venv = root / "venv"
    exe = venv / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if exe.exists() and py_version(exe) == py_version(found):
        return exe, "venv"
    out.step("venv", 1, f"Making a Python environment ({found})")
    shutil.rmtree(venv, ignore_errors=True)
    run([found, "-m", "venv", "--without-pip", venv], out)
    if not exe.exists():
        raise Failure("python", f"{found} -m venv made no {exe}")
    return exe, "venv"


def setup(root, out):
    root.mkdir(parents=True, exist_ok=True)
    check_vcredist()
    check_linux_libs()
    py, how = target_python(root, out)
    stamp = root / "tools.json"
    want = {"lock": lock_hash(), "python": str(py), "pip": PIP["version"]}
    try:
        have = json.loads(stamp.read_text())
    except (OSError, ValueError):
        have = None
    if have == want:
        out.step("check", 5, "Checking the music tools")
        code, lines = run([py, *PYFLAGS, "-c", CHECK_IMPORT], out, check=False)
        if code == 0:
            return py
        out.say("the installed packages don't load; installing them again")
    stamp.unlink(missing_ok=True)

    out.step("pip", 3, f"Getting pip {PIP['version']}")
    code, lines = run([py, *PYFLAGS, "-m", "pip", "--version"], out, check=False)
    if code or not lines or f"pip {PIP['version']} " not in lines[-1] + " ":
        whl = download(PIP["url"], root / "downloads" / PIP["url"].rsplit("/", 1)[-1], PIP["sha256"], PIP["bytes"],
                       out, "pip")
        # pip runs from its own wheel to install itself: no get-pip.py, no ensurepip. As a
        # module (PIP_FROM_WHEEL), as `python -m pip` would: started as `python <wheel>/pip`,
        # pip on Windows refuses to install pip ("To modify pip, please run ... -m pip").
        run([py, *PYFLAGS, "-c", PIP_FROM_WHEEL, whl, "--isolated", "install", "--no-index", "--no-deps",
             "--no-warn-script-location", "--disable-pip-version-check", "--force-reinstall", whl], out)

    out.step("packages", 4, f"Getting Python packages (0 of {PACKAGES})")
    cache = root / "pip-cache"
    run([py, *PYFLAGS, "-m", "pip", "--isolated", "install", "--no-deps", "--only-binary=:all:", "--require-hashes",
         "--no-input", "--disable-pip-version-check", "--no-warn-script-location", "--progress-bar", "off",
         "--force-reinstall", "--index-url", "https://pypi.org/simple", "--cache-dir", cache, "-r", LOCK], out)

    out.step("check", 5, "Checking the music tools")
    code, lines = run([py, *PYFLAGS, "-c", CHECK_IMPORT], out, check=False)
    if code:
        tail = "\n".join(lines[-4:])
        if os.name == "nt" and "DLL load failed" in tail:
            raise Failure("vcredist", "A package couldn't load a Windows library: install the Microsoft Visual C++ "
                                      f"Redistributable (x64) from {VCREDIST_URL} , then try again. ({tail})")
        raise Failure("tools", f"The music tools don't load: {tail}")
    stamp.write_text(json.dumps(want, indent=1))
    shutil.rmtree(cache, ignore_errors=True)
    shutil.rmtree(root / "downloads", ignore_errors=True)
    return py


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", help="where the tools go (default: <user data>/race-masters/music-tools)")
    ap.add_argument("--progress-json", action="store_true",
                    help="progress as JSON lines on standard output (for the launcher); stop on 'cancel' or end of input")
    args = ap.parse_args()
    root = (Path(args.root) if args.root else data_root() / "music-tools").resolve()
    logs = root / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    log_path = logs / time.strftime("setup-%Y%m%d-%H%M%S.log")
    for old in sorted(logs.glob("setup-*.log"))[:-4]:
        old.unlink(missing_ok=True)
    with open(log_path, "w", encoding="utf-8") as log:
        out = Out(args.progress_json, log)
        if args.progress_json:
            out.watch_stdin()
        out.emit("stage", stage="tools", n=1, of=1, text="Setting up the music tools")
        try:
            py = setup(root, out)
        except Cancel:
            out.emit("cancelled")
            out.say("stopped")
            return 3
        except Failure as e:
            out.say(f"error ({e.code}): {e.text}")
            out.emit("error", code=e.code, text=e.text, log=str(log_path))
            if not args.progress_json:
                print(e.text, file=sys.stderr)
            return 1
        except OSError as e:
            code = "disk" if getattr(e, "errno", None) == 28 else "other"
            out.say(f"error: {e}")
            out.emit("error", code=code, text=str(e), log=str(log_path))
            return 1
        out.say(f"ready: {py}")
        out.emit("ready", python=str(py))
        if not args.progress_json:
            print(py)
    return 0


if __name__ == "__main__":
    sys.exit(main())
