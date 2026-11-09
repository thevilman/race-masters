"""The music generator's parts that need no third-party packages and no network: progress
lines, cancelling, resumable downloads, the files fetched one by one or out of a zip with
range requests (against a local server), unpacking, the pins (requirements.txt, the
launcher's Python), the list of instrument files (every instrument the code names is
downloaded), the .sfz reader, consent.txt, the release layout, how many tracks and threads at
once, the log shared with the render processes. (Some checks run only where the render
packages are installed: that the .sfz reader agrees with sfizz itself, and a track's parts
played in threads and mixed in their order with the same numbers.)

    python3 -B -m unittest discover -s tools/music/tests     (ctest: music_tools)
"""
import hashlib
import http.server
import io
import json
import math
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import unittest.mock
import urllib.error
import urllib.parse
import wave
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True
MUSIC = Path(__file__).resolve().parents[1]
REPO = MUSIC.parents[1]
sys.path.insert(0, str(MUSIC))

import generate as G  # noqa: E402
import setup_tools as S  # noqa: E402
from tgmusic import catalog as C  # noqa: E402
from tgmusic import extract as X  # noqa: E402
from tgmusic import libraries as L  # noqa: E402
from tgmusic import progress as P  # noqa: E402
from tgmusic import sfz as Z  # noqa: E402


class ProgressLines(unittest.TestCase):
    def test_flat_json_lines(self):
        buf = io.StringIO()
        pr = P.Progress(buf)
        pr.emit("track", i=3, of=149, label="rio D (race)", secs=150.123456, skipped=None, path=Path("a/b"), ok=True)
        line = buf.getvalue()
        self.assertTrue(line.endswith("\n") and line.count("\n") == 1)
        obj = json.loads(line)
        self.assertEqual(obj, {"ev": "track", "i": 3, "of": 149, "label": "rio D (race)", "secs": 150.123,
                               "path": str(Path("a/b")), "ok": True})
        for v in obj.values():  # flat: what the launcher's parser reads
            self.assertIsInstance(v, (str, int, float, bool))
        self.assertTrue(all(ord(c) < 128 for c in line))

    def test_non_ascii_escaped(self):
        buf = io.StringIO()
        P.Progress(buf).emit("plan", out="C:\\Users\\Zoë\\audio-modern")
        self.assertIn("\\u00eb", buf.getvalue())

    def test_reader_gone_cancels(self):
        class Broken(io.StringIO):
            def write(self, s):
                raise BrokenPipeError()
        pr = P.Progress(Broken())
        pr.emit("plan", tracks=1)
        self.assertTrue(pr.cancelled.is_set())
        self.assertFalse(pr.json)
        with self.assertRaises(P.Cancelled):
            pr.check()

    def test_plain_mode_is_silent(self):
        P.Progress(None).emit("plan", tracks=1)  # no stream: nothing, no error

    def test_eta_words(self):
        self.assertEqual(P.fmt_eta(None), "unknown")
        self.assertEqual(P.fmt_eta(30), "under a minute")
        self.assertEqual(P.fmt_eta(12 * 60), "12 min")
        self.assertEqual(P.fmt_eta(3 * 3600 + 600), "3 h 10 min")

    def test_eta_model(self):
        e = P.Eta(download_bytes=8e9, notes=True, tracks=10, jobs=2)
        self.assertAlmostEqual(e.seconds(), 1000 + 8 * 40 + 660 + 10 * P.Eta.TRACK_S / 2)
        e.rate, e.download_left, e.unpack_left_gb, e.notes_left = 16e6, 0, 0, 0
        e.track_times = [100, 300]
        self.assertAlmostEqual(e.seconds(), 10 * 200 / 2)


class Cancelling(unittest.TestCase):
    CHILD = ("import sys, time; sys.path.insert(0, sys.argv[1]); from tgmusic import progress as P\n"
             "pr = P.Progress(sys.stdout); pr.watch_stdin(); pr.emit('plan', tracks=1)\n"
             "t0 = time.time()\n"
             "while not pr.cancelled.is_set() and time.time() - t0 < 20: time.sleep(0.05)\n"
             "pr.emit('cancelled') if pr.cancelled.is_set() else None\n"
             "sys.exit(3 if pr.cancelled.is_set() else 1)\n")

    def run_child(self, how):
        p = subprocess.Popen([sys.executable, "-B", "-c", self.CHILD, str(MUSIC)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, text=True)
        self.assertEqual(json.loads(p.stdout.readline())["ev"], "plan")
        t0 = time.time()
        if how == "line":
            p.stdin.write("cancel\n")
            p.stdin.flush()
        else:
            p.stdin.close()  # the launcher went away
        out = p.stdout.read()
        p.wait(10)
        p.stdout.close()
        if not p.stdin.closed:
            p.stdin.close()
        self.assertLess(time.time() - t0, 5)
        self.assertEqual(p.returncode, P.EXIT_CANCELLED)
        self.assertEqual(json.loads(out.strip())["ev"], "cancelled")

    def test_cancel_line(self):
        self.run_child("line")

    def test_end_of_input(self):
        self.run_child("eof")

    @unittest.skipUnless(os.name == "nt", "the Windows pipe")
    def test_watcher_leaves_the_pipe_free(self):
        """While the watcher waits, the rest of the process can still use standard input's
        handle: a read left pending on a Windows pipe makes GetFileType on it wait, which is
        what loading numpy's OpenBLAS does (the build hung there when the launcher ran it)."""
        child = ("import sys, time, ctypes, msvcrt; sys.path.insert(0, sys.argv[1]); from tgmusic import progress as P\n"
                 "pr = P.Progress(sys.stdout); pr.watch_stdin(); time.sleep(1)\n"
                 "h = ctypes.c_void_p(msvcrt.get_osfhandle(sys.stdin.fileno()))\n"
                 "print(ctypes.windll.kernel32.GetFileType(h), flush=True)\n")
        p = subprocess.Popen([sys.executable, "-B", "-c", child, str(MUSIC)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, text=True)
        try:
            p.wait(15)
        except subprocess.TimeoutExpired:
            p.kill()
            self.fail("GetFileType on standard input waited for the cancel watcher")
        finally:
            p.stdin.close()
        self.assertEqual(p.stdout.read().strip(), "3")  # FILE_TYPE_PIPE
        p.stdout.close()

    def test_run_cancellable_stops_child(self):
        pr = P.Progress(None)
        with tempfile.TemporaryDirectory() as d:
            marker = Path(d) / "still-running"
            code = "import time, pathlib, sys\nfor i in range(100):\n    time.sleep(0.1)\npathlib.Path(sys.argv[1]).write_text('x')"
            threading.Timer(0.5, pr.cancelled.set).start()
            t0 = time.time()
            with self.assertRaises(P.Cancelled):
                P.run_cancellable([sys.executable, "-c", code, marker], pr, log=Path(d) / "log.txt")
            self.assertLess(time.time() - t0, 8)
            time.sleep(0.3)
            self.assertFalse(marker.exists())

    def test_run_cancellable_heartbeat_and_code(self):
        pr = P.Progress(None)
        beats = []
        code = P.run_cancellable([sys.executable, "-c", "import time, sys; time.sleep(0.7); sys.exit(5)"], pr,
                                 heartbeat=beats.append, every=0.2)
        self.assertEqual(code, 5)
        self.assertGreaterEqual(len(beats), 1)

    def test_one_build_per_cache(self):
        with tempfile.TemporaryDirectory() as d:
            a = P.hold_lock(Path(d) / "build.lock")
            self.assertIsNotNone(a)
            self.assertIsNone(P.hold_lock(Path(d) / "build.lock"))
            a.close()
            b = P.hold_lock(Path(d) / "build.lock")
            self.assertIsNotNone(b)
            b.close()


class Concurrency(unittest.TestCase):
    """How many tracks at once, how many threads each, and when another may start."""

    def test_auto_choice(self):
        self.assertEqual(P.concurrency("auto", "auto", cpus=12, total_mb=16000), (6, 2))  # 2P + 8E cores, 16 GB
        self.assertEqual(P.concurrency("auto", "auto", cpus=8, total_mb=8000), (4, 2))
        self.assertEqual(P.concurrency("auto", "auto", cpus=4, total_mb=16000), (2, 2))
        self.assertEqual(P.concurrency("auto", "auto", cpus=16, total_mb=6500), (3, 4))  # what the memory holds
        self.assertEqual(P.concurrency("auto", "auto", cpus=2, total_mb=4000), (1, 1))
        self.assertGreaterEqual(P.concurrency("auto", "auto", cpus=12)[0], 1)  # this computer's memory
        self.assertEqual(P.concurrency("3", "auto", cpus=12), (3, 4))  # asked for: as asked
        self.assertEqual(P.concurrency(2, 1, cpus=12), (2, 1))

    def test_launcher_agrees(self):
        """The launcher picks --jobs as the generator would (src/music_build.c tg_mb_auto_jobs)."""
        c = (REPO / "src" / "music_build.c").read_text()
        m = re.search(r"\(ramMb - (\d+)\) / (\d+)", c)
        self.assertIsNotNone(m)
        self.assertEqual((int(m.group(1)), int(m.group(2))), (P.SPARE_MB, P.TRACK_MB))
        self.assertIn("int byCpu = cpus / 2;", c)

    def test_room_for_track(self):
        # A third of what the memory holds always runs, at least two (where --jobs allows them).
        self.assertEqual(P.always_at_once(16000), 3)
        self.assertEqual(P.always_at_once(8000), 2)
        self.assertEqual(P.always_at_once(64000), 13)
        self.assertGreaterEqual(P.always_at_once(), 2)
        self.assertTrue(P.room_for_track([], avail_mb=0, always=2))
        self.assertTrue(P.room_for_track([1], avail_mb=0, always=2))
        self.assertTrue(P.room_for_track([1, 2], avail_mb=0, always=3))
        # Beyond: a running track at its fullest takes nothing more; one starting may take up to TRACK_MB.
        need = P.TRACK_MB + P.SPARE_MB / 2
        full = {1: P.TRACK_MB + 300, 2: P.TRACK_MB}
        self.assertTrue(P.room_for_track([1, 2], avail_mb=need, sizes=full, always=2))
        self.assertFalse(P.room_for_track([1, 2], avail_mb=need, sizes={**full, 2: 100}, always=2))
        self.assertTrue(P.room_for_track([1, 2], avail_mb=need + P.TRACK_MB - 100, sizes={**full, 2: 100}, always=2))
        self.assertFalse(P.room_for_track([1, 2, 3], avail_mb=need + P.TRACK_MB, sizes={1: 0, 2: None, 3: 0},
                                          always=2))

    def test_memory_readable(self):
        total, avail = P.memory_mb()
        if total is None:
            self.skipTest("no memory figures on this system")
        self.assertGreater(total, 100)
        self.assertTrue(0 < avail <= total)
        now = P.process_mb(os.getpid())
        if now is not None:
            self.assertGreater(now, 1)
            self.assertGreaterEqual(P.peak_mb(), now * 0.5)
        self.assertIsNone(P.process_mb(2 ** 31 - 3))  # no such process

    def test_log_shared_with_children(self):
        """The generator writes the log through one handle and its render processes through
        another (their output): every line of both ends up in it, none over another (on
        Windows a file opened as Python opens it did lose lines so)."""
        child = "import sys\nfor i in range(20):\n    print(f'{sys.argv[1]} {i}', flush=True)\n"
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "build.log"
            mine = P.open_log(path)
            theirs = P.open_log(path)
            want = []
            for k in range(3):
                for i in range(20):
                    mine.write(f"parent{k} {i}\n")
                    want.append(f"parent{k} {i}")
                subprocess.run([sys.executable, "-B", "-c", child, f"child{k}"], stdout=theirs, stderr=subprocess.STDOUT,
                               check=True)
                want += [f"child{k} {i}" for i in range(20)]
            mine.close()
            theirs.close()
            got = path.read_text(encoding="utf-8").splitlines()
            self.assertEqual(sorted(got), sorted(want))

    def test_full_speed_is_harmless(self):
        P.full_speed()
        p = subprocess.Popen([sys.executable, "-c", "pass"])
        P.full_speed(p)
        self.assertEqual(p.wait(10), 0)

    def test_render_all_waits_for_room(self):
        """render_all starts a track only while room() says the memory holds another."""
        with tempfile.TemporaryDirectory() as d:
            seen = []

            def room(pids):
                seen.append(len(pids))
                return len(pids) < 1  # memory for one at a time only

            fake = Path(d) / "fake.py"
            # A stand-in for generate.py --worker: writes its result, takes a moment.
            fake.write_text("import json, sys, time\ntime.sleep(0.3)\nr = sys.argv[sys.argv.index('--result') + 1]\n"
                            "open(r, 'w').write(json.dumps({'seconds': 0.3, 'mb': 1, 'error': None, 'fetched': 0}))\n")
            jobs = [("variation", s, "faithful") for s in ("01_title", "02_las_vegas", "03_hiroshima")]
            with unittest.mock.patch.object(G, "__file__", str(fake)), \
                    __import__("contextlib").redirect_stdout(io.StringIO()):
                failed, late = G.render_all(jobs, Path(d) / "out", ("ogg",), 3, Path(d) / "results", str, P.Progress(None),
                                            P.Eta(tracks=3, notes=False), 0, 3, None, room=room)
            self.assertEqual((failed, late), ([], 0))
            self.assertTrue(seen and max(seen) <= 1)


class RangeHandler(http.server.BaseHTTPRequestHandler):
    """Serves one file; honours Range + If-Range (or ignores ranges, or changes the file)."""
    data = b""
    etag = '"v1"'
    ranges = True
    stop_after = None  # bytes, then the connection is cut (a lost connection)
    requests = []
    files = {}  # path -> bytes, served instead of data

    def log_message(self, *a):
        pass

    def do_GET(self):
        cls = type(self)
        cls.requests.append(dict(self.headers))
        data = cls.files.get(self.path, cls.data)
        start = 0
        rng = self.headers.get("Range")
        if rng and cls.ranges and self.headers.get("If-Range") in (None, cls.etag):
            start = int(re.match(r"bytes=(\d+)-", rng).group(1))
            if start >= len(data):
                self.send_response(416)
                self.end_headers()
                return
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{len(data) - 1}/{len(data)}")
        else:
            self.send_response(200)
        body = data[start:]
        self.send_header("Content-Length", str(len(body)))
        self.send_header("ETag", cls.etag)
        self.end_headers()
        if cls.stop_after is not None:
            body = body[:cls.stop_after]
        try:
            self.wfile.write(body)
        except OSError:
            pass


class Downloads(unittest.TestCase):
    def setUp(self):
        RangeHandler.data = os.urandom(3 << 20)
        RangeHandler.etag, RangeHandler.ranges, RangeHandler.stop_after = '"v1"', True, None
        RangeHandler.requests = []
        RangeHandler.files = {}
        self.srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), RangeHandler)
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.srv.server_address[1]}/lib.zip"
        self.tmp = tempfile.TemporaryDirectory()
        self.dest = Path(self.tmp.name) / "lib.zip"

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.tmp.cleanup()

    def cancel_after(self, nbytes):
        pr = P.Progress(None)

        def on_bytes(got, total):
            if got >= nbytes:
                pr.cancelled.set()
        return pr, on_bytes

    def test_resume_after_cancel(self):
        pr, on_bytes = self.cancel_after(1 << 20)
        with self.assertRaises(P.Cancelled):
            L._download(self.url, self.dest, pr, on_bytes)
        part = self.dest.with_name("lib.zip.part")
        self.assertTrue(part.exists() and 0 < part.stat().st_size < len(RangeHandler.data))
        got = []
        digest = L._download(self.url, self.dest, P.Progress(None), lambda g, t: got.append((g, t)))
        self.assertEqual(digest, hashlib.sha256(RangeHandler.data).hexdigest())
        self.assertEqual(self.dest.read_bytes(), RangeHandler.data)
        self.assertFalse(part.exists())
        self.assertRegex(RangeHandler.requests[-1].get("Range", ""), r"^bytes=[1-9][0-9]*-$")  # asked for the rest
        self.assertEqual(RangeHandler.requests[-1].get("If-Range"), RangeHandler.etag)
        self.assertGreater(got[0][0], 1 << 20)  # counted from what was kept
        self.assertEqual(got[-1], (len(RangeHandler.data), len(RangeHandler.data)))

    def test_server_without_ranges_starts_again(self):
        pr, on_bytes = self.cancel_after(1 << 20)
        with self.assertRaises(P.Cancelled):
            L._download(self.url, self.dest, pr, on_bytes)
        RangeHandler.ranges = False
        digest = L._download(self.url, self.dest)
        self.assertEqual(digest, hashlib.sha256(RangeHandler.data).hexdigest())

    def test_changed_file_starts_again(self):
        pr, on_bytes = self.cancel_after(1 << 20)
        with self.assertRaises(P.Cancelled):
            L._download(self.url, self.dest, pr, on_bytes)
        RangeHandler.data = os.urandom(2 << 20)  # a new upload: If-Range fails, the server sends it all
        RangeHandler.etag = '"v2"'
        digest = L._download(self.url, self.dest)
        self.assertEqual(digest, hashlib.sha256(RangeHandler.data).hexdigest())

    def test_lost_connection_is_an_error_and_resumes(self):
        RangeHandler.stop_after = 1 << 20
        with self.assertRaises(OSError):
            L._download(self.url, self.dest)
        RangeHandler.stop_after = None
        digest = L._download(self.url, self.dest)
        self.assertEqual(digest, hashlib.sha256(RangeHandler.data).hexdigest())

    def test_fetch_counts_the_whole_library(self):
        """A library of two files: the bytes reported grow through both, the library is
        checked, put in place and stamped, and its downloads are removed."""
        a, b = os.urandom(300000), os.urandom(200000)
        RangeHandler.files = {"/a.bin": a, "/b.bin": b}
        base = self.url.rsplit("/", 1)[0]
        L.LIBRARIES["test-lib"] = dict(name="Test library", licence="CC0-1.0", homepage="https://example.org", files=[
            dict(url=base + "/a.bin", sha256=hashlib.sha256(a).hexdigest(), name="a.bin", mb=0),
            dict(url=base + "/b.bin", sha256=hashlib.sha256(b).hexdigest(), name="b.bin", mb=0)])
        try:
            libs, downloads = Path(self.tmp.name) / "libs", Path(self.tmp.name) / "downloads"
            seen, unpacked = [], []
            L.fetch("test-lib", libs, downloads, on_bytes=lambda got, total: seen.append(got))
            self.assertEqual(seen, sorted(seen))
            self.assertEqual(seen[-1], len(a) + len(b))
            self.assertTrue(L.is_ready(libs, "test-lib"))
            self.assertEqual((libs / "test-lib" / "a.bin").read_bytes(), a)
            self.assertEqual((libs / "test-lib" / "b.bin").read_bytes(), b)
            self.assertFalse((downloads / "test-lib").exists())
            # A wrong file is refused and removed.
            L.LIBRARIES["test-lib"]["files"][1]["sha256"] = "0" * 64
            (libs / "test-lib" / L.STAMP).unlink()
            with self.assertRaises(L.HashMismatch):
                L.fetch("test-lib", libs, downloads)
            self.assertFalse((downloads / "test-lib" / "b.bin").exists())
        finally:
            del L.LIBRARIES["test-lib"]

    def test_complete_part_416(self):
        part = self.dest.with_name("lib.zip.part")
        part.write_bytes(RangeHandler.data + b"extra")
        self.dest.with_name("lib.zip.part.json").write_text(json.dumps({"validator": RangeHandler.etag}))
        digest = L._download(self.url, self.dest)
        self.assertEqual(digest, hashlib.sha256(RangeHandler.data).hexdigest())


class Unpacking(unittest.TestCase):
    def make_zip(self, d, names):
        z = Path(d) / "a.zip"
        with zipfile.ZipFile(z, "w") as zf:
            for n in names:
                zf.writestr(n, b"x" * 10)
        return z

    def test_strip_keep(self):
        with tempfile.TemporaryDirectory() as d:
            z = self.make_zip(d, ["top/Keep.vst3/Contents/x86_64-win/Keep.vst3", "top/Other.clap", "top/Keep.vst3/desktop.ini"])
            L._unpack(z, Path(d) / "out", 1, ["Keep.vst3"])
            got = sorted(p.relative_to(Path(d) / "out").as_posix() for p in (Path(d) / "out").rglob("*") if p.is_file())
            self.assertEqual(got, ["Keep.vst3/Contents/x86_64-win/Keep.vst3", "Keep.vst3/desktop.ini"])

    def test_unsafe_paths(self):
        with tempfile.TemporaryDirectory() as d:
            for bad in ["top/../../evil.txt", "C:/evil.txt"]:
                z = self.make_zip(d, [bad])
                with self.assertRaises(RuntimeError):
                    L._unpack(z, Path(d) / "out", 0, None)

    def test_cancel_while_unpacking(self):
        with tempfile.TemporaryDirectory() as d:
            z = self.make_zip(d, [f"f{i}.wav" for i in range(5)])
            pr = P.Progress(None)
            pr.cancelled.set()
            with self.assertRaises(P.Cancelled):
                L._unpack(z, Path(d) / "out", 0, None, pr)


class Pins(unittest.TestCase):
    def test_platform_files(self):
        win = L.files("surge-xt", "win32-x86_64")
        lin = L.files("surge-xt", "linux-x86_64")
        self.assertTrue(win[0]["url"].endswith("surge-xt-win64-1.3.4-pluginsonly.zip"))
        self.assertTrue(lin[0]["url"].endswith("surge-xt-linux-1.3.4-pluginsonly.tar.gz"))
        self.assertEqual(win[1], lin[1])  # the factory patches are the same files
        self.assertTrue(L.files("dragonfly-reverb", "win32-x86_64")[0]["url"].endswith("-win64.zip"))
        self.assertIsNone(L.files("surge-xt", "darwin-arm64"))
        self.assertEqual(L.unsupported(["surge-xt", "sso"], "darwin-arm64"), ["surge-xt"])
        self.assertEqual(L.unsupported(L.needed(G.ALL_STYLES), "win32-x86_64"), [])
        self.assertEqual(L.unsupported(L.needed(G.ALL_STYLES), "linux-x86_64"), [])
        for lib in L.LIBRARIES:
            for plat in ("win32-x86_64", "linux-x86_64"):
                for f in L.files(lib, plat):
                    if "repo" in f:
                        self.assertRegex(f["repo"], r"^[\w.-]+/[\w.-]+$")
                        self.assertRegex(f["commit"], r"^[0-9a-f]{40}$")
                        continue
                    self.assertRegex(f["sha256"], r"^[0-9a-f]{64}$")
                    self.assertTrue((f.get("url") or f.get("zip")).startswith("https://"))
                    self.assertIn("bytes" if "zip" in f else "mb", f)
                    self.assertGreater(L.spec_mb(lib, f) if "zip" in f else 1, 0)

    def test_earlier_stamps_stay_valid(self):
        """What earlier versions put in place is kept: a library made only of whole files has the
        stamp it had, and the Surge XT plug-in, unpacked by an earlier version beside the
        whole content pack, is recognised as in place (not downloaded again)."""
        os.environ.pop("TGMUSIC_PIN", None)

        def old_line(f):
            return {k: f.get(k) for k in ("url", "sha256", "tree_sha256", "strip", "keep", "name")}
        for lib in ("dragonfly-reverb", "generaluser-gs"):
            self.assertEqual(L._fingerprint(lib), json.dumps([old_line(f) for f in L.files(lib)], sort_keys=True))
        content = dict(url=f"{L.GH}/surge-synthesizer/releases-xt/releases/download/1.3.4/surge-xt-portable-content-1.3.4.tar.gz",
                       sha256="7ed24822eab69f73101d8558222979b1bceb4882a780292bb7087309b7b101bd", strip=0, keep=None)
        plugin = L.files("surge-xt", "linux-x86_64")[0]
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / L.STAMP).write_text(json.dumps([old_line(plugin), old_line(content)], sort_keys=True))
            self.assertIn(L._part_id("surge-xt", plugin), L._parts_in_place(Path(d)))

    def test_requirements_locked(self):
        text = S.LOCK.read_text()
        blocks = re.findall(r"^([A-Za-z0-9_.\-]+)==(\S+) \\\n((?:    --hash=sha256:[0-9a-f]{64}(?: \\)?\n?)+)", text, re.M)
        names = [b[0] for b in blocks]
        self.assertEqual(len(names), S.PACKAGES)
        self.assertEqual(len(set(n.lower() for n in names)), len(names))
        for name, ver, hashes in blocks:
            n = hashes.count("--hash=sha256:")
            self.assertGreaterEqual(n, 1, name)
        # Every requirement line is one of these blocks (nothing unpinned or unhashed).
        reqs = [ln for ln in text.splitlines() if ln and not ln.startswith(("#", " "))]
        self.assertEqual(len(reqs), S.PACKAGES)
        for must in ("pedalboard", "pysfizz", "numba", "llvmlite", "tinysoundfont", "imageio-ffmpeg"):
            self.assertIn(must, names)

    def test_launcher_python_pin_matches(self):
        h = (REPO / "src" / "music_build.h").read_text()

        def define(name):
            v = re.search(rf'#define {name} (.+)', h).group(1).strip()
            return re.match(r'"(?:[^"\\]|\\.)*"', v).group(0) if v.startswith('"') else v.split("//")[0].strip()
        self.assertEqual(define("TG_MB_PY_VERSION").strip('"'), S.PYTHON_EMBED["version"])
        self.assertEqual(define("TG_MB_PY_URL").strip('"'), S.PYTHON_EMBED["url"])
        self.assertEqual(define("TG_MB_PY_SHA256").strip('"'), S.PYTHON_EMBED["sha256"])
        self.assertEqual(int(define("TG_MB_PY_BYTES")), S.PYTHON_EMBED["bytes"])
        self.assertEqual(define("TG_MB_PY_STAMP").strip('"'), S.EMBED_STAMP)
        self.assertEqual(define("TG_MB_PY_PTH_NAME").strip('"'), S.PTH_NAME)
        pth = bytes(define("TG_MB_PY_PTH").strip('"'), "ascii").decode("unicode_escape")
        self.assertEqual(pth, S.PTH)
        self.assertEqual(define("TG_MB_VCREDIST_URL").strip('"'), S.VCREDIST_URL)
        self.assertEqual(int(define("TG_MB_TRACKS")), len(G.SONGS) * 3 + 32 * 4)

    def test_consent_up_to_date(self):
        self.assertEqual(G.CONSENT.read_text(encoding="utf-8"), G.consent_text(),
                         "run: python3 tools/music/generate.py --write-consent")

    def test_typical_sizes(self):
        """libraries.TYPICAL_MB (what consent.txt's "usually about" comes from): for every by_note
        library, a size for every style that plays on it, never more than the library's list; and
        the lines in consent.txt."""
        text = G.consent_text()
        for lib, spec in L.LIBRARIES.items():
            if not spec.get("by_note"):
                self.assertNotIn(lib, L.TYPICAL_MB)
                continue
            for st in G.ALL_STYLES:
                if lib in L.STYLE_LIBS[st]:
                    mb = L.TYPICAL_MB[lib][st]
                    self.assertLessEqual(mb, L.size_mb(lib), (lib, st))
                    self.assertIn(f"typical\t{lib}\t{st}\t{mb}\n", text)
        usual = sum(L.typical_mb(lib, G.ALL_STYLES) for lib in L.needed(G.ALL_STYLES))
        most = sum(L.size_mb(lib) for lib in L.needed(G.ALL_STYLES))
        self.assertLess(usual, most / 2)

    def test_build_order(self):
        """The stages a build goes through, in the same order in generate.py, in the progress
        stand-in the launcher is checked with (fake_generate.py) and in the launcher."""
        got = re.findall(r'prog\.emit\("stage", stage="(\w+)", n=(\d), of=(\d)', (MUSIC / "generate.py").read_text())
        self.assertEqual([(s, int(n), int(o)) for s, n, o in got],
                         [("notes", 1, 4), ("plan", 2, 4), ("libraries", 3, 4), ("render", 4, 4)])
        import fake_generate
        self.assertEqual(fake_generate.STAGES, [s for s, _, _ in got])
        c = (REPO / "src" / "music_build.c").read_text()
        for s in fake_generate.STAGES:
            self.assertIn(f'strcmp(name, "{s}")', c)
        self.assertIn('strcmp(ev, "planning")', c)
        # The time left counts the plan.
        self.assertGreater(P.Eta(plan=100, plan_jobs=2, notes=False).seconds(), 60)


class FilesHandler(http.server.BaseHTTPRequestHandler):
    """Serves files by path; honours Range bytes=a-b (unless ranges is off); can answer 503
    once for a path, and logs (path, Range) of every request."""
    files = {}
    ranges = True
    fail_once = set()
    log = []
    pause = 0  # seconds between 16 KB pieces of a body (a slow server)

    def log_message(self, *a):
        pass

    def do_GET(self):
        cls = type(self)
        path = urllib.parse.unquote(self.path)
        rng = self.headers.get("Range")
        cls.log.append((path, rng))
        if path in cls.fail_once:
            cls.fail_once.discard(path)
            self.send_response(503)
            self.send_header("Retry-After", "0")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        data = cls.files.get(path)
        if data is None:
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if rng and cls.ranges:
            a, b = re.match(r"bytes=(\d+)-(\d*)$", rng).groups()
            a, b = int(a), int(b) if b else len(data) - 1
            body = data[a:b + 1]
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {a}-{a + len(body) - 1}/{len(data)}")
        else:
            body = data
            self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            for i in range(0, len(body), 16384):
                self.wfile.write(body[i:i + 16384])
                if cls.pause:
                    self.wfile.flush()
                    time.sleep(cls.pause)
        except OSError:
            pass


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Listed(unittest.TestCase):
    """The sample libraries' files, fetched one by one (a repository) or out of a zip with
    range requests, against a local server; into the layout the generator reads."""

    COMMIT = "c" * 40

    def setUp(self):
        FilesHandler.files, FilesHandler.ranges, FilesHandler.fail_once, FilesHandler.log = {}, True, set(), []
        FilesHandler.pause = 0
        self.srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FilesHandler)
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        self.base = f"http://127.0.0.1:{self.srv.server_address[1]}"
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        self.libs, self.downloads = self.d / "libs", self.d / "downloads"
        self.saved = (L.RAW, L.SAMPLES, L.RETRY_S, dict(L._MANIFEST), L.WORKERS)
        L.RAW, L.RETRY_S = self.base, (0, 0)
        L.SAMPLES = self.d / "samples.tsv"
        L._MANIFEST.clear()

    def tearDown(self):
        L.RAW, L.SAMPLES, L.RETRY_S = self.saved[:3]
        L.WORKERS = self.saved[4]
        L._MANIFEST.clear()
        L._MANIFEST.update(self.saved[3])
        for k in [k for k in L.LIBRARIES if k.startswith("test-")]:
            del L.LIBRARIES[k]
        self.srv.shutdown()
        self.srv.server_close()
        self.tmp.cleanup()

    def library(self, lib, spec, files, extra=()):
        """Define a test library whose list is `files` ({path: bytes}); `extra`: more parts."""
        L.LIBRARIES[lib] = dict(name=f"Test {lib}", licence="CC0-1.0", homepage="https://example.org",
                                files=list(extra) + [spec])
        lines = [f"@{lib}\t{L.source_of(spec)}"] + [f"{sha(b)}\t{len(b)}\t{p}" for p, b in sorted(files.items())]
        L.SAMPLES.write_text("\n".join(lines) + "\n")
        L._MANIFEST.clear()

    def repo_files(self, n=6, size=150000):
        names = ["Samples/a b.wav", "Samples/Kick, hard #1.wav", "Inst/Ünïcode.sfz", "Inst/x/y/z.wav",
                 "LICENSE", "README.md", "Samples/more.wav", "Samples/last.wav"][:n]
        files = {p: os.urandom(size + i) for i, p in enumerate(names)}
        for p, b in files.items():
            FilesHandler.files[f"/o/r/{self.COMMIT}/{p}"] = b
        return files

    def leftovers(self):
        return sorted(p.name for p in self.libs.rglob("*" + L.TMP))

    def test_repo_files_resume_after_cancel(self):
        files = self.repo_files(n=8)
        spec = dict(repo="o/r", commit=self.COMMIT)
        self.library("test-repo", spec, files)
        FilesHandler.pause, L.WORKERS = 0.04, 2  # a file takes about 0.4 s: some finish before the cancel
        pr = P.Progress(None)
        total = sum(len(b) for b in files.values())

        def on_bytes(got, _):
            if got >= total // 3:
                pr.cancelled.set()
        with self.assertRaises(P.Cancelled):
            L.fetch("test-repo", self.libs, self.downloads, progress=pr, on_bytes=on_bytes)
        self.assertFalse(L.is_ready(self.libs, "test-repo"))
        self.assertEqual(self.leftovers(), [])  # nothing half written
        there = {p for p in files if (self.libs / "test-repo" / p).is_file()}
        self.assertLess(len(there), len(files))
        for p in there:
            self.assertEqual((self.libs / "test-repo" / p).read_bytes(), files[p])
        self.assertGreater(len(there), 0)
        FilesHandler.log, FilesHandler.pause = [], 0
        seen = []
        L.fetch("test-repo", self.libs, self.downloads, on_bytes=lambda got, _: seen.append(got))
        self.assertTrue(L.is_ready(self.libs, "test-repo"))
        for p, b in files.items():
            self.assertEqual((self.libs / "test-repo" / p).read_bytes(), b)
        asked = {urllib.parse.unquote(u).split(f"/{self.COMMIT}/", 1)[1] for u, _ in FilesHandler.log}
        self.assertEqual(asked, set(files) - there)  # only what was missing
        self.assertEqual(seen[0], sum(len(files[p]) for p in there))  # counted from what was kept
        self.assertEqual(seen[-1], total)
        self.assertEqual(seen, sorted(seen))
        # Ready: nothing asked for again.
        FilesHandler.log = []
        L.fetch("test-repo", self.libs, self.downloads)
        self.assertEqual(FilesHandler.log, [])

    def test_bad_file_refused(self):
        files = self.repo_files(n=3)
        self.library("test-repo", dict(repo="o/r", commit=self.COMMIT), files)
        bad = next(iter(files))
        FilesHandler.files[f"/o/r/{self.COMMIT}/{bad}"] = os.urandom(len(files[bad]))  # same size, other bytes
        with self.assertRaises(L.HashMismatch):
            L.fetch("test-repo", self.libs, self.downloads)
        self.assertFalse((self.libs / "test-repo" / bad).exists())
        self.assertEqual(self.leftovers(), [])
        self.assertFalse(L.is_ready(self.libs, "test-repo"))

    def test_server_trouble_retried_missing_file_not(self):
        files = self.repo_files(n=3)
        self.library("test-repo", dict(repo="o/r", commit=self.COMMIT), files)
        flaky = f"/o/r/{self.COMMIT}/{sorted(files)[1]}"
        FilesHandler.fail_once = {flaky}
        L.fetch("test-repo", self.libs, self.downloads)
        self.assertTrue(L.is_ready(self.libs, "test-repo"))
        self.assertEqual([u for u, _ in FilesHandler.log].count(flaky), 2)
        # A file the server doesn't have is an error at once (with its URL), not retried.
        (self.libs / "test-repo" / L.STAMP).unlink()
        gone = sorted(files)[0]
        (self.libs / "test-repo" / gone).unlink()
        del FilesHandler.files[f"/o/r/{self.COMMIT}/{gone}"]
        FilesHandler.log = []
        with self.assertRaises(urllib.error.HTTPError) as cm:
            L.fetch("test-repo", self.libs, self.downloads)
        self.assertEqual(cm.exception.code, 404)
        self.assertIn(urllib.parse.quote(gone), str(cm.exception))
        self.assertEqual(len(FilesHandler.log), 1)

    def make_zip(self):
        """A release zip: stored and deflated members, one with a data descriptor (written to a
        stream that can't seek), and a sound file in a folder; the list takes some of them."""
        class Stream(io.RawIOBase):
            def __init__(self):
                self.b = bytearray()

            def writable(self):
                return True

            def write(self, b):
                self.b += b
                return len(b)
        out = Stream()
        members = {f"Kit/Samples/s{i:02d}.wav": os.urandom(40000 + i) if i % 2 else bytes(50000 + i) for i in range(12)}
        members["Kit/Programs/02-basic.sfz"] = b"<region> sample=../Samples/s01.wav\n"
        members["Kit/readme.txt"] = b"free\n" * 100
        with zipfile.ZipFile(out, "w") as z:
            for i, (n, b) in enumerate(members.items()):
                z.writestr(zipfile.ZipInfo(n), b, compress_type=zipfile.ZIP_DEFLATED if i % 3 else zipfile.ZIP_STORED)
        data = bytes(out.b)
        FilesHandler.files["/rel/kit.zip"] = data
        wanted = {n: members[n] for n in ["Kit/Programs/02-basic.sfz", "Kit/Samples/s01.wav", "Kit/Samples/s02.wav",
                                          "Kit/Samples/s03.wav", "Kit/Samples/s09.wav", "Kit/readme.txt"]}
        return data, wanted

    def test_zip_members_with_ranges(self):
        data, wanted = self.make_zip()
        spec = dict(zip=self.base + "/rel/kit.zip", sha256=sha(data), bytes=len(data))
        self.library("test-zip", spec, wanted)
        L.fetch("test-zip", self.libs, self.downloads)
        self.assertTrue(L.is_ready(self.libs, "test-zip"))
        for n, b in wanted.items():
            self.assertEqual((self.libs / "test-zip" / n).read_bytes(), b)
        got = sorted(p.relative_to(self.libs / "test-zip").as_posix() for p in (self.libs / "test-zip").rglob("*")
                     if p.is_file() and p.name not in (L.STAMP, L.CHECKED))
        self.assertEqual(got, sorted(wanted))
        self.assertTrue(all(r for _, r in FilesHandler.log))  # only parts were asked for
        asked = sum(int(b) - int(a) + 1 for a, b in (re.match(r"bytes=(\d+)-(\d+)", r).groups() for _, r in FilesHandler.log))
        self.assertLess(asked, len(data) * 0.7)
        self.assertFalse((self.downloads / "test-zip").exists())

    def test_zip_whole_when_no_ranges(self):
        data, wanted = self.make_zip()
        spec = dict(zip=self.base + "/rel/kit.zip", sha256=sha(data), bytes=len(data))
        self.library("test-zip", spec, wanted)
        FilesHandler.ranges = False
        L.fetch("test-zip", self.libs, self.downloads)
        for n, b in wanted.items():
            self.assertEqual((self.libs / "test-zip" / n).read_bytes(), b)
        self.assertFalse((self.downloads / "test-zip").exists())  # the zip is deleted once used
        # A zip that isn't the one pinned is refused.
        spec["sha256"] = "0" * 64
        self.library("test-zip", spec, wanted)
        (self.libs / "test-zip" / "Kit/Samples/s02.wav").unlink()
        with self.assertRaises(L.HashMismatch):
            L.fetch("test-zip", self.libs, self.downloads)

    def test_cancel_during_zip(self):
        data, wanted = self.make_zip()
        spec = dict(zip=self.base + "/rel/kit.zip", sha256=sha(data), bytes=len(data))
        self.library("test-zip", spec, wanted)
        pr = P.Progress(None)
        pr.cancelled.set()
        with self.assertRaises(P.Cancelled):
            L.fetch("test-zip", self.libs, self.downloads, progress=pr)
        self.assertEqual(self.leftovers(), [])
        L.fetch("test-zip", self.libs, self.downloads)
        self.assertTrue(L.is_ready(self.libs, "test-zip"))

    def test_earlier_whole_library_kept_then_pruned(self):
        """A library an earlier version unpacked whole: the listed files there are checked and
        kept (none downloaded), a whole file in place by the old stamp is kept, the rest goes."""
        files = self.repo_files(n=4)
        plain = os.urandom(5000)
        FilesHandler.files["/plain/Plugin.bin"] = plain
        extra = dict(url=self.base + "/plain/Plugin.bin", sha256=sha(plain), name="Plugin.bin", mb=0)
        self.library("test-repo", dict(repo="o/r", commit=self.COMMIT), files, extra=[extra])
        lib = self.libs / "test-repo"
        for p, b in files.items():
            (lib / p).parent.mkdir(parents=True, exist_ok=True)
            (lib / p).write_bytes(b)
        (lib / "Plugin.bin").write_bytes(plain)
        (lib / "Samples" / "unused.wav").write_bytes(b"x" * 1000)
        (lib / "Unused" / "deep").mkdir(parents=True)
        (lib / "Unused" / "deep" / "u.sfz").write_bytes(b"x")
        old = {k: extra.get(k) for k in ("url", "sha256", "tree_sha256", "strip", "keep", "name")}
        (lib / L.STAMP).write_text(json.dumps([old, {"url": "https://example.org/whole.zip"}], sort_keys=True))
        L.fetch("test-repo", self.libs, self.downloads)
        self.assertEqual(FilesHandler.log, [])  # nothing downloaded
        self.assertTrue(L.is_ready(self.libs, "test-repo"))
        self.assertFalse((lib / "Samples" / "unused.wav").exists())
        self.assertFalse((lib / "Unused").exists())
        self.assertEqual((lib / "Plugin.bin").read_bytes(), plain)
        for p, b in files.items():
            self.assertEqual((lib / p).read_bytes(), b)

    # --- Downloads by note (libraries.fetch(want=...), needs.py) --------------------------

    def asked(self):
        return sorted(urllib.parse.unquote(u).split(f"/{self.COMMIT}/", 1)[1] for u, _ in FilesHandler.log)

    def test_fetch_wanted_only_keeps_what_is_listed(self):
        """Only the files wanted are fetched; a later build wanting others fetches just those
        and keeps every listed file an earlier one fetched (unlisted ones go); a file checked
        once is in CHECKED; all of them makes the library whole."""
        files = self.repo_files(n=8)
        self.library("test-repo", dict(repo="o/r", commit=self.COMMIT), files)
        names = sorted(files)
        first, second = set(names[:2]), set(names[2:4])
        self.assertEqual(L.missing_bytes(self.libs, "test-repo", first), sum(len(files[p]) for p in first))
        L.fetch("test-repo", self.libs, self.downloads, want=first)
        self.assertEqual(self.asked(), sorted(first))
        self.assertTrue(L.is_ready(self.libs, "test-repo", first))
        self.assertFalse(L.is_ready(self.libs, "test-repo", first | second))
        self.assertFalse(L.is_ready(self.libs, "test-repo"))
        self.assertEqual(L.missing_bytes(self.libs, "test-repo", first), 0)
        self.assertEqual(set(L._checked(self.libs / "test-repo")), first)
        (self.libs / "test-repo" / "stray.wav").write_bytes(b"x")  # not listed: goes
        FilesHandler.log = []
        L.fetch("test-repo", self.libs, self.downloads, want=second)
        self.assertEqual(self.asked(), sorted(second))
        for p in first | second:
            self.assertEqual((self.libs / "test-repo" / p).read_bytes(), files[p])
        self.assertFalse((self.libs / "test-repo" / "stray.wav").exists())
        self.assertEqual(set(L._checked(self.libs / "test-repo")), first | second)
        self.assertTrue(L.is_ready(self.libs, "test-repo", first))  # still there
        FilesHandler.log = []
        L.fetch("test-repo", self.libs, self.downloads)
        self.assertEqual(self.asked(), sorted(set(names) - first - second))
        self.assertTrue(L.is_ready(self.libs, "test-repo"))
        self.assertTrue(L.is_ready(self.libs, "test-repo", {names[0]}))
        self.assertEqual(self.leftovers(), [])

    INST = ("<control> default_path=Samples/\n<group> lovel=1 hivel=127\n"
            "<region> sample=k60.wav key=60\n<region> sample=k61.wav key=61\n"
            "<region> sample=k62.wav lokey=62 hikey=63 seq_length=2 seq_position=1\n"
            "<region> sample=k62b.wav lokey=62 hikey=63 seq_length=2 seq_position=2\n"
            "<region> sample=rel60.wav key=60 trigger=release\n<region> sample=pedal.wav key=60 locc64=65\n")

    @staticmethod
    def wav(n):
        buf = io.BytesIO()
        with wave.open(buf, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(48000)
            w.writeframes(b"".join(int(3000 * ((i * n) % 37 - 18) / 18).to_bytes(2, "little", signed=True)
                                   for i in range(24000)))
        return buf.getvalue()

    def note_library(self):
        """A by_note test library with one instrument (Inst/i.sfz), served by the local server."""
        files = {"Inst/i.sfz": self.INST.encode(), "LICENSE": b"free\n", "Inst/Samples/readme.txt": b"about\n",
                 "Other/readme.txt": b"other\n", "Other/x.wav": self.wav(9)}
        for i, n in enumerate(["k60", "k61", "k62", "k62b", "rel60", "pedal"]):
            files[f"Inst/Samples/{n}.wav"] = self.wav(i + 2)
        for p, b in files.items():
            FilesHandler.files[f"/o/r/{self.COMMIT}/{p}"] = b
        self.library("test-notes", dict(repo="o/r", commit=self.COMMIT), files)
        L.LIBRARIES["test-notes"]["by_note"] = True
        from tgmusic import needs as N
        N._LIBS.clear()
        del N.FETCHED[:]
        return files, N

    PLANNED = [{"job": ["band", ["X", "Y"], "D"], "error": None, "misses": [],
                "parts": [{"name": "keys", "type": "sfz", "instrument": "test-notes:Inst/i.sfz", "on": [[60, 100, 3]]},
                          {"name": "lead", "type": "surge", "instrument": "Leads/Square.fxp", "notes": 4}],
                "probes": [{"type": "sfz", "instrument": "test-notes:Inst/i.sfz", "on": [[62, 80, 1]]}]}]

    def test_planned_set(self):
        """What a build downloads: the definition files, the samples the planned notes can play
        (with a measurement's notes, every round robin, the release ones) and the licence and
        readme files that go with them; nothing else."""
        files, N = self.note_library()
        want = N.wanted(self.PLANNED, self.libs)
        self.assertEqual(want, {"test-notes": {"Inst/i.sfz", "Inst/Samples/k60.wav", "Inst/Samples/rel60.wav",
                                               "Inst/Samples/k62.wav", "Inst/Samples/k62b.wav", "LICENSE",
                                               "Inst/Samples/readme.txt"}})
        self.assertEqual(self.asked(), ["Inst/i.sfz"])  # only the definitions are fetched to work it out
        self.assertEqual(N.definitions_missing(self.PLANNED, self.libs), 0)
        L.fetch("test-notes", self.libs, self.downloads, want=want["test-notes"])
        self.assertEqual(sorted(p.relative_to(self.libs / "test-notes").as_posix() for p in (self.libs / "test-notes").rglob("*")
                                if p.is_file() and p.name not in (L.STAMP, L.CHECKED)), sorted(want["test-notes"]))

    @unittest.skipUnless(__import__("importlib").util.find_spec("pysfizz"), "pysfizz is not installed")
    def test_planned_set_covers_the_render_and_safety_net(self):
        """With the planned set there, rendering the planned notes fetches nothing (the safety
        net stays idle) and sounds as with every file of the list there; without it, the safety
        net fetches what is missing before sfizz loads the instrument, and the render is the
        same again. The link folder is gone after loading."""
        files, N = self.note_library()
        notes = [{"t": 0.05, "dur": 0.3, "note": 60, "vel": 100 / 127}, {"t": 0.5, "dur": 0.2, "note": 62, "vel": 80 / 127},
                 {"t": 0.8, "dur": 0.2, "note": 62, "vel": 80 / 127}]
        with unittest.mock.patch.dict(os.environ, {"TGMUSIC_LIBS": str(self.libs)}):
            from tgmusic import instruments as I
            path = self.libs / "test-notes" / "Inst" / "i.sfz"

            def render():
                I._EXPECTED.clear()
                return I.SfzInstrument(path).render(notes, total=48000)
            want = N.wanted(self.PLANNED, self.libs)["test-notes"]
            L.fetch("test-notes", self.libs, self.downloads, want=want)
            FilesHandler.log = []
            planned = render()
            self.assertEqual(N.FETCHED, [])  # nothing the plan didn't foresee
            self.assertEqual(FilesHandler.log, [])
            self.assertGreater(float(abs(planned).max()), 0.01)
            self.assertFalse((self.libs / ".views").exists() and any((self.libs / ".views").iterdir()))
            # Every listed file there, the instrument loaded from its own folder: the same.
            L.fetch("test-notes", self.libs, self.downloads)
            with unittest.mock.patch.dict(os.environ, {"TGMUSIC_SFZ_WHOLE": "1"}):
                whole = render()
            self.assertEqual(planned.tobytes(), whole.tobytes())
            # The safety net: the samples gone, they are fetched before sfizz loads it.
            for p in ("Inst/Samples/k60.wav", "Inst/Samples/k62b.wav"):
                (self.libs / "test-notes" / p).unlink()
            FilesHandler.log = []
            again = render()
            self.assertEqual(sorted(p for _, p, _ in N.FETCHED), ["Inst/Samples/k60.wav", "Inst/Samples/k62b.wav"])
            self.assertEqual(self.asked(), ["Inst/Samples/k60.wav", "Inst/Samples/k62b.wav"])
            self.assertEqual(planned.tobytes(), again.tobytes())
            self.assertEqual(self.leftovers(), [])
            # Repeatable: the same notes, the same audio.
            for _ in range(3):
                self.assertEqual(render().tobytes(), planned.tobytes())

    def test_safety_net_several_processes_at_once(self):
        """Render processes fetching the same missing file at once: each writes its own
        temporary file, the file ends up whole and checked, nothing is left behind."""
        files, _ = self.note_library()
        FilesHandler.pause = 0.01
        target = "Inst/Samples/k61.wav"
        cfg = json.dumps({"raw": self.base, "samples": str(L.SAMPLES), "spec": L.LIBRARIES["test-notes"],
                          "libs": str(self.libs), "path": target})
        code = ("import json, sys\nsys.path.insert(0, sys.argv[1])\nfrom pathlib import Path\n"
                "from tgmusic import libraries as L\ncfg = json.loads(sys.argv[2])\n"
                "L.RAW, L.SAMPLES = cfg['raw'], Path(cfg['samples'])\nL.LIBRARIES['test-notes'] = cfg['spec']\n"
                "print(json.dumps(L.fetch_files('test-notes', [cfg['path']], cfg['libs'])))\n")
        procs = [subprocess.Popen([sys.executable, "-B", "-c", code, str(MUSIC), cfg], stdout=subprocess.PIPE, text=True)
                 for _ in range(3)]
        outs = [p.communicate(timeout=60)[0] for p in procs]
        self.assertEqual([p.returncode for p in procs], [0, 0, 0])
        self.assertTrue(any(json.loads(o) for o in outs))
        self.assertEqual((self.libs / "test-notes" / target).read_bytes(), files[target])
        self.assertEqual(self.leftovers(), [])

    def test_plan_store_resumes(self):
        """A build stopped while planning plans only what is left; plans made under another key
        (the code, the notes or this computer's measurements changed) are dropped."""
        from tgmusic import needs as N
        job, job2 = ["band", ["USA", "Las Vegas"], "D"], ["variation", "01_title", "faithful"]
        res = {"job": job, "error": None, "parts": [], "misses": [], "probes": []}
        store = N.PlanStore(self.d / "plan", "k1")
        self.assertIsNone(store.get(job))
        store.put(job, res)
        store.put(job2, dict(res, job=job2, error="Traceback ..."))
        again = N.PlanStore(self.d / "plan", "k1")
        self.assertEqual(again.get(job), res)
        self.assertIsNone(again.get(job2))  # a failed plan is planned again
        self.assertIsNone(N.PlanStore(self.d / "plan", "k2").get(job))
        work = self.d / "work"
        (work / "events").mkdir(parents=True)
        (work / "events" / "01_title.json").write_text("{}")
        k = N.plan_key(work, self.libs, False)
        self.assertEqual(k, N.plan_key(work, self.libs, False))
        self.assertNotEqual(k, N.plan_key(work, self.libs, True))
        (work / "events" / "01_title.json").write_text('{"notes": []}')
        self.assertNotEqual(k, N.plan_key(work, self.libs, False))


class Instruments(unittest.TestCase):
    """Every instrument file the generator's code can load is in a download (the guard that
    keeps a new instrument from breaking players' builds), and the list matches the pins."""

    def test_every_instrument_downloaded(self):
        entries, unknown = C.instrument_files()
        self.assertEqual(unknown, [], "tgmusic/catalog.py does not understand these: teach it the new form")
        self.assertEqual(C.not_downloaded(entries), [],
                         "instrument files missing from tgmusic/data/samples.tsv: run tools/music/make_manifest.py")
        # The scan sees what the styles play (a broken scan finding nothing would pass above).
        for lib, n in {"sso": 19, "vsco2-ce": 38, "vcsl": 9, "mtg-solo-sax": 4, "naked-drums": 1, "big-rusty-drums": 1,
                       "surge-xt": 18, "generaluser-gs": 1}.items():
            self.assertGreaterEqual(len(entries.get(lib, [])), n, lib)

    def test_list_matches_pins(self):
        m = L.manifest()
        listed = {lib for lib, s in L.LIBRARIES.items() for f in s["files"] if L.is_listed(f)}
        self.assertEqual(set(m), listed)
        for lib in listed:
            (f,) = [f for f in L.LIBRARIES[lib]["files"] if L.is_listed(f)]
            self.assertEqual(m[lib]["source"], L.source_of(f), f"{lib}: run make_manifest.py")
            paths = [p for p, _, _ in m[lib]["files"]]
            self.assertEqual(paths, sorted(set(paths)))
            disk = [L.disk_path(f, p).lower() for p in paths]
            self.assertEqual(len(disk), len(set(disk)), f"{lib}: two files differ only in case")
            for p, size, h in m[lib]["files"]:
                self.assertRegex(h, r"^[0-9a-f]{64}$")
                self.assertGreater(size, 0, p)
                self.assertFalse(p.startswith("/") or ".." in p.split("/"), p)
            self.assertTrue(any(re.search(r"(?i)licen[cs]e|readme", p) for p in paths), f"{lib}: no licence or readme")

    def test_paths_fit_windows(self):
        """Under the default folders (a 20-character user name) every file, even under its
        temporary name, stays well under Windows' 260 characters: sfizz gets plain paths."""
        base = len("C:\\Users\\" + "x" * 20 + "\\AppData\\Local\\race-masters\\music-tools\\cache\\libs\\")
        for lib, v in L.manifest().items():
            (f,) = [f for f in L.LIBRARIES[lib]["files"] if L.is_listed(f)]
            for p, _, _ in v["files"]:
                self.assertLess(base + len(lib) + 1 + len(L.disk_path(f, p)) + len(L.TMP), 220, p)

    def test_long_windows_paths(self):
        short = "C:\\Users\\x\\AppData\\Local\\race-masters\\music-tools\\cache\\libs\\vcsl\\a.wav"
        self.assertEqual(L.win_long(short), short)
        deep = "C:\\Users\\" + "x" * 20 + "\\deep" * 45 + "\\a.wav"
        self.assertEqual(L.win_long(deep), "\\\\?\\" + deep)
        self.assertEqual(L.win_long("\\\\?\\" + deep), "\\\\?\\" + deep)
        unc = "\\\\server\\share" + "\\deep" * 50
        self.assertEqual(L.win_long(unc), "\\\\?\\UNC\\server\\share" + "\\deep" * 50)
        if os.name != "nt":
            self.assertEqual(L.long_path("a/b"), Path("a/b"))

    def test_guard_catches_a_new_instrument(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "config.py").write_text("def lib(name):\n    pass\n")
            (Path(d) / "instruments.py").write_text(
                "from . import config\nSURGE_PATCHES = config.lib('surge-xt') / 'Surge Synth Team' / 'SurgeXTData' / "
                "'patches_factory'\n")
            (Path(d) / "new.py").write_text(
                "from . import config\nVS = config.lib('vsco2-ce')\n\n\ndef vs(name):\n    return VS / name\n\n\n"
                "A = vs('Harp.sfz')\nB = VS / 'Brand New.sfz'\nP = {'type': 'surge', 'patch': 'Leads/Nope.fxp'}\n"
                "Q = 'odd/' + 'thing.sfz'\n")
            entries, unknown = C.instrument_files(d)
            self.assertEqual(entries["vsco2-ce"], ["Brand New.sfz", "Harp.sfz"])
            self.assertEqual(entries["surge-xt"], ["Surge Synth Team/SurgeXTData/patches_factory/Leads/Nope.fxp"])
            self.assertEqual([t for _, t in unknown], ["thing.sfz"])
            self.assertEqual(C.not_downloaded(entries), [("surge-xt", "Surge Synth Team/SurgeXTData/patches_factory/Leads/Nope.fxp"),
                                                         ("vsco2-ce", "Brand New.sfz")])


def deps(files, root):
    """sfz.deps over {path: text} (latin-1 bytes)."""
    return Z.deps(root, lambda p: files[p].encode("latin-1") if p in files else None)


class SfzReader(unittest.TestCase):
    """tgmusic/sfz.py: which files an instrument loads, read as sfizz reads it."""

    def test_includes_from_the_root_folder(self):
        files = {
            "Inst/Main.sfz": '#define $DIR Samples\n<control> default_path=../\n#include "inc/group.sfz"\n',
            # an include inside an include is still relative to the root .sfz's folder
            "Inst/inc/group.sfz": '<group> lokey=1 // a comment\n#include "inc/regions.sfz"\n',
            "Inst/inc/regions.sfz": "<region> sample=$DIR/a b.wav lokey=60 hikey=61\n"
                                    "<region> sample=$DIR\\sub\\c.wav /* block\ncomment */ <region> sample=*sine\n"
                                    "<region>sample=d.wav<region> sample=e.wav pitch_keycenter=60\n",
        }
        r = deps(files, "Inst/Main.sfz")
        self.assertEqual(r.files, ["Inst/Main.sfz", "Inst/inc/group.sfz", "Inst/inc/regions.sfz"])
        self.assertEqual(r.samples, ["Samples/a b.wav", "Samples/sub/c.wav", "d.wav", "e.wav"])
        self.assertEqual(r.crossed, ["Inst"] * 4)
        self.assertEqual(r.problems, [])

    def test_defines(self):
        files = {
            "I.sfz": "#define $A x\n#define $AB y\n<region> sample=$AB.wav\n"  # the shortest name wins: x + B.wav
                     "#define $V v1 <region> sample=$V.wav\n"                    # the value ends at the first blank
                     '#define $N 1\n#include "r.sfz"\n#define $N 2\n#include "r.sfz"\n'
                     "<region> sample=$NOPE.wav\n"                              # $N is defined: 2 + OPE
                     "<region> sample=x$UNDEF.wav\n",                           # undefined: dropped
            "r.sfz": "<region> sample=s$N.wav\n",
        }
        r = deps(files, "I.sfz")
        self.assertEqual(r.samples, ["xB.wav", "v1.wav", "s1.wav", "s2.wav", "2OPE.wav", "x.wav"])
        self.assertEqual(len(r.problems), 1)
        self.assertIn("$UNDEF", r.problems[0])

    def test_default_path_and_levels(self):
        files = {"P/i.sfz": "<control> default_path=Samples\\Piano/\n<global> sample=g.wav\n<group> sample=grp.wav\n"
                            "<region> lokey=1\n<region> sample=r.wav\n<master>\n<region>\n"
                            "<control>\n<group>\n<region>\n<global>\n<region> sample=last.wav\n"}
        r = deps(files, "P/i.sfz")
        # group's sample, the region's own, after <master> the global's, after a new <control>
        # no default_path (and the global's sample still), then a region of its own
        self.assertEqual(r.samples, ["P/Samples/Piano/grp.wav", "P/Samples/Piano/r.wav", "P/Samples/Piano/g.wav",
                                     "P/g.wav", "P/last.wav"])

    def test_problems(self):
        files = {"a/i.sfz": '#include "nope.sfz"\n<region> sample=../../x.wav\n<region> sample=ok.wav\n',
                 "a/bom.sfz": "\xef\xbb\xbf<region> sample=skipped.wav\n<region> sample=kept.wav\n"}
        r = deps(files, "a/i.sfz")
        self.assertEqual(r.samples, ["a/ok.wav"])
        self.assertEqual(len(r.missing), 2)
        self.assertEqual(r.problems, [])
        # sfizz reads a byte-order mark as text: the first line is skipped (as here)
        self.assertEqual(deps(files, "a/bom.sfz").samples, ["a/kept.wav"])

    def test_find_ignores_case_like_sfizz(self):
        listing = {"Kit/Samples/Kick.WAV", "Kit/Samples/snare.wav"}
        dirs = {"": {"Kit"}, "Kit": {"Samples"}, "Kit/Samples": {"Kick.WAV", "snare.wav"}}

        def exists(p):
            return p in listing or p in dirs
        self.assertEqual(Z.find("kit/samples/kick.wav", exists, lambda d: sorted(dirs.get(d, ()))), "Kit/Samples/Kick.WAV")
        self.assertEqual(Z.find("Kit/Samples/snare.wav", exists, lambda d: sorted(dirs.get(d, ()))), "Kit/Samples/snare.wav")
        self.assertIsNone(Z.find("Kit/Samples/tom.wav", exists, lambda d: sorted(dirs.get(d, ()))))

    @unittest.skipUnless(__import__("importlib").util.find_spec("pysfizz"), "pysfizz is not installed")
    def test_agrees_with_sfizz(self):
        """The same instruments loaded by sfizz itself: the samples its regions play are the ones
        deps() lists."""
        import pysfizz
        files = {
            "Inst/Main.sfz": "#define $DIR Samples\n<control> default_path=../\n#include \"inc/group.sfz\"\n"
                             "<control> default_path=../Other/\n<region> sample=o.wav key=70\n",
            "Inst/inc/group.sfz": '<group> lokey=1 hikey=50\n#include "inc/regions.sfz"\n',
            "Inst/inc/regions.sfz": "<region> sample=$DIR/a b.wav lokey=60 hikey=61\n#define $N 2\n"
                                    "<region> sample=$DIR\\sub\\c$N.wav /* x */ key=62 <region> sample=*sine key=63\n"
                                    "<master>\n<region>sample=$DIR/d.wav key=64<region> sample=$DIR/e.wav key=65\n",
        }
        with tempfile.TemporaryDirectory() as d:
            for p, t in files.items():
                (Path(d) / p).parent.mkdir(parents=True, exist_ok=True)
                (Path(d) / p).write_bytes(t.encode("latin-1"))
            r = deps(files, "Inst/Main.sfz")
            for s in r.samples:
                (Path(d) / s).parent.mkdir(parents=True, exist_ok=True)
                with wave.open(str(Path(d) / s), "wb") as w:
                    w.setnchannels(1)
                    w.setsampwidth(2)
                    w.setframerate(48000)
                    w.writeframes(b"\x10\x00" * 4800)
            syn = pysfizz.Synth(48000, 256)
            self.assertTrue(syn.load_sfz_file(str(Path(d) / "Inst" / "Main.sfz")))
            got = set()
            for i in range(syn._synth.get_num_regions()):
                sid = syn._synth.get_region_data(i)["sample_id"]
                if not sid.startswith("*"):
                    got.add(Z.walk("Inst", sid)[0])
            self.assertEqual(got, set(r.samples))
            self.assertEqual(len(r.samples), 5)
            # sfizz drops a region whose sample is missing: all kept (the five and *sine) means
            # deps() missed none.
            self.assertEqual(syn._synth.get_num_regions(), len(r.samples) + 1)

    def test_playable_leaves_out_controller_only_samples(self):
        files = {"P.sfz": "<control> set_cc80=100\n"
                          "<group> locc64=65 hicc64=127\n<region> sample=pedal.wav\n"  # sustain pedal down: never
                          "<group> locc64=0 hicc64=64\n<region> sample=up.wav\n"
                          "<group> locc80=1\n<region> sample=mic80.wav\n"              # set_cc80=100: plays
                          "<group> locc81=1\n<region> sample=mic81.wav\n"              # cc81 starts at 0: never
                          "<group> lohdcc1=0.5\n<region> sample=hd.wav\n"              # not judged: kept
                          "<group> locc140=1\n<region> sample=own.wav\n"               # sfizz's own controller: kept
                          "<group> locc7=90 hicc7=110\n<region> sample=vol.wav\n"      # cc7 starts at 100
                          "<group> locc7=0 hicc7=99\n<region> sample=vol_low.wav\n"    # never
                          "<group>\n<region> sample=pedal.wav locc64=0\n"}             # a region that plays it after all
        self.assertEqual(Z.playable(deps(files, "P.sfz")), ["up.wav", "mic80.wav", "hd.wav", "own.wav", "vol.wav", "pedal.wav"])
        # none would be left: all kept (sfizz doesn't load an instrument with none of its samples)
        self.assertEqual(Z.playable(deps({"N.sfz": "<region> sample=a.wav locc64=65\n"}, "N.sfz")), ["a.wav"])

    def test_generator_sends_no_controller(self):
        """playable() rests on it: the .sfz instruments get notes only (instruments.py)."""
        for f in sorted((MUSIC / "tgmusic").glob("*.py")):
            src = f.read_text(encoding="utf-8")
            for call in (".cc(", "set_cc(", "hdcc("):
                self.assertNotIn(call, src, f"{f.name} sends a controller message: tools/music/make_manifest.py "
                                            "must then keep the samples that need one (sfz.playable)")

    @unittest.skipUnless(__import__("importlib").util.find_spec("pysfizz"), "pysfizz is not installed")
    def test_controller_start_values_are_sfizz(self):
        """sfz.CC_START is sfizz's own: for every controller 0-127, a region for exactly its
        starting value plays, and one for every other value doesn't."""
        import pysfizz

        def plays(d, text):
            p = Path(d) / "probe.sfz"
            p.write_text(text)
            syn = pysfizz.Synth(48000, 256)
            if not syn.load_sfz_file(str(p)):
                return False
            syn._synth.note_on(0, 60, 100)
            for _ in range(4):
                left, right = syn._synth.render_block()
                if any(left) or any(right):
                    return True
            return False

        with tempfile.TemporaryDirectory() as d:
            for cc in range(128):
                start = Z.CC_START.get(cc, 0)
                lo, hi = (start + 1, 127) if start < 127 else (0, 126)
                self.assertTrue(plays(d, f"<region> sample=*sine locc{cc}={start} hicc{cc}={start}\n"), f"cc{cc}")
                self.assertFalse(plays(d, f"<region> sample=*sine locc{cc}={lo} hicc{cc}={hi}\n"), f"cc{cc}")


# An instrument for note_samples(): key names, levels, offsets (the latin-1 characters of the
# UTF-8 sign, as sfizz reads the bytes).
SHARP = "♯".encode().decode("latin-1")
NOTES_SFZ = {"I/i.sfz": "<global> hivel=100\n"
                        "<group> lokey=c4 hikey=E4\n"
                        "<region> sample=g1.wav\n"                        # 60-64, velocity 1-100 (the global's)
                        "<region> sample=g2.wav lovel=101 hivel=127\n"    # 60-64, 101-127
                        "<group>\n<region> sample=all.wav\n"              # a new group: 0-127, 1-100
                        f"<region> sample=sharp.wav key=f{SHARP}4\n"      # 66
                        "<region> sample=flat.wav lokey=bb3 hikey=b3\n"   # 58-59
                        "<region> sample=none.wav hikey=-1\n"             # no note starts it
                        "<region> sample=bad.wav key=e#4\n"               # not a note: no note starts it
                        "<region> sample=empty.wav lokey=70 hikey=69\n"   # no key
                        "<control> octave_offset=1 note_offset=-2\n"
                        "<region> sample=moved.wav lokey=48 hikey=50\n"   # +10: 58-60
                        "<region> sample=whole.wav\n"                     # 0-127 doesn't move
                        "<control> default_path=x/\n"                     # the offsets stay
                        "<region> sample=top.wav key=125\n"}              # 135, clamped: 127


class NoteSamples(unittest.TestCase):
    """sfz.note_samples(): the samples sfizz can play for given note-ons (key, velocity)."""

    def test_key_values(self):
        for text, key in [("60", 60), ("60.5", 60), ("60x", 60), (" 61 ", 61), ("c4", 60), ("C4", 60), ("c#4", 61),
                          ("db4", 61), (f"c{SHARP}4", 61), ("d♭4".encode().decode("latin-1"), 61), ("bb3", 58),
                          ("b3", 59), ("c-1", 0), ("g9", 127), ("a0", 21)]:
            self.assertEqual(Z.key_value(text), key, text)
        for text in ("-1", "128", "g#9", "e#4", "cb4", "h4", "c", "c4x", "", "x60"):
            self.assertIsNone(Z.key_value(text), text)

    def test_velocities(self):
        """velocity / 127 against lovel / 127 and hivel (+1 when 0-126, just below) / 127."""
        self.assertEqual(Z.velocities(0, 127), (1, 127))
        self.assertEqual(Z.velocities(64, 64), (64, 64))
        self.assertEqual(Z.velocities(0, 63), (1, 63))
        self.assertEqual(Z.velocities(64.5, 100), (65, 100))
        self.assertEqual(Z.velocities(0, 126.5), (1, 126))
        self.assertEqual(Z.velocities(100, 50), (1, 0))  # none
        self.assertEqual(Z.velocities(-5, 0), (1, 0))    # velocity 0 only: never sent

    def test_keys_levels_offsets(self):
        r = deps(NOTES_SFZ, "I/i.sfz")
        got = {x.sample.split("/")[-1]: (x.keys, x.vels, x.notes) for x in r.regions}
        self.assertEqual(got, {
            "g1.wav": ((60, 64), (1, 100), True), "g2.wav": ((60, 64), (101, 127), True),
            "all.wav": ((0, 127), (1, 100), True), "sharp.wav": ((66, 66), (1, 100), True),
            "flat.wav": ((58, 59), (1, 100), True), "none.wav": ((0, 127), (1, 100), False),
            "bad.wav": ((60, 60), (1, 100), False), "empty.wav": ((70, 69), (1, 100), True),
            "moved.wav": ((58, 60), (1, 100), True), "whole.wav": ((0, 127), (1, 100), True),
            "top.wav": ((127, 127), (1, 100), True)})

        def names(notes):
            return [p.split("/")[-1] for p in Z.note_samples(r, notes)]
        self.assertEqual(names([(62, 50)]), ["g1.wav", "all.wav", "whole.wav"])
        self.assertEqual(names([(62, 120)]), ["g2.wav"])
        self.assertEqual(names([(66, 10), (59, 10)]), ["all.wav", "sharp.wav", "flat.wav", "moved.wav", "whole.wav"])
        self.assertEqual(names([(127, 5)]), ["all.wav", "whole.wav", "top.wav"])
        self.assertEqual(Z.note_samples(r, []), Z.playable(r))  # nothing needed: sfizz still loads it

    def test_release_round_robins_and_conditions_kept(self):
        files = {"R.sfz": "<group> key=60 seq_length=3\n<region> sample=rr1.wav seq_position=1\n"
                          "<region> sample=rr2.wav seq_position=2\n<region> sample=rr3.wav seq_position=3\n"
                          "<group> key=60 lorand=0 hirand=0.5\n<region> sample=ra.wav\n"
                          "<group> key=60 lorand=0.5 hirand=1\n<region> sample=rb.wav\n"
                          "<group> key=60 trigger=release lovel=100\n<region> sample=rel.wav\n"  # any velocity
                          "<group> key=61 trigger=release_key\n<region> sample=rel61.wav\n"     # 61 isn't played
                          "<group> key=60 lovel=100 sw_vel=previous\n<region> sample=prev.wav\n"  # any velocity
                          "<group> key=60 lovel=100 trigger=legato\n<region> sample=legato.wav\n"  # an attack
                          "<group> key=60 sw_last=24 lovel=100\n<region> sample=ks_loud.wav\n"
                          "<group> key=60 sw_last=24\n<region> sample=ks.wav\n"                  # keyswitch kept
                          "<group> key=60 lobend=100 hibend=200\n<region> sample=bend.wav\n"     # bend kept
                          "<group> key=60 locc64=64\n<region> sample=pedal.wav\n"}               # never
        r = deps(files, "R.sfz")
        self.assertEqual(Z.note_samples(r, [(60, 50)]),
                         ["rr1.wav", "rr2.wav", "rr3.wav", "ra.wav", "rb.wav", "rel.wav", "prev.wav", "ks.wav", "bend.wav"])
        self.assertIn("legato.wav", Z.note_samples(r, [(60, 100)]))
        self.assertIn("ks_loud.wav", Z.note_samples(r, [(60, 100)]))
        self.assertEqual(Z.note_samples(r, [(61, 1)]), ["rel61.wav"])
        self.assertEqual(r.regions[0].unjudged, ["seq_length", "seq_position"])
        self.assertEqual(r.regions[7].unjudged, ["sw_vel"])
        self.assertEqual(r.regions[8].unjudged, ["trigger"])

    def test_within_playable(self):
        """Never a sample playable() leaves out (a controller range never met)."""
        files = {"P.sfz": "<group> locc64=65 hicc64=127\n<region> sample=pedal.wav\n"
                          "<group> locc64=0 hicc64=64\n<region> sample=up.wav\n<region> sample=low.wav hikey=40\n"}
        r = deps(files, "P.sfz")
        self.assertEqual(Z.note_samples(r, [(k, v) for k in range(128) for v in (1, 64, 127)]), ["up.wav", "low.wav"])
        self.assertEqual(Z.note_samples(r, [(50, 64)]), ["up.wav"])

    @unittest.skipUnless(__import__("importlib").util.find_spec("pysfizz"), "pysfizz is not installed")
    def test_agrees_with_sfizz(self):
        """sfizz itself, with every sample there: its regions' key and velocity ranges and
        triggers are deps()' (in the same order), and for every key and velocity the regions it
        matches are among note_regions()' (unless a hikey / key it can't read stops notes)."""
        import pysfizz
        files = dict(NOTES_SFZ)
        files["I/i.sfz"] += ("<region> sample=rel.wav lokey=c5 hikey=d5 trigger=release lovel=90\n"
                             "<region> sample=rk.wav key=72 trigger=release_key hivel=63.5\n"
                             "<region> sample=leg.wav key=73 trigger=legato\n"
                             "<region> sample=rr.wav key=74 seq_length=2 seq_position=2\n"
                             "<region> sample=*sine key=75\n"
                             "<region> sample=num.wav lokey=76x hikey=77.9 lovel=10.5 hivel=64e\n")
        with tempfile.TemporaryDirectory() as d:
            r = deps(files, "I/i.sfz")
            for p, t in files.items():
                (Path(d) / p).parent.mkdir(parents=True, exist_ok=True)
                (Path(d) / p).write_bytes(t.encode("latin-1"))
            for s in r.samples:
                (Path(d) / s).parent.mkdir(parents=True, exist_ok=True)
                with wave.open(str(Path(d) / s), "wb") as w:
                    w.setnchannels(1)
                    w.setsampwidth(2)
                    w.setframerate(48000)
                    w.writeframes(b"\x10\x00" * 4800)
            syn = pysfizz.Synth(48000, 256)
            self.assertTrue(syn.load_sfz_file(str(Path(d) / "I" / "i.sfz")))
            n = syn._synth.get_num_regions()
            self.assertEqual(n, len(r.regions))
            vels = []
            for i, region in enumerate(r.regions):
                data = syn._synth.get_region_data(i)
                ok = [v for v in range(1, 128) if data["lovel"] <= Z._f32(v / 127) <= data["hivel"]]
                vels.append(set(ok))
                self.assertEqual((data["lokey"], data["hikey"]), region.keys, region.sample)
                self.assertEqual((ok[0], ok[-1]) if ok else (1, 0), region.vels, region.sample)
                self.assertEqual(data["trigger"], region.trigger, region.sample)
            index = {id(x): i for i, x in enumerate(r.regions)}
            no_note = {i for i, x in enumerate(r.regions) if not x.notes}
            for k in range(128):
                at_key = syn._synth.get_regions_for_note(k)
                for v in range(1, 128):
                    theirs = {i for i in at_key if v in vels[i]}
                    mine = {index[id(x)] for x in Z.note_regions(r, [(k, v)])}
                    self.assertEqual(theirs - mine - no_note, set(), (k, v))

    @unittest.skipUnless(__import__("importlib").util.find_spec("numpy"), "numpy is not installed")
    def test_events_sent(self):
        """instruments.sfz_events(): what SfzInstrument.render sends sfizz (and generate.py
        --plan-notes records): velocity round(vel * 127) in 1-127, at least 10 ms, nothing after
        the last block."""
        with tempfile.TemporaryDirectory() as d, unittest.mock.patch.dict(os.environ, {"TGMUSIC_LIBS": d}):
            from tgmusic import instruments as I
        ev = I.sfz_events([{"t": 0.0, "dur": 0.0, "note": 60, "vel": 0.5}, {"t": 0.1, "dur": 0.2, "note": 62, "vel": 0.001},
                           {"t": 2.0, "dur": 1.0, "note": 64, "vel": 1.0}], 48000)
        self.assertEqual(ev, [(0, 1, 60, 64), (480, 0, 60, 0), (4800, 1, 62, 1), (14400, 0, 62, 0)])


class Layout(unittest.TestCase):
    def test_repo_detected(self):
        self.assertEqual(G.REPO, REPO)
        with self.assertRaises(SystemExit):
            G.outside_repo(REPO / "build" / "audio-modern", "--out")
        self.assertEqual(G.outside_repo(REPO.parent / "elsewhere", "--out"), (REPO.parent / "elsewhere").resolve())

    def test_release_layout(self):
        """A release's music-tools/ (music/, audio/, tg_audio) is not a repository: generate.py
        writes anywhere (even under the game's folder), and finds tools/audio beside it."""
        with tempfile.TemporaryDirectory() as d:
            tools = Path(d) / "race-masters" / "music-tools"
            (tools / "music" / "tgmusic").mkdir(parents=True)
            (tools / "audio").mkdir()
            (tools / "music" / "generate.py").write_text((MUSIC / "generate.py").read_text())
            for f in ("__init__.py", "progress.py", "config.py", "extract.py"):
                (tools / "music" / "tgmusic" / f).write_text((MUSIC / "tgmusic" / f).read_text())
            (tools / X.EXE).write_bytes(b"")
            code = ("import sys; sys.path.insert(0, sys.argv[1]); import generate as G; print(G.REPO);"
                    "print(G.outside_repo(sys.argv[2], 'out'));"
                    "from tgmusic import extract as X; print(X.AUDIO_TOOLS); print(X.find_tg_audio())")
            r = subprocess.run([sys.executable, "-B", "-c", code, str(tools / "music"), str(Path(d) / "race-masters" / "audio-modern")],
                               capture_output=True, text=True, env=dict(os.environ, TG_AUDIO=""))
            self.assertEqual(r.returncode, 0, r.stderr)
            lines = r.stdout.splitlines()
            self.assertEqual(lines[0], "None")
            self.assertEqual(Path(lines[1]), (Path(d) / "race-masters" / "audio-modern").resolve())
            self.assertEqual(Path(lines[2]), (tools / "audio").resolve())
            self.assertEqual(Path(lines[3]), (tools / X.EXE).resolve())

    def test_find_tg_audio(self):
        with tempfile.TemporaryDirectory() as d:
            exe = Path(d) / X.EXE
            exe.write_bytes(b"")
            self.assertEqual(X.find_tg_audio(str(exe)), exe.resolve())
            with self.assertRaises(SystemExit):
                X.find_tg_audio(str(Path(d) / "missing"))

    def test_stale_temp_removed(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)
            (out / "variations" / "01_title").mkdir(parents=True)
            tmp = out / "variations" / "01_title" / ".01_title__faithful.tmp.wav"
            keep = out / "variations" / "01_title" / "01_title__faithful.ogg"
            tmp.write_bytes(b"x")
            keep.write_bytes(b"x")
            G.remove_stale_temp(out)
            self.assertFalse(tmp.exists())
            self.assertTrue(keep.exists())

    def test_styles(self):
        self.assertEqual(G.parse_styles("all"), G.ALL_STYLES)
        self.assertEqual(G.parse_styles("faithful,modern,band"), ["faithful", "D", "C"])
        with self.assertRaises(SystemExit):
            G.parse_styles("polka")


class SetupTools(unittest.TestCase):
    def test_python_range(self):
        lo, hi = S.PY_RANGE["win32" if os.name == "nt" else "other"]
        self.assertTrue(S.in_range(lo))
        self.assertTrue(S.in_range(hi))
        self.assertFalse(S.in_range((3, 13)))
        self.assertFalse(S.in_range((3, 9)))
        self.assertFalse(S.in_range(None))

    def test_pip_pin(self):
        self.assertRegex(S.PIP["sha256"], r"^[0-9a-f]{64}$")
        self.assertTrue(S.PIP["url"].startswith("https://files.pythonhosted.org/") and S.PIP["url"].endswith(
            f"pip-{S.PIP['version']}-py3-none-any.whl"))

    def test_bad_download_is_refused(self):
        RangeHandler.data, RangeHandler.etag, RangeHandler.ranges, RangeHandler.stop_after = b"not pip", '"x"', True, None
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), RangeHandler)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        try:
            with tempfile.TemporaryDirectory() as d, open(Path(d) / "log", "w") as log:
                out = S.Out(False, log)
                with self.assertRaises(S.Failure) as cm:
                    S.download(f"http://127.0.0.1:{srv.server_address[1]}/pip.whl", Path(d) / "pip.whl", "0" * 64, 7, out, "pip")
                self.assertEqual(cm.exception.code, "hash")
                self.assertFalse((Path(d) / "pip.whl").exists())
        finally:
            srv.shutdown()
            srv.server_close()


HAVE_RENDER = all(__import__("importlib").util.find_spec(m) for m in
                  ("numpy", "scipy", "pedalboard", "pyloudnorm", "soundfile", "numba", "mido"))


@unittest.skipUnless(HAVE_RENDER, "the render packages are not installed")
class PartsInThreads(unittest.TestCase):
    """A track's parts: Surge XT parts played ahead in threads, each on an instance of its own,
    taken in the parts' order; the mix's arithmetic done in place gives the same numbers."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        d = Path(cls.tmp.name)
        cls.env = unittest.mock.patch.dict(os.environ, {"TGMUSIC_LIBS": str(d / "libs"), "TGMUSIC_WORK": str(d / "work"),
                                                        "NUMBA_CACHE_DIR": str(d / "numba")})
        cls.env.start()
        from tgmusic import instruments as I
        from tgmusic import render as R
        cls.I, cls.R = I, R

    @classmethod
    def tearDownClass(cls):
        cls.env.stop()
        cls.tmp.cleanup()

    def test_parts_in_order(self):
        import numpy as np
        from concurrent.futures import ThreadPoolExecutor
        I, R = self.I, self.R
        width, total = 3, 64
        state = {"playing": 0, "most": 0, "free": width}
        lock = threading.Lock()

        class FakePatch:
            def __init__(self, i):
                self.i = i

            def start(self, ns, total_, pool, wait=True):
                with lock:
                    if not state["free"]:
                        if not wait:
                            return None
                if not state["free"]:
                    while not state["free"]:
                        time.sleep(0.001)
                with lock:
                    state["free"] -= 1

                def play():
                    with lock:
                        state["playing"] += 1
                        state["most"] = max(state["most"], state["playing"])
                    time.sleep(0.02 * (3 - self.i % 3))  # later parts end first
                    with lock:
                        state["playing"] -= 1
                        state["free"] += 1
                    return np.full((total_, 2), self.i, np.float32)
                return pool.submit(play)
        kinds = ["surge", "sfz", "surge", "surge", "synth", "surge", "sfz", "surge", "sfz", "surge"]
        played = [({"name": f"p{i}", "spec": {"type": k, "patch": i, "sfz": f"inst{i % 2}.sfz"}}, [{"i": i}])
                  for i, k in enumerate(kinds)]
        order, loaded = [], {}

        def other(spec, ns, total_):
            order.append(ns[0]["i"])
            return np.full((total_, 2), ns[0]["i"], np.float32)
        with unittest.mock.patch.object(R, "_surge_part", lambda spec, ns: (FakePatch(spec["patch"]), ns)), \
                unittest.mock.patch.dict(R.RENDER, {"sfz": other, "synth": other}), \
                unittest.mock.patch.dict(R._SFZ, {"inst0.sfz": "a", "inst1.sfz": "b"}, clear=True), \
                ThreadPoolExecutor(width) as pool:
            for p, ns, box in R.rendered(played, total, pool, width):
                x = box.pop()
                self.assertEqual(float(x[0, 0]), ns[0]["i"])
                loaded[ns[0]["i"]] = sorted(R._SFZ)
                order.append(("mixed", ns[0]["i"]))
        self.assertEqual([i for kind, i in (o for o in order if isinstance(o, tuple))], list(range(len(kinds))))
        self.assertEqual([o for o in order if not isinstance(o, tuple)], [1, 4, 6, 8])  # the others in their order
        self.assertLessEqual(state["most"], width)
        self.assertGreater(state["most"], 1)  # some did play at once
        # An .sfz instrument is let go once its last part has played: inst1 plays part 1 only,
        # inst0 parts 6 and 8.
        self.assertEqual(loaded[0], ["inst0.sfz", "inst1.sfz"])
        self.assertEqual(loaded[1], ["inst0.sfz"])
        self.assertEqual(loaded[6], ["inst0.sfz"])
        self.assertEqual(loaded[8], [])

    def test_surge_pool(self):
        I = self.I
        made = []
        pool = I._SurgePool()
        pool.limit = 2
        with unittest.mock.patch.object(I, "_load_surge", lambda: made.append(object()) or made[-1]):
            a, b = pool.take(), pool.take()
            self.assertEqual(len(made), 2)
            self.assertIsNone(pool.take(wait=False))
            threading.Timer(0.1, pool.release, [a]).start()
            self.assertIs(pool.take(), a)  # waits for one to be given back
            pool.release(a)
            pool.release(b)
            pool.clear()
            self.assertEqual((pool.free, pool.count), ([], 0))
            pool.take()
            self.assertEqual(len(made), 3)

    def test_surge_user_folder(self):
        I = self.I
        here = Path(self.tmp.name) / "libs" / "surge-xt" / "SurgeXTUserData"
        with unittest.mock.patch.object(I, "SURGE_USER", here):  # (instruments may have been loaded elsewhere)
            self._surge_user_folder(I)

    def _surge_user_folder(self, I):
        f = I.surge_user_folder() / "SurgeXTUserDefaults.xml"
        self.assertEqual(f.parent.name, "SurgeXTUserData")
        self.assertEqual(f.parent.parent.name, "surge-xt")  # beside the plug-in, where Surge looks for it
        self.assertEqual(f.read_text(encoding="utf-8"), I.SURGE_DEFAULTS)
        self.assertTrue((f.parent / "Patches").is_dir())
        f.write_text('<defaults version="1"><default key="x" value="1" type="0"/></defaults>', encoding="utf-8")
        I.surge_user_folder()
        self.assertEqual(f.read_text(encoding="utf-8"), I.SURGE_DEFAULTS)  # every default as Surge's own

    def test_in_place_mix_is_the_same(self):
        """The stem arithmetic in place (render_song) against the expressions it replaced."""
        import numpy as np
        R = self.R
        rng = np.random.default_rng(3)
        x0 = (rng.standard_normal((48000, 2)) * rng.choice([1e-3, 0.3, 1e-38], (48000, 1))).astype(np.float32)
        x0 = np.asfortranarray(x0)  # as a plug-in's output comes
        rms, gain, pan, rev = 0.0123, -7, -0.3, 0.25
        old = R.pan_st(x0 * (R.db(-18) / max(rms, 1e-6)) * R.db(gain), pan)
        new = np.array(x0, np.float32, copy=True, order="C")
        new *= R.db(-18) / max(rms, 1e-6)
        new *= R.db(gain)
        R.pan_st(new, pan, out=new)
        self.assertEqual(old.tobytes(), np.ascontiguousarray(new).tobytes())
        send_old, send_new, tmp = np.zeros_like(new), np.zeros_like(new), np.empty_like(new)
        send_old += old * rev
        np.multiply(new, rev, out=tmp)
        send_new += tmp
        send_old += old * 0.0  # a send of 0: skipped, the same
        self.assertEqual(send_old.tobytes(), send_new.tobytes())
        music, drums, r, d, e = [(rng.standard_normal((4800, 2)) * 0.1).astype(np.float32) for _ in range(5)]
        mix_old = music + drums + r * 0.9 + d * 0.6 + e * 0.7
        mix = music.copy()
        mix += drums
        for ret, level in ((r.copy(), 0.9), (d.copy(), 0.6), (e.copy(), 0.7)):
            ret *= level
            mix += ret
        self.assertEqual(mix_old.tobytes(), mix.tobytes())

    def test_surge_midi_as_mido_gave_it(self):
        """The MIDI a Surge XT part is sent: what pedalboard made of the mido messages before."""
        import mido
        import numpy as np
        from pedalboard.midi_utils import normalize_midi_messages
        rng = np.random.default_rng(7)
        notes = []
        for i in range(400):
            n = {"t": float(rng.uniform(0, 30)), "dur": float(rng.choice([0.0, 0.005, 0.3, 2.0])),
                 "note": int(rng.integers(0, 128)), "vel": float(rng.uniform(0, 1.2))}
            if i % 7 == 0:
                n["bends"] = [(float(rng.uniform(0, 0.3)), float(rng.uniform(-14, 14))) for _ in range(3)]
            notes.append(n)
        notes.append(dict(notes[0], note=np.int64(64)))  # numpy integers, as the arrangement may give

        def before(notes, bend_range=12):
            msgs = []
            for n in notes:
                v = int(np.clip(round(n["vel"] * 127), 1, 127))
                msgs.append((n["t"], 1, mido.Message("note_on", note=n["note"], velocity=v)))
                msgs.append((n["t"] + max(n["dur"], 0.01), 0, mido.Message("note_off", note=n["note"])))
                for bt, semis in n.get("bends", []):
                    val = int(np.clip(semis / bend_range * 8191, -8192, 8191))
                    msgs.append((n["t"] + bt, 2, mido.Message("pitchwheel", pitch=val)))
                if n.get("bends"):
                    msgs.append((n["t"] + max(n["dur"], 0.01) + 1e-4, 3, mido.Message("pitchwheel", pitch=0)))
            msgs.sort(key=lambda m: (m[0], m[1]))
            return [m.copy(time=t) for t, _, m in msgs]
        self.assertEqual(normalize_midi_messages(self.I.SurgePatch._messages(notes, 12)),
                         normalize_midi_messages(before(notes)))
        for bad, error in ((128, ValueError), (-1, ValueError), (60.0, TypeError)):
            with self.assertRaises(error):
                before([dict(notes[0], note=bad)])
            with self.assertRaises(error):
                self.I.SurgePatch._messages([dict(notes[0], note=bad)], 12)

    def test_surge_state_encoding(self):
        """surgestate.jenc, three bytes at a time, as the bit by bit original."""
        from tgmusic import surgestate as SS

        def before(data):
            n = len(data)
            bits = n * 8
            chars = []
            for i in range(0, bits, 6):
                v = 0
                for b in range(6):
                    bi = i + b
                    if bi < bits and data[bi >> 3] & (1 << (bi & 7)):
                        v |= 1 << b
                chars.append(SS.ALPH[v])
            return f"{n}." + "".join(chars)
        rng = __import__("random").Random(11)
        for n in list(range(0, 40)) + [30085, 30086, 30087]:
            data = bytes(rng.randrange(256) for _ in range(n))
            self.assertEqual(SS.jenc(data), before(data))
            self.assertEqual(SS.jdec(SS.jenc(data)), data)

    def test_true_peak_by_channel(self):
        import numpy as np
        from scipy.signal import resample_poly
        R = self.R
        y = np.asfortranarray((np.random.default_rng(5).standard_normal((48000, 2)) * 0.2).astype(np.float32))
        old = 20 * math.log10(max(1e-9, float(np.abs(resample_poly(y, 4, 1, axis=0)).max())))
        for threads in ("1", "4"):
            with unittest.mock.patch.dict(os.environ, {"TGMUSIC_THREADS": threads}):
                self.assertEqual(R.true_peak_db(y), old)


if __name__ == "__main__":
    unittest.main()
