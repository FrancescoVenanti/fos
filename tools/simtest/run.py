#!/usr/bin/env python3
"""Headless FOs simulator test runner.

Replays a scripted list of inputs against the simulator and saves PNG
screenshots, using the simulator's CROSSPOINT_SIM_INPUT_SCRIPT and
CROSSPOINT_SIM_SCREENSHOTS hooks.

A scenario is tools/simtest/scenarios/<name>.json:

    {
      "description": "Open Settings from the launcher",
      "boot_ms": 3000,          # time before the first step (default 3000)
      "settle_ms": 900,         # pause after each input (default 900)
      "sd": "budget-online",    # optional overlay folder in tools/simtest/sd-overlays/
      "env": {"FOS_SIM_NOW": "2026-10-26T09:41:00"},   # optional extra env vars
      "steps": [
        {"shot": "launcher"},
        {"tap": [240, 400]},
        {"swipe": [20, 400, 400, 400]},
        {"key": "DOWN"},        # UP, DOWN, ENTER (OK), POWER, BACK, HOME, SLEEP
        {"key": "POWER", "hold": 1500},
        {"key": "ENTER", "settle": 150},  # per-step pause after the input
        {"tap": [240, 400], "hold": 1500, "shot_at": 700, "shot_name": "pressed"},
        {"wait": 2000},
        {"shot": "settings"}
      ]
    }

Each run starts from a fresh copy of tools/simtest/sd/ (plus the overlay) as
the SD card. Output: tools/simtest/out/<name>/NN-<shot>.png and sim.log.

Usage:
    python tools/simtest/run.py launcher
    python tools/simtest/run.py --all
    python tools/simtest/run.py --list
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SCENARIOS = os.path.join(HERE, "scenarios")
SD_TEMPLATE = os.path.join(HERE, "sd")
SD_OVERLAYS = os.path.join(HERE, "sd-overlays")
OUT = os.path.join(HERE, "out")
PROGRAM = os.path.join(REPO, ".pio", "build", "simulator", "program.exe" if os.name == "nt" else "program")


def build_timeline(scenario, out_dir):
    t = int(scenario.get("boot_ms", 3000))
    default_settle = int(scenario.get("settle_ms", 900))
    inputs, shots, names = [], [], []

    def shot_at(at, label):
        name = "%02d-%s.png" % (len(shots) + 1, label)
        shots.append("%d:%s" % (at, os.path.join(out_dir, name)))
        names.append(name)

    for step in scenario["steps"]:
        settle = int(step.get("settle", default_settle))
        if "shot_at" in step:
            # Screenshot while this input is still in progress (e.g. a held tap).
            shot_at(t + int(step["shot_at"]), step.get("shot_name", "during"))
        if "wait" in step:
            t += int(step["wait"])
        elif "shot" in step:
            shot_at(t, step["shot"])
            t += 100
        elif "tap" in step:
            x, y = step["tap"]
            hold = step.get("hold")
            inputs.append("%d:TAP:%d,%d%s" % (t, x, y, ",%d" % hold if hold else ""))
            t += (hold or 80) + settle
        elif "swipe" in step:
            x1, y1, x2, y2 = step["swipe"]
            inputs.append("%d:SWIPE:%d,%d,%d,%d,%d" % (t, x1, y1, x2, y2, step.get("ms", 250)))
            t += step.get("ms", 250) + settle
        elif "key" in step:
            key = step["key"].upper()
            hold = step.get("hold")
            if key == "SLEEP":
                inputs.append("%d:SLEEP" % t)
            else:
                inputs.append("%d:%s%s" % (t, key, ":%d" % hold if hold else ""))
            t += (hold or 80) + settle
        else:
            raise ValueError("unknown step: %r" % step)
    inputs.append("%d:QUIT" % (t + 300))
    return ";".join(inputs), ";".join(shots), names, t + 300


def prepare_sd(scenario, run_dir):
    sd = os.path.join(run_dir, "sd")
    if os.path.isdir(sd):
        shutil.rmtree(sd)
    if os.path.isdir(SD_TEMPLATE):
        shutil.copytree(SD_TEMPLATE, sd)
    else:
        os.makedirs(sd)
    overlay = scenario.get("sd")
    if overlay:
        shutil.copytree(os.path.join(SD_OVERLAYS, overlay), sd, dirs_exist_ok=True)
    return sd


def run(name):
    with open(os.path.join(SCENARIOS, name + ".json"), encoding="utf-8") as f:
        scenario = json.load(f)
    run_dir = os.path.join(OUT, name)
    if os.path.isdir(run_dir):
        shutil.rmtree(run_dir)
    os.makedirs(run_dir)
    sd = prepare_sd(scenario, run_dir)
    inputs, shots, names, total_ms = build_timeline(scenario, run_dir)

    env = os.environ.copy()
    env.update({
        "CROSSPOINT_SIM_SD": sd,
        "CROSSPOINT_SIM_INPUT_SCRIPT": inputs,
        "CROSSPOINT_SIM_SCREENSHOTS": shots,
        "FOS_SIM_HEADLESS": "1",
        # Keep the window off-screen-ish and small; SDL still needs a window.
        "SDL_VIDEO_WINDOW_POS": "0,0",
    })
    env.update({k: str(v) for k, v in scenario.get("env", {}).items()})

    with open(os.path.join(run_dir, "sim.log"), "w", encoding="utf-8", errors="replace") as log:
        log.write("INPUT %s\nSHOTS %s\n\n" % (inputs, shots))
        log.flush()
        try:
            proc = subprocess.run([PROGRAM], cwd=REPO, env=env, stdout=log, stderr=subprocess.STDOUT,
                                  timeout=total_ms / 1000 + 30)
            code = proc.returncode
        except subprocess.TimeoutExpired:
            code = "timeout"

    missing = [n for n in names if not os.path.isfile(os.path.join(run_dir, n))]
    status = "ok" if not missing and code in (0, "0") else "FAIL"
    print("%-28s %s (exit %s)%s" % (name, status, code, "  missing: " + ", ".join(missing) if missing else ""))
    for n in names:
        if n not in missing:
            print("    " + os.path.join(run_dir, n))
    return status == "ok"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenario", nargs="*")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    available = sorted(f[:-5] for f in os.listdir(SCENARIOS) if f.endswith(".json"))
    if args.list:
        print("\n".join(available))
        return 0
    if not os.path.isfile(PROGRAM):
        print("Simulator not built: run  .\\tools\\build.ps1 simulator")
        return 2
    names = available if args.all else args.scenario
    if not names:
        parser.print_help()
        return 2
    results = [run(n) for n in names]
    print("%d/%d scenarios ok" % (sum(results), len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
