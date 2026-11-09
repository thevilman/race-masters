"""Sample-library and plugin renderers.

* SfzInstrument  - any .sfz (Karoryfer CC0 drums, Sonatina orchestra, VSCO 2 CE, ...) via pysfizz
* Sf2Instrument  - GeneralUser GS (General MIDI) via tinysoundfont, for world/ethnic colours
* SurgePatch     - Surge XT VST3 via pedalboard, factory .fxp injected into the plugin state

Every renderer takes a list of note dicts {t, dur, note, vel(0-1), [bend]} in seconds and
returns stereo float32 (n, 2) at 48 kHz. The libraries are in the cache folder
(libraries.py), found through config.lib().
"""
from pathlib import Path

import numpy as np

from . import config
from . import surgestate

SR = config.SR

SURGE_VST = config.lib("surge-xt") / "Surge XT.vst3"
SURGE_PATCHES = config.lib("surge-xt") / "Surge Synth Team" / "SurgeXTData" / "patches_factory"
SSO = config.lib("sso") / "Sonatina Symphonic Orchestra"
DRUMS = config.lib("big-rusty-drums") / "Programs"
GM_SF2 = config.lib("generaluser-gs") / "GeneralUser-GS.sf2"

BLOCK = 256


def _length(notes, tail):
    end = max((n["t"] + n["dur"] for n in notes), default=0.0)
    return int((end + tail) * SR) + BLOCK


def _need(path):
    if not Path(path).exists():
        raise FileNotFoundError(f"{path} is missing: run generate.py again to fetch the instrument libraries")
    return path


def sfz_events(notes, n_total):
    """The MIDI events SfzInstrument.render sends sfizz for `notes` in a render of n_total
    samples, in the order sent: (sample, 1 note-on / 0 note-off, key, velocity 1-127). A note
    lasts at least 10 ms; an event after the last block is not sent. (generate.py
    --plan-notes records the note-ons from here.)"""
    ev = []
    for n in notes:
        on = int(n["t"] * SR)
        off = int((n["t"] + max(n["dur"], 0.01)) * SR)
        v = int(np.clip(round(n["vel"] * 127), 1, 127))
        ev.append((on, 1, n["note"], v))
        ev.append((off, 0, n["note"], 0))
    ev.sort(key=lambda e: (e[0], e[1]))
    end = (n_total // BLOCK + 1) * BLOCK
    return [e for e in ev if e[0] < end]


_EXPECTED = {}  # instrument path -> the note-ons its track's parts will send (expect())


def expect(path, notes, n_total):
    """render_song, before a track's parts render: the notes the instrument at `path` will be
    sent, so that it loads once with every sample all of them need."""
    _EXPECTED.setdefault(str(Path(path)), set()).update(
        (k, v) for _, kind, k, v in sfz_events(notes, n_total) if kind == 1)


class SfzInstrument:
    """An .sfz instrument, loaded (needs.prepare) at its first render with the samples the
    notes of that render and those expect()ed for it need, each read whole into memory: the
    same notes give the same audio every time. Notes that need more samples than it was
    loaded with (they shouldn't: expect() names them all) load it again, with them."""

    def __init__(self, path, voices=128):
        self.path, self.voices, self.s, self.notes, self.files = Path(path), voices, None, set(), None

    def _load(self, notes):
        import pysfizz
        from . import needs
        notes = set(notes) | _EXPECTED.get(str(self.path), set())
        if needs.where(self.path) is None:
            _need(self.path)
        with needs.prepare(self.path, notes) as loading:
            s = pysfizz.Synth(SR, BLOCK)
            s._synth.set_num_voices(self.voices)
            s._synth.set_sample_quality(3) if hasattr(s._synth, "set_sample_quality") else None
            if not s.load_sfz_file(str(loading.root)):
                raise RuntimeError(f"cannot load {self.path}")
            # Offline: nothing is waited for (every sample is in memory already).
            if hasattr(s._synth, "enable_freewheeling"):
                s._synth.enable_freewheeling()
        self.s, self.notes, self.files = s, notes, loading.files

    def _ready(self, ons):
        if self.s is None:
            self._load(ons)
        elif not ons <= self.notes:
            from . import needs
            more = needs.needed(self.path, ons)
            if more is not None and not more <= self.files:
                print(f"    {self.path.name}: notes it wasn't loaded for: loading it again with their samples", flush=True)
                self._load(self.notes | ons)
            else:
                self.notes |= ons

    def render(self, notes, tail=3.0, total=None):
        """notes: dicts with t, dur, note, vel. Notes only: no controller message is ever sent,
        so every controller keeps its starting value, and the downloads leave out the samples
        that only play with another value (a piano's sustain-pedal samples: sfz.playable()).
        Sending one would need those samples back (tools/music/make_manifest.py)."""
        n_total = total or _length(notes, tail)
        n_blocks = n_total // BLOCK + 1
        ev = sfz_events(notes, n_total)
        self._ready({(k, v) for _, kind, k, v in ev if kind == 1})
        out = np.zeros((n_blocks * BLOCK, 2), np.float32)
        syn = self.s._synth
        i = 0
        for b in range(n_blocks):
            b0 = b * BLOCK
            while i < len(ev) and ev[i][0] < b0 + BLOCK:
                d = max(0, ev[i][0] - b0)
                kind, a, v = ev[i][1], ev[i][2], ev[i][3]
                if kind == 1:
                    syn.note_on(d, a, v)
                else:
                    syn.note_off(d, a, 0)
                i += 1
            l, r = syn.render_block()
            out[b0:b0 + BLOCK, 0] = l
            out[b0:b0 + BLOCK, 1] = r
        syn.all_sound_off()
        return out[:n_total]


class Sf2Instrument:
    def __init__(self, program, bank=0, path=None):
        import tinysoundfont
        self.syn = tinysoundfont.Synth(samplerate=SR, gain=-3)
        self.sfid = self.syn.sfload(str(_need(path or GM_SF2)))
        self.program, self.bank = program, bank

    def render(self, notes, tail=3.0, total=None, channel=0):
        n_total = total or _length(notes, tail)
        syn = self.syn
        syn.program_select(channel, self.sfid, self.bank, self.program, is_drums=(self.bank == 128))
        ev = []
        for n in notes:
            ev.append((int(n["t"] * SR), 1, n["note"], int(np.clip(n["vel"] * 127, 1, 127))))
            ev.append((int((n["t"] + max(n["dur"], 0.01)) * SR), 0, n["note"], 0))
        ev.sort(key=lambda e: (e[0], e[1]))
        out = np.zeros((n_total, 2), np.float32)
        pos = 0
        for at, kind, key, vel in ev + [(n_total, -1, 0, 0)]:
            at = min(at, n_total)
            if at > pos:
                buf = syn.generate(at - pos)
                arr = np.frombuffer(buf, dtype=np.float32).reshape(-1, 2)
                out[pos:pos + len(arr)] = arr[: at - pos]
                pos = at
            if kind == 1:
                syn.noteon(channel, key, vel)
            elif kind == 0:
                syn.noteoff(channel, key)
        syn.notes_off()
        return out


def load_vst3(path):
    """A VST3 plug-in through pedalboard, from its bundle folder (Name.vst3/), on every system.
    Windows: should a pedalboard build not take the bundle folder, the plug-in file inside it
    (Name.vst3/Contents/x86_64-win/Name.vst3) is the same plug-in."""
    import os
    import pedalboard
    path = Path(_need(path))
    try:
        return pedalboard.load_plugin(str(path))
    except Exception:  # noqa: BLE001 - pedalboard raises ImportError / RuntimeError / ValueError
        inner = path / "Contents" / "x86_64-win" / path.name
        if os.name != "nt" or not inner.is_file():
            raise
        return pedalboard.load_plugin(str(inner))


# --- Surge XT ---------------------------------------------------------------------------------
#
# Surge XT reads its user defaults (SurgeXTUserDefaults.xml, in its user folder) at every lookup
# for as long as the file is missing, and it looks one up for nearly everything it shows or
# loads: about 600,000 times as pedalboard lists its parameters, thousands per patch. Each
# lookup asks the file system whether the file exists; on Windows that is a system call, and
# in a Documents folder kept by OneDrive a slow one (measured: 160-185 s to load the plug-in,
# 134 s for its first patch, about 1 s each on Linux). Surge takes a folder named
# SurgeXTUserData beside its binary (or in a folder above it) as its user folder: one is made in
# its library with an empty defaults file, read once. Every default keeps the value Surge has
# without the file, as before; Surge no longer makes a "Surge XT" folder in the user's Documents.
SURGE_USER = config.lib("surge-xt") / "SurgeXTUserData"
SURGE_DEFAULTS = '<?xml version = "1.0" encoding = "UTF-8" ?>\n<defaults version="1">\n</defaults>\n'


def surge_user_folder():
    """Make Surge XT's user folder in its library (above), with an empty defaults file."""
    import os
    f = SURGE_USER / "SurgeXTUserDefaults.xml"
    try:
        if f.read_text(encoding="utf-8") == SURGE_DEFAULTS:
            return SURGE_USER
    except OSError:
        pass
    # Its Patches folder too, so that Surge finds the folder made and makes nothing there.
    (SURGE_USER / "Patches").mkdir(parents=True, exist_ok=True)
    tmp = f.with_name(f"{f.name}.{os.getpid()}.tmp")
    tmp.write_text(SURGE_DEFAULTS, encoding="utf-8")
    os.replace(tmp, f)  # several render processes may do this at once
    return SURGE_USER


def _load_surge():
    """A Surge XT instance (on the main thread: pedalboard makes plug-ins only there), without
    pedalboard's look at every parameter as it loads: for each of Surge's 600 parameters it reads
    the text of 1,001 values (seconds), and it does it again at the first attribute set
    (_set_state and _set_parameters set what is wanted directly). It sets nothing: the patch
    loaded next replaces every parameter."""
    from pedalboard import _pedalboard as pbx
    surge_user_folder()
    mixin = pbx._PythonExternalPluginMixin
    scan = mixin.__set_initial_parameter_values__

    def no_scan(self, parameter_values):
        if parameter_values:
            return scan(self, parameter_values)
    mixin.__set_initial_parameter_values__ = no_scan
    try:
        return load_vst3(SURGE_VST)
    finally:
        mixin.__set_initial_parameter_values__ = scan


def _set_state(s, raw):
    """s.raw_state = raw, without pedalboard's attribute lookup (which lists every parameter)."""
    type(s).raw_state.fset(s, raw)


_PARAMS = {}  # (name, value) -> (the plug-in's parameter name, raw value): the same for every instance


def _resolve_parameters(s, params):
    """What setattr(s, name, value) sets for each of params, as pedalboard works it out (the
    value's raw value, from the parameter's texts), looking at those parameters only, not at all
    600; kept (_PARAMS). Called before a patch is loaded, as pedalboard did its look (at the
    first attribute set, before the first patch)."""
    from pedalboard import _pedalboard as pbx
    todo = [k for k in params if (k, params[k]) not in _PARAMS]
    if not todo:
        return
    names = {}
    for p in s._parameters:
        if any(r.match(p.name) for r in pbx.PARAMETER_NAME_REGEXES_TO_IGNORE):
            continue
        py = pbx.to_python_parameter_name(p)
        if py in params:
            names[py] = p.name  # the last of a name, as pedalboard's own list
    for k in todo:
        if k in names:
            ap = pbx.AudioProcessorParameter(s, names[k])
            _PARAMS[(k, params[k])] = (names[k], ap.get_raw_value_for(params[k]))
        else:
            _PARAMS[(k, params[k])] = None


def _set_parameters(s, params):
    """setattr(s, name, value) for each (_resolve_parameters first). A name no parameter has is
    set nowhere, as with setattr (pedalboard keeps it as a Python attribute)."""
    _resolve_parameters(s, params)
    for k, v in params.items():
        hit = _PARAMS[(k, v)]
        if hit:
            s._get_parameter(hit[0]).raw_value = hit[1]


class _SurgePool:
    """Surge XT instances, loaded on the main thread as needed (at most `limit`), each playing
    one part at a time. Not thread-safe except release() (called as a part ends)."""

    def __init__(self):
        import threading
        self.free, self.count, self.limit = [], 0, 1
        self.changed = threading.Condition()

    def take(self, wait=True):
        """A free instance (loaded if fewer than `limit` are), or None if none is free and
        wait is False."""
        with self.changed:
            while True:
                if self.free:
                    return self.free.pop()
                if self.count < self.limit:
                    self.count += 1
                    break
                if not wait:
                    return None
                self.changed.wait()
        try:
            return _load_surge()
        except BaseException:
            with self.changed:
                self.count -= 1
            raise

    def release(self, s):
        with self.changed:
            self.free.append(s)
            self.changed.notify_all()

    def clear(self):
        """Let go of the instances not playing (the memory of each comes back)."""
        with self.changed:
            self.count -= len(self.free)
            self.free = []


SURGE_POOL = _SurgePool()


def _midi_data(name, value):
    """A MIDI data byte, checked as mido checks one (an integer, 0-127)."""
    import numbers
    if not isinstance(value, numbers.Integral):
        raise TypeError(f"{name}: data byte must be int")
    if not 0 <= value <= 127:
        raise ValueError(f"{name}: data byte must be in range 0..127")
    return int(value)


def _midi_time(t):
    """A message's time, checked as mido checks it (a real number)."""
    import numbers
    if not isinstance(t, numbers.Real):
        raise TypeError("time must be int or float")


class SurgePatch:
    def __init__(self, patch, params=None):
        p = Path(patch)
        if not p.is_absolute():
            p = SURGE_PATCHES / patch
        self.fxp = _need(p).read_bytes()
        self.params = params or {}

    def _prepare(self, s):
        """The patch and parameters on instance s (main thread)."""
        s.reset()
        _resolve_parameters(s, self.params)
        _set_state(s, surgestate.set_chunk(s.raw_state, self.fxp[60:]))
        _set_parameters(s, self.params)
        return s

    @staticmethod
    def _messages(notes, bend_range):
        """The notes as MIDI for pedalboard: (bytes, time in seconds), what it makes of each
        mido.Message it is given (pedalboard.midi_utils), the same bytes mido.Message("note_on",
        note=, velocity=), ("note_off", note=) (velocity 64) and ("pitchwheel", pitch=) give,
        checked as mido checks them, without a Message object each (mido's checks took
        seconds a part)."""
        msgs = []
        for n in notes:
            key = _midi_data("note", n["note"])
            v = int(np.clip(round(n["vel"] * 127), 1, 127))
            msgs.append((n["t"], 1, bytes((0x90, key, v))))
            msgs.append((n["t"] + max(n["dur"], 0.01), 0, bytes((0x80, key, 64))))
            for bt, semis in n.get("bends", []):
                val = int(np.clip(semis / bend_range * 8191, -8192, 8191)) + 8192
                msgs.append((n["t"] + bt, 2, bytes((0xE0, val & 0x7F, val >> 7))))
            if n.get("bends"):
                msgs.append((n["t"] + max(n["dur"], 0.01) + 1e-4, 3, bytes((0xE0, 0, 64))))
        msgs.sort(key=lambda m: (m[0], m[1]))
        for t, _, _ in msgs:
            _midi_time(t)
        return [(m, t) for t, _, m in msgs]

    @staticmethod
    def _play(s, msgs, n_total):
        # pedalboard lets go of the GIL while the plug-in plays: parts play at once in threads.
        audio = s(msgs, duration=n_total / SR, sample_rate=SR, num_channels=2, buffer_size=512, reset=True)
        return audio.T.astype(np.float32)[:n_total]

    def render(self, notes, tail=3.0, total=None, bend_range=12):
        s = SURGE_POOL.take()
        try:
            self._prepare(s)
            n_total = total or _length(notes, tail)
            return self._play(s, self._messages(notes, bend_range), n_total)
        finally:
            SURGE_POOL.release(s)

    def start(self, notes, total, executor, bend_range=12, wait=True):
        """render() in executor, on an instance of the pool's of its own (prepared here, on the
        main thread): a Future of the audio, or None if no instance is free and wait is False."""
        s = SURGE_POOL.take(wait)
        if s is None:
            return None
        try:
            self._prepare(s)
            msgs = self._messages(notes, bend_range)
        except BaseException:
            SURGE_POOL.release(s)
            raise

        def play():
            try:
                return self._play(s, msgs, total)
            finally:
                SURGE_POOL.release(s)
        return executor.submit(play)
