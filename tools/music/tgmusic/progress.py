"""Progress for a program watching the generator (the launcher's Music card), cancelling, and
the child processes the generator starts.

--progress-json: standard output carries only progress lines, one flat JSON object per line
(strings, numbers, booleans; no nesting), each with an "ev" naming the event:

  {"ev":"plan","tracks":149,"done":10,"todo":139,"download_mb":1100,"download_max_mb":3168,"jobs":2,"eta_s":24000}
      first the usual download and the most it may be; once the tracks are planned, again
      with "planned":true and download_mb the exact download
  {"ev":"stage","stage":"notes"|"plan"|"libraries"|"render","n":1,"of":4,"text":"...","eta_s":...}
      in that order: the notes, the plan, the libraries, the tracks
  {"ev":"notes","elapsed_s":120,"eta_s":...}
  {"ev":"planning","i":40,"of":139,"eta_s":...}
  {"ev":"download","lib":"vcsl","i":3,"of":9,"got":B,"total":B,"all_got":B,"all_total":B,"rate":B/s,"eta_s":...}
  {"ev":"unpack","lib":"vcsl","i":3,"of":9,"eta_s":...}
  {"ev":"track","i":12,"of":149,"label":"...","status":"done"|"failed","secs":150,"eta_s":...}
  {"ev":"render","done":12,"of":149,"running":"label, label","eta_s":...}
  {"ev":"done","tracks":149,"failed":0,"mb":650,"minutes":380,"late_samples":0}
  {"ev":"cancelled"}
  {"ev":"error","code":"network"|"hash"|"disk"|"plugin"|"tracks"|"rom"|"consent"|"other","text":"...","log":"<file>"}

Everything a person would read goes to a log file instead (and the render processes write
there too). Cancel: a line "cancel" on standard input, or standard input closing (the
launcher closed): the generator stops its render processes, keeps every finished track and
download, and exits with EXIT_CANCELLED. Run again to go on from there.
"""
import json
import os
import subprocess
import sys
import threading
import time

EXIT_OK, EXIT_ERROR, EXIT_USAGE, EXIT_CANCELLED, EXIT_TRACKS = 0, 1, 2, 3, 4

# Python flags for every Python process started (render workers, tools/audio): no bytecode
# files, no user site-packages (a player's own packages must not shadow the pinned ones), no
# PYTHON* variables, and UTF-8 text files on every system (Windows' default is the locale's).
PYFLAGS = ["-B", "-s", "-E", "-X", "utf8"]


class Cancelled(Exception):
    """The run was cancelled (by the watching program, or standard input closed)."""


CANCEL_WORDS = ("cancel", "stop", "quit")


def wait_for_cancel():
    """Returns once standard input says "cancel" (or "stop", "quit") or closes.

    On Windows a pipe is polled (PeekNamedPipe) and read only when it holds data, never left
    with a read pending: a thread blocked reading a synchronous pipe makes every other use of
    that handle wait for the read to end, and loading a DLL whose C runtime looks at the
    standard handles as it starts (numpy's OpenBLAS, built on msvcrt) then hangs until the
    launcher writes (the build stopped for good while importing numpy). Standard library only
    (setup_tools.py uses it too)."""
    if os.name == "nt":
        import ctypes
        import msvcrt
        from ctypes import wintypes
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        try:
            fd = sys.stdin.fileno()
            h = wintypes.HANDLE(msvcrt.get_osfhandle(fd))
        except (AttributeError, OSError, ValueError):
            return
        if k32.GetFileType(h) == 3:  # FILE_TYPE_PIPE
            avail, rest = wintypes.DWORD(), b""
            while k32.PeekNamedPipe(h, None, 0, None, ctypes.byref(avail), None):  # (fails once it's closed)
                if not avail.value:
                    time.sleep(0.2)
                    continue
                data = os.read(fd, avail.value)  # that much is there: returns at once
                if not data:
                    return
                *lines, rest = (rest + data).split(b"\n")
                if any(ln.strip().lower().decode("ascii", "replace") in CANCEL_WORDS for ln in lines):
                    return
            return
    try:
        for line in sys.stdin:
            if line.strip().lower() in CANCEL_WORDS:
                return
    except (OSError, ValueError):
        pass


class Progress:
    def __init__(self, json_out=None):
        self.out = json_out  # a text stream for the JSON lines, or None (plain output)
        self.cancelled = threading.Event()
        self.lock = threading.Lock()

    @property
    def json(self):
        return self.out is not None

    def emit(self, ev, **fields):
        if self.out is None:
            return
        obj = {"ev": ev}
        for k, v in fields.items():
            if v is None:
                continue
            if isinstance(v, float):
                v = round(v, 3)
            elif not isinstance(v, (int, bool, str)):
                v = str(v)
            obj[k] = v
        line = json.dumps(obj, separators=(",", ":"), ensure_ascii=True)
        with self.lock:
            try:
                self.out.write(line + "\n")
                self.out.flush()
            except (OSError, ValueError):
                # Nobody is reading any more (the launcher was closed): stop cleanly.
                self.out = None
                self.cancelled.set()

    def watch_stdin(self):
        """Cancel when standard input says "cancel" or closes. Only for --progress-json runs."""
        def run():
            wait_for_cancel()
            self.cancelled.set()
        threading.Thread(target=run, name="cancel-watch", daemon=True).start()

    def check(self):
        if self.cancelled.is_set():
            raise Cancelled()


def fmt_eta(seconds):
    """'about 3 h 20 min', '12 min', 'under a minute' (for the plain-text output)."""
    if seconds is None:
        return "unknown"
    s = int(max(0, seconds))
    if s < 60:
        return "under a minute"
    m = (s + 30) // 60
    if m < 60:
        return f"{m} min"
    h, m = divmod(m, 60)
    return f"{h} h {m:02d} min"


class Eta:
    """Time left for the whole run, from what is left of each step.

    Before anything is measured it uses the numbers in tools/music/README.md (downloads at
    8 MB/s, unpacking 40 s a GB, the notes 11 minutes, planning 1.5 s a track, a track 80 s
    per job: all 149 rendered in 34 minutes, --jobs auto on a 12-thread laptop). Once tracks
    are done, their mean time at the number running."""
    TRACK_S = 80.0
    RATE = 8e6
    UNPACK_S_PER_GB = 40.0
    NOTES_S = 660.0
    PLAN_S = 1.5

    def __init__(self, download_bytes=0, notes=True, tracks=0, jobs=1, plan=0, plan_jobs=1):
        self.download_left = float(download_bytes)
        self.unpack_left_gb = download_bytes / 1e9
        self.notes_left = self.NOTES_S if notes else 0.0
        self.plan_left = plan * self.PLAN_S / max(1, plan_jobs)
        self.tracks_left = tracks
        self.jobs = max(1, jobs)
        self.rate = None
        self.track_times = []

    def seconds(self):
        rate = self.rate or self.RATE
        avg = sum(self.track_times) / len(self.track_times) if self.track_times else self.TRACK_S
        return (self.download_left / rate + self.unpack_left_gb * self.UNPACK_S_PER_GB + self.notes_left
                + self.plan_left + self.tracks_left * avg / self.jobs)


# --- Child processes ---------------------------------------------------------------------

def lower_priority():
    """Render below normal priority, so the game (or anything else) stays smooth meanwhile.
    The processes started from here inherit it. On Windows, also at full speed (full_speed)."""
    try:
        if os.name == "nt":
            import ctypes
            BELOW_NORMAL_PRIORITY_CLASS = 0x4000
            k32 = ctypes.windll.kernel32
            k32.SetPriorityClass(k32.GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)
        else:
            os.nice(10)
    except (OSError, AttributeError, ImportError):
        pass
    full_speed()


def full_speed(process=None):
    """Windows: this process (or `process`, a subprocess.Popen) out of power throttling
    ("efficiency mode", EcoQoS). Windows 10/11 runs a process without a window of its own on
    the efficiency cores at a low clock when it sees fit, and decides it per process, so each
    render process asks for itself: measured on a laptop with 2 performance and 8 efficiency
    cores, the same work took 2 to 3 times as long throttled. The priority stays below normal
    (lower_priority): whatever the person does meanwhile comes first. No-op elsewhere."""
    if os.name != "nt":
        return
    try:
        import ctypes
        from ctypes import wintypes

        class State(ctypes.Structure):  # PROCESS_POWER_THROTTLING_STATE
            _fields_ = [("Version", wintypes.ULONG), ("ControlMask", wintypes.ULONG), ("StateMask", wintypes.ULONG)]
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.GetCurrentProcess.restype = wintypes.HANDLE
        k32.SetProcessInformation.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        handle = k32.GetCurrentProcess() if process is None else int(process._handle)
        # ProcessPowerThrottling (4); EXECUTION_SPEED controlled (1), and off (0).
        st = State(1, 0x1, 0x0)
        k32.SetProcessInformation(handle, 4, ctypes.byref(st), ctypes.sizeof(st))
    except (OSError, AttributeError, ImportError, ValueError, TypeError):
        pass


# --- Memory --------------------------------------------------------------------------------

MB = 1 << 20


def memory_mb():
    """(total, available) physical memory of this computer, MB (2^20 bytes), each None if it
    can't be read."""
    try:
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes

            class Status(ctypes.Structure):  # MEMORYSTATUSEX
                _fields_ = [("dwLength", wintypes.DWORD), ("dwMemoryLoad", wintypes.DWORD)] + [
                    (n, ctypes.c_uint64) for n in ("ullTotalPhys", "ullAvailPhys", "ullTotalPageFile", "ullAvailPageFile",
                                                   "ullTotalVirtual", "ullAvailVirtual", "ullAvailExtendedVirtual")]
            st = Status()
            st.dwLength = ctypes.sizeof(st)
            if ctypes.WinDLL("kernel32").GlobalMemoryStatusEx(ctypes.byref(st)):
                return st.ullTotalPhys / MB, st.ullAvailPhys / MB
            return None, None
        info = {}
        with open("/proc/meminfo", encoding="ascii") as f:
            for line in f:
                k, _, v = line.partition(":")
                info[k] = int(v.split()[0]) * 1024 / MB  # kB
        return info.get("MemTotal"), info.get("MemAvailable")
    except (OSError, ValueError, AttributeError, ImportError, IndexError):
        return None, None


def _win_memory(handle):
    """PROCESS_MEMORY_COUNTERS of a process handle (Windows), or None."""
    import ctypes
    from ctypes import wintypes

    class Counters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
            (n, ctypes.c_size_t) for n in ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                                           "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
                                           "PagefileUsage", "PeakPagefileUsage")]
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.K32GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
    c = Counters()
    c.cb = ctypes.sizeof(c)
    return c if k32.K32GetProcessMemoryInfo(handle, ctypes.byref(c), c.cb) else None


def process_mb(pid):
    """The memory a process (by id) has in use now (resident set / working set), MB, or None."""
    try:
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes
            k32 = ctypes.WinDLL("kernel32", use_last_error=True)
            k32.OpenProcess.restype = wintypes.HANDLE
            h = k32.OpenProcess(0x1000 | 0x0010, False, int(pid))  # QUERY_LIMITED_INFORMATION | VM_READ
            if not h:
                return None
            try:
                c = _win_memory(h)
                return c.WorkingSetSize / MB if c else None
            finally:
                k32.CloseHandle(wintypes.HANDLE(h))
        with open(f"/proc/{int(pid)}/statm", encoding="ascii") as f:
            return int(f.read().split()[1]) * os.sysconf("SC_PAGE_SIZE") / MB
    except (OSError, ValueError, AttributeError, ImportError, IndexError):
        return None


def peak_mb():
    """This process's peak memory (resident set / working set), MB, or 0 if unknown."""
    try:
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes
            k32 = ctypes.WinDLL("kernel32", use_last_error=True)
            k32.GetCurrentProcess.restype = wintypes.HANDLE
            c = _win_memory(k32.GetCurrentProcess())
            return c.PeakWorkingSetSize / MB if c else 0.0
        import resource
        rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        return rss * 1024 / MB if sys.platform != "darwin" else rss / MB
    except (OSError, ValueError, AttributeError, ImportError):
        return 0.0


# --- How much at once ------------------------------------------------------------------------
#
# A track's render process at its fullest, MB: the largest measured peak (Stockholm D, a long
# track with many sampled instruments: 1.48 GB on Windows, 1.57 on Linux; most tracks 0.2-1.1
# GB); and the memory a build leaves to everything else. See tools/music/README.md ("Time,
# disk and memory").
TRACK_MB = 1600
SPARE_MB = 1500


def concurrency(jobs="auto", threads="auto", cpus=None, total_mb=None):
    """(tracks rendered at once, threads each track's parts play in) for --jobs and --threads,
    each "auto" or a number. auto: half the logical processors (the other half for the parts
    each plays at once, and for the person using the computer), at most as many as the memory
    holds (TRACK_MB each, SPARE_MB left); each track's share of the processors for its parts,
    2 to 4. Beyond always_at_once(), render_all starts a track only when the memory available
    holds it, too (room_for_track)."""
    cpus = max(1, cpus or os.cpu_count() or 2)
    if str(jobs) == "auto":
        if total_mb is None:
            total_mb = memory_mb()[0]
        by_memory = int((total_mb - SPARE_MB) // TRACK_MB) if total_mb else 2
        jobs = min(max(1, cpus // 2), by_memory)
    jobs = max(1, int(jobs))
    if str(threads) == "auto":
        threads = min(4, max(2, cpus // jobs)) if cpus >= 4 else 1
    return jobs, max(1, int(threads))


def always_at_once(total_mb=None):
    """Tracks that may render at once whatever the memory free (when --jobs allows): a third of
    what the memory holds (TRACK_MB each, SPARE_MB left), at least two. The memory "available"
    leaves out what the system would take back from idle programs, so it alone holds back too
    much (WSL running: 1-3 GB free of 16 and one track at a time); and two tracks before these
    changes could take 2.9 GB each, more than three take now."""
    if total_mb is None:
        total_mb = memory_mb()[0]
    holds = int((total_mb - SPARE_MB) // TRACK_MB) if total_mb else 0
    return max(2, holds // 3)


def room_for_track(running, avail_mb=None, sizes=None, always=None):
    """Whether one more track may start, with the render processes `running` (their process
    ids): always while fewer than `always` (always_at_once()) run; beyond, the memory
    available now, less what each running one may still take up to TRACK_MB, must hold TRACK_MB
    more and leave half of SPARE_MB. avail_mb / sizes ({pid: MB in use}) for testing; read from
    the system otherwise."""
    if len(running) < (always if always is not None else always_at_once()):
        return True
    if avail_mb is None:
        avail_mb = memory_mb()[1]
        if avail_mb is None:
            return True
    used = sizes if sizes is not None else {pid: process_mb(pid) for pid in running}
    promised = sum(max(0.0, TRACK_MB - (used.get(pid) or 0.0)) for pid in running)
    return avail_mb - promised >= TRACK_MB + SPARE_MB / 2


_JOB = None


def kill_children_with_me():
    """Windows: put this process in a job object that ends every process started from here
    (render workers, tools/audio, ffmpeg) when this one ends, however it ends. Elsewhere the
    children get their own process group (popen_kwargs) and are stopped with it."""
    global _JOB
    if os.name != "nt" or _JOB is not None:
        return
    try:
        import ctypes
        from ctypes import wintypes
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.CreateJobObjectW.restype = wintypes.HANDLE
        k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        k32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        k32.GetCurrentProcess.restype = wintypes.HANDLE

        class BASIC(ctypes.Structure):
            _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                        ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                        ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                        ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD),
                        ("SchedulingClass", wintypes.DWORD)]

        class IO(ctypes.Structure):
            _fields_ = [(n, ctypes.c_uint64) for n in ("ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
                                                         "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]

        class EXTENDED(ctypes.Structure):
            _fields_ = [("BasicLimitInformation", BASIC), ("IoInfo", IO), ("ProcessMemoryLimit", ctypes.c_size_t),
                        ("JobMemoryLimit", ctypes.c_size_t), ("PeakProcessMemoryUsed", ctypes.c_size_t),
                        ("PeakJobMemoryUsed", ctypes.c_size_t)]

        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000
        JobObjectExtendedLimitInformation = 9
        job = k32.CreateJobObjectW(None, None)
        if not job:
            return
        info = EXTENDED()
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not k32.SetInformationJobObject(job, JobObjectExtendedLimitInformation, ctypes.byref(info), ctypes.sizeof(info)):
            return
        if k32.AssignProcessToJobObject(job, k32.GetCurrentProcess()):
            _JOB = job  # kept open for this process's life: closing it (at exit) ends the children
    except (OSError, AttributeError, ImportError, ValueError):
        pass


def die_with_parent():
    """Linux: end this process (a render worker) if the generator that started it ends, even
    when it was killed outright (Windows does this with kill_children_with_me's job object)."""
    if not sys.platform.startswith("linux"):
        return
    try:
        import ctypes
        import signal
        PR_SET_PDEATHSIG = 1
        ppid = os.getppid()
        ctypes.CDLL(None, use_errno=True).prctl(PR_SET_PDEATHSIG, int(signal.SIGTERM), 0, 0, 0)
        if os.getppid() != ppid:  # the parent ended before the call
            os._exit(1)
    except (OSError, AttributeError, ValueError):
        pass


def hold_lock(path):
    """An open file locked for this process's life (the system lets go of it when the process
    ends, however it ends), or None if another process holds it: one build at a time per cache."""
    path.parent.mkdir(parents=True, exist_ok=True)
    f = open(path, "a+")
    try:
        if os.name == "nt":
            import msvcrt
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        f.close()
        return None
    return f


def open_log(path):
    """The log file `path` for appending (UTF-8 text, line-buffered), as the generator and the
    processes it gives it to as their output share it. On Windows it is opened for appending
    only (FILE_APPEND_DATA), so that every write by anyone lands at its end: opened as Python
    opens it, a child given it writes at a position it shares with the parent's handle, behind
    what the generator wrote meanwhile through its own handle, and wrote over it (a build's log
    lost the "[n/149] ... done in" lines). Elsewhere O_APPEND does this already."""
    if os.name == "nt":
        try:
            import ctypes
            import msvcrt
            from ctypes import wintypes
            k32 = ctypes.WinDLL("kernel32", use_last_error=True)
            k32.CreateFileW.restype = wintypes.HANDLE
            k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
                                        wintypes.DWORD, wintypes.HANDLE]
            FILE_APPEND_DATA, SYNCHRONIZE, SHARE_ALL, OPEN_ALWAYS, NORMAL = 0x4, 0x100000, 0x7, 4, 0x80
            h = k32.CreateFileW(str(path), FILE_APPEND_DATA | SYNCHRONIZE, SHARE_ALL, None, OPEN_ALWAYS, NORMAL, None)
            if h and h != wintypes.HANDLE(-1).value:
                fd = msvcrt.open_osfhandle(h, os.O_APPEND | os.O_WRONLY)
                return open(fd, "a", encoding="utf-8", buffering=1)
        except (OSError, AttributeError, ImportError, ValueError):
            pass
    return open(path, "a", encoding="utf-8", buffering=1)


def popen_kwargs():
    """Keyword arguments for subprocess.Popen: no console window on Windows (the children
    share the generator's hidden one), their own process group elsewhere (so a cancel stops
    a child and everything it started)."""
    if os.name == "nt":
        return {"creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)}
    return {"start_new_session": True}


def stop_process(p, wait=10.0):
    """Stop a child started with popen_kwargs() and whatever it started."""
    if p.poll() is not None:
        return
    try:
        if os.name == "nt":
            p.terminate()  # the job object ends its children when the generator exits
        else:
            import signal
            os.killpg(p.pid, signal.SIGTERM)
    except (OSError, ProcessLookupError):
        pass
    try:
        p.wait(wait)
    except subprocess.TimeoutExpired:
        try:
            if os.name == "nt":
                p.kill()
            else:
                import signal
                os.killpg(p.pid, signal.SIGKILL)
        except (OSError, ProcessLookupError):
            pass
        p.wait()


def run_cancellable(cmd, progress, log=None, env=None, heartbeat=None, every=5.0):
    """Runs cmd to its end; output to the log file. Raises Cancelled (after stopping it) if
    the run is cancelled meanwhile. heartbeat(elapsed seconds) is called every `every` s.
    Returns the exit code."""
    out = open_log(log) if log else subprocess.DEVNULL
    try:
        p = subprocess.Popen([str(c) for c in cmd], stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                             env=env, **popen_kwargs())
        full_speed(p)
        t0 = last = time.time()
        try:
            while p.poll() is None:
                if progress.cancelled.wait(0.25):
                    raise Cancelled()
                if heartbeat and time.time() - last >= every:
                    last = time.time()
                    heartbeat(last - t0)
        except BaseException:  # cancelled, Ctrl+C: the child (in its own group) stops too
            stop_process(p)
            raise
        return p.returncode
    finally:
        if log:
            out.close()
