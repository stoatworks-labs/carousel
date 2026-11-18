# Attributions

Carousel is built on other people's work. This file lists what that work is, who did it,
and what it is doing here.

It is a provisional hand copy: the master lists live in the `stoatworks-backend` repo and
are pushed out by `scripts/sync-attributions.py` once a project is registered there.
Carousel is not registered yet, so this file was written by hand on 2026-11-17, adapted
from penumbra's, and will be replaced by the generated one.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The lamp — Stoatworks filament

Not yet published.  
Licence: MIT  
Copyright: Stoatworks Labs

The filament's thermal ODE in units of the rated point, tungsten's resistivity table and
interpolation rule, the NIST-JANAF heat capacity, the design-point split (15% conducted,
emissivity 0.30, half the coil radiating), the wire sized from power and voltage, and the
Planck-to-sRGB conventions are filament's (source/Lamp.\*, source/Colour.\*), re-sized for a
300 W 82 V EXR at 3350 K and integrated on the CPU in double. tools/data/ciexyz31_1.csv is
filament's copy of CVRL's file, checked by sha256.

### The mixer, the harness and the tooling — Stoatworks penumbra

Not yet published.  
Licence: MIT  
Copyright: Stoatworks Labs

The mixer's shape (the input guards, each input at its own size, index 0 sacrificial,
`Opacity` named for Arena's layer fader), the harness's two-input rig, its `--pipe` and
`--pipe-src` mode and cue sheets, tools/verify.sh, tools/glslc.sh, tools/mutate.sh,
tools/sweep.py, source/Diag.{h,cpp} and the About headers (provisional hand copies, `guide`
empty: no user guide exists) are penumbra's, adapted. Penumbra had them from pepper, haze
and lenticular.

### The clock's unit vote and the latched button — Stoatworks relay

Not yet published.  
Licence: MIT  
Copyright: Stoatworks Labs

Settling whether the host's clock is in milliseconds or seconds by watching it against the
wall clock (source/Clock.\*), and a button that acts on its rising edge with Take as an XOR
on the fader's meaning, are relay's.

### What a mixer is in Arena — Stoatworks genlock

<https://github.com/stoatworks-labs/genlock>  
Licence: MIT  
Copyright: Stoatworks Labs

genlock's AGENTS.md is the measured account of how Resolume Arena drives an FFGL mixer
(which input is Dest, the padding, the hidden first parameter), and `catest --mixer`'s
magenta-padding sentinel is genlock's test, adapted. No code beyond the test's shape.

### The GL state guard — Stoatworks vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/GLState.h is haze's, which is radar's, which is vectrix's, which lifted it from
`resolume-scopes`' Scopes.cpp: the host's GL state captured and put back.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, at the fleet's pin b1afaf9.

The plugin ABI itself. An FFGL mixer is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample. Part of the
upstream SDK tree rather than something this plugin calls directly.

## Data

### CIE 1931 colour-matching functions, 2° observer — CIE

<https://doi.org/10.25039/CIE.DS.xvudnb9b>  
Licence: Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)  
Copyright: International Commission on Illumination (CIE)

The colour-matching functions at 1 nm from 360 to 830 nm (CIE 015:2018; CIE 018:2019), in
the copy the Colour & Vision Research Laboratory (CVRL), UCL, publishes as "CIE 1931 2-deg,
XYZ CMFs" (<http://www.cvrl.org/database/data/cmfs/ciexyz31_1.csv>): committed unedited as
tools/data/ciexyz31_1.csv (filament's copy, sha256
ccbe601d7abf2227b46007ab29742f1f6bcb8621a6413283fb5ade7626a662c5, checked before use). The
plugin sums the published numbers as written (source/Cmf.h, generated from it by
tools/cmf_header.py); the harness reads the CSV itself at run time. The data and the table
baked from it are CC BY-SA 4.0, not MIT; the code is MIT.

### Tungsten

W. E. Forsythe and A. G. Worthing, "The properties of tungsten and the characteristics of
tungsten lamps", Astrophysical Journal 61 (1925): the resistivity table, 300–3600 K.
NIST-JANAF Thermochemical Tables, via the NIST Chemistry WebBook: the Shomate coefficients
for tungsten's heat capacity.

## Work we checked ourselves against

No code was taken from these. They are where the numbers come from; each is cited where it is
used, and SPEC-carousel.md lists what each gives.

- **Kodak**: Ektagraphic III Service Manual SM4530-1 (1996) — the 1 s slide change, the EXR
  lamp, the 102 mm f/2.8 lens, the LO resistor's 10.6 V; Ektapro 320 Instruction Manual;
  "Servicing the KODAK EKTAGRAPHIC Slide Projector" (1971) — the order of the cycle;
  Ektagraphic III Operating Manual — popping and mounts; Carousel 850H manual — focusing on a
  warm slide.
- **Patents**: US 3,093,030 and US 3,282,155 (dissolve units: the rheostat, the shaped law,
  the warm-filament minimum); US 4,743,109 and US 5,609,404 (Eastman Kodak: the pop's
  direction and timescale).
- **Lamps**: Ushio's projection-lamp datasheet (EXR 3350 K); Osram Sylvania 54392 (EXR,
  925 lm).
- **Power laws**: IES Lighting Handbook (1947); ERCO Handbook of Lighting Design; Welch Allyn
  (2009); D. Klipstein, "Characteristics of Incandescent Lamps"; B. Cleyet's summary of
  Levi's heating and cooling rule (the book itself was not read).
- **Dissolve rates**: Arion Mirage 701/901 and Pro Dissolve; Rollei Twin MSC 300P (the 2 s
  default).

## Standards and published specifications

What the implementation is measured against.

- **OpenGL Shading Language 4.10** (revision 6, 24 July 2010) — §4.5.1: the precision of
  `+ − ×`, division, `exp2`, `log2` and so `pow`, from which every tolerance in `catest` is
  built; §8.3: `mix` with a boolean selector.
- **IEC 61966-2-1:1999 (sRGB)**, amended 2003 — the decode and encode in source/Shaders.cpp,
  the XYZ-to-linear-sRGB matrix to its four decimals in source/Colour.cpp, and the harness's
  own separate copies.
- **ITU-R BT.709-6**, item 3.2 — the luminance weights the harness reads a screen's light
  with.
- **CIE 15** — colorimetry by summing the CMFs at 1 nm; the CIE 1960 UCS in which the harness
  inverts a chromaticity to a colour temperature.
- **The 2019 SI** — the exact h, c and k in Planck's law.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or
examined — the debt is to the idea.

### The two-projector slide show

Kodak Carousel and Ektagraphic projectors on a dissolve unit: the warm sag in the middle of a
dissolve, the shutter's dark gap, the slide drifting into focus. Built from how the lamps,
the shutter and the film behave; nothing is copied from any product or anyone's software.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
