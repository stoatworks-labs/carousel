# Carousel user guide

> **Photosensitivity.** SW Carousel doesn't flash on its own: a slide change on the lit
> projector is one dark gap, at most once a second. But it cuts as fast as you cut. Cutting
> between contrasting slides more than about **five times a second** with Time at 0 to 0.1 s,
> with Take, the layer's fader or anything mapped to them, crosses **three flashes a second**
> over the whole frame. Flashing like that can trigger seizures in people with photosensitive
> epilepsy. See [Flashing, and keeping it safe](#flashing-and-keeping-it-safe).

Carousel is **two Kodak slide projectors on a dissolve unit, for
[Resolume](https://resolume.com) Arena and Avenue**: one FFGL **mixer**, **SW Carousel**. The
layer below is projector A's slide; the layer you set it on is projector B's. The layer's
fader is the dissolve fader.

Nothing is drawn. Each projector's light is a 300 W tungsten lamp whose filament follows the
dissolve unit's voltage through its thermal lag. So a dissolve sags and turns amber in the
middle, and even a cut is a quick crossfade while one filament cools and the other heats. A
slide change is the projector's one-second shutter cycle, and a cold card-mounted slide
drifts into focus as it warms in the beam.

![Three frames side by side: a pile of pale skulls on the left; in the middle the skulls and
a gold star pattern faintly over each other, dim and orange; on the right the gold star
pattern alone](carousel.png)

*SW Carousel on a plain rheostat dissolve, rendered by the offline harness rather than
captured from Resolume: projector A alone (fader 0), half way (fader 0.5), projector B alone
(fader 1). Halfway each lamp has half its voltage, so the screen sags to about a ninth of one
lamp's light and warms from 3350 K to 2457 K. The slides are single frames of Resolume's demo
clips NoHopeJustFear_44 and IntoTheGlow_02.*

> **Before you rely on this:** this is **v0.1.0**, the first release, and early days. The
> lamps are measured, not just asserted. An offline harness drives the plugin's own code with
> two real inputs, at two sizes, on an Apple Silicon Mac's GPU and again on Apple's software
> renderer, against its **own** model of the lamps. Every frame of a cut and a dissolve
> matches it within a derived tolerance; the colour temperature read back out of the picture
> is the filament's to **0.0005 K**; a cut at Time 0 leaves the frame at the cut **bit for
> bit** the frame before it; one projector alone at rest gives its slide back unchanged.
> Nineteen deliberately wrong versions of the plugin and thirty-five one-character sabotages
> of its code are all caught. In **Resolume Arena 7.29.0 on a Mac** and **7.27.1 on Windows**
> it registers as a mixer, takes its dissolve from the layer's fader, and draws what the
> harness draws to the code value. But that was over test stills: it hasn't been through a
> slide show or a show yet. The lamp model, the shutter's timing and the
> slide's warming time are assumptions (see [Known limits](#known-limits)). **Try it on a
> spare layer first**, and shout if anything misbehaves.
>
> This codebase was created with AI assistance, directed and reviewed by a human author.

---

## Installing

Download the build for your platform. For macOS there's a universal `.dmg` or `.zip`
(Apple silicon and Intel), **Developer ID-signed and notarised** so the bundle just loads
(the signing happens on my Mac shortly after each release is published, so a download made
in the first few minutes may need **Open** from the context menu once), and for Windows an
x64 installer or `.zip`. Put `Carousel.bundle` (macOS) or `Carousel.dll` (Windows) in
Resolume's FFGL folder, then restart Resolume:

```
macOS    ~/Documents/Resolume Arena/Extra Effects/
Windows  %USERPROFILE%\Documents\Resolume Arena\Extra Effects\
```

Avenue uses the same layout in its own folder. Yes, it's **Extra Effects** for a mixer too:
Resolume has one FFGL plugin folder, and sources, effects and mixers all load from it.

The Windows installer puts the DLL there for you, per user, with no admin rights needed. It
defaults to Arena's folder (Avenue's if only Avenue is installed) and lets you change it.
The Windows builds aren't code-signed. Plugin files aren't gated the way `.exe` files are,
so Resolume loads them as normal; only the installer trips SmartScreen, once: **More info**
→ **Run anyway**.

SW Carousel is a mixer, so it isn't in the effects browser. It's in a layer's **Blend Mode**
list, beside Resolume's own blend modes (and in the layer's transition list; see
[If it looks wrong](#if-it-looks-wrong)).

---

## It is a mixer, not an effect

An effect gets one picture. A dissolve needs two. So SW Carousel is a blend mode, and it reads
the two layers like this:

| Projector | In the code | In Resolume | What it is here |
|---|---|---|---|
| **A** | `inputTextures[0]` (Dest) | the layer **below** | Projector A's slide. |
| **B** | `inputTextures[1]` (Src) | **this** layer, the one whose Blend Mode is SW Carousel | Projector B's slide. |

Put projector A's clip on one layer and projector B's on the layer **above** it, then set the
upper layer's **Blend Mode** to SW Carousel. Handed only one picture, the plugin declines to
draw. The two layers don't have to be the same size: each is read at its own resolution and
filled to the frame.

A "slide" is whatever the layer is playing: a still is the obvious thing, but a clip plays on
through the gate. Each slide is its clip composited over black, so a transparent pixel is
black film. The output is opaque: a screen is lit, not transparent.

---

## Start here

1. Put a still or a clip on layer 1: projector A.
2. Put another on layer 2: projector B.
3. Set layer 2's **Blend Mode** to **SW Carousel**.

At the defaults the fader is up, so you see projector B. Then:

- **Pull layer 2's fader down.** The dissolve unit follows it, no faster than one full travel
  in **Time** (2 s by default): projector B's lamp dims and projector A's comes up. Half way
  the screen sags and turns amber, because each lamp has half its voltage. That's the plain
  rheostat a dissolve unit was, and the idea.
- **Press Take.** One press dissolves to the other projector in Time, wherever the fader is.
- **Press Change B** while projector A is showing. Nothing visible happens: projector B's
  shutter shuts while its slide is changed, which is how a two-projector show is run. Put
  B's next clip on layer 2 while the shutter is shut, press Take, and the new slide comes up
  soft and drifts into focus as it warms.

---

## How it works, in one paragraph

Per frame, on the CPU in double precision: the dissolve unit's position follows the fader
(or the fader turned round, after a Take) no faster than one full travel per Time. The curve
turns that position into each lamp's voltage. Each lamp is a 300 W, 82 V projection lamp (an
ANSI EXR, 3350 K, the lamp Kodak lists first for the Ektagraphic III), and its filament's
temperature is integrated from its heat balance, C(T) dT/dt = V²/R(T) − radiation −
conduction, with tungsten's own resistivity and heat capacity. Its light and colour are
Planck's law at that temperature, summed against the CIE 1931 colour-matching functions and
balanced so the lamp at its rated voltage is white. On the GPU, each slide is lit by its own
lamp, behind its own shutter, blurred by how far its film has bowed out of focus, and the
two projectors' light adds on the screen in linear light.

---

## The Dissolve group

**Opacity** (default 1). The dissolve fader: 0 is projector A, 1 is projector B. Resolume
puts the **layer's opacity fader** on a mixer parameter named Opacity, so in Arena this is the
fader on projector B's layer, not a control in the blend mode's panel. In Arena on a Mac,
setting the fader to 0.2, 0.85, 0.5, 1 and 0.3 set Opacity to exactly those values, and
writing Opacity directly was overridden by the fader.

**Take** (a button). Swaps which projector the fader means: after one press, fader up means
projector A and down means B; a second press swaps back. So a press dissolves to the other
projector in Time without touching the fader, which is how a dissolve unit's Take button
works. It acts when pressed; releasing it does nothing.

**Time** (0 to 16 s, default 2 s). How long the dissolve unit takes for a full travel from one
projector to the other; a smaller move takes its share. The fader can move as fast as you
like: the unit follows no faster than this. **0 cuts**: the voltages switch at once. The
filaments still can't, so a cut is a quick crossfade, the incoming lamp passing half its light
about 13 frames after the cut at 60 fps. The slider is squared, so the short times, where a
dissolve turns into a cut, get most of its travel. 2 s is the Rollei Twin MSC 300P's default
and the "slow dissolve" of US 3,282,155.

**Curve** (Voltage or Light, default Voltage). How the unit sets the two voltages.

- **Voltage**: a plain rheostat, each lamp's voltage straight from the position. Half way,
  each lamp has half its voltage and gives far less than half its light: the screen sags to
  **0.114** of one lamp's light, at **2457 K**. This is the sag and the amber middle, and the
  default because it's the idea.
- **Light**: a shaped unit, each lamp's voltage set so the two lamps' light crosses straight.
  Half way both lamps run at 0.818 of their voltage and give one lamp's light between them,
  so the screen keeps its brightness, but each filament is cooler: the middle still warms, to
  **3077 K**. US 3,282,155 shaped its dissolve "to eliminate reddening"; it didn't quite.

The dark projector's lamp is kept warm at 700 K, below glow, as dissolve units did, so it
comes up faster than a cold lamp would.

---

## The Lamps group

**Lamp** (High or Low, default High). **Low** is the Ektagraphic's LO position: a 3 Ω
resistor in series, 87% of the voltage. Both lamps are dimmer and warmer, at **3160 K**, and
the change follows the filament's lag rather than snapping.

---

## The Slides group

**Change A** and **Change B** (buttons). A slide change on that projector: Kodak's cycle is
one revolution, **1 s**. The shutter closes over 0.04 s, the slide drops half way through,
and the shutter opens over the last 0.06 s. A press during a cycle is ignored, so a
projector can change at most once a second.

- **On the dark projector**, as a show is run, nobody sees it. Put that projector's next
  clip on its layer during the second, and dissolve to it when you're ready.
- **On the lit projector** it's a second of dark: about 0.9 s fully black (54 frames at
  59.94 fps).

The plugin can't change the clip for you: what the layer plays is Resolume's. The slide that
"drops" at half way is whatever the layer is showing then, and it starts cold.

**Mounts** (Card or Glass, default Card). **Card** mounts let the film bow as it heats
("popping"); **Glass** mounts hold it flat, so Glass slides never go out of focus.

**Pop** (0 to 1 mm, default 0.55 mm). How far a warm card-mounted slide bows. The lens was
focused on a warm, bowed slide, so a freshly changed, cold, flat one is out of focus by Pop
and drifts in as it warms, over a few seconds: the slide warms with a 3 s time constant
while its lamp is on. At Pop 1 mm a 640-wide slide opens 1.57 px out of focus and is sharp
6.7 s after the shutter opens. It happens again, more gently, whenever a projector has been
dissolved out long enough for its slide to cool. 0.55 mm is measured, not chosen: it's the
smallest Pop that halves a freshly changed slide's detail, the median over Resolume's 15
Shop74 demo clips at 1080p. No published figure for a slide's pop was found.

---

## The Screen group

**Linear Light** (on). The two projectors' light adds in linear light, as light adds on a
screen. Off adds code values instead. **Resolume hides this control**: Arena never shows a
mixer's first parameter, so it sits there on purpose, on, and stays on.

---

## Running a slide show

The classic two-projector show alternates: one projector is up while the other changes.

1. Projector A showing (fader down, or after a Take), its slide warm and sharp.
2. **Change B**, and put B's next clip on its layer during the second the shutter is shut.
   Nobody sees it.
3. **Take**: a dissolve over Time to B. The new slide comes up soft and sharpens as it warms
   (Card mounts).
4. **Change A**, put A's next clip on its layer, **Take**, and so on.

Map Take, Change A and Change B to MIDI or keys, and Time to a knob. A Time of 1 to 4 s is a
dissolve; 0 is a cut that still shows the filaments' lag. With Card mounts and Pop up, every
new slide drifts in; with Glass, every slide opens sharp.

Resolume's own layer transition ramps the layer's fader, which is Opacity here: an Arena
transition drives the dissolve, and Time limits how fast the unit follows it.

---

## Flashing, and keeping it safe

People with photosensitive epilepsy can have a seizure from flashing light. Broadcast
(Ofcom) and web (WCAG 2.x) guidance draws the line at **three flashes a second** over a large
part of the picture. SW Carousel never crosses it on its own, and its defaults don't. Cutting
does.

Measured from the plugin's own output (60 fps, white and black slides unless said): a
transition is a change of at least a tenth of white in mean linear luminance whose darker end
is below 0.8; two opposite transitions are one flash; more than 6 transitions in any second
crosses. Counted over the whole frame and over third-of-frame windows; the two agreed.

| What | Flashes a second |
|---|---|
| Change on the lit projector, pressed as fast as it will go | 1 |
| Change A and B alternating, both projectors lit | 2 |
| Take at Time 0, up to 5 presses a second | 3 at most (on the line at 5) |
| Take at Time 0, 6, 8, 10, 15, 30 presses a second | **3.5, 4.5, 5.5, 7.5, 15: crosses** |
| Take at Time 0.05 s or 0.1 s, 6 or more presses a second | **crosses** |
| Take at Time 0.25 s or more, any rate tried (up to 10 a second) | under the line |
| Two of Resolume's demo clips (NoHopeJustFear_44, Cyberspace_09), Take at Time 0, 6 a second | **crosses** |

**To keep it safe**: keep Time at 0.25 s or more when cuts come fast, or keep cuts at five a
second or fewer. At Time 0 the fader drives the lamps directly, so throwing it end to end is
a cut too, and so is anything mapped to Take or the fader (a beat-synced MIDI clip, an
envelope, a BPM-linked control). These figures come from the harness's white and black slides
and two demo clips, not from every clip.

---

## Performance

On the Mac it was measured on (an Apple M4 Max), the mean of 30 frames after a warm-up, with
the GPU finished both sides: about **0.03 ms** a frame with the slides in focus, at any size
up to 4K. While a slide is out of focus after a change (Pop 1 mm, both slides just changed,
the worst case) it's **0.4 ms** at 720p, **1.1 ms** at 1080p and **7.9 ms** at 4K (47% of a
60 fps frame), for the few seconds the slides take to warm. Another GPU will differ, and
nothing has been timed inside Resolume.

---

## If it looks wrong

- **Nothing happens when I pick SW Carousel.** It needs both layers: something on this layer
  and something on the layer below. Handed one picture, it declines to draw.
- **The fader doesn't do anything.** Opacity is the layer's opacity fader in Resolume, not a
  control in the blend mode's panel.
- **The fader works backwards.** You've pressed Take an odd number of times. Press it again.
- **I moved the fader and it took two seconds to follow.** That's Time. Set it to 0 for the
  fader to cut, or shorter for a quicker dissolve.
- **The middle of a dissolve goes dark and orange.** That's Curve Voltage, a plain rheostat,
  and it's what a real one did. Curve Light keeps the brightness.
- **A new slide is blurred.** That's the pop: a cold card-mounted slide is out of focus until
  it warms, a few seconds. Glass mounts, or Pop 0, turn it off.
- **Both slides go a little soft when I hold the dissolve half way.** Each lamp is dimmed, so
  its slide cools and moves a little out of focus (about 0.75 px at 1080p at the Light
  midpoint, at the default Pop). The lens was focused on a fully warm slide.
- **Change didn't do anything.** A press during a cycle is ignored, so a second press within
  the second does nothing. On the dark projector a change is invisible, as it should be.
- **The layer below looks darker than it should where it's partly transparent.** Arena hands
  the layer below premultiplied, and this version takes it as straight alpha, so its soft
  edges are darkened twice (see [Known limits](#known-limits)). Opaque clips are exactly
  right.
- **There's no Linear Light control.** Resolume hides a mixer's first parameter. It's on, as
  it should be.
- **As a transition, it's odd.** SW Carousel is in the layer's transition list too. There
  Resolume gives it an instance of its own and ramps its Opacity (measured on the fleet's
  other mixers, not on this one), which the dissolve unit follows at Time's pace. It's meant
  as a blend mode.

---

## Known limits

- **Tried in Resolume on test stills only.** It loads and draws in Resolume Arena 7.29.0 on a
  Mac and in Arena 7.27.1 on Windows, the Windows one on Mesa's software renderer, not a
  graphics card's driver. Nobody has run a slide show, or a show, through it yet, or timed it
  inside Resolume.
- **The layer below's soft edges are darkened twice.** In Arena on a Mac and on Windows, a
  half-transparent white layer below came out exactly as if its colour had been multiplied by
  its alpha twice: Arena hands the layer below premultiplied, and this version takes its
  inputs as straight alpha. A pixel at alpha 0.5 lights as a quarter, not a half. Opaque
  clips, and clips flattened onto black, are unaffected. Whether this layer (projector B)
  arrives premultiplied too hasn't been measured.
- **One lamp**: a 300 W EXR, on High or Low. Other projection lamps (FHS, ELH, EXY) differ by a
  few percent in lag and colour; there's no choice of them.
- **No still mode.** The gate shows its layer live; a slide isn't a frame held at the change.
- **Chosen numbers.** The shutter's 0.04 s close, 0.5 s drop and 0.06 s open, the slide's 3 s
  warming time and the dark lamp's 700 K preheat come from no source; they're reasonable
  guesses.
- **The lamp is a textbook filament** (a grey body), sized for an EXR. Its light and colour
  follow its voltage within the published sources' spread, and its lag is near the usual rule
  of thumb, but the sources disagree with each other.
- **The pop is the same across the frame**: no bow profile, and no autofocus. Kodak's service
  manual has the Ektagraphic III's autofocus settling on a cold slide within about a second;
  this is a manually focused projector.
- **A dimmed slide stays a little out of focus** while a dissolve is held part way (above).
- **The 4K worst case**: 7.9 ms a frame on an M4 Max for the seconds after both slides change
  at Pop 1 mm, and the two float blur buffers hold about 265 MB of GPU memory at 4K between
  them (arithmetic, not measured).
- **The default Curve is Voltage**, because the sag is the idea. Light is one click away.
- **Other GPUs.** It has run on one Apple GPU and Apple's software renderer only.

---

## About

The last group, **About**, has the plugin's name, version, licence and maker, and buttons that
open this user guide, the project page, the source on GitHub and the support page in your
browser.

The code is MIT. The CIE 1931 colour-matching functions the lamps' colour is summed from are
the CIE's (© International Commission on Illumination), and they and the table built from
them are CC BY-SA 4.0, not MIT.

## Links

- Project page: [stoatworks-labs.com/software/carousel](https://stoatworks-labs.com/software/carousel/)
- Browser demo: [carousel-demo.stoatworks-labs.com](https://carousel-demo.stoatworks-labs.com/)
- Source: [github.com/stoatworks-labs/carousel](https://github.com/stoatworks-labs/carousel)
- User guide: [stoatworks-labs.com/software/carousel/guide](https://stoatworks-labs.com/software/carousel/guide/)

## Reporting something

Something wrong, odd or missing? Open an issue at
[github.com/stoatworks-labs/carousel/issues](https://github.com/stoatworks-labs/carousel/issues).
The plugin keeps a short log of what the host did (the host and its version, the first frame's
input sizes, any guard that refused a frame, and the first few hundred changes of Opacity),
which helps a lot:

```
macOS    ~/Library/Logs/carousel/
Windows  %LOCALAPPDATA%\carousel\logs\
```
