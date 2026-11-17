# carousel

Two Kodak slide projectors on a dissolve unit, for Resolume Arena/Avenue, as one FFGL
**mixer**: the layer below is projector A's slide, this layer projector B's. Each lamp is a
300 W tungsten filament following the dissolve unit's voltage through its thermal lag
(filament's ODE, RK4 on the CPU in double); a slide change is a 1 s shutter cycle; a
card-mounted slide drifts into focus as it warms. C++/GLSL, CMake MODULE → universal
`.bundle` (macOS) + Windows `.dll` (never built). MIT.

| id | display name | bundle | type | inputs |
| --- | --- | --- | --- | --- |
| `CA01` | `SW Carousel` | `Carousel.bundle`, `com.stoatworks.ffgl.carousel` | mixer | Dest = the layer below (projector A), Src = this layer (projector B) |

Read `AGENTS.md` before touching the lamp (`source/Lamp.*`), the dissolve, shutter and heat
(`source/Model.*`), the colour (`source/Colour.*`, `source/Cmf.h`, generated), the shaders'
transmittance and exact branch (`source/Shaders.cpp`), index 0, or any tolerance in
`tools/catest/main.cpp`. Read genlock's AGENTS.md (mixer section) before touching
`ProcessOpenGL`'s inputs. The design is `~/Projects/resolume/specs/SPEC-carousel.md`; its "As
built" section says where the code departs from it.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64` (iteration builds here are arm64,
  in `build-arm64`)
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (into Arena's Extra Effects). **Not run on
  this machine**: the plugin has never been loaded into Resolume.
- Render offline: `./build/catest --out /tmp/carousel.png` (the `slide` cards as Dest and
  Src, on the defaults); `--frames N --fps F` renders N frames and writes the last
- Inputs: `--input-a NAME` (Dest) and `--input-b NAME` (Src), each one of `slide slide-alpha
  split edge black white grey`; `--src-size WxH` for a Src of another size; `--size WxH` for
  the output
- List parameters: `./build/catest --list`
- Set anything by name: `./build/catest --set "Opacity=0.5" --set "Curve=Light"` (a standard
  control takes the host's 0..1; an option takes its element name; anything unparsable, or a
  name that doesn't exist, exits 2)
- A clip through it: `ffmpeg -nostdin -i a.mov -f rawvideo -pix_fmt rgba - | ./build/catest
  --pipe --size WxH --pipe-src b.rgba | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -i -
  out.mp4`. stdin is Dest (projector A), `--pipe-src` (a file or FIFO) is Src (projector B);
  without it, `--input-b` is held. The run ends with the shorter input; a partial last frame
  is dropped; a reader that hangs up ends it with exit 1 (SIGPIPE ignored). `--fps` sets the
  synthetic clock (default 60).
- Cue sheets for `--out` and `--pipe`: `--script cues`, `frame Name value` per line; standard
  parameters ramp between keys, options and buttons step (a button keyed 1 then 0 is a
  press); a bad line exits 2.
- Render cost: `./build/catest --bench` (720p, 1080p and 4K: one lit, both lit, both
  blurred at Pop 1 mm).

## Verify
- Everything: `OXBOW=… tools/verify.sh [BUILD_DIR]` (source greps, the FFGL pin and
  `Cmf.h`, the three shaders through `glslc`, a fresh universal build in `build-verify` with
  no warnings outside the SDK, every check below on the GPU and again on Apple's software
  renderer, the pipe, the bundle through lipo, nm, plist, version and an ad-hoc signature,
  `oxbow probe` and `selftest`, 35 mutants, the sweep and its `--bare` list, the demo clips,
  the bench). Most of its time is the software renderer. It ends with one line,
  `verify: all N checks passed (M negative controls; logs in …)`, or the failures; each
  check's whole output is in the logs folder.
- **Set `TMPDIR`** to a writable scratch directory in a sandboxed Claude session, or the
  heredocs in verify.sh, glslc.sh and mutate.sh hang.
- **Set `OXBOW`** to a built `oxbow` binary. The default,
  `~/Projects/resolume/oxbow/build/oxbow`, isn't built on this Mac, and verify **fails**
  without one (it never skips). Build it out of tree: `cmake -S ~/Projects/resolume/oxbow
  -B <scratch>/oxbow-build && cmake --build <scratch>/oxbow-build` (never inside
  ~/Projects). oxbow is an effect host and hands a mixer one input, so its `selftest` must be
  refused on frame 0; verify requires that.
- **Set `CAROUSEL_LOG_DIR`** for hand runs of `catest`: every run instantiates the real
  plugin, which logs, and the default is the folder an Arena session's log goes to.
- **The checks** (GL, each at 640×360 and 320×180, each with its negative controls):
  `--mixer` (the guards; two padded inputs of two sizes; no padding reaches the picture; each
  projector resampled from 0.6 and 1.37 of the output; the host's GL state comes back),
  `--unchanged` (one projector alone at rest is its slide over black, to the bit),
  `--lamp` (each gain frame by frame against the harness's own ODE; the lag printed),
  `--colour` (the CCT from the picture is the filament's), `--dissolve` (the sag, the warm
  middle, the Light curve's flat sum, the ramp lasting Time), `--cut` (Time 0: the frame at
  the cut bitwise the frame before; the lag), `--change` (the shutter's cycle and dark gap at
  59.94 fps), `--pop` (both projectors: the blur's spread, the kernel's mass, τ from the
  picture, Glass never blurs), `--alpha`, `--linear`, `--resize`, `--rates` (24, 59.94, 60,
  144 fps), `--clock` (origins to a year, ms or s, the unit vote), `--mutation` (one
  character of the shipped GLSL through the shader hook). `--names` needs no GL.
- `CATEST_RENDERER=software` runs any of it on Apple's software renderer; verify runs all of
  them there; `--mixer` is the slowest there by far.
- The checks can fail: `tools/mutate.sh [WORDS]` (35 one-character mutants of the GLSL and
  the C++, or those whose description has WORDS, each built arm64-only in a temporary tree
  and run against its check under a 300 s perl alarm).
- No dead controls: `python3 tools/sweep.py --build build` (`--bare` drops the per-control
  contexts: six go dead, listed in AGENTS.md, and verify requires exactly those).
- The demo clips: `python3 tools/clips.py --build build [--pop] [--native] [--alpha]
  [--check]` (needs numpy and ffmpeg, and Resolume's media folder).
- `python3 tools/cmf_header.py [--check]` regenerates (or checks) `source/Cmf.h` from
  `tools/data/ciexyz31_1.csv`, whose sha256 it checks first.

## Notes
- **Units.** Time in seconds, Pop in millimetres (0–1, linear), the blur in output pixels:
  σ_px = Pop·|1 − h|/(4·2.8·36 mm)·W. All host parameters are 0..1 and mapped in
  `source/Controls.cpp` (Time is 16·p² s); options hold the element index.
- **State persists** on the CPU in double: both lamps, the dissolve's position, the
  shutters' cycles, the slides' heat. A resize keeps it. The GPU holds only the two blur
  buffers (RGBA32F at the output's size each, made on first use).
- **Only time differences** of the host's clock are used, in double, clamped to 0..0.25 s; the
  unit (ms or s) is voted by watching it against the wall clock.
- **Index 0 is sacrificial**: Arena hides a mixer's first parameter, so it is Linear Light
  (on). Arena binds a parameter named `Opacity` to the layer fader: the dissolve fader.
- **Buttons act on the rising edge**; the release is ignored. `Change A/B` during a cycle is
  ignored.
- GLSL reserved words must not be identifiers; `verify.sh` greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **macOS make compares whole-second mtimes.** mutate.sh deletes the touched objects and the
  binary before every build; never hand-mutate a source file in a shared build directory.
- `catest`'s `fmt()` takes doubles only, at most four: never pass it a `%s` or `%d` format.
- **No GitHub workflows** (the brief). If the repo goes to GitHub as a private repo, build
  and release locally (the fleet's Actions quota).
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide yet)
  and `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend,
  `sync-about.py` and `sync-attributions.py` generate them; edit them there then.
- Local only: `~/dev/carousel`, no remote, nothing released, not on the website.

## Not done yet
- Never loaded in Resolume (either platform); never built on Windows.
- No user guide, no OpenFX port, no browser demo, no presets, no still mode.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/carousel/carousel.YYYY-MM-DD.log
    %LOCALAPPDATA%\carousel\logs\carousel.YYYY-MM-DD.log

`CAROUSEL_LOG_DIR` overrides the folder. It logs the host, its version and the bundle's
path, the GL vendor and renderer, the first frame's sizes, each change of Opacity (up to 400
lines an instance: the only evidence a host gives of what drives a mixer's fader), every
guard that refused a frame (once each) and DeInitGL.
