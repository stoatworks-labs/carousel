"""Render every parameter at both ends of its range and fail if any made no
difference.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Carousel has state and a clock, so each control is swept where it can act:
through the real plugin at 320x180, catest's `slide` cards as both slides,
for a stated number of frames at 60 fps, with a cue sheet (catest --script)
where a button has to be pressed. An option is set by its element name. A
control whose ends differ by less than `--floor` (mean 8-bit difference per
channel) is barely alive: vectrix's lesson.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context, keeping only the defaults: what
goes dead there is the list of controls that only act with another one set,
which AGENTS.md records.
"""

import argparse
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

# Cue sheets: catest --script, 'frame Name value'. A button is a step to 1
# and a step back to 0.
SCRIPTS = {
    "press-take": "1 Take 1\n2 Take 0\n",
    "press-a": "1 Change A 1\n2 Change A 0\n",
    "press-b": "1 Change B 1\n2 Change B 0\n",
    "to-a": "0 Opacity 1\n1 Opacity 0\n",
    "none": "",
}

# name -> (low, high, context, frames). A setting is "Name=value" (--set) or
# ("script", key). The context is dropped by --bare; the frame count is not.
SWEEP = {
    # Both lit, so the screen is the general path, not one slide's exact one.
    "Linear Light": (["Linear Light=0"], ["Linear Light=1"], ["Curve=Light", "Time=0", "Opacity=0.5"], 3),
    "Opacity": (["Opacity=0"], ["Opacity=1"], ["Time=0"], 3),
    # Half a second after the press: the dissolve under way.
    "Take": ([("script", "none")], [("script", "press-take")], [], 30),
    # A cut to A at frame 1 against a 16 s dissolve: half a second later.
    "Time": (["Time=0"], ["Time=1"], [("script", "to-a")], 30),
    # At rest at the midpoint.
    "Curve": (["Curve=Voltage"], ["Curve=Light"], ["Opacity=0.5"], 3),
    "Lamp": (["Lamp=High"], ["Lamp=Low"], [], 3),
    # Seventy frames after B's change: the shutter open on a cold slide.
    "Mounts": (["Mounts=Card"], ["Mounts=Glass"], [("script", "press-b"), "Pop=1"], 70),
    "Pop": (["Pop=0"], ["Pop=1"], [("script", "press-b")], 70),
    # Half a second after the press: the shutter shut. A lit for A's.
    "Change A": ([("script", "none")], [("script", "press-a")], ["Opacity=0"], 30),
    "Change B": ([("script", "none")], [("script", "press-b")], [], 30),
}

# The About block: a text line and browser buttons, with no pixel to sweep.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide"}


def read_png(path):
    """Enough of PNG for catest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(catest, scratch, out, settings, frames):
    args = [str(catest), "--out", str(out), "--size", "320x180", "--frames", str(frames)]
    scripts = [s[1] for s in settings if isinstance(s, tuple)]
    if len(scripts) > 1:
        raise RuntimeError(f"two cue sheets in one render: {scripts}")
    if scripts:
        args += ["--script", str(scratch / f"{scripts[0]}.txt")]
    for setting in settings:
        if not isinstance(setting, tuple):
            args += ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"catest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(catest):
    result = subprocess.run([str(catest), "--list"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"catest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(catest, scratch, name, bare):
    low, high, context, frames = SWEEP[name]
    context = [] if bare else context
    # A cue sheet in the context and one in an end would collide; none of the
    # table's entries has both.
    tag = f"p{abs(hash(name))}"
    before = render(catest, scratch, scratch / f"{tag}-a.png", context + low, frames)
    after = render(catest, scratch, scratch / f"{tag}-b.png", context + high, frames)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    catest = build / "catest"
    if not catest.exists():
        print(f"{catest} not found", file=sys.stderr)
        return 1

    declared = parameters(catest)
    unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
    if unknown:
        # A new parameter with no sweep is a hole, not a pass.
        print(f"no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
        return 1
    missing = [n for n in SWEEP if n not in declared]
    if missing:
        # A sweep for a parameter that no longer exists is a stale table.
        print(f"swept but not declared: {', '.join(missing)}", file=sys.stderr)
        return 1
    dead, weak = [], []
    with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR")) as scratch:
        scratch = pathlib.Path(scratch)
        os.environ.setdefault("CAROUSEL_LOG_DIR", str(scratch / "logs"))
        for key, text in SCRIPTS.items():
            (scratch / f"{key}.txt").write_text(text)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            names = [n for n in declared if n in SWEEP]
            results = list(pool.map(lambda n: sweep_one(catest, scratch, n, args.bare), names))
    for name, delta in results:
        if delta == 0.0:
            dead.append(name)
            print(f"  DEAD {name:16s} both ends identical")
        elif delta < args.floor:
            weak.append(name)
            print(f"  WEAK {name:16s} mean delta {delta:.4f}")
        elif args.verbose:
            print(f"  ok   {name:16s} mean delta {delta:.3f}")
    print(f"SW Carousel: {len(results)} parameters swept{' (bare: defaults only)' if args.bare else ''}, "
          f"{len(dead)} dead, {len(weak)} barely alive")
    if dead or weak:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
