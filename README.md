# carousel

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has **never been loaded into
> Resolume**, on macOS or Windows, and has never been built on Windows. Everything below is
> measured by an offline harness, `catest`, that drives the real plugin class in a headless
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

## Status

**v0.1.0, 17 November 2026, unreleased, and early days.** It has **never been loaded into
Resolume** on either platform and never been built on Windows. `oxbow probe` reads the
bundle as a host does (`SW Carousel` / `CA01` / **mixer**, inputs 2..2, parameter 0 Linear
Light, a standard parameter named Opacity), and `oxbow selftest`, an effect host that hands
the plugin one input, is refused on its first frame, as a mixer must refuse it. There's no
user guide yet, no OpenFX port and no browser demo.

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple M4 Max, macOS 26.4.1) against a fresh
universal Release build (17 November 2026). It runs every picture check at **two rasters**,
640×360 and 320×180, on the GPU and all of them again on **Apple's software renderer**. Every
tolerance is derived from something stated (GLSL 4.10's `pow` budget, a counted number of
float roundings, the plugin's integration error measured against a tighter integrator), not
fitted to the GPU; `AGENTS.md` lists them. Figures are 640×360 on the GPU unless said.

| check | result |
| --- | --- |
| `--lamp` | a cut to A, then a 2 s dissolve back to B: both projectors' gains, every channel, every frame of 300, against the harness's ODE, each within its tolerance; the closest
frame **2.43e-7** against 5.51e-6. From the picture at 1 ms frames, A heats from 700 K to 90% of its light in 0.4163 s and B cools to 10% in 0.1684 s (Levi's rule of thumb: 0.372 and 0.174 s; printed, not asserted) |
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
| `--rates` | a cut, a change and a 1.3 s dissolve at 24, 59.94, 60 and 144 fps, every frame against continuous time (closest 6.6e-7 against 4.5e-5, at 144 fps) |
| `--clock` | the same run from host clocks of 0, 4.1e6 ms, 4.99e8 ms (5.8 days), 3.2e10 ms (a year) and 4.99e5 s, declared or voted: every frame within tolerance (closest 3.4e-7 against 2.9e-5); the vote settles on ms or s |
| `--names` | 14 parameters within FFGL's 16 bytes, unique, About last; index 0 Linear Light, on; Opacity a standard parameter; Take and the changes are buttons |
| `--pipe` | 3 frames in, 3 out; at Opacity 0, Dest back byte for byte; at 1, Src; the shorter input ends the run; a partial frame is dropped; cues ramp Opacity and step Lamp; a bad cue or `--set` exits 2; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| negative controls | **19** deliberately wrong versions built into the real plugin (resistance fixed, radiation linear in T, no thermal lag, no white balance, the colour never warming, the dissolve per frame, Time 0 as a ramp, a 60-frame shutter, the slide's heat kept across a change, the kernel losing its outer taps, resampling on straight colour, a float clock, a resize resetting the lamps, and more), each run against every check it should fail, 22 pairings, at both rasters: all caught; and one mutated shader through the shader hook |
| mutation | **35** one-character mutants of the shipped GLSL and C++, **35** caught |
| `tools/sweep.py` | all **10** controls change the picture |
| `tools/clips.py` | Pop's default is the clips' median (0.55 mm); on the two demo clips with alpha, resampled and blurred, white under the transparent pixels changes nothing |
| shaders | all 3 compile through `glslc`, not merely through Apple's driver |
| the bundle | universal (`x86_64 arm64`), exports `plugMain`, ad-hoc signs; `oxbow` reads its name, id, type, inputs and parameter 0 |

**Render cost.** Wall-clock with `glFinish` either side, the mean of 30 frames after 60 of
warm-up, both inputs at the output's size, from verify's run on 17 November 2026 on an M4 Max:

| | 720p | 1080p | 4K |
| --- | --- | --- | --- |
| one lit (one projector at rated, in focus) | BENCH | BENCH | BENCH |
| both lit (the Light midpoint, glass) | BENCH | BENCH | BENCH |
| both blurred (both slides just changed, Pop 1 mm) | BENCH | BENCH | BENCH |

The blur is what costs, and only for the seconds after a change while a slide is out of
focus; at the default Pop 0.55 mm the kernels are about half as long. macOS figures only, on
one GPU.

### Not established

- **Resolume.** Never loaded, on either platform. That Arena hides a mixer's first parameter
  and binds `Opacity` to the layer fader was measured on five other mixers (genlock, wipe,
  relay, lenticular, pepper), not on this one.
- **Windows.** Never built.
- **The lamp model** is a grey body with a textbook filament (filament's model, sized for an
  EXR). Its exponents fall within the sources' spread and its lag near Levi's rule of thumb;
  those comparisons are printed, not asserted, because the sources disagree with each other.
- **The chosen numbers**: the shutter's 0.04 s close, 0.5 s drop and 0.06 s open, the slide's
  3 s warming time and the 700 K preheat. No source gives them.
- **The pop is uniform across the frame**: no bow profile, no autofocus. A dimmed lamp's
  slide is cooler and stays a little out of focus while the dissolve is held mid-way.
- **Inputs are taken as straight alpha.** Whether Arena hands Dest premultiplied is not
  measured.
- **The slide is live**: the gate shows the clip, not a frame held at the change.
- **Other GPUs.** Run on one Apple GPU and Apple's software renderer only.

## Build

macOS (universal: Apple Silicon and Intel), from a checkout with its submodule
(`git submodule update --init --recursive`; the repository is local, with no remote yet):

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
Restart Arena and it should be in a layer's Blend Mode list as **SW Carousel**, where the
fleet's other mixers are (this one has never been loaded).

Windows needs Visual Studio 2022 and vcpkg (for GLEW). It has never been built there.

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

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
