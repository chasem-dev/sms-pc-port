#!/usr/bin/env python3
"""Measure warm coastal views with the player's resolution and texture settings.

Uses a private copy of the memory card. Compare the same view and clock mode
with --exe pointing to each build. Normal pacing measures delivered frame time;
--unpaced measures rendering headroom, not the player's displayed FPS.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from smoke import BOOT, ROOT

VIEWS = {
    "plaza-inland": (1, (-9300, 400, -6800), "-9300,2200,-6700,0,1800,-1000"),
    "plaza-park": (1, (-9300, 400, -6800), "-12000,2000,-5000,-46000,1500,19000"),
    "harbor-inland": (3, (14000, 1503, 3400), "12000,4500,14000,0,1400,2000"),
}


def summarize(log):
    field = 0
    samples = []
    pattern = re.compile(
        r"stats: (\d+) frames, ([\d.]+) draws/frame, ([\d.]+) vertices/frame, "
        r"(\d+) shader compiles, (\d+) texture uploads, ([\d.]+) ms/frame total, "
        r"([\d.]+) ms/frame in sms_gx")
    for line in log.splitlines():
        capture = re.search(r"captured field (\d+) ", line)
        if capture:
            field = int(capture[1])
        stats = pattern.search(line)
        if field >= 4800 and stats and stats[4] == stats[5] == "0":
            samples.append(tuple(float(stats[i]) for i in (1, 2, 3, 6, 7)))
    if len(samples) < 2:
        raise RuntimeError("Fewer than two warm measurement windows; inspect run.log")
    frames = sum(s[0] for s in samples)
    return {
        "samples": len(samples), "measured_frames": int(frames),
        **{key: sum(s[0] * s[i] for s in samples) / frames
           for i, key in enumerate(("draws", "vertices", "total_ms", "gx_ms"), 1)},
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--iso", type=Path, required=True)
    p.add_argument("--seed-card", type=Path, required=True)
    p.add_argument("--exe", type=Path, default=ROOT / "build/linux-64/sms")
    p.add_argument("--view", choices=VIEWS, required=True)
    p.add_argument("--unpaced", action="store_true")
    p.add_argument("--disable-culling", action="store_true")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--timeout", type=float, default=180)
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    shutil.copytree(a.seed_card, out / "card")
    (out / "shots").mkdir()
    stage, (x, y, z), view = VIEWS[a.view]
    r = (-10000, 400, -8300, -.5, -.8660254) if stage == 1 else (14500, 1503, 3950, 1, 0)
    u = (x-r[0])*r[3] + (z-r[2])*r[4]
    v = -(x-r[0])*r[4] + (z-r[2])*r[3]
    env = os.environ.copy()
    for key in list(env):
        if key.startswith(("SMS_SEA_TEST_", "SMS_OPEN_WORLD_TEST_")) or key in ("SMS_GX_SCALE", "SMS_TEXTURE_PACKS"):
            del env[key]
    env.update(SMS_OPEN_WORLD="1", SMS_OPEN_WORLD_TEST_SPAWN=f"{u},{v},{y-r[1]+5}",
               SMS_OPEN_WORLD_TEST_VIEW=view, SMS_OPEN_WORLD_TEST_CULL="0" if a.disable_culling else "1",
               SMS_OPEN_WORLD_LOG="1", SMS_FRAME_RATE="60", SMS_GX_STATS="120", SMS_AUDIO="0",
               SMS_SKIP_MOVIES="1" if a.unpaced else "0", SMS_VI_DETERMINISTIC="1" if a.unpaced else "0",
               SMS_FIELD_CLOCK="retrace", SMS_WARP=f"{stage},{2 if stage == 1 else 0},50",
               SMS_SAVE_DIR=str(out / "card"), SMS_AUTOPRESS=BOOT,
               SMS_SHOTS="4200,4800,5400,6000,6600", SMS_SHOT_DIR=str(out / "shots"))
    with (out / "run.log").open("w") as log:
        proc = subprocess.Popen([str(a.exe.resolve()), str(a.iso.resolve()), "--headless"],
                                cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + a.timeout
            while proc.poll() is None and time.monotonic() < deadline:
                if "captured field 6600 " in (out / "run.log").read_text(errors="replace"):
                    break
                time.sleep(.3)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    log = (out / "run.log").read_text(errors="replace")
    if "captured field 6600 " not in log or "fatal signal" in log or "unknown FIFO opcode" in log:
        raise RuntimeError(f"Incomplete replay; inspect {out / 'run.log'}")
    result = summarize(log)
    result.update(view=a.view, clock="unpaced" if a.unpaced else "normal",
                  culling_override="disabled" if a.disable_culling else "default",
                  executable=str(a.exe.resolve()), saved_player_settings=True,
                  note="Warm, zero-upload/compile windows after field 4800; local headless measurements.")
    if not a.unpaced:
        result["fps"] = 1000 / result["total_ms"]
    (out / "performance.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
