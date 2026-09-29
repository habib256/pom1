#!/usr/bin/env python3
# movie_play_cli: the headless replay of an input movie (issue #40).
#
#   POM1 --movie-play FILE [--movie-frames DIR]
#
# The movies come from input_movie_smoke, which leaves three in a directory:
#   rerecorded.p1m  recorded over a rewind (re-recording)  -> exit 0, VERIFIED
#   tampered.p1m    the same with its end hash flipped      -> exit 1, DIVERGED
#   gen2.p1m        recorded on a GEN2 machine              -> exit 0 + PNG frames
# and a missing file must exit 2. The exit code IS the interface: a longplay
# script runs --movie-play before encoding and stops on anything but 0.
#
#   python3 tools/test_movie_play.py --pom1 PATH --movies DIR
# Exits 77 (skip) when POM1 or the movies are missing.

import argparse
import os
import subprocess
import sys
import tempfile

FRAME_CYCLES = 17045   # POM1_CPU_CYCLES_PER_FRAME_1X_60HZ


def run(pom1, *args):
    cmd = [pom1, "--preset-dir", "", *args]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=90)
    except subprocess.TimeoutExpired:
        return None, "timeout"
    return r.returncode, r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pom1", required=True)
    ap.add_argument("--movies", required=True)
    a = ap.parse_args()
    movies = {n: os.path.join(a.movies, n + ".p1m") for n in ("rerecorded", "tampered", "gen2")}
    if not os.path.isfile(a.pom1) or not all(os.path.isfile(p) for p in movies.values()):
        print("SKIP: POM1 or the input_movie_smoke fixtures are missing")
        return 77

    failures = []

    def check(name, ok, log):
        print(("PASS " if ok else "FAIL ") + name)
        if not ok:
            failures.append(name)
            print(log)

    rc, log = run(a.pom1, "--movie-play", movies["rerecorded"])
    check("re-recorded movie replays VERIFIED, exit 0",
          rc == 0 and "--movie-play: VERIFIED" in log, log)

    rc, log = run(a.pom1, "--movie-play", movies["tampered"])
    check("tampered movie replays DIVERGED, exit 1",
          rc == 1 and "--movie-play: DIVERGED" in log, log)

    rc, log = run(a.pom1, "--movie-play", os.path.join(a.movies, "absent.p1m"))
    check("unreadable movie exits 2", rc == 2, log)

    rc, log = run(a.pom1, "--movie-frames", a.movies)
    check("--movie-frames without --movie-play is refused", rc not in (0, None), log)

    with tempfile.TemporaryDirectory() as frames:
        rc, log = run(a.pom1, "--movie-play", movies["gen2"], "--movie-frames", frames)
        pngs = sorted(f for f in os.listdir(frames) if f.endswith(".png"))
        with open(movies["gen2"], "rb") as f:
            data = f.read()
        end_cycle = int.from_bytes(data[-16:-8], "little")
        # One per started frame; a slice may overshoot by an instruction, which
        # can fold the last sliver of the movie into the frame before it.
        expected = -(-end_cycle // FRAME_CYCLES)
        headers_ok = all(open(os.path.join(frames, p), "rb").read(8) == b"\x89PNG\r\n\x1a\n"
                         for p in pngs)
        check(f"GEN2 movie writes {expected} frames (got {len(pngs)}), exit 0",
              rc == 0 and expected - 1 <= len(pngs) <= expected and len(pngs) > 0 and headers_ok
              and (not pngs or pngs[0] == "frame_000000.png"), log)

    if failures:
        print(f"movie_play_cli: {len(failures)} failure(s)")
        return 1
    print("movie_play_cli: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
