#!/usr/bin/env python3
"""The defaults that the demo clips can measure, through the real plugin.

Every frame here goes through `catest --pipe` -- the shipped plugin class on
a synthetic clock -- with the clip's frame as Dest (projector A) or Src
(projector B), raw RGBA.

**Pop** (`--pop`, the default mode). The spec's rule: the smallest Pop, in
0.05 mm steps, at which a freshly changed slide -- the frame the shutter is
fully open -- loses at least half of the clip's mean absolute horizontal
gradient, the median over the clips, at 1920x1080. Each clip's middle frame,
upscaled from its 1280x720 by ffmpeg's lanczos (what a host does with a 720p
clip in a 1080p composition), is projector A's slide, A lit; Change A is
pressed at frame 1 at 10 fps, and frame 11 is one second later, the shutter
just open. The gradient is the mean over every pixel and channel of
|I( x + 1, y ) - I( x, y )| on the 8-bit output, against the same frame
glass-mounted (no drift, the same lamp, the same instant). Bisection over
the 21 steps, the loss being monotone in Pop (checked either side of each
answer). A clip never halved by 1 mm counts as above the range: the median
is over every clip. `--native` repeats it at the clips' own 1280x720, for
comparison.

**Alpha** (`--alpha`): the two demo clips with transparency, as projector B
resampled to 1920x1080 and blurred: the same frame with white painted under
its transparent pixels renders the same bytes, and with some opaque pixels
painted it does not.

`--check` compares the measured Pop with catest's declared default and fails
if they differ.

    tools/clips.py [--build DIR] [--pop] [--native] [--alpha] [--check]
"""

import argparse
import os
import pathlib
import re
import statistics
import subprocess
import sys
import tempfile

import numpy

REPO = pathlib.Path(__file__).resolve().parent.parent
MEDIA = pathlib.Path("/Applications/Resolume Arena/media")
# The picture clips. The AV folder's are audio-reactive abstracts, not
# pictures one would put in a slide projector.
CLIPS = sorted((MEDIA / "Shop74").glob("*.mov"))
ALPHA_CLIPS = [MEDIA / "Shop74" / "BattleWeapon_Tank_09.mov", MEDIA / "Shop74" / "Ethnik2.mov"]
FPS = 10
PRESS, OPEN = 1, 11  # the press at frame 1; frame 11 is 1.0 s later
STEPS = [round(0.05 * i, 2) for i in range(21)]


def frame_of(clip, width, height, scale=True):
    """The clip's middle frame as raw RGBA, top row first."""
    probe = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(clip)],
                           capture_output=True, text=True, stdin=subprocess.DEVNULL, check=True)
    middle = float(probe.stdout.strip()) / 2.0
    vf = [] if not scale else ["-vf", f"scale={width}:{height}:flags=lanczos"]
    result = subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-ss", f"{middle:.3f}", "-i", str(clip), "-frames:v", "1", *vf,
                             "-pix_fmt", "rgba", "-f", "rawvideo", "-"], capture_output=True, check=True)
    if len(result.stdout) != width * height * 4:
        raise RuntimeError(f"{clip.name}: {len(result.stdout)} bytes, not {width}x{height} RGBA")
    return result.stdout


def pipe(catest, scratch, dest, width, height, frames, settings, src=None, src_size=None, script=None):
    """Hold `dest` for `frames` frames through catest --pipe; every frame out."""
    args = [str(catest), "--pipe", "--size", f"{width}x{height}", "--fps", str(FPS)]
    for s in settings:
        args += ["--set", s]
    if src is not None:
        path = scratch / "src.rgba"
        path.write_bytes(src * frames)
        args += ["--pipe-src", str(path), "--src-size", src_size]
    if script is not None:
        path = scratch / "cues.txt"
        path.write_text(script)
        args += ["--script", str(path)]
    result = subprocess.run(args, input=dest * frames, capture_output=True)
    size = width * height * 4
    if result.returncode != 0 or len(result.stdout) != size * frames:
        raise RuntimeError(f"catest --pipe: exit {result.returncode}, {len(result.stdout)} bytes\n{result.stderr.decode()[-400:]}")
    return [result.stdout[i * size:(i + 1) * size] for i in range(frames)]


def gradient(frame, width, height):
    """Mean |I( x + 1, y ) - I( x, y )| over every pixel pair and colour channel."""
    image = numpy.frombuffer(frame, dtype=numpy.uint8).reshape(height, width, 4)[:, :, :3].astype(numpy.int32)
    return float(numpy.abs(image[:, 1:] - image[:, :-1]).mean())


def opened(catest, scratch, dest, width, height, mounts, pop):
    """Projector A's slide at the frame its shutter is just open after a change."""
    frames = pipe(catest, scratch, dest, width, height, OPEN + 1, ["Opacity=0", f"Mounts={mounts}", f"Pop={pop}"],
                  script=f"{PRESS} Change A 1\n{PRESS + 1} Change A 0\n")
    return frames[OPEN]


def pop_for(catest, scratch, clip, width, height, scale):
    dest = frame_of(clip, width, height, scale)
    sharp = gradient(opened(catest, scratch, dest, width, height, "Glass", 0.0), width, height)
    cache = {}

    def loss(i):
        if i not in cache:
            cache[i] = 1.0 - gradient(opened(catest, scratch, dest, width, height, "Card", STEPS[i]), width, height) / sharp
        return cache[i]

    if sharp == 0.0 or loss(len(STEPS) - 1) < 0.5:
        return None, sharp, cache
    lo, hi = 0, len(STEPS) - 1  # loss( lo ) < 0.5 <= loss( hi )
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if loss(mid) >= 0.5:
            hi = mid
        else:
            lo = mid
    if not (loss(hi - 1) < 0.5 <= loss(hi)):
        raise RuntimeError(f"{clip.name}: the loss is not monotone around {STEPS[hi]} mm")
    return STEPS[hi], sharp, cache


def declared_pop(catest):
    out = subprocess.run([str(catest), "--list"], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 4 and parts[1] == "Pop":
            return round(float(parts[3]) * 1.0, 4)  # Pop is 0..1 mm, linear
    raise RuntimeError("catest --list has no Pop")


def run_pop(catest, scratch, width, height, scale):
    found = []
    print(f"Pop at {width}x{height}{' (upscaled from 1280x720, lanczos)' if scale else ''}: the smallest that halves a freshly changed slide's gradient")
    for clip in CLIPS:
        mm, sharp, cache = pop_for(catest, scratch, clip, width, height, scale)
        tried = ", ".join(f"{STEPS[i]:.2f}:{cache[i]:.3f}" for i in sorted(cache))
        if mm is None:
            # Its answer is above the range, and it still counts: the rule is
            # the median over the clips, not over the clips that answered.
            found.append(float("inf"))
            print(f"  {clip.name:28s} gradient {sharp:7.3f}  above 1 mm   (Pop:loss {tried})")
            continue
        found.append(mm)
        print(f"  {clip.name:28s} gradient {sharp:7.3f}  {mm:.2f} mm   (Pop:loss {tried})")
    median = statistics.median_low(found)
    above = sum(1 for mm in found if mm == float("inf"))
    if median == float("inf"):
        raise RuntimeError("most clips are never halved by 1 mm: the range is too small")
    print(f"  median over {len(found)} clips ({above} above 1 mm): {median:.2f} mm")
    return median


def run_alpha(catest, scratch):
    width, height, ok = 1920, 1080, True
    black = bytes([0, 0, 0, 255]) * (width * height)
    print("Alpha: the demo clips with transparency, as B, resampled from 1280x720 and blurred")
    for clip in ALPHA_CLIPS:
        src = frame_of(clip, 1280, 720, scale=False)
        pixels = numpy.frombuffer(src, dtype=numpy.uint8).reshape(-1, 4)
        clear = int((pixels[:, 3] == 0).sum())
        partial = int(((pixels[:, 3] > 0) & (pixels[:, 3] < 255)).sum())
        white = pixels.copy()
        white[white[:, 3] == 0, :3] = 255
        # The control: 200 opaque pixels, spread out, painted white.
        painted = pixels.copy()
        opaque = numpy.flatnonzero(painted[:, 3] == 255)
        chosen = opaque[:: max(1, len(opaque) // 200)][:200]
        painted[chosen, :3] = 255
        n = len(chosen)
        src, white, painted = pixels.tobytes(), white.tobytes(), painted.tobytes()
        renders = []
        for variant in (src, white, painted):
            frames = pipe(catest, scratch, black, width, height, OPEN + 1, ["Pop=1", "Lamp=Low"], src=variant, src_size="1280x720",
                          script=f"{PRESS} Change B 1\n{PRESS + 1} Change B 0\n")
            renders.append(frames[OPEN])
        same = renders[0] == renders[1]
        seen = renders[0] != renders[2]
        ok = ok and same and seen and clear > 0
        print(f"  {clip.name:28s} {clear} transparent, {partial} partly transparent pixels: white under the transparent ones "
              f"{'changes nothing' if same else 'CHANGES the picture'}; {n} opaque pixels painted {'do' if seen else 'do NOT'} show")
    return ok


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--pop", action="store_true")
    parser.add_argument("--native", action="store_true")
    parser.add_argument("--alpha", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not (args.pop or args.native or args.alpha):
        args.pop = True
    build = args.build if args.build.is_absolute() else REPO / args.build
    catest = build / "catest"
    if not catest.exists() or not CLIPS:
        print(f"need {catest} and the clips in {MEDIA}", file=sys.stderr)
        return 1
    status = 0
    with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR")) as scratch:
        scratch = pathlib.Path(scratch)
        os.environ.setdefault("CAROUSEL_LOG_DIR", str(scratch / "logs"))
        if args.pop:
            median = run_pop(catest, scratch, 1920, 1080, True)
            declared = declared_pop(catest)
            print(f"  catest declares Pop {declared:.2f} mm")
            if args.check and abs(declared - median) > 1e-6:
                print(f"  the declared default is not the measured {median:.2f} mm", file=sys.stderr)
                status = 1
        if args.native:
            run_pop(catest, scratch, 1280, 720, False)
        if args.alpha and not run_alpha(catest, scratch):
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
