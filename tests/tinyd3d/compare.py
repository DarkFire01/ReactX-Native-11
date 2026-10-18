"""
PROJECT:     ReactX tests
LICENSE:     MIT (https://spdx.org/licenses/MIT)
PURPOSE:     Run tinyd3d scenes on System32 and ReactX and compare the captured frames
COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>

compare.py <bin dir> <output dir> [scene ...] [--frames N] [--timeout S] [--tolerance T]
"""

import argparse
import os
import struct
import subprocess
import sys


def run_scene(exe, scene, runtime, capture, frames, timeout):
    command = [exe, scene, "-runtime", runtime, "-frames", str(frames), "-capture", capture]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, "timed out after %d s" % timeout

    lines = [line for line in (result.stdout + result.stderr).splitlines() if line.startswith("error")]
    if result.returncode != 0 or not os.path.exists(capture):
        return False, "; ".join(lines) or "exit %d" % result.returncode
    return True, ""


def read_bmp(path):
    with open(path, "rb") as handle:
        data = handle.read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    return abs(width), abs(height), data[offset:]


def compare(first, second, tolerance):
    width, height, a = read_bmp(first)
    other_width, other_height, b = read_bmp(second)
    if (width, height) != (other_width, other_height):
        return "size %dx%d vs %dx%d" % (width, height, other_width, other_height)

    differing = 0
    worst = 0
    for index in range(0, width * height * 4, 4):
        # Alpha is not compared; the swapchain ignores it
        delta = max(abs(a[index + channel] - b[index + channel]) for channel in range(3))
        if delta > tolerance:
            differing += 1
        worst = max(worst, delta)

    if differing == 0:
        return None
    return "%d pixels differ (%.3f%%), worst channel delta %d" % (differing, 100.0 * differing / (width * height), worst)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("bin")
    parser.add_argument("out")
    parser.add_argument("scenes", nargs="*")
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--timeout", type=int, default=20)
    parser.add_argument("--tolerance", type=int, default=1)
    args = parser.parse_args()

    exe = os.path.join(args.bin, "tinyd3d.exe")
    os.makedirs(args.out, exist_ok=True)

    scenes = args.scenes
    if not scenes:
        listing = subprocess.run([exe, "-list"], capture_output=True, text=True).stdout
        scenes = [line.split()[0] for line in listing.splitlines() if line.strip()]

    failures = 0
    for scene in scenes:
        captures = {}
        problems = []
        for runtime in ("system", "reactx"):
            capture = os.path.join(args.out, "%s-%s.bmp" % (scene, runtime))
            if os.path.exists(capture):
                os.remove(capture)
            ok, message = run_scene(exe, scene, runtime, capture, args.frames, args.timeout)
            if ok:
                captures[runtime] = capture
            else:
                problems.append("%s: %s" % (runtime, message))

        if not problems:
            difference = compare(captures["system"], captures["reactx"], args.tolerance)
            if difference:
                problems.append(difference)

        status = "PASS" if not problems else "FAIL"
        failures += 1 if problems else 0
        print("%-10s %s %s" % (scene, status, " | ".join(problems)))
        sys.stdout.flush()

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
