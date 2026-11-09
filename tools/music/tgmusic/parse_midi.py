"""Turn the DSP-logged MIDI of each song (tools/audio, from the player's ROM) into a clean,
musical note list.

Output (<work>/events/NN_name.json):
  song meta (bpm, pass length, loop markers, key) and a list of notes:
  {t, dur, beat, beats, pitch (float MIDI incl. bend), note (int), vel (0-1),
   pan (-1..1), echo, voice, inst ($NN), role, drum (kick/snare/hat/tom/perc|None),
   bends [[dt, semis], ...] for slides/vibrato}
Times are seconds from the loop start (the song's first musical tick).
"""
import json
from collections import Counter
from pathlib import Path

import mido

from . import config

SONGS = ["01_title", "02_las_vegas", "03_hiroshima", "04_bordeaux",
         "05_frankfurt", "06_qualified", "07_ending"]

# Drum identity by sample file (tools/audio names samples by number, source number and a
# hash of the sample's data; which kind of drum each is was found by spectral analysis).
DRUM_BY_SAMPLE = {
    "04_srcn04_2633c700.wav": "kick",    # 86 Hz body
    "05_srcn05_db89da16.wav": "snare",   # noise + 220 Hz body
    "12_srcn0F_5497e60d.wav": "snare",
    "02_srcn02_7b371321.wav": "hat",     # ~9 kHz noise
    "03_srcn03_ebeb6dd9.wav": "hat",
    "01_srcn01_9fde047b.wav": "tom",     # 1 kHz knock
}


def role_of(name):
    n = name.lower()
    for r in ("lead", "bass", "arpeggio", "chords", "drums"):
        if r in n:
            return {"arpeggio": "arp", "chords": "pad", "drums": "drum"}.get(r, r)
    return "other"


def parse(song):
    rebuild = config.audio_dir() / "rebuild" / song
    sheet = json.loads((rebuild / "sheet.json").read_text())
    mid = mido.MidiFile(rebuild / f"{song}.mid")
    tpb = mid.ticks_per_beat
    insts = sheet["instruments"]
    chan_inst = {v["midi_channel"] - 1: k for k, v in insts.items()}

    # tempo map from track 0 -> tick->seconds
    tempo_events = []
    markers = {}
    tick = 0
    for msg in mid.tracks[0]:
        tick += msg.time
        if msg.type == "set_tempo":
            tempo_events.append((tick, msg.tempo))
        elif msg.type == "marker":
            markers[msg.text] = tick
    tempo_events.sort()

    def sec(tk):
        s, last_t, cur = 0.0, 0, 500000
        for et, tempo in tempo_events:
            if et >= tk:
                break
            s += (et - last_t) * cur / 1e6 / tpb
            last_t, cur = et, tempo
        return s + (tk - last_t) * cur / 1e6 / tpb

    loop_tick = markers.get("loopStart", 0)
    end_tick = markers.get("loopEnd")
    t0 = sec(loop_tick)
    main_tempo = tempo_events[-1][1]
    bpm = 60e6 / main_tempo
    spb = 60.0 / bpm

    notes = []
    last_tick = 0
    for ti, trk in enumerate(mid.tracks[1:]):
        voice = ti
        tick = 0
        bend = {}
        pan = {}
        echo = {}
        vol = {}
        active = {}
        for msg in trk:
            tick += msg.time
            last_tick = max(last_tick, tick)
            ch = getattr(msg, "channel", None)
            if msg.type == "pitchwheel":
                bend[ch] = msg.pitch / 8192 * 12
                for key, n in active.items():
                    if key[0] == ch:
                        n["bends"].append([sec(tick) - n["_ts"], bend[ch]])
            elif msg.type == "control_change":
                if msg.control == 10:
                    pan[ch] = (msg.value - 64) / 63
                elif msg.control == 91:
                    echo[ch] = msg.value > 63
                elif msg.control in (7, 11):
                    vol[ch] = msg.value / 127
            elif msg.type == "note_on" and msg.velocity > 0:
                inst = chan_inst.get(ch)
                name = insts[inst]["name"] if inst else "?"
                sample = Path(insts[inst]["sample"]).name if inst else ""
                ts = sec(tick)
                n = {"_ts": ts, "_tick": tick, "note": msg.note, "b0": bend.get(ch, 0.0),
                     "vel": msg.velocity / 127 * vol.get(ch, 1.0), "pan": pan.get(ch, 0.0),
                     "echo": echo.get(ch, False), "voice": voice, "inst": inst,
                     "name": name, "role": role_of(name), "sample": sample,
                     "pitched": bool(insts[inst]["pitched"]) if inst else True, "bends": []}
                active[(ch, msg.note)] = n
            elif msg.type in ("note_off", "note_on"):
                n = active.pop((ch, msg.note), None)
                if n:
                    te = sec(tick)
                    n["dur"] = te - n["_ts"]
                    notes.append(n)
    out = []
    for n in notes:
        t = n["_ts"] - t0
        drum = DRUM_BY_SAMPLE.get(n["sample"]) if not n["pitched"] else None
        if not n["pitched"] and drum is None:
            drum = "perc"
        pitch = n["note"] + n["b0"]
        bends = [[round(dt, 4), round(b - n["b0"], 3)] for dt, b in n["bends"] if abs(b - n["b0"]) > 0.05]
        out.append({
            "t": round(t, 5), "dur": round(n["dur"], 5),
            "beat": round(t / spb, 4), "beats": round(n["dur"] / spb, 4),
            "pitch": round(pitch, 3), "note": int(round(pitch)),
            "vel": round(n["vel"], 4), "pan": round(n["pan"], 3), "echo": n["echo"],
            "voice": n["voice"], "inst": n["inst"], "role": "drum" if drum else n["role"],
            "drum": drum, "drum_pitch": n["note"] if drum else None, "bends": bends,
        })
    out.sort(key=lambda n: (n["t"], n["voice"]))
    pass_s = (sec(end_tick) - t0) if end_tick else (max(n["t"] + n["dur"] for n in out))
    meta = {
        "song": song, "title": sheet["title"], "bpm": bpm, "spb": spb,
        "pass_s": pass_s, "pass_beats": pass_s / spb, "loops": end_tick is not None,
        "key": sheet["key_estimate"]["key"], "echo": sheet["echo"],
        "length_s": sheet["length_s"],
    }
    return {"meta": meta, "notes": out}


def events_path(song):
    return config.events_dir() / f"{song}.json"


def main(songs=SONGS, verbose=True):
    config.events_dir().mkdir(parents=True, exist_ok=True)
    for s in songs:
        d = parse(s)
        events_path(s).write_text(json.dumps(d))
        if verbose:
            roles = Counter((n["role"], n["drum"]) for n in d["notes"])
            m = d["meta"]
            print(f"  {s}: {m['bpm']:.2f} bpm, pass {m['pass_s']:.3f}s ({m['pass_beats']:.2f} beats), "
                  f"key {m['key']}, loops={m['loops']}, notes {len(d['notes'])} {dict(roles)}", flush=True)
