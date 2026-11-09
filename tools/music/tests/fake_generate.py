#!/usr/bin/env python3
"""A stand-in for generate.py that only sends progress lines (tgmusic/progress.py): it
downloads, reads and renders nothing. For checking the launcher's Music card in every state
without building anything:

    TG_MUSIC_FAKE=<scenario> race-masters-launcher

Scenarios: tools, notes, plan, download, unpack, render, done, cancel, error-network,
error-disk, error-vcredist, error-tracks, slow (a whole build in about a minute). After its
lines it waits for "cancel" or the end of standard input (as the generator does), then says it
stopped. The stages come in generate.py's order (STAGES): the notes, the plan, the libraries,
the tracks.
"""
import json
import sys
import threading
import time

sys.dont_write_bytecode = True
GB = 1e9
cancelled = threading.Event()


def emit(ev, **kw):
    sys.stdout.write(json.dumps({"ev": ev, **kw}, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def watch():
    for line in sys.stdin:
        if line.strip() == "cancel":
            break
    cancelled.set()


def wait_cancel():
    cancelled.wait()
    emit("cancelled")
    sys.exit(3)


STAGES = ["notes", "plan", "libraries", "render"]  # generate.py's order
TEXT = {"notes": "Reading the notes from your ROM", "plan": "Working out what to download",
        "libraries": "Getting the instrument libraries", "render": "Rendering 149 tracks"}


def plan(done=0, planned=False):
    emit("plan", tracks=149, done=done, todo=149 - done, download_mb=1040 if planned else 1100, download_max_mb=3168,
         libs=7 if planned else 9, jobs=2, eta_s=12700, **({"planned": True} if planned else {}))


def stage(name, eta_s):
    emit("stage", stage=name, n=STAGES.index(name) + 1, of=len(STAGES), text=TEXT[name], eta_s=eta_s)


def main():
    scenario = sys.argv[1] if len(sys.argv) > 1 else "render"
    threading.Thread(target=watch, daemon=True).start()
    if scenario == "tools":
        emit("stage", stage="tools", n=1, of=1, text="Setting up the music tools")
        emit("tools", step="packages", i=4, of=5, n=7, count=21, pkg="pedalboard", text="Getting Python packages (7 of 21: pedalboard)")
    elif scenario in ("download", "error-network"):
        plan(planned=True)
        stage("libraries", 12700)
        emit("download", lib="vcsl", name="Versilian Community Sample Library", i=6, of=7, got=0.19 * GB, total=0.32 * GB,
             all_got=0.62 * GB, all_total=1.04 * GB, rate=9.6e6, eta_s=12200)
        if scenario == "error-network":
            time.sleep(0.2)
            emit("error", code="network", text="Couldn't download the Versilian Community Sample Library: the connection "
                 "was lost (urlopen error timed out). What was downloaded is kept.", log="build.log")
            sys.exit(1)
    elif scenario == "unpack":
        plan(planned=True)
        stage("libraries", 12300)
        emit("unpack", lib="dragonfly-reverb", name="Dragonfly Reverb", i=4, of=7, eta_s=12300)
    elif scenario == "notes":
        plan()
        stage("notes", 20500)
        emit("notes", elapsed_s=250, eta_s=20100)
    elif scenario == "plan":
        plan()
        stage("plan", 20000)
        emit("planning", i=40, of=149, eta_s=19900)
    elif scenario in ("render", "error-tracks", "done", "cancel"):
        plan(done=0, planned=True)
        stage("render", 19800)
        last = 37 if scenario in ("render", "cancel") else 149
        for i in range(1, last + 1):
            emit("track", i=i, of=149, label=f"City {i} (race)", status="failed" if scenario == "error-tracks" and i in (40, 77)
                 else "done", secs=150, eta_s=(149 - i) * 75)
        emit("render", done=last, of=149, running="Las Vegas (race) / Rio (race)", eta_s=(149 - last) * 75)
        if scenario == "error-tracks":
            emit("error", code="tracks", text="2 track(s) failed: track 40, track 77", log="build.log")
            sys.exit(4)
        if scenario == "done":
            emit("done", tracks=149, failed=0, mb=640, minutes=372)
            sys.exit(0)
    elif scenario == "error-disk":
        plan()
        emit("error", code="disk", text="Not enough disk space: about 5.8 GB is needed for the music cache and 3.1 GB is free.")
        sys.exit(1)
    elif scenario == "error-vcredist":
        emit("stage", stage="tools", n=1, of=1, text="Setting up the music tools")
        emit("error", code="vcredist", text="The Microsoft Visual C++ Redistributable (x64) is needed (several of the music "
             "tools' packages use its MSVCP140.dll). Get it from Microsoft, install it, then try again.")
        sys.exit(1)
    elif scenario == "slow":
        plan()
        stage("notes", 70)
        time.sleep(2)
        stage("plan", 65)
        for i in range(0, 150, 10):
            if cancelled.is_set():
                wait_cancel()
            emit("planning", i=i, of=149, eta_s=65 - i / 30)
            time.sleep(0.2)
        stage("libraries", 60)
        plan(planned=True)
        for k in range(20):
            if cancelled.is_set():
                wait_cancel()
            emit("download", lib="sso", name="Sonatina Symphonic Orchestra", i=1, of=1, got=k * 0.05 * GB, total=GB,
                 all_got=k * 0.05 * GB, all_total=GB, rate=5e7, eta_s=60 - k)
            time.sleep(0.5)
        stage("render", 35)
        for i in range(1, 150):
            if cancelled.is_set():
                wait_cancel()
            emit("track", i=i, of=149, label=f"City {i} (race)", status="done", secs=0.2, eta_s=(149 - i) * 0.2)
            time.sleep(0.2)
        emit("done", tracks=149, failed=0, mb=640, minutes=1)
        sys.exit(0)
    else:
        sys.exit(f"unknown scenario {scenario}")
    wait_cancel()


if __name__ == "__main__":
    main()
