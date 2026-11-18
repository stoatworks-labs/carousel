"""The browser demo against the plugin: the same frames, two hosts, one picture.

    python3 demo/tools/crosscheck/crosscheck.py [--catest build-verify/catest] [--size 320x180] [--keep DIR]

One run of frames, rendered twice:

  - by the PLUGIN built for this Mac: `catest --pipe` (the real Carousel class
    in a GL 4.1 context, MixRig's host: a millisecond clock declared, 60 fps
    from 0), with Dest on stdin and Src from --pipe-src, and a cue sheet.
  - by the PAGE: demo/ served from here and loaded in headless Chrome, through
    window.carouselDemo.sequence, which makes a fresh instance of the plugin
    compiled to WebAssembly (carousel-core.wasm) on the page's WebGL2 context
    and runs the same frames with the same cues by catest's own rules.

The cue sheet walks through what the plugin is for: a 2 s Voltage dissolve
from B to A (Take), a slide change on the lit projector (the dark gap and the
cold slide drifting into focus, at Pop 1 mm), a second press mid-cycle (which
must be ignored), a Light-shaped dissolve back, a cut at Time 0, Lamp Low,
Linear Light off, a change on the dark projector and the fader moved at Time
0. Dest is at the output's size; Src at three quarters of it, so the plugin
resamples B and blurs a resampled slide, and Src carries a band at alpha 128,
so a straight-alpha slide is composited over black on both sides.

Both halves are the plugin's own code, so what this tests is everything in
between: emscripten's compilation (no fused multiply-add, musl's libm), the
page's host (textures, the clock, the cues), the kit's port() of the GLSL to
ES 3.00 and WebGL2 through ANGLE against GL 4.1 on the same GPU. Before any
picture, the page's table of controls (names, kinds, defaults, elements) is
compared with `catest --list`.

A frame passes when no pixel is more than --max levels of 255 apart and
--exact % are identical. Negative controls -- the page with one cue a frame
late, and with Time one slider step (0.001) off -- are compared with the
plugin's frames as they should be, and each must fail that test, so the
comparison is shown to see a slip of that size.

Not run by tools/verify.sh: it needs a GPU and Chrome. Exit 1 if the table
differs, a frame fails or a negative control passes. Measured figures: see
AGENTS.md, "The browser demo".
"""
import argparse
import functools
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(DEMO, ".."))
sys.dont_write_bytecode = True

FPS = 60
TIME_2S = "0.35355339"  # ParamForTime( 2 ): Time's default, 16 p^2 s

CUES = f"""# demo/tools/crosscheck: catest's cue format, `frame Name value`, host units.
0 Pop 1
0 Opacity 1
0 Time {TIME_2S}
379 Time {TIME_2S}
# A 2 s Voltage dissolve, B to A.
20 Take 1
21 Take 0
# A slide change on the lit projector, and a second press mid-cycle.
160 Change A 1
161 Change A 0
180 Change A 1
181 Change A 0
# A Light-shaped dissolve back to B.
260 Curve 1
260 Take 1
261 Take 0
# A cut at Time 0, then Lamp Low and Linear Light off.
380 Time 0
390 Take 1
391 Take 0
400 Lamp 1
410 Linear Light 0
# A change on the dark projector, then the fader moved at Time 0.
420 Change B 1
421 Change B 0
430 Opacity 1
470 Opacity 0
"""
FRAMES = 480

# Each negative control: a label, the cue sheet changed, and the frames
# compared (the page sends only those).
NEGATIVES = [
    ("Change A one frame late", lambda c: c.replace("160 Change A 1\n161 Change A 0", "161 Change A 1\n162 Change A 0"), (150, 260)),
    ("Time one slider step (0.001) off", lambda c: c.replace(f"Time {TIME_2S}", "Time 0.35455339"), (15, 160)),
]


def parse_size(text):
    m = re.fullmatch(r"(\d+)x(\d+)", text)
    if not m:
        sys.exit(f"--size {text}: want WxH")
    return int(m.group(1)), int(m.group(2))


def pictures(width, height, sw, sh):
    """Dest and Src, top row first, RGBA8. Sharp edges, so the focus shows."""
    y, x = np.mgrid[0:height, 0:width]
    dest = np.zeros((height, width, 4), np.uint8)
    bars = np.array([[235, 235, 235], [235, 235, 16], [16, 235, 235], [16, 235, 16],
                     [235, 16, 235], [235, 16, 16], [16, 16, 235], [16, 16, 16]], np.uint8)
    dest[..., :3] = bars[(x * 8) // width]
    ramp = (y >= height * 2 // 3)
    dest[ramp, 0] = dest[ramp, 1] = dest[ramp, 2] = ((x[ramp] * 255) // max(1, width - 1)).astype(np.uint8)
    box = (abs(x - width // 2) < width // 10) & (abs(y - height // 3) < height // 8)
    dest[box, :3] = 255
    dest[..., 3] = 255

    v, u = np.mgrid[0:sh, 0:sw]
    src = np.zeros((sh, sw, 4), np.uint8)
    check = ((u // 12) + (v // 12)) % 2 == 0
    src[..., 0] = np.where(check, 230, 30)
    src[..., 1] = np.where(check, 200, 60)
    src[..., 2] = ((u * 255) // max(1, sw - 1)).astype(np.uint8)
    src[..., 3] = 255
    band = (v >= sh * 3 // 4)
    src[band, 3] = 128  # straight alpha: composited over black by the plugin
    return dest.tobytes(), src.tobytes()


def run_catest(catest, args, env, stdin=None):
    p = subprocess.run([catest, *args], input=stdin, capture_output=True, env=env)
    if p.returncode != 0:
        sys.exit(f"catest {' '.join(args)} failed ({p.returncode}): {p.stderr.decode(errors='replace')}")
    return p.stdout


LIST_ROW = re.compile(r"^(\d+)\s+(.+?)\s{2,}(boolean|standard|event|option|about)\s+(\S+)\s+(\[[^\]]*\]|-)\s*(.*)$")


def compare_table(catest, page_table, env):
    """`catest --list` against the page's table: name, kind, default (4 dp), elements, in order."""
    rows = []
    for line in run_catest(catest, ["--list"], env).decode().splitlines()[1:]:
        m = LIST_ROW.match(line.rstrip())
        if m is None:
            continue
        index, name, kind, value, _, elements = m.groups()
        if kind == "about":
            continue
        rows.append((int(index), name, kind, float(value), [e.strip() for e in elements.split("|")] if elements else []))
    problems = []
    if len(rows) != len(page_table):
        problems.append(f"catest lists {len(rows)} controls before the About block, the page {len(page_table)}")
    for (index, name, kind, value, elements), mine in zip(rows, page_table):
        if (name, kind) != (mine["name"], mine["kind"]) or abs(value - mine["value"]) > 0.00005 or elements != mine["elements"]:
            problems.append(f"#{index}: catest says {name} {kind} {value:.4f} {elements}, the page "
                            f"{mine['name']} {mine['kind']} {mine['value']:.4f} {mine['elements']}")
    return len(rows), problems


def compare(a, b, width, height):
    x = np.frombuffer(a, np.uint8).reshape(height, width, 4).astype(np.int16)
    y = np.frombuffer(b, np.uint8).reshape(height, width, 4).astype(np.int16)
    worst = np.abs(x - y).max(axis=2)
    return float((worst == 0).mean() * 100.0), int(worst.max())


class Handler(http.server.SimpleHTTPRequestHandler):
    """demo/ as it is, plus /xc/ for the job, the pictures and the page's answers."""

    def __init__(self, *args, state, **kwargs):
        self.state = state
        super().__init__(*args, directory=DEMO, **kwargs)

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/xc/job.json":
            return self.reply(json.dumps(self.state["job"]).encode(), "application/json")
        if self.path in ("/xc/dest", "/xc/src"):
            return self.reply(self.state[self.path[4:]], "application/octet-stream")
        return super().do_GET()

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        m = re.fullmatch(r"/xc/out/(\w+)/(\d+)", self.path)
        if m:
            self.state["page"].setdefault(m.group(1), {})[int(m.group(2))] = body
        elif self.path == "/xc/table":
            self.state["table"] = json.loads(body)
        elif self.path == "/xc/notes":
            self.state["notes"] = json.loads(body)
        elif self.path == "/xc/error":
            self.state["error"] = body.decode(errors="replace")
            self.state["finished"].set()
        elif self.path == "/xc/done":
            self.state["finished"].set()
        self.reply(b"ok", "text/plain")

    def reply(self, body, kind):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--catest", default=os.path.join(REPO, "build-verify", "catest"))
    parser.add_argument("--size", default="320x180")
    parser.add_argument("--keep", help="write the plugin's and the page's frame at each cue here as PNGs")
    parser.add_argument("--max", type=int, default=1, help="the largest difference allowed anywhere, in levels of 255 (default 1)")
    parser.add_argument("--exact", type=float, default=99.0, help="share of pixels, %%, that must be identical in every frame (default 99)")
    parser.add_argument("--chrome", default=os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    args = parser.parse_args(argv)
    width, height = parse_size(args.size)
    sw, sh = width * 3 // 4, height * 3 // 4
    if not os.access(args.catest, os.X_OK):
        sys.exit(f"no catest at {args.catest}: build it (tools/verify.sh leaves one in build-verify/)")

    work = tempfile.mkdtemp(prefix="carousel-xc-")
    # The harness writes the plugin's log; keep it out of ~/Library/Logs.
    env = {**os.environ, "CAROUSEL_LOG_DIR": os.path.join(work, "log")}
    dest, src = pictures(width, height, sw, sh)
    frame_bytes = width * height * 4
    cues = os.path.join(work, "cues.txt")
    with open(cues, "w") as handle:
        handle.write(CUES)
    srcfile = os.path.join(work, "src.raw")
    with open(srcfile, "wb") as handle:
        handle.write(src * FRAMES)
    plugin = run_catest(args.catest, ["--pipe", "--size", f"{width}x{height}", "--src-size", f"{sw}x{sh}",
                                      "--script", cues, "--pipe-src", srcfile], env, stdin=dest * FRAMES)
    if len(plugin) != FRAMES * frame_bytes:
        sys.exit(f"catest wrote {len(plugin)} bytes, not {FRAMES} frames")

    runs = [{"id": "main", "cues": CUES, "keep": [0, FRAMES]}]
    for i, (_, change, window) in enumerate(NEGATIVES):
        runs.append({"id": f"neg{i}", "cues": change(CUES), "keep": list(window)})
    job = {"width": width, "height": height, "srcWidth": sw, "srcHeight": sh, "fps": FPS, "frames": FRAMES, "runs": runs}
    state = {"job": job, "dest": dest, "src": src, "page": {}, "table": None, "notes": None,
             "error": None, "finished": threading.Event()}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, state=state))
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = tempfile.mkdtemp(prefix="carousel-chrome-")
    chrome = subprocess.Popen([
        args.chrome, "--headless=new", "--no-first-run", "--no-default-browser-check",
        f"--user-data-dir={profile}", "--use-angle=metal", "--enable-gpu", "--ignore-gpu-blocklist",
        "--window-size=1600,1200", f"http://127.0.0.1:{port}/tools/crosscheck/",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not state["finished"].wait(600):
            sys.exit("the page never finished (600 s)")
    finally:
        chrome.terminate()
        try:
            chrome.wait(10)
        except subprocess.TimeoutExpired:
            chrome.kill()
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)
    if state["error"]:
        shutil.rmtree(work, ignore_errors=True)
        sys.exit(f"the page failed:\n{state['error']}")

    failures = 0
    count, problems = compare_table(args.catest, state["table"] or [], env)
    if problems:
        failures += len(problems)
        print("the page's controls differ from catest --list:")
        for line in problems:
            print(f"  {line}")
    else:
        print(f"controls: all {count} of catest --list's before the About block, same names, kinds, defaults and elements, in order")
    notes = state["notes"] or {}
    if notes.get("glErrors"):
        failures += 1
        print(f"GL errors on the page: {notes['glErrors']}")
    print(f"the page compiled {notes.get('shaders', '?')} shader stages (four an instance), every one its own checked copy;"
          f" the page's own instance settled the host clock on {notes.get('clock')} (1: seconds)")

    def frame_of(blob, k):
        return blob[k * frame_bytes:(k + 1) * frame_bytes]

    page = state["page"].get("main", {})
    worst_max, worst_exact, worst_frame, failed = 0, 100.0, None, []
    for k in range(FRAMES):
        got = page.get(k)
        if got is None or len(got) != frame_bytes:
            failed.append(k)
            continue
        exact, biggest = compare(frame_of(plugin, k), got, width, height)
        if biggest > worst_max or (biggest == worst_max and exact < worst_exact):
            worst_frame = k
        worst_max = max(worst_max, biggest)
        worst_exact = min(worst_exact, exact)
        if biggest > args.max or exact < args.exact:
            failed.append(k)
    identical = sum(1 for k in range(FRAMES) if page.get(k) == frame_of(plugin, k))
    print(f"\n{width}x{height} (Src {sw}x{sh}), {FRAMES} frames at {FPS} fps: {identical} identical to the byte")
    if identical != FRAMES:
        print(f"  worst pixel {worst_max} level(s) apart (frame {worst_frame}), fewest identical pixels in a frame {worst_exact:.3f}%")
    if failed:
        failures += 1
        print(f"  FAIL at frames {failed[:20]}{' …' if len(failed) > 20 else ''}")
    else:
        print("  every frame passes")

    if args.keep:
        from PIL import Image
        os.makedirs(args.keep, exist_ok=True)
        for k in (0, 50, 81, 140, 175, 200, 320, 395, 450, 479):
            Image.frombytes("RGBA", (width, height), frame_of(plugin, k)).save(os.path.join(args.keep, f"{k:03d}-plugin.png"))
            if page.get(k):
                Image.frombytes("RGBA", (width, height), page[k]).save(os.path.join(args.keep, f"{k:03d}-page.png"))

    print("\nnegative controls: the page a little off, against the plugin as it should be")
    for i, (label, _, (lo, hi)) in enumerate(NEGATIVES):
        got = state["page"].get(f"neg{i}", {})
        caught, biggest, fewest = False, 0, 100.0
        for k in range(lo, hi):
            if got.get(k) is None:
                continue
            exact, big = compare(frame_of(plugin, k), got[k], width, height)
            biggest, fewest = max(biggest, big), min(fewest, exact)
            caught = caught or big > args.max or exact < args.exact
        failures += 0 if caught else 1
        print(f"  {label:<36} worst {biggest:>3} levels, fewest identical {fewest:7.3f}%  {'caught' if caught else 'NOT CAUGHT'}")

    print(f"\n{'every frame passes, every negative control is caught' if not failures else f'{failures} problem(s)'}"
          f" (pass: no pixel more than {args.max} level(s) apart and {args.exact}% identical, every frame)")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
