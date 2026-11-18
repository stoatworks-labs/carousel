# AGENTS.md: Carousel

Why the code is the way it is. `CLAUDE.md` is the command reference; the design is
`~/Projects/resolume/specs/SPEC-carousel.md`, whose "As built" section lists where this repo
departs from it. Built 2026-11-17 from the spec in `~/dev/carousel`; public as `stoatworks-labs/carousel` from
2026-11-18.

Status, 2026-11-17: v0.1.0, unreleased, **never loaded into Resolume**, never built on
Windows. Everything below that says "measured" was measured by `catest` on this Mac (Apple
M4 Max, macOS 26.4.1), on its GPU and on Apple's software renderer.

## The idea

**Each projector's light is its lamp's filament temperature, and the filament follows the
dissolve unit's voltage through its thermal lag; the slide is a transparency in that light,
behind a shutter, going out of focus as it heats.**

Dest (`inputTextures[0]`, the layer below) is projector A's slide; Src (`[1]`, this layer)
is projector B's. Per frame, on the CPU in double (`source/Model.cpp`):

1. **The dissolve unit.** Its position s (0 = A, 1 = B) follows the target no faster than
   one full travel per `Time` seconds; `Time` 0 cuts. The target is `Opacity`, or
   1 − `Opacity` after an odd number of `Take` presses (relay's XOR). The curve maps s to
   each lamp's drive u = V/V_rated: **Voltage** u_A = max(u_pre, 1 − s), u_B = max(u_pre, s);
   **Light** u = L⁻¹(s) for B and L⁻¹(1 − s) for A, L the steady light at u relative to
   rated, from a 4097-point table built once per process. `Lamp = Low` scales both by
   71.4/82 (SM4530-1's 3.0 Ω LO resistor, 10.6 V).
2. **The lamps** (`source/Lamp.*`): filament's ODE, in units of the rated point, for a 300 W
   82 V EXR at 3350 K. Fixed RK4 substeps of at most 0.25 ms (`lamp::kMaxStep`), cut at every
   point where the drive changes slope (the dissolve reaching its target, each shutter edge,
   the slide drop); the drive is evaluated at each stage's own time. The dark lamp is held at
   the drive that keeps it at 700 K (US 3,282,155's "minimum voltage … without
   incandescence").
3. **The shutter**: `Change A/B` start a 1 s cycle: closed linearly over 0.04 s, the new slide
   drops at 0.5 s (its heat set to 0), open linearly over the last 0.06 s. A press during a
   cycle is ignored. A press takes effect at the first frame that sees it.
4. **The slide's heat** h: dh/dt = (q − h)/3 s, q = shutter × (T⁴ − T₀⁴)/(T_full⁴ − T₀⁴),
   T_full the filament at this Lamp setting's full voltage. The film sits Pop × |1 − h| from
   focus; σ_px = Pop·|1 − h|/(4 · 2.8 · 36 mm) × W, the disc's second moment through the
   102 mm f/2.8 lens with the slide's 36 mm across the output's width.
5. **Colour**: Planck × the CIE 1931 2° CMFs summed in double at 1 nm (471 rows of
   `source/Cmf.h`, generated from CVRL's CSV and checked by sha256), the IEC 61966-2-1 matrix,
   each channel divided by the same lamp's at rated. gain_c = shutter × that. A gain below
   2⁻²⁴ is 0, within 2⁻²⁴ of 1 is 1.

Host time: only the difference between consecutive readings, in double, clamped to 0 … 0.25 s
(`Clock`, relay's unit vote for ms or s). The first frame starts at rest: both lamps at their
steady temperature for the fader, the lit slide warmed through.

**The GPU** (`source/Shaders.cpp`) gets per projector a gain per channel, a radius and the
normalised half-kernel (CPU-computed weights, at most 48 taps each side), and draws:

- **Transmittance** τ = decode(rgb × a) per texel (premultiplied, composited over black,
  then decoded), read with `texelFetch` at a position clamped to the input's **used** size:
  the padding is never read. An input not at the output's size is resampled by a bilinear
  filter built from four such texels, never the hardware's filter on straight colour.
- **Blur**, separable: a horizontal pass per blurred projector into an RGBA32F buffer at the
  output's size (`BlurBuffer`, made the first time a slide is out of focus, kept until
  `DeInitGL`), the vertical pass in the screen shader. Beyond the frame is the mount: black.
- **The screen**: encode(clamp(g_A τ_A + g_B τ_B, 0, 1)), **alpha 1**. With one gain exactly
  1, the other exactly 0, no blur and the input at the output's size, an exact branch outputs
  that texel's rgb × a: a branch and a fetch.

## Decisions taken without asking (the brief says decide and write it down)

- **The drive, as the brief decided it**: the fader (`Opacity`) is the target, `Time` the
  rate, `Take` the button. Arena's layer transition ramps `Opacity`, so Arena's own
  transition drives the dissolve and `Time` limits how fast the unit follows it.
- **Pop defaults to 0.55 mm, measured** by `tools/clips.py --pop` through `catest --pipe`,
  the spec's rule: the smallest Pop, in 0.05 mm steps, at which each of the 15 Shop74 clips'
  middle frames, lanczos-upscaled from 1280×720 to 1920×1080, loses at least half its mean
  absolute horizontal gradient on the frame the shutter is fully open after a change (frame 11
  at 10 fps, the press at frame 1), against the same frame glass-mounted. Twelve clips answer
  (0.30–0.80 mm); three (Ethnik2_23, Metalive 01, Trinity_09) never lose half by 1 mm and
  **count as above the range**, since the rule is the median over the clips. Median of 15:
  **0.55 mm**. Counting only the twelve would give 0.50. At the clips' native 1280×720 the
  same rule gives 0.70 mm (five above 1 mm); the spec names 1080p. `clips.py --check` (in
  verify) fails if the declared default moves off the measurement.
- **Every other default is sourced or chosen**, not measurable from the clips: Time 2 s
  (Rollei Twin MSC 300P's default; US 3,282,155's slow dissolve), Curve Voltage (the sag and
  the amber middle are the idea), Lamp High, Mounts Card, Opacity 1.
- **`--unchanged`'s float target is compared to the rasteriser's own unorm8 decode.** The
  plugin's exact branch multiplies its fetch by its alpha and nothing else; the fetch's
  conversion of a byte to a float is the rasteriser's. GL asks for c/255; Apple's GPU returns
  it correctly rounded (0 of 256 values differ), Apple's software renderer does not (**126 of
  256 values 1 ULP off**). `unormDecode()` probes the table with a pass-through shader and the
  check is bitwise against it. The 8-bit target is bitwise against c/255 on both.
- **`--pop` covers both projectors.** The spec's `--pop` named only B. The first mutation run
  showed A's blur and sharp paths swapped in the screen shader passed every check (`--lamp`'s
  split-card bands are flat, and a blurred flat band is the same flat band), so `--pop` runs A
  (Opacity 0, Change A) and B, Card and Glass.
- **A blurred edge's plateaus must be the unblurred level.** The horizontal kernel's last tap
  dropped moved the measured spread by about 1e-3 px against a 2e-3 px bound and survived.
  Every `--pop` and `--resize` drift frame now also requires every middle-row pixel more than
  R + 1 from the edge to be g·τ to the output's bound: a kernel that has lost mass moves them
  all. New negative control `kFaultKernelMass` (the outermost taps zeroed after normalising).
- **Inputs are taken as straight alpha** (penumbra's convention); Dest's real form in Arena
  is not measured. See open questions.
- **Index 0 is Linear Light, on.** Arena hides a mixer's first parameter (measured on genlock,
  wipe, relay, lenticular and pepper), so it is a control whose default is right for ever:
  two projectors' light adds in linear light.
- **The lamp's comparisons with the sources are printed, not asserted** (the spec's
  decision): the sources disagree with each other by more than the model disagrees with any
  of them. `--lamp` prints heat 700 K → 90% in 0.4163 s and cool to 10% in 0.1684 s beside
  Levi's 0.372 and 0.174 s; `--colour` prints CCT ∝ V^0.420 beside 0.376 and 0.4.
- **GitHub workflows** came with the release (2026-11-18): penumbra's `ci.yml` and
  `release.yml`, adapted, with its `tools/wincheck.ps1`, which loads the DLL as a host would.
  The repo is public, so they run on Actions.

## The traps actually hit

- **`fmt()` takes doubles only.** Passing it a `%s` is undefined behaviour; messages are
  concatenated outside it. It also takes at most four values (`--resize`'s level line didn't
  compile with five).
- **A negative control nobody can see.** SizeSwap (B read at A's size) went uncaught while B
  was always blurred at the Light midpoint, because SizeSwap only touches the unblurred path.
  `--mixer` now runs Glass for frames 0–29, then Card with Change B.
- **A sentinel-distance test was too weak** for the padding; it is a bitwise comparison of
  two renders, magenta padding against green.
- **A self-consistent mutant.** GainA-for-GainB passed `--linear`, which compares the picture
  with its own measured gains; the mutation check now uses a steady check against the model
  at Opacity 0.35.
- **Reading the wrong frame's expectation.** `--change`'s cue callback read the model before
  the frame advanced; an after-callback does it now.
- **A spread about the mean is not exactly 0** for a sharp edge; moments about the step's
  own position (p₀ = W/2 − 0.5) are.
- **Operator precedence** in the τ fit's frame filter; and a lag printed `%.4f` showed
  0.0000 (now `%.3g`).
- **Stepped `--script` cues fired before their first key**: `valueAt` returned the first
  key's value at frame 0, so a Take keyed at frame 40 pressed at frame 0. A stepped track is
  now NaN before its first key and its callers skip it.
- **The harness wrote its logs to `~/Library/Logs/carousel`.** Every harness run instantiates
  the real plugin, which logs. `CAROUSEL_LOG_DIR` is set by verify, mutate, sweep and clips;
  set it yourself for hand runs.
- **The software renderer's unorm decode** (above): `--unchanged`'s float target failed
  263,967 values at 640×360, all 1 ULP.
- **The first mutation run had two survivors** (above), both real holes in the checks, not
  in the mutants. A mutant listed against the wrong check is a hole too: A's blur swap was
  listed against `--lamp`.
- **Rebuilding a binary under a running check.** The long software-renderer run uses a copy
  of `catest`; rebuilding `build-arm64/catest` under it risks the run.
- **zsh has no PIPESTATUS**: an exit status read with it printed nothing. verify.sh, mutate.sh
  and glslc.sh are bash.
- **`grep -E` has no lookahead**: `(?!` is "repetition-operator operand invalid".
- **Importing `tools/clips.py` from Python left `tools/__pycache__`**; it is gitignored.
- **macOS make compares whole-second mtimes**: mutate.sh deletes every touched object and the
  binary before each mutant, and requires a GLSL mutant's text in the binary.
- **oxbow is an effect host**: it hands a mixer one input, so its `selftest` must fail on
  frame 0, and verify requires exactly "FF_PROCESS_OPENGL failed on frame 0".
- **verify.sh's cue check forgot the lag it sits next to.** It wanted Opacity keyed at
  frame 1 to dim A at frame 1 and Lamp keyed at frame 4 to show at 4; the picture showed
  each one frame later, which is the plugin's design (the lamps integrate to frame N's
  instant with the drive as it was; `--cut` holds the frame at the cut bitwise to the one
  before). The check now wants frames 0 and 1 bitwise equal then dimming from 2, and
  frames 0-4 bitwise equal then a step at 5: a cue applied a frame early or late fails it.
  Shifting the keys by hand (the step to 3 or 5, the ramp to 1..9, Opacity held to 8) is
  refused each time; that was run once by hand, not in verify.
- **The cue parser skipped what it couldn't read.** A line whose first word wasn't a whole
  number was dropped as if blank, so `Lamp 1` (no frame) ran with exit 0, as did a negative
  frame and a NaN or infinite value. Found while building that negative control (a zsh loop
  that didn't word-split wrote frameless cues, and catest took them). Only blank and comment
  lines are skipped now; verify feeds seven malformed lines, each exit 2, and a sheet of
  comments and blank lines, exit 0. The old binary took four of the seven.

## Verified, and assumed

Verified on this Mac's GPU and on Apple's software renderer, at 640×360 and 320×180, by
`catest` (the README's table has the numbers): each lamp's gain frame by frame against the
harness's own ODE; the colour temperature from the picture; the sag, the warm middle, the
Light curve's flat sum and the ramp's length; the cut's frame bitwise and its lag; the
shutter's cycle and dark gap; the blur's spread, the kernel's mass and τ from the picture;
alpha through resampling and blur; linear and gamma sums; resize, frame rate and clock
origin; the guards, the padding and the host's GL state; the About block and names.

Assumed, and said so in the README:

- **The lamp model** is filament's (sized for an EXR): grey-body Planck, Forsythe & Worthing
  resistivity, NIST-JANAF c_p, 15% conduction, emissivity 0.30. Its exponents fall inside the
  sources' spread (printed); from the picture it heats 12% slower and cools 3% faster than
  Levi's rule of thumb (printed, not asserted).
- **The chosen numbers**: shutter 0.04 / 0.5 / 0.06 s, τ = 3 s, preheat 700 K. No source
  gives them.
- **The pop is uniform across the frame**; no bow profile, no snap-through, no autofocus.
- **Inputs are straight alpha.**
- **Arena's behaviour** with mixers (index 0 hidden, `Opacity` on the fader, Dest and Src)
  is measured on genlock, wipe, relay, lenticular and pepper, not on this plugin.
- **A backwards host clock** gives dt = 0 (`Clock::Tick` returns at least 0): read in the
  code, never exercised by a check.

## Tolerances, and where they come from

Every numeric tolerance is three times a stated bound. The model side of every gain is the
plugin's **measured** method error: the harness integrates its own SI filament by
Dormand–Prince at 1e-11 (`ref::Sim`) and again by RK4 at the plugin's step with the plugin's
4097-point Light table (`rk4Method`); the difference, at that frame, is the plugin's method
error.

| quantity | bound | from |
| --- | --- | --- |
| the sRGB pair (`kPowBound`) | 4e-6 absolute on the encoded value | GLSL 4.10 §4.5.1: `+ − ×` correctly rounded, `/` 2.5 ULP, `pow` as `exp2(y·log2 x)`, exp2 (3 + 2\|x\|) ULP, log2 3 ULP (2⁻²¹ absolute in [0.5, 2]); summed worst cases on a grid: decode 5.1e-6 relative at its knee, encode 6.8e-7 absolute, encode(decode(·)·L) 1.9e-6 (penumbra's derivation, the same pair) |
| float32 (`kFloatUlp`) | 2⁻²⁴ relative | IEEE 754 single |
| a stored output (`outputBound`) | kPowBound + encode′(L)·L·(transmittanceUlps(R) + 3)·2⁻²⁴ | the bilinear resample (two mixes, 2 ULP each); when blurred, two passes of 2R + 1 multiply-adds (2R + 1 ULP, the products' 1) and the weights' rounding to float (1 ULP a pass); the gain's multiply and the sum (3 ULP) |
| a gain read from a band (`readBound`) | 3 × (outputBound carried back through decode′, over the slide, + 2·2⁻²⁴·max(g, 1)) | the output's bound; the gain's rounding to a float uniform; the 2⁻²⁴ snap |
| the model's gain (`modelBound`) | 3·\|DP − RK4\| + 3·(4.4e-10 + 1.1e-13) | the measured method error; two double implementations drifting apart (4 ULP of 4000 K a step, 4000 steps a second, 20 s: 1.5e-7 K; a gain moves at most 3e-3 of itself per kelvin) and two 471-term double sums |
| a luminance (`luminanceBound`) | Σ BT.709 weight × (readBound + modelBound) per channel | the channels' bounds, linearly |
| a colour temperature | Σ_c \|CCT(g + δg_c) − CCT(g)\| + 3·\|CCT(model gains) − T\| | each channel's gain bound carried through the locus inversion by finite difference, plus the inversion's own error measured on the model's gains |
| an edge's spread (`spreadBound`) | 3 × the variance's bound over 2σ | each line-spread difference off by twice the per-pixel bound over the step; only the 2R + 3 around the edge are not exactly 0; carried through the moments, plus the weights' rounding (2(2R + 1) ULP of the variance); plus 3 × the measured method error of σ |
| τ from the picture | τ²·Σ\|tᵢ − t̄\|eᵢ / Σ(tᵢ − t̄)² | least squares on ln σ; each point's spread bound carried through σ(spread) and the log, then into the slope by linear propagation |
| the plateaus of a blurred edge | 3 × (outputBound + encode′·τ·modelBound) | the same output bound, per pixel |
| a resampled pixel (`--mixer`) | 3 × (outputBound + encode′·(2·(hi − lo)·δq + 3·2⁻²⁴·hi·2.4)) | the four texels' spread times the position's error per axis, and the harness's double against the GPU's float rgb × a |
| bitwise claims | 0 | a branch and a fetch, or the same uniforms twice: `--unchanged`, the cut's frame, `--alpha`'s painted-under-transparent renders, `--mixer`'s padding, `--resize`'s equal frames |

**The Light table.** The plugin inverts the steady light curve by linear interpolation in a
4097-point table; the harness's RK4 reference uses the same table and its Dormand–Prince
reference a 20001-point one, so the table's interpolation error is inside the measured method
error. At the Light midpoint the two lamps' steady light sums to 1.0000002 of one lamp's
(|1 − sum| 2.4e-7 against 6.1e-5).

## Would this hold on another rasteriser, at another raster?

Every picture check runs at 640×360 and 320×180 on this Mac's GPU and, in verify, again on
Apple's software renderer (all fourteen GL checks; the software pass takes most of verify's
time).

- **Nothing spectral, thermal or temporal is on the GPU.** Gains, σ, radii and weights are
  computed on the CPU in double and uploaded as floats (1 ULP each, inside `readBound`). The
  only transcendental in GLSL is the sRGB pair's `pow`, bound from GLSL 4.10 §4.5.1, not from
  this GPU.
- **Inputs are read with `texelFetch` at integer positions** from `floor(gl_FragCoord.xy)`,
  clamped to the used size in integers: the padding claim holds on any conforming rasteriser.
- **The resample's weights** come from a float pixel centre; their error is a stated number of
  ULPs of the coordinate (δq), and the tolerance carries it.
- **The blur's float error scales with R**, which scales with the raster: `outputBound` takes
  R, so a bigger raster's longer kernels get proportionally looser bounds automatically.
- **Bitwise claims rest on a branch and a fetch**, on identical uniforms, or on
  0 × anything = 0 for a transparent texel (8-bit inputs cannot be NaN or infinite). The one
  place a rasteriser legitimately differs, unorm8 → float, is probed rather than assumed
  (`unormDecode`).
- **Widths refuse to pass on nothing**: each fit requires a minimum count of measured frames
  (`measured > 100`, `fit.size() >= 20`, both crossings found) at both rasters.
- **Timing faults are not hidden by frame boundaries**: the clock starts at 1.2345678 s;
  the shutter's ramps start 0.04 and 0.94 s into the cycle, not whole numbers of frames at 24,
  60 or 144 fps. The slide drop (0.5 s, inside the dark gap, where it changes nothing visible)
  and the cycle's end (1.0 s) are, which the spec missed; `--change` runs at 59.94 fps, where
  none of the four is.
- Not run: llvmpipe, other GPUs, Windows drivers.

## The negative controls and mutants

`SetFaultForTest` builds the wrong answer into the real plugin: 19 faults (`Model.h`'s
`Fault`), run against the checks they should fail in 22 pairings, plus `--mutation`'s
mutated shader. Each is caught at both rasters (counts are assertions failed at 640×360):

| check | the wrong answer | failed |
| --- | --- | --- |
| mixer | a texture left bound | 1 |
| mixer | B read at A's size | 3 |
| unchanged | raw Planck sRGB, not white-balanced | 8 |
| lamp | resistance fixed at its hot value (no inrush) | 1 |
| lamp | radiation linear in T, not T⁴ | 1 |
| lamp, cut | the filament at its steady T instantly | 1, 6 |
| colour | the colour of the rated lamp at every T | 2 |
| colour | raw Planck sRGB, not white-balanced | 1 |
| dissolve | the Light curve driving voltage | 1 |
| dissolve, rates | the dissolve advanced per frame, not per second | 3, 3 |
| cut | Time 0 as a one-frame ramp | 2 |
| change | the cycle timed in 60 frames, not 1 s | 1 |
| pop | the slide's heat kept across a change | 4 |
| pop | the drift's time constant 1 s | 4 |
| pop | the Gaussian's outermost taps zeroed | 4 |
| alpha | resampling and blur on straight colour | 5 |
| linear | Linear Light's sum on code values | 1 |
| resize | a resize restarts the projectors at rest | 2 |
| rates | the lamps stepped 1/60 s per frame, whatever the frame | 3 |
| clock | the host's readings held in floats | 6 |
| mutation | one character of the screen shader through the shader hook | 1 |

`tools/mutate.sh`: **35** one-character mutants, each in a temporary tree, arm64, under a
300 s perl alarm (a timeout counts as caught), each run against the one check that should
catch it. 17 are GLSL: decode's gamma 2.5; encode's 1/2.2; alpha added not multiplied; one
texel of padding readable; the fetch path for the other size; the resample a tenth of a pixel
off; the horizontal kernel's last tap dropped; B's and A's vertical blur read from the wrong
column; Linear Light's encode inverted; B's exact path showing A's slide; A's exact path
inverted; B read at A's size; A's blur and sharp swapped; B weighted by A's gain; decode
inverted on Linear; premultiplied twice. 18 are C++: the slide's heat running away; the
shutter never closing; a warm slide from the tray; the dissolve twice as fast as Time; A's
drive rising with B's; the blur in output widths, not pixels; Time 0 no longer cutting;
radiation from the room added; power rising with resistance; Shomate's last term inverted;
blue white-balanced against green; Planck's λ⁵ broken; a millisecond host read as 2 ms; the
unit divided; Time's mapping unsquared; the Gaussian unnormalised; the Gaussian narrower than
σ; B's exact path never taken. The first full run caught 33 of 35; both survivors were holes
in the checks (above), fixed by strengthening `--pop`.

## The sweep's honest list

`python3 tools/sweep.py` moves all 10 controls with pixels (the About block's four have none)
from one end to the other at 320×180 in the context that should show each, and finds every
one live, none barely. `--bare` drops the contexts and runs the defaults (Opacity 1: B lit at
rest, A dark), and six go dead, each for the reason its context exists:

- **Linear Light**: one projector alone at rated takes the exact branch, the same either way.
- **Time** and **Curve**: no dissolve is running.
- **Mounts** and **Pop**: no slide has changed, and the lit slide is warmed through.
- **Change A**: A is dark at Opacity 1; a change on the dark projector is invisible, which is
  how a two-projector show is run.

verify requires exactly those six in `--bare`, so a default that moves shows up.

## Demo clips

`tools/clips.py` reads each clip's middle frame with `ffmpeg -nostdin` from
`/Applications/Resolume Arena/media/Shop74` (15 clips, DXV, 1280×720; the AV set's are
audio-reactive abstracts and were left out). `--pop` is above. `--alpha`: the two clips with
transparency, BattleWeapon_Tank_09 (561,770 transparent and 7,614 partly transparent pixels
of 921,600) and Ethnik2 (468,985 and 4,217), as projector B at 1280×720 resampled to
1920×1080 and blurred (Pop 1, Lamp Low, frame 11 after a change): white painted under every
transparent pixel renders the **same bytes**; 200 opaque pixels painted white do show.

## Render cost

`catest --bench`: wall-clock with `glFinish` either side, the mean of 30 frames after 60 of
warm-up, both inputs at the output's size, 8-bit. **One lit** is one projector at rated, in
focus (the exact branch); **both lit** the Light midpoint, glass (two fetches, decoded,
added); **both blurred** both lit and both slides just changed at Pop 1 mm, the worst case.
Figures are in the README, from verify's run. 4K both blurred is the one that costs: at Pop
1 mm a freshly changed 4K slide's σ is up to 9.5 px by the formula, R = 39 taps each side,
two passes, two projectors: 7.9 ms in the final verify run (47% of a 60 fps frame), 8.1 ms
in the one before. Not looked into further: a cheaper blur (a downsampled pass, or a
box-cascade) is an open question.

**Blur memory**, by arithmetic, not measured: each projector's buffer is RGBA32F at the
output's size, 16 bytes a pixel: 14.7 MB at 720p, 33.2 MB at 1080p, 132.7 MB at 4K, two of
them once both slides have been out of focus. Made on first use, kept until `DeInitGL`.

## The browser demo (2026-11-18)

https://carousel-demo.stoatworks-labs.com/, from `demo/`, deployed by `wrangler.toml` (a Worker
route: the zone is at Cloudflare's 100 custom domains, so `carousel-demo` needs a proxied AAAA
`100::` record made through the API before the first deploy) and
`.github/workflows/deploy.yml` on every push to main. The fleet's first WebAssembly mixer.

- **It runs the plugin, it doesn't port it.** `demo/carousel-core.wasm` is `Carousel.cpp` and
  everything it calls (Controls, Lamp, Colour with `Cmf.h`, Clock, Model, BlurBuffer, Diag,
  Shaders) plus the FFGL SDK's plugin classes, CFFGLPluginInfo, FFGLLog, FFGLShader,
  FFGLScreenQuad and the scoped bindings, compiled unmodified by emscripten 6.0.5
  (`demo/tools/build-wasm.sh`, nipkow's and fountain's pattern). Left out: `MixerEntry.cpp`
  (the build stamp). The lamps' ODE, the dissolve, the shutters, the slides' heat, the Planck
  sum and the Gaussian's weights are that C++, in double; nothing of the CPU half is ported.
- **The CIE table is in the .wasm.** `Cmf.h` (CIE 1931 2°, © CIE, CC BY-SA 4.0) is compiled
  in, so the deployed page distributes it. The page's disclosure says so in its own item,
  beside the attribution; the README's demo section points at License.
- **What is the page's**: `demo/wasm/glue.cpp` plays the host as catest's MixRig does (two
  inputs, Dest = inputTextures[0]; FF_RESIZE when the composition changes; the declarations
  read back through the SDK's host getters; `ca_convert` / `ca_param_from` are Controls.cpp's
  conversions for the read-outs and the presets; `ca_instant` is `InstantForTest()` and
  `ClockForTest().Scale()` for the line under the picture; `ca_clock_scale_for_test` is for
  the crosscheck only). `demo/wasm/gl_shim.cpp` takes four calls: glShaderSource (the check
  and the port) and glEnable, glDisable and glIsEnabled for GL_PROGRAM_POINT_SIZE, which
  WebGL2 lacks. Nipkow's glTexImage2D and glReadPixels shims aren't needed: BlurBuffer's
  `GL_RGBA32F, GL_RGBA, GL_FLOAT, NULL` is already the ES pair, and nothing is read back. The
  blur buffers need EXT_color_buffer_float; without it the page stops and says why.
- **The ES port** is the kit's port(), checked at load and in verify
  (`demo/tools/check_shaders.py`): the version line (and the blank line after it) and the
  highp defaults, nothing else. Carousel's GLSL has no `precise` and no `float[](`.
- **Two inputs, both the page's.** Slide A is the kit's clip picker renamed (its "Use my
  own…" also sets the composition to the picture's size); Slide B is a second
  `SourceRenderer` with its own file button, kept at its own size so the plugin resamples it.
  Inputs are unpadded (Width = HardwareWidth): the `--mixer` padding path is never exercised.
  The kit's clips are premultiplied and the plugin takes straight alpha: every clip offered is
  opaque (the kit's `alpha` clip is left off), and a picture of your own with transparency
  comes out darker at its soft edges (rgb × a²). The page says all of it.
- **The tray ("Next slide on Change") is the page's.** A Change press arms it when the
  plugin's last instant says that projector isn't cycling (a press the plugin will ignore arms
  nothing); it moves that projector's picture to the next on the page's list on the first
  frame the plugin's shutter reads 0, and disarms if the cycle ends first. A picture of your
  own stays put. `?tray=0` starts with it off.
- **Buttons** send 1 then 0 in one go (fountain's). The plugin latches the rising edge in
  SetFloatParameter, so a press and release between two frames still lands on the next frame.
- **The clock** is the kit's seconds, capped at 0.1 s a frame. The plugin's vote settles
  seconds within four frames (the line under the picture says which). Pause gives dt 0: the
  projectors freeze, and a press waits for Play. Restart is a backward jump, which the clock
  turns into dt 0: the projectors carry on, they are not reset.
- **Trap: the log opens in the constructor** (`diag::init()` in `Carousel()`), and the page
  constructs an instance to read the declarations, so `ca_prepare_log()` (which sets
  `CAROUSEL_LOG_DIR` in the page's memory file system) has to run before that. Run later, the
  log quietly lands at `/home/web_user/.local/state/carousel/logs`.
- **Not reproducible to the byte**: Diag.cpp's first line carries `__DATE__ __TIME__`, so each
  build-wasm.sh run writes a different .wasm. The manifest records whichever was built.
- **Flash safety, the page's side.** Carousel doesn't flash on its own: a Change on the lit
  projector is one dark gap (about 0.9 s of its 1 s cycle) and a press mid-cycle is ignored,
  so at most one dark gap a second a projector, two alternating A and B. Cutting does: Take or
  the fader at Time 0–0.1 s faster than about five cuts a second crosses three flashes a
  second (white against black at Time 0: 5 cuts/s is 3 flashes/s, 6 is 3.5, 10 is 5.5, 30 is
  15; Resolume's NoHopeJustFear_44 against Cyberspace_09 at Time 0 crosses at 6 cuts/s); at
  Time 0.25 s or more even 10 presses a second stayed under (the release brief's figures,
  measured on the plugin's output, 2026-11-18). So: a short note at the top of the page (no
  Start gate: nothing flashes unless a visitor makes it), a caution line under the picture
  whenever Time is under 0.25 s, and **nothing on the page presses Take or Change by itself**
  (no URL parameter presses one either). The defaults stay the plugin's.
- **Checked on the page** (headless Chrome, GPU through ANGLE on Metal, 960×540, no console
  errors or warnings): all four stage compilations accepted; B lit at load, A dark at 700 K;
  Opacity 1 → 0 a 2.1 s Voltage dissolve; held at 0.5, Voltage, each lamp at 2475 K and 0.064
  of its light (held still, so not the moving dissolve's 0.1139), and Light, each at u 0.818,
  3077 K, the two summing to 1.000 of one lamp; Lamp Low 3160 K; Take dissolving over Time;
  Change B on the lit projector at Pop 1 mm: shut from about 0.1 s to 0.95 s with the canvas
  pure black, then the next picture (the tray) open at σ 2.38 px, 1.67 px at 2 s, 0.61 px at
  5 s, in focus by 9 s; a second Change A mid-cycle ignored; the caution under 0.25 s.
- **Against catest** (`demo/tools/crosscheck/crosscheck.py --catest <build>/catest`, needs
  Chrome and a GPU, not in verify): the page's table of controls equals `catest --list`, and
  480 frames at 320×180 (Src 240×135 with a band at alpha 128, Pop 1 mm; a Voltage dissolve, a
  change on the lit projector and an ignored second press, a Light dissolve, a cut at Time 0,
  Lamp Low, Linear Light off, a change on the dark projector, the fader at Time 0) came out
  **identical to the byte**, every frame (M4 Max, Chrome's WebGL2 on Metal against GL 4.1,
  18 November 2026). Negative controls caught: Change A a frame late (worst 113 levels), Time
  one slider step off (worst 3 levels, 3.3% of pixels identical). Other browsers, GPUs and
  compositions were not compared.
- **Rebuild after any change under `source/`**, the generated `StoatworksAbout*.h` and
  `Cmf.h` included: `demo/tools/build-wasm.sh`, then commit both outputs and
  `demo/wasm/inputs.sha256`; verify's "browser demo" step fails until you do.

## Open questions

- **Still mode.** The gate shows the input live. Holding the frame at each slide change
  (what a slide is) would need a frame store per projector and a decision about when it
  latches.
- **Lamp types.** Only the EXR; FHS, ELH and EXY differ in a few percent of lag and colour.
- **Is Dest premultiplied in Arena?** Taken as straight. If Arena hands Dest premultiplied,
  τ = decode(rgb × a) squares the alpha on Dest's soft edges.
- **The default Curve.** Voltage, for the sag and the amber middle; Light is the
  period-correct shaped unit. Allan's call.
- **The chosen shutter, τ and preheat** (0.04 / 0.5 / 0.06 s, 3 s, 700 K): no source.
- **Autofocus.** SM4530-1's autofocus settles "within 1 second" on the cold slide; this is a
  manually focused projector.
- **A dimmed lamp's slide stays out of focus.** At the Light midpoint each lamp runs at
  u = 0.818, 3077 K from the picture, so by the model's formula its slide's heat settles at
  q = (T⁴ − T₀⁴)/(T_full⁴ − T₀⁴) = 0.712 and both slides hold a blur of
  Pop·0.288/(4·2.8·36)·W, 0.75 px at 1080p at the default Pop, for as long as the dissolve is
  held there (more at the Voltage midpoint, where the lamps are cooler). Consistent with a lens
  focused on a fully warmed slide; an operator would refocus.
- **NoPremultiply is invisible at the output's size, in focus**: a fetch then a multiply is
  the same in either order. It shows only through resampling and blur, which is where
  `--alpha`'s negative fails (5 assertions).
- **4K both blurred** at Pop 1 mm costs a large share of a 60 fps frame (README); RGBA16F
  blur buffers or a lower tap cap would cut it and the memory; not evaluated against the
  tolerances.
- **Never in Resolume, never on Windows.**

## Generated files

Carousel was registered with the fleet on 2026-11-18. `source/StoatworksAbout.h` is
`sync-about.py`'s (stoatworks-backend), from the website's projects.json: with a guide
listed it carries the guide's URL, and the About block grows a User guide button, so the
plugin declares 15 parameters (wincheck in both workflows expects 15).
`StoatworksAboutLinks.h` and `StoatworksAboutParams.h` are the fleet's, unchanged.
`ATTRIBUTIONS.md` is `sync-attributions.py`'s, from `attributions/derived.json`, which keeps
the CIE colour-matching functions under CC BY-SA 4.0. `.github/FUNDING.yml` and the issue
forms (the plugin variant, asking for the log at Diag.cpp's paths) are the fleet's too. Edit
any of them in stoatworks-backend, not here.
