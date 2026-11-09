"""Which files an .sfz instrument loads, worked out the way sfizz (the sampler pysfizz wraps,
at the commit pysfizz 0.1.3 builds) reads it, so that only those files need downloading.

The rules followed (sfizz src/sfizz/parser/Parser.cpp, Synth.cpp, Region.cpp, FilePool.cpp):
  * The file is read byte by byte; `//` and `/* */` are comments.
  * `#include "file"`: the path is relative to the folder of the ROOT .sfz (also inside an
    included file), with `$VAR`s expanded and `\\` read as `/`; the text is read in place, so
    headers and opcodes run on across files. Opened as written (no case-insensitive search).
  * `#define $VAR value`: the value ends at the first blank (as ARIA); a later #define of the
    same name replaces it, so an included file sees the values defined before it.
  * `$VAR` in names and values: the shortest defined name after the `$` wins; an undefined one
    is dropped. Expanded again until no `$` is left.
  * An opcode's value runs to the end of the line, a comment, a `<header>`, a `#define` /
    `#include`, or the next `name=` (so a sample path may hold spaces).
  * `<control> default_path=`: put in front of every later `sample=` (reset by each <control>).
    A region's sample is the last `sample=` of its <global>, <master>, <group> and <region>,
    with the default_path in force when the region ends. `*sine`, `*silence`... are built in.
  * A sample path is relative to the root .sfz's folder; `\\` is read as `/`. If it does not
    exist as written, sfizz on Linux looks for each part ignoring case (Windows ignores case
    anyway).

deps() takes callbacks, so it works on a repository listing (make_manifest.py) as well as on
files on disk (the tests).

playable() leaves out the samples that only regions needing a controller value play: the
generator never sends a controller message (instruments.py), so every controller keeps its
starting value, and a region whose locc/hicc range misses it never sounds (a piano's samples
with the sustain pedal down, locc64=65). sfizz skips a region whose sample is missing and
plays the others, so leaving those out changes nothing heard.

note_samples() goes further, for given notes: the samples sfizz can play when it is sent only
those note-ons (and their note-offs), judged region by region as sfizz's Layer::registerNoteOn
and registerNoteOff do (Synth.cpp buildRegion, Region.cpp, Opcode.cpp):
  * The key range: lokey / hikey / key, numbers or note names (c4 = 60, c#4 = db4 = 61, also
    with the ♯ and ♭ signs; a number is read from the start of the value, "60x" = 60). A value
    outside 0-127 or unreadable: lokey 0, hikey 127, key 60; a hikey or key sfizz can't read
    (hikey=-1) also stops notes from starting the region. <control> octave_offset /
    note_offset (in force when the region is built, kept across <control> headers) move a
    range that isn't 0-127, clamped to 0-127. lokey above hikey: no key.
  * The velocity range: lovel / hivel compared with velocity / 127 in float, as sfizz does
    (hivel 0-126 takes up to the next value: hivel=64 lets 64 through, not 65).
  * The trigger: attack (also first, legato: kept as attack) on a note-on; release and
    release_key on a note-off, with the key's note-on velocity: such a region is kept for a
    key played at any velocity.
  * Kept as if they held (not judged): keyswitches (sw_*), round robins and random choices
    (seq_length / seq_position, lorand / hirand: every alternative is kept), sw_vel=previous
    (any velocity), pitch bend, program, aftertouch, tempo and timer ranges; the controller
    ranges as playable() judges them.
"""
import posixpath
import re
import struct

EOF = ""
MAX_INCLUDE_DEPTH = 32  # sfizz's default
# sfizz's starting value of a controller that no message and no <control> set_ccN has set: a
# region for exactly that value plays (found by probing pysfizz; the music_tools test checks it
# when pysfizz is installed). Every other controller 0-127 starts at 0. Above 127 are sfizz's own
# controllers (velocity, note, random, keyswitch...), which change with each note: never judged.
CC_START = {7: 100, 10: 63, 11: 127}
_CC_COND = re.compile(r"(lo|hi)(hd)?cc(\d+)")
_SET_CC = re.compile(r"set_(hd)?cc(\d+)")
TRIGGERS = ("attack", "first", "legato", "release", "release_key")
RELEASE = ("release", "release_key")
# The opcodes of conditions note_samples() doesn't judge (a region with one is kept).
UNJUDGED = re.compile(r"sw_(lokey|hikey|last|lolast|hilast|down|up|previous|vel|default)|seq_(length|position)"
                      r"|(lo|hi)(rand|bend|prog|chanaft|polyaft|bpm|timer|chan)|(start|on)_(lo|hi)(hd)?cc\d+")
_BLANKS = " \t\n\v\f\r"
_NOTES = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
# After a note name's letter: (mark, semitones, letters it may follow). The UTF-8 signs as the
# latin-1 characters the file is read as.
_ACCIDENTALS = (("#", 1, "cdfga"), ("♯".encode().decode("latin-1"), 1, "cdfga"),
                ("b", -1, "degab"), ("♭".encode().decode("latin-1"), -1, "degab"))


def _atoi(s):
    """absl::SimpleAtoi: a whole number, blanks around it allowed; else None."""
    s = s.strip(_BLANKS)
    return int(s) if re.fullmatch(r"[+-]?[0-9]+", s) else None


def _leading_int(s):
    """sfizz readLeadingInt: the whole number a value starts with ("60x" = 60), else None."""
    m = re.match(r"[+-]?[0-9]+", s)
    return int(m.group()) if m else None


def _leading_float(s):
    """sfizz readLeadingFloat: the number a value starts with, else None (an "e" is taken in
    even with no exponent after it: "64e" is None, as sfizz reads it)."""
    m = re.match(r"[+-]?[0-9]*(\.[0-9]*)?(e[-+0-9]?[0-9]*)?", s)
    try:
        return float(m.group())
    except ValueError:
        return None


def key_value(value):
    """A lokey / hikey / key value as sfizz reads it: a number, else a note name (c4 = 60,
    c#4 = db4 = 61, c-1 = 0, ♯ ♭ too); None if neither, or outside 0-127."""
    v = value.strip(_BLANKS)
    n = _leading_int(v)
    if n is None and v:
        letter = v[0].lower() if "A" <= v[0] <= "Z" else v[0]  # absl::ascii_tolower
        if letter not in _NOTES:
            return None
        n, rest = _NOTES[letter], v[1:]
        for mark, step, letters in _ACCIDENTALS:
            if rest.startswith(mark):
                if letter not in letters:
                    return None
                n, rest = n + step, rest[len(mark):]
                break
        octave = _atoi(rest)
        if octave is None:
            return None
        n += 12 * (octave + 1)
    return n if n is not None and 0 <= n <= 127 else None


def _f32(x):
    """x rounded to a C float (sfizz compares velocities in float)."""
    try:
        return struct.unpack("<f", struct.pack("<f", x))[0]
    except OverflowError:
        return float("inf") if x > 0 else float("-inf")


def _below(x):
    """std::nextafter(x, 0.0f) for a float x > 0: the float just below it."""
    return struct.unpack("<f", struct.pack("<I", struct.unpack("<I", struct.pack("<f", x))[0] - 1))[0]


def velocities(lo, hi):
    """The velocities 1-127 that lovel=lo hivel=hi let through, as sfizz compares them
    (velocity / 127 in float; a hivel of 0-126 takes up to the next value): (first, last),
    first > last if none."""
    lo_n = _f32(_f32(lo) / 127)
    hi_n = _below(_f32(_f32(_f32(hi) + 1) / 127)) if 0 <= hi <= 126 else _f32(_f32(hi) / 127)
    ok = [v for v in range(1, 128) if lo_n <= _f32(v / 127) <= hi_n]
    return (ok[0], ok[-1]) if ok else (1, 0)


class Region:
    """One <region> as sfizz builds it from its <global>, <master>, <group> and own opcodes:
      sample    its path (relative to the library), None for a built-in one (*sine ...);
      cc, odd   its controller ranges {cc: (lo, hi)}, and whether it has one not judged
                (playable());
      keys      the key range (lo, hi), after <control> octave_offset / note_offset;
      notes     whether notes start it (False: a hikey or key sfizz can't read, e.g. -1);
      vels      the velocities it takes, (first, last) (velocities());
      any_vel   sw_vel=previous: judged with another note's velocity, so any;
      trigger   attack, first, legato, release or release_key;
      unjudged  the opcodes of its conditions not judged (UNJUDGED)."""

    def __init__(self):
        self.sample, self.cc, self.odd = None, {}, False
        self.keys, self.notes, self.trigger, self.any_vel = (0, 127), True, "attack", False
        self.vels, self._vel, self.unjudged = (1, 127), [0.0, 127.0], []

    def read(self, name, value):
        """One opcode, as sfizz's Region::parseOpcode reads it (those that matter here)."""
        v = value.strip(_BLANKS)
        if name == "lokey":
            k = key_value(v)
            self.keys = (0 if k is None else k, self.keys[1])
        elif name == "hikey":
            k = key_value(v)
            self.notes = k is not None
            self.keys = (self.keys[0], 127 if k is None else k)
        elif name == "key":
            k = key_value(v)
            self.notes = k is not None
            self.keys = (60, 60) if k is None else (k, k)
        elif name in ("lovel", "hivel"):
            x = _leading_float(v)
            hi = name == "hivel"
            self._vel[hi] = (127.0 if hi else 0.0) if x is None else x
        elif name == "trigger":
            self.trigger = v if v in TRIGGERS else "attack"
        elif name == "sw_vel":
            self.any_vel = v == "previous"
        if UNJUDGED.fullmatch(name) or (name == "trigger" and v in ("first", "legato")):
            self.unjudged.append(name)

    def done(self, octave_offset, note_offset):
        """After its opcodes: sfizz's offsetAllKeys (a range other than 0-127 moves) and the
        velocities."""
        if (octave_offset or note_offset) and self.keys != (0, 127):
            off = 12 * octave_offset + note_offset
            self.keys = tuple(min(max(k + off, 0), 127) for k in self.keys)
        self.vels = velocities(*self._vel)


def _ident(c):
    return c == "_" or "a" <= c <= "z" or "A" <= c <= "Z" or "0" <= c <= "9"


def _is_identifier(s):
    return bool(s) and all(_ident(c) for c in s)


class _Reader:
    """One file's text (bytes as latin-1 characters, as sfizz reads bytes); put back = step back."""

    def __init__(self, text, path):
        self.text, self.i, self.path = text, 0, path

    def peek(self):
        return self.text[self.i] if self.i < len(self.text) else EOF

    def get(self):
        c = self.peek()
        if c:
            self.i += 1
        return c

    def take(self, c):
        if self.peek() == c and c:
            self.i += 1
            return True
        return False

    def take_while(self, pred):
        j = self.i
        while self.i < len(self.text) and pred(self.text[self.i]):
            self.i += 1
        return self.text[j:self.i]

    def skip(self, chars):
        return len(self.take_while(lambda c: c in chars))

    def line(self):
        return self.text.count("\n", 0, self.i) + 1

    def to_eol(self):
        """Up to the end of the line or a comment (sfizz extractToEol)."""
        j = self.i
        t = self.text
        while self.i < len(t):
            c = t[self.i]
            if c in "\r\n" or (c == "/" and self.i + 1 < len(t) and t[self.i + 1] in "/*"):
                break
            self.i += 1
        return t[j:self.i]

    def skip_comment(self):
        t, i = self.text, self.i
        if t.startswith("//", i):
            j = i + 2
            while j < len(t) and t[j] not in "\r\n":
                j += 1
            self.i = min(j + 1, len(t))  # the line end goes with the comment
            return True
        if t.startswith("/*", i):
            j = t.find("*/", i + 2)
            self.i = len(t) if j < 0 else j + 2
            return True
        return False

    def recover(self):
        """After an error sfizz skips the rest of the line."""
        self.take_while(lambda c: c != "\n")


def _bytes_path(s):
    """A path read as latin-1 characters back to the text it is (UTF-8 if it decodes)."""
    b = s.encode("latin-1")
    try:
        return b.decode("utf-8")
    except UnicodeDecodeError:
        return s


def walk(base, rel):
    """base/rel with '.', '..' and doubled slashes collapsed, and the folders a '..' steps out
    of (they must exist for Linux to follow it). (None, []) if it climbs out of the library
    or is absolute."""
    if rel.startswith("/") or (len(rel) > 1 and rel[1] == ":"):
        return None, []
    out, crossed = [p for p in base.split("/") if p not in ("", ".")], []
    for part in rel.split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            if not out:
                return None, []
            crossed.append("/".join(out))
            out.pop()
        else:
            out.append(part)
    return "/".join(out), crossed


class Result:
    """What one root .sfz loads: files (the .sfz files read, root first), samples (paths
    relative to the library, as written: find() gives the file sfizz opens), crossed (folders
    a '..' steps out of), missing (files it includes that aren't there, or paths outside the
    library), problems (what sfizz would warn about too, e.g. a line it can't read: text).
    For playable() and note_samples(): regions, a Region per <region> with a sample, in the
    order sfizz builds them; set_cc, the <control> headers' starting values ({cc: value
    0-127})."""

    def __init__(self):
        self.files, self.samples, self.crossed, self.missing, self.problems = [], [], [], [], []
        self.regions, self.set_cc = [], {}


def deps(root, read):
    """The files that sfizz reads for the instrument `root` (a '/' path relative to the
    library). read(path) -> the file's bytes, or None if there is no such file."""
    res = Result()
    root_dir = posixpath.dirname(root)
    stack = []
    defs = {}
    st = dict(header=None, opcodes=[], default_path="", levels={"global": [], "master": [], "group": []},
              octave_offset=0, note_offset=0)

    def problem(r, text):
        res.problems.append(f"{r.path}:{r.line()}: {text}" if r else text)

    def include(rel, r):
        full, crossed = walk(root_dir, rel) if stack else (root, [])
        if full is None:
            res.missing.append(f"{r.path}:{r.line()}: #include {rel!r} climbs out of the library")
            return
        res.crossed += crossed
        if len(stack) == MAX_INCLUDE_DEPTH:
            problem(r, f"#include {rel!r}: deeper than {MAX_INCLUDE_DEPTH} includes")
            return
        data = read(full)
        if data is None:
            res.missing.append(f"{r.path}:{r.line()}: #include {rel!r}: {full} not found" if r else f"{full} not found")
            return
        res.files.append(full)
        stack.append(_Reader(data.decode("latin-1"), full))

    def expand(s, r):
        for _ in range(64):  # sfizz expands until no $ is left
            if "$" not in s:
                return s
            out, i, n = [], 0, len(s)
            while i < n:
                c = s[i]
                i += 1
                if c != "$":
                    out.append(c)
                    continue
                name = ""
                while i < n and _ident(s[i]) and name not in defs:
                    name += s[i]
                    i += 1
                if not name:
                    problem(r, "expected a variable name after $")
                elif name in defs:
                    out.append(defs[name])
                else:
                    problem(r, f"${name} is not defined")
            s = "".join(out)
        return s

    def flush():
        h, ops = st["header"], st["opcodes"]
        if h == "control":
            st["default_path"] = ""
            for name, value in ops:
                if name == "default_path":
                    st["default_path"] = value.strip(" \t\r\n").replace("\\", "/")
                if name in ("octave_offset", "note_offset"):  # kept until set again
                    n = _leading_int(value.strip(_BLANKS))
                    st[name] = 0 if n is None else n
                m = _SET_CC.fullmatch(name)
                if m and int(m.group(2)) < 128:
                    try:
                        v = float(value.strip())
                    except ValueError:
                        continue
                    res.set_cc[int(m.group(2))] = v * 127 if m.group(1) else v
        elif h in ("global", "master", "group"):
            st["levels"][h] = list(ops)
            # a new <global> starts its masters and groups again, a new <master> its groups
            for lower in {"global": ("master", "group"), "master": ("group",)}.get(h, ()):
                st["levels"][lower] = []
        elif h == "region":
            sample = None
            region = Region()
            ranges, odd = {}, False  # its controller ranges {cc: [lo, hi]}; a range not judged
            for name, value in st["levels"]["global"] + st["levels"]["master"] + st["levels"]["group"] + ops:
                if name == "sample":
                    sample = value
                region.read(name, value)
                m = _CC_COND.fullmatch(name)
                if m:
                    cc = int(m.group(3))
                    try:
                        v = float(value.strip())
                    except ValueError:
                        odd = True
                        continue
                    if m.group(2) or cc > 127:  # a high-resolution range, or sfizz's own controllers
                        odd = True
                        continue
                    ranges.setdefault(cc, [0.0, 127.0])[0 if m.group(1) == "lo" else 1] = v
            sample = (sample or "").strip(" \t\r\n")
            path = None
            if sample and not sample.startswith("*"):
                written = _bytes_path(st["default_path"] + sample.replace("\\", "/"))
                path, crossed = walk(root_dir, written)
                if path is None:
                    res.missing.append(f"{root}: sample {written!r} is outside the library")
                else:
                    res.samples.append(path)
                    res.crossed += crossed
            if sample:
                region.sample, region.cc, region.odd = path, {cc: tuple(r) for cc, r in ranges.items()}, odd
                region.done(st["octave_offset"], st["note_offset"])
                res.regions.append(region)
        st["header"], st["opcodes"] = None, []

    def directive(r):
        r.get()  # '#'
        name = r.take_while(_ident)
        if name == "define":
            r.skip(" \t")
            if not r.take("$"):
                problem(r, "expected $name after #define")
                r.recover()
                return
            var = r.take_while(_ident)
            if not var:
                problem(r, "expected $name after #define")
                r.recover()
                return
            r.skip(" \t")
            value = r.to_eol()
            cut = min([k for k in (value.find(" "), value.find("\t")) if k >= 0], default=-1)
            if cut >= 0:  # the rest of the line is read again (as opcodes)
                r.i -= len(value) - cut
                value = value[:cut]
            defs[var] = value
        elif name == "include":
            r.skip(" \t")
            if r.take('"'):
                path = r.take_while(lambda c: c not in '"\r\n')
                if r.take('"'):
                    include(_bytes_path(expand(path, r).replace("\\", "/")), r)
                    return
            problem(r, 'expected "file.sfz" after #include')
            r.recover()
        else:
            problem(r, f"unknown directive #{name}")
            r.recover()

    def header(r):
        r.get()  # '<'
        name = r.take_while(lambda c: c not in "\r\n>")
        if r.peek() != ">":
            problem(r, "expected > at the end of a header")
            r.recover()
            return
        r.get()
        if not _is_identifier(name):
            problem(r, f"bad header <{name}>")
            r.recover()
            return
        flush()
        st["header"] = name

    def opcode(r):
        raw = r.take_while(lambda c: _ident(c) or c == "$")
        if not raw or r.peek() != "=":
            problem(r, "expected an opcode")
            r.recover()
            return
        name = expand(raw, r)
        if not _is_identifier(name):
            problem(r, f"bad opcode name {name!r}")
            r.recover()
            return
        r.get()  # '='
        if name == "base64data":  # an embedded sample: up to the next header or directive
            st["opcodes"].append((name, r.take_while(lambda c: c not in "<#")))
            return
        value = r.to_eol()
        # Where the value ends (sfizz processOpcode): before a header, a directive, the next
        # name=, or blanks at the end.
        end, n = 0, len(value)
        while end < n:
            i = end + 1
            stop = False
            if value[end] == "<":
                stop = True
            elif value[end] in " \t":
                while i < n and value[i] in " \t":
                    i += 1
                if i == n or value[i] == "<":
                    stop = True
                elif value[i] == "#":
                    j = i + 1
                    while j < n and _ident(value[j]):
                        j += 1
                    stop = value[i + 1:j] in ("define", "include")
                elif _ident(value[i]):
                    i += 1
                    while i < n and (_ident(value[i]) or value[i] == "$"):
                        i += 1
                    stop = i < n and value[i] == "="
            if stop:
                break
            end = i
        r.i -= n - end
        st["opcodes"].append((name, expand(value[:end], r)))

    include(root, None)
    while stack:
        r = stack[-1]
        while r.skip(" \t\r\n") or r.skip_comment():
            pass
        c = r.peek()
        if c == EOF:
            stack.pop()
        elif c == "#":
            directive(r)
        elif c == "<":
            header(r)
        else:
            opcode(r)
    flush()
    return res


def find(path, exists, listdir):
    """The file sfizz opens for a sample `path` (relative to the library): the path itself,
    else (Linux) each part matched ignoring case, the first match in listing order. None if
    there is none. exists(p) -> bool; listdir(folder) -> names in it."""
    if exists(path):
        return path
    cur = ""
    for part in path.split("/"):
        nxt = f"{cur}/{part}" if cur else part
        if exists(nxt):
            cur = nxt
            continue
        hit = next((n for n in listdir(cur) if n.lower() == part.lower()), None)
        if hit is None:
            return None
        cur = f"{cur}/{hit}" if cur else hit
    return cur


def _sounds(res, region):
    """Whether a region's locc/hicc ranges all hold their controllers' starting values (the
    instrument's <control> set_ccN, else CC_START, else 0), or it has a range not judged."""
    return region.odd or all(lo <= res.set_cc.get(cc, CC_START.get(cc, 0)) <= hi for cc, (lo, hi) in region.cc.items())


def playable(res):
    """The samples of res (deps()) that can sound when no controller message is ever sent: those
    of a region whose locc/hicc ranges all hold their controllers' starting values (the
    instrument's <control> set_ccN, else CC_START, else 0). A region with a range not judged
    keeps its sample. Should no region with a sample be left, all are kept (sfizz doesn't load
    an instrument none of whose samples is there)."""
    keep = [r.sample for r in res.regions if r.sample is not None and _sounds(res, r)]
    if not keep:
        return list(res.samples)
    return list(dict.fromkeys(keep))


def note_regions(res, notes):
    """The regions of res (deps()) that sfizz can start when it is sent only the note-ons
    `notes`, (key, velocity 1-127) pairs, each followed by its note-off (and no controller
    message): those playable() keeps that such a note starts by key and velocity, and the
    release regions of the keys played, at any velocity (see the top of this file). Every
    round robin or random choice is kept, and a region with a condition not judged when its
    key and velocity match: a superset of what sfizz plays. In order (built-in ones too)."""
    played = {}
    for key, vel in notes:
        played.setdefault(int(key), set()).add(int(vel))
    out = []
    for r in res.regions:
        if not r.notes or not _sounds(res, r):
            continue
        first, last = r.vels
        any_vel = r.any_vel or r.trigger in RELEASE
        if any(any_vel or any(first <= v <= last for v in played[k])
               for k in range(r.keys[0], r.keys[1] + 1) if k in played):
            out.append(r)
    return out


def note_samples(res, notes):
    """The samples of note_regions(res, notes), each once, in order: what sfizz needs to play
    those notes as it would with every sample there. Should none be needed, playable()'s are
    kept: sfizz doesn't load an instrument none of whose samples is there."""
    out = [r.sample for r in note_regions(res, notes) if r.sample is not None]
    return list(dict.fromkeys(out)) or playable(res)
