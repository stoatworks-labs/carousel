# carousel

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has been loaded into **Resolume
> Arena 7.29.0 on a Mac and 7.27.1 on Windows**, over test stills only, where it registers as
> a mixer, takes its dissolve from the layer's fader and draws the lamps the harness predicts
> to the code value. It hasn't been through a show.
> Everything else below is measured by an offline harness, `catest`, that drives the real plugin class in a headless
> GL context with two inputs, at two rasters, on this Mac's GPU and again on Apple's software
> renderer, and reads the light straight off the plugin's float output, against the harness's
> **own** model of the lamps: its own filament ODE integrated by Dormand–Prince, its own
> Planck sum read from the CIE table at run time. Every frame of a cut and a dissolve matches
> it within a derived tolerance; the closest frame is **2.4e-7** off in gain against 5.5e-6.
> The colour temperature read back out of the picture is the filament's to **0.0005 K**. A
> voltage dissolve's middle sags to **0.1139** of one lamp's light at **2457 K**; a cut at
> zero time leaves the frame at the cut **bit for bit** the frame before it. One projector alone at rest gives its slide back with
> **0** values different. **19** deliberately wrong versions of the plugin and **35**
> one-character mutants of its GLSL and C++ are all caught. The lamp model, the shutter's
> timing and the slide's warming time are **assumptions**: see [Status](#status).

Two Kodak slide projectors on a dissolve unit, for [Resolume](https://resolume.com) Arena and
Avenue, as an FFGL **mixer**, **SW Carousel**. The layer below is projector A's slide; the
layer you set it on is projector B's. The layer's fader is the dissolve fader. Each lamp is a
300 W tungsten filament that follows the dissolve unit's voltage through its thermal lag, so a
dissolve sags and turns amber in the middle, and even a cut crosses through a dim, warm moment
while one filament cools and the other heats.
Change A and Change B run a one-second slide change: the shutter's dark gap, then a cold
card-mounted slide drifting into focus as it warms.

> **Photosensitivity.** SW Carousel doesn't flash on its own: a slide change on the lit
> projector is one dark gap, at most once a second. But it cuts as fast as you cut. Cutting
> between contrasting slides more than about **five times a second** with Time at 0 to 0.1 s,
> with Take or the layer's fader, crosses **three flashes a second** over the whole frame, the
> line broadcast and WCAG guidance draw. Flashing like that can trigger seizures in people with
> photosensitive epilepsy. See [Flashing](#flashing).

## The one idea

**Each projector's light is its lamp's filament temperature, and the filament follows the
dissolve unit's voltage through its thermal lag; the slide is a transparency in that light,
behind a shutter, going out of focus as it heats.**

- **The lamp** is a 300 W, 82 V tungsten-halogen projection lamp (ANSI EXR, the lamp Kodak
  lists first for the Ektagraphic III, 3350 K). Its temperature follows the heat balance of
  its filament, C(T) dT/dt = V²/R(T) − radiation − conduction, integrated on the CPU in double
  every frame. Light and colour are Planck's law at that temperature, summed against the CIE
  1931 colour-matching functions.
- **The dissolve unit** sets the two lamps' voltages from one position: a plain rheostat
  drives voltage linearly (US 3,093,030); a shaped one drives each lamp so its light crosses
  linearly (US 3,282,155). The dark lamp is kept warm at 700 K, below glow.
- **The shutter** closes for a slide change: Kodak's cycle is one revolution, 1 s.
- **The slide** heats in the beam and bows ("pops"). The lens was focused on a warm slide, so
  a freshly changed, cold, flat one is out of focus and drifts in as it warms. Glass mounts
  hold the film flat.

## What falls out

None of this is drawn. It's two lamps, a shutter and a piece of film.

- **A voltage dissolve sags and warms in the middle.** Halfway, each lamp has half its
  voltage and the screen falls to 0.1139 of one lamp's light at 2457 K, from 3350 K at either
  end. US 3,282,155 shaped its dissolve "to eliminate reddening".
- **A light-shaped dissolve doesn't sag, but still warms**: at the midpoint both lamps at
  0.818 of their voltage give one lamp's light between them (1.0000002 of it, measured), at
  3077 K.
- **A cut is not a cut.** At zero dissolve time the voltages switch at once, but the filament
  can't: one frame after a cut at 60 fps, the incoming lamp gives 2.09e-5 of its light and the
  outgoing one still 0.798 of its own; the incoming one passes half its light at the 13th
  frame after the cut.
- **A slide change on the lit projector is a dark gap**: 54 frames fully dark at 59.94 fps
  (0.901 s) of the 1 s cycle. On the dark projector, which is how a two-projector show is
  run, it is invisible.
- **Focus drifts after every change** with card mounts: at Pop 1 mm a 640-wide slide opens
  1.57 px out of focus and is sharp (under 0.17 px) 6.7 s after the shutter opens; and
  again, more gently, whenever a projector has been dissolved out long enough for its slide
  to cool.
- **Colour comes from the lamp**, not a tint: the light dims and warms together, and at
  rated voltage the slide comes back exactly as it was.

## Controls

| Group | |
| --- | --- |
| **Screen** | Linear Light (on: the two projectors' light adds in linear light; off adds code values. Arena hides it). |
| **Dissolve** | Opacity (the dissolve fader, 0 = projector A, 1 = projector B; Arena puts the layer's fader on it, so a layer transition is a dissolve), Take (a button: swaps which projector the fader means, so one press dissolves to the other in Time), Time (the dissolve unit's rate, 0–16 s for a full travel; 0 cuts), Curve (Voltage, the plain rheostat; Light, the shaped unit). |
| **Lamps** | Lamp (High; Low, the Ektagraphic's LO position, 87% of the voltage). |
| **Slides** | Mounts (Card, which pop; Glass, which don't), Pop (how far a warm card-mounted slide bows, 0–1 mm), Change A and Change B (buttons: a slide change on that projector). |

**The defaults**: Opacity 1 (projector B), Time 2 s (Rollei's default dissolve, and the
"slow dissolve" of US 3,282,155), Curve Voltage (the sag is the idea), Lamp High, Mounts
Card, Pop **0.55 mm**. Pop's default is measured, not chosen: `tools/clips.py` puts each of
Resolume's 15 Shop74 demo clips through the plugin at 1920×1080 and finds the smallest Pop
that halves a freshly changed slide's detail; 0.55 mm is the median. No published figure for
a slide's pop was found.

**Using it.** Put projector A's clip on a layer and projector B's on the layer above, and set
the upper layer's Blend Mode to SW Carousel. Pull the layer's fader to dissolve, or press
Take. Press Change A or Change B to change a slide; on the dark projector nobody sees the
gap, only the next slide drifting into focus when it's dissolved in. The gate shows its clip
live: a "slide" is whatever the clip is doing.

**Alpha**: each slide is its clip composited over black, so a transparent pixel is opaque
black film, whatever colour sits under it, through resampling and blur alike. The output is
opaque: a screen is lit, not transparent.

## Browser demo

[carousel-demo.stoatworks-labs.com](https://carousel-demo.stoatworks-labs.com/) runs the
plugin in your browser: its own C++ (the plugin class, the two lamps and the dissolve in
double, the shutters, the slides and its clock), compiled unmodified to WebAssembly and
drawing with its own GLSL in WebGL2, with two generated pictures, or pictures of your own,
as projectors A and B. The page is the host: Opacity is a slider rather than the layer's
fader, Take and the changes are buttons, and moving a projector on to its next picture
during a change is the page's, labelled as such. Rendered through the same 480 frames as
`catest`, every frame came out identical to the byte (Chrome on an M4 Max). It needs 32-bit
float render targets (`EXT_color_buffer_float`) and stops without them. The CIE table
compiled into it is CC BY-SA 4.0, not MIT ([License](#license)). It is a demo, not the
plugin, and the page lists what it leaves out.

## Flashing

People with photosensitive epilepsy can have a seizure from flashing light. Broadcast (Ofcom)
and web (WCAG 2.x) guidance draws the line at **three flashes a second** over a large part of
the picture. Measured from the plugin's own output (`catest --pipe` at 60 fps, white and black
slides unless said), with xenon's rule: a transition is a change of at least 0.1 of white in
mean linear luminance, its darker end below 0.8; two opposite transitions are one flash; more
than 6 transitions in any second crosses. Counted over the whole frame and over a 5 × 5 grid
of third-of-frame windows (the two agreed in every case).

| What | Transitions in the worst second | Flashes a second |
| --- | --- | --- |
| Change B on the lit projector, pressed as fast as it will go (a press during a cycle is ignored), card or glass | 2 | 1 |
| Change A and B alternating, both projectors lit (Light, fader 0.5) | 4 | 2 |
| Take at Time 0, 1 / 2 / 3 / 4 presses a second | 2 / 3 / 4 / 5 | under the line |
| Take at Time 0, 5 a second | 6 | 3, on the line |
| Take at Time 0, 6 / 8 / 10 / 15 / 30 a second | 7 / 9 / 11 / 15 / 30 | **3.5 to 15: crosses** |
| Take at Time 0.05 s or 0.1 s, 5 / 6 / 8 / 10 a second | 6 / 7 / 8 / 11 | **crosses from 6 a second** |
| Take at Time 0.25 s or 0.5 s, any rate tried (2 to 10 a second) | 5 at most | under the line |
| Resolume's NoHopeJustFear_44 and Cyberspace_09 as the slides, Take at Time 0, 2 / 3 / 4 / 6 a second | 3 / 4 / 5 / 7 | **crosses at 6 a second** |

So SW Carousel never crosses the line by itself, and the defaults don't (Time 2 s). Cutting
between projectors faster than about five times a second at a Time of 0.1 s or less does,
whether by Take, by the layer's fader, or by anything mapped to them. At Time 0 the fader
drives the lamps directly, so throwing it from end to end is a cut. This was measured on the harness's white
and black slides and two of Resolume's demo clips, not on every clip.

## Status

**v0.1.0, 18 November 2026, and early days.** It has been loaded into Resolume Arena 7.29.0
on a Mac and 7.27.1 on Windows, over test stills only (below). It hasn't been through a show,
and nothing has been timed inside Resolume.
`oxbow probe` reads the bundle as a host does (`SW Carousel` / `CA01` / **mixer**, inputs
2..2, parameter 0 Linear Light, a standard parameter named Opacity), and `oxbow selftest`, an
effect host that hands the plugin one input, is refused on its first frame, as a mixer must
refuse it. There's a [user guide](docs/USER-GUIDE.md) and a [browser demo](#browser-demo),
but no presets, no OpenFX port and no still mode.

### In Resolume Arena

**On a Mac** (Arena 7.29.0, Apple M4 Max, 18 November 2026), over Arena's REST API with the
plugin's own log, two layers added on top of a composition and removed afterwards:

- Arena loads the bundle and registers **SW Carousel** as a mixer (`uid: CA01 category: 2`),
  in the layer's Blend Mode list. It shows 14 of the 15 parameters: everything but Linear
  Light, which Arena hides on every mixer.
- The **layer's fader is the dissolve fader**: set to 0.2, 0.85, 0.5, 1 and 0.3, the
  plugin's Opacity read back the same each time, and a write of 0.15 straight to Opacity was
  overridden by the fader (it read 0.3). Both inputs arrive at 1920×1080 of 1920×1088, and
  Arena drew about 118 frames a second.
- **The picture is the harness's.** With an opaque white slide on projector A and black on B,
  Arena's composition snapshot read 155, 142, 120 at fader 0.3 and 84, 70, 44 at 0.5 (mean
  red, green, blue over the slide): what `catest` renders for the same inputs, to the code
  value. That is the lamp dimmed and warmed, in Arena.
- **Take** pressed over REST dissolved to the other projector over about 1.3 s, and after it
  the fader meant the other way round (fader 1 showed projector A at full). **Change A**
  pressed over REST took projector A to black and back about a second later. A press released
  50 ms after it was sent didn't reach the plugin; held for 0.3 s it did.
- **Arena hands Dest premultiplied** (on Windows too, below). With the layer below half
  white at alpha 128, its transparent half came out at 35, 31, 25 at fader 0.3 and 15, 10, 4
  at 0.5: exactly what `catest` renders for a premultiplied Dest (straight would have been
  75, 68, 57 and 38, 30, 17). The plugin takes its inputs as straight alpha, so a partly
  transparent layer below is darkened twice (see Not established). Opaque clips are
  unaffected.

**On Windows** (Arena 7.27.1 on win-lab, Mesa's llvmpipe, no GPU, 18 November 2026), with
the DLL from the v0.1.0 release's own zip, the same probe over REST with the plugin's log:

- It registers as **SW Carousel**, `uid: CA01 category: 2`, is offered as a Blend Mode, and
  shows 14 of its 15 parameters, Linear Light the one hidden.
- The **layer's fader is the dissolve fader**: 0.2, 0.85, 0.5, 1 and 0.3 read back as the
  plugin's Opacity and appear in its log at the frames they were set, and a write of 0.15 to
  Opacity was overridden. Both inputs arrive at 1280×720 of 1280×768; Arena drew about 6
  frames a second on the software renderer.
- **The picture is the Mac's.** The same stills gave the same composition snapshot to the
  code value: 155, 142, 120 and 35, 31, 25 at fader 0.3, 84, 70, 44 and 15, 10, 4 at 0.5. So
  Arena on Windows hands Dest premultiplied too.
- **Take** held 0.4 s at fader 0.3 dissolved to the other projector (the opaque half from
  155, 142, 120 to 19, 10, 0, as on the Mac), and **Change A** held 0.3 s took the lit
  projector to black and back within a second.

Before that, the DLL is compiled by CI (MSVC, no warnings in this repo's code) and
`tools/wincheck.ps1` loads it as a host does: `SW Carousel`, `CA01`, type 2 (mixer), inputs
2 to 2, 15 parameters, Linear Light first, Opacity among them.

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple M4 Max, macOS 26.4.1). It runs every picture
check at **two rasters**, 640×360 and 320×180, on the GPU and all of them again on **Apple's
software renderer**, which takes hours. The full run, against a fresh universal Release build
of this source before its About block was generated (17 November 2026), ended `verify: all 60
checks passed (45 negative controls; …)`. On the released tree (18 November 2026), against a
fresh universal Release build, every step but the software renderer's passed again, ending
`verify: all 47 checks passed (45 negative controls; …)`: the 60 less the 14 software-renderer
checks, plus the browser demo's. Of those 14, `--mixer`, `--unchanged` and `--lamp` were re-run
on the released tree and passed. Nothing that draws changed between the two runs: the
generated About block added the user guide's button, the 15th parameter. Every
tolerance is derived from something stated (GLSL 4.10's `pow` budget, a counted number of
float roundings, the plugin's integration error measured against a tighter integrator), not
fitted to the GPU; `AGENTS.md` lists them. Figures are 640×360 on the GPU unless said.

| check | result |
| --- | --- |
| `--lamp` | a cut to A, then a 2 s dissolve back to B: both projectors' gains, every channel, every frame of 300, against the harness's ODE, each within its tolerance; the closest frame **2.43e-7** against 5.51e-6. From the picture at 1 ms frames, A heats from 700 K to 90% of its light in 0.4163 s and B cools to 10% in 0.1684 s (Levi's rule of thumb: 0.372 and 0.174 s; printed, not asserted) |
| `--colour` | the colour temperature recovered from the picture's gains at 8 voltages and on Low is the filament's to **0.00054 K** (tolerance 0.25 K, the gains' bound carried through the locus inversion); warmer at every step down; Low at 3160.3 K |
| `--dissolve` | a 2 s Voltage dissolve at 48 fps, every frame, closest 5.6e-7 against 4.28e-5. The middle sags to **0.1139** of one lamp's light (model 0.1139, tolerance 1.6e-5) at 2457 K. A 1.5 s dissolve fits a 1.5 s ramp and misses a 0.9× or 1.1× one by thousands of times the tolerance. Light curve at the midpoint: the two lamps sum to 1.0000002 of one lamp's (tolerance 6.1e-5), at 3077 K |
| `--cut` | Time 0, High and Low, 8-bit and float: the frame at the cut is the frame before it, **0** values different; the next 60 frames follow the ODE from a step at the cut's instant (closest 2.43e-7 against 5.51e-6) |
| `--change` | Change B at 59.94 fps, a second press mid-cycle ignored: both projectors every frame (closest 2.95e-7 against 4.52e-5); 54 frames dark in the picture and in the model, 5 caught on the shutter's ramps |
| `--pop` | Pop 1 mm, both projectors, 522 frames each with the shutter open: the edge's spread from the picture is the model's every frame (closest 4.4e-6 px against 0.0071 px for A, 2e-6 against 0.0035 for B); the plateaus either side are the unblurred level (the kernel keeps its mass); τ fitted from the picture is **3.00000 s** against the stated 3 s (tolerance 0.055 s for B); Glass never spreads, **0** px |
| `--unchanged` | one projector alone at rest is its slide's rgb × a, alpha 1: **0** values different, 8-bit and float, opaque and with alpha, A and B. The float target is compared with the rasteriser's own byte-to-float conversion, probed: exact c/255 on the GPU, 126 of 256 values 1 ULP off on the software renderer |
| `--alpha` | white painted under every transparent pixel changes **0** bytes, at the output's size, resampled from 0.6 of it, and blurred, for each projector; 200 opaque pixels painted do show (600 to 11,072 bytes); a pixel at alpha 128 is encode(gain × decode(rgb × a)) to 0.003 of the tolerance |
| `--linear` | the band both projectors light is g_A τ_A + g_B τ_B, in light with Linear Light on and in code values with it off, to 0.002 and 0.005 of the tolerance |
| `--mixer` | null, zero and one input, null or zero-sized inputs and a zero viewport each return FF_FAIL and leave the plugin working; Dest 200×120 of 256×256 and Src 96×70 of 128×128, padded in magenta: **0** bytes differ from green padding over 130 frames, in focus and blurred; each projector at 0.6 and 1.37 of the output is the bilinear resample, worst 0.06 of its tolerance; the host's textures, framebuffer and viewport are back |
| `--resize` | output and inputs resized mid-dissolve and mid-drift and back: every frame follows the unresized model (closest 3.7e-7 against 3.45e-5) |
| `--rates` | a cut, a change and a 1.3 s dissolve at 24, 59.94, 60 and 144 fps, every frame against continuous time (closest 2.43e-7 against 5.51e-6, at 60 fps; at 144 fps 6.6e-7 against 4.5e-5) |
| `--clock` | the same run from host clocks of 0, 4.1e6 ms, 4.99e8 ms (5.8 days), 3.2e10 ms (a year) and 4.99e5 s, declared or voted: every frame within tolerance (closest 3.4e-7 against 2.9e-5); the vote settles on ms or s |
| `--names` | 15 parameters within FFGL's 16 bytes, unique, About last; index 0 Linear Light, on; Opacity a standard parameter; Take and the changes are buttons |
| `--pipe` | 3 frames in, 3 out; at Opacity 0, Dest back byte for byte; at 1, Src; the shorter input ends the run; a partial frame is dropped; cues ramp Opacity and step Lamp, each first showing one frame after its key (the lag `--cut` measures); every malformed cue line (no frame, a negative one, no value, NaN) and every bad `--set` exits 2; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| negative controls | **19** deliberately wrong versions built into the real plugin (resistance fixed, radiation linear in T, no thermal lag, no white balance, the colour never warming, the dissolve per frame, Time 0 as a ramp, a 60-frame shutter, the slide's heat kept across a change, the kernel losing its outer taps, resampling on straight colour, a float clock, a resize resetting the lamps, and more), each run against every check it should fail, 22 pairings, at both rasters: all caught; and one mutated shader through the shader hook |
| mutation | **35** one-character mutants of the shipped GLSL and C++, **35** caught |
| `tools/sweep.py` | all **10** controls change the picture |
| `tools/clips.py` | Pop's default is the clips' median (0.55 mm); on the two demo clips with alpha, resampled and blurred, white under the transparent pixels changes nothing |
| shaders | all 3 compile through `glslc`, not merely through Apple's driver |
| the bundle | universal (`x86_64 arm64`), exports `plugMain`, ad-hoc signs; `oxbow` reads its name, id, type, inputs and parameter 0 |

**Render cost.** Wall-clock with `glFinish` either side, the mean of 30 frames after 60 of
warm-up, both inputs at the output's size, 8-bit, from the verify run started on 17 November 2026,
on the M4 Max:

| | 720p | 1080p | 4K |
| --- | --- | --- | --- |
| one lit (one projector at rated, in focus) | 0.027 ms | 0.026 ms | 0.043 ms |
| both lit (the Light midpoint, glass) | 0.035 ms | 0.028 ms | 0.048 ms |
| both blurred (both slides just changed, Pop 1 mm) | 0.411 ms (2.5% of a 60 fps frame) | 1.058 ms (6.3%) | 7.865 ms (47.2%) |

The verify run before it, the same evening, gave 0.600, 1.128 and 8.083 ms for both blurred:
these are wall-clock means, good to tens of percent at 720p, not a benchmark. The blur is what
costs, and only for the seconds after a change while a slide is out of focus; at the default
Pop 0.55 mm the kernels are about half as long (σ scales with Pop; not timed). macOS figures
only, on one GPU.

### Not established

- **Resolume, beyond test stills.** In Arena on a Mac and on Windows it registers, follows the
  layer's fader and draws what the harness draws (above), but over still test pictures only:
  no real slide show, no show, and nothing timed inside Resolume. On Windows only Mesa's
  software renderer has run it, not a GPU driver.
- **Alpha in the layer below is darkened twice.** Arena hands Dest premultiplied (measured,
  above), and the plugin takes its inputs as straight alpha, so where the layer below is
  partly transparent its colour comes out multiplied by its alpha twice: a pixel at alpha 0.5
  lights as a quarter, not a half. Opaque clips, and clips flattened onto black, are exactly
  right. Whether Src (this layer) also arrives premultiplied is not measured.
- **The lamp model** is a grey body with a textbook filament (filament's model, sized for an
  EXR). Its exponents fall within the sources' spread and its lag near Levi's rule of thumb;
  those comparisons are printed, not asserted, because the sources disagree with each other.
- **The chosen numbers**: the shutter's 0.04 s close, 0.5 s drop and 0.06 s open, the slide's
  3 s warming time and the 700 K preheat. No source gives them.
- **The pop is uniform across the frame**: no bow profile, no autofocus. A dimmed lamp's
  slide is cooler and stays a little out of focus while the dissolve is held mid-way.
- **One lamp type**: a 300 W EXR on High or Low. No other lamp, no xenon, no LED.
- **No still mode**: the gate shows its layer live; there is no frame held at the change.
- **The default Curve is Voltage**, the plain rheostat, because the sag is the idea; a shaped
  dissolve unit is one click away (Light).
- **The 4K worst case**: both slides just changed at Pop 1 mm costs 7.9 ms a frame at 4K on
  the M4 Max (47% of a 60 fps frame), for the seconds the slides are out of focus. The two
  blur buffers are RGBA32F at the output's size, so at 4K they hold about 265 MB of GPU
  memory between them (arithmetic, not measured).
- **Other GPUs.** Run on one Apple GPU and Apple's software renderer only.

## Build

macOS (universal: Apple Silicon and Intel), from a checkout with its submodule
(`git submodule update --init --recursive`):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Carousel.bundle` into `~/Documents/Resolume Arena/Extra Effects`.
Restart Arena and it's in a layer's Blend Mode list as **SW Carousel**, where the fleet's
other mixers are.

Windows needs Visual Studio 2022 and vcpkg (for GLEW); CI builds it
(`.github/workflows/ci.yml`), and `tools/wincheck.ps1` loads the DLL as a host would.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no warnings,
every check at two rasters on the GPU and on the software renderer, the pipe, the bundle's
architecture, plist and ad-hoc signature, a host's view of it through `oxbow`, the mutants,
the sweep for dead controls, the demo clips and the render cost. It needs a built `oxbow` and
fails without one. `CLAUDE.md` lists the checks one by one.

Render a frame of your own:

```bash
./build/catest --out /tmp/carousel.png --set "Opacity=0.5" --frames 120
```

Or put two clips through it, projector A on stdin and projector B from a file:

```bash
./build/catest --pipe --size 1280x720 --pipe-src b.rgba < a.rgba > out.rgba
```

## License

The code is MIT: see [LICENSE](LICENSE). The CIE 1931 colour-matching functions in
`tools/data/ciexyz31_1.csv` are the CIE's, © International Commission on Illumination, and
they and the table the plugin is built with from them (`source/Cmf.h`) are CC BY-SA 4.0, not
MIT: see [tools/data/README.md](tools/data/README.md) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
