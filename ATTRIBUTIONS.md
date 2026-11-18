# Attributions

Carousel is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The lamp — Stoatworks filament

<https://github.com/stoatworks-labs/filament>  
Licence: MIT  
Copyright: Stoatworks Labs

The filament's thermal ODE in units of the rated point, tungsten's resistivity table and interpolation rule, the NIST-JANAF heat capacity, the design-point split (15% conducted, emissivity 0.30, half the coil radiating), the wire sized from power and voltage, and the Planck-to-sRGB conventions are filament's (source/Lamp.*, source/Colour.*), re-sized for a 300 W 82 V EXR at 3350 K and integrated on the CPU in double. tools/data/ciexyz31_1.csv is filament's copy of CVRL's file, checked by sha256.

### The mixer, the harness and the tooling — Stoatworks penumbra, and pepper, haze and lenticular before it

<https://github.com/stoatworks-labs/penumbra>  
Licence: MIT  
Copyright: Stoatworks Labs

The mixer's shape (the input guards, each input at its own size, index 0 sacrificial, Opacity named for Arena's layer fader, the per-instance Opacity log), the harness's two-input rig, its --pipe and --pipe-src mode (SIGPIPE ignored, a closed stdout exits 1) and cue sheets, tools/verify.sh, tools/glslc.sh, tools/mutate.sh, tools/sweep.py, source/Diag.{h,cpp} and the first draft of the About headers the generated ones replaced are penumbra's, adapted. Penumbra had them from pepper, haze and lenticular, and those debts carry over.

### The release workflow and the DLL check — Stoatworks penumbra and xenon

<https://github.com/stoatworks-labs/penumbra>  
Licence: MIT  
Copyright: Stoatworks Labs

.github/workflows/ci.yml and release.yml are penumbra's (xenon's, slitscan's before them): a universal bundle and a Windows DLL, ATTRIBUTIONS.md in each zip, the Windows installer per user into Resolume's Extra Effects. tools/wincheck.ps1, which loads the DLL as a host loads it and reads back its name, id, type, inputs and parameters, is penumbra's (pepper's). Adapted.

### The clock's unit vote and the latched button — Stoatworks relay

<https://github.com/stoatworks-labs/relay>  
Licence: MIT  
Copyright: Stoatworks Labs

Settling whether the host's clock is in milliseconds or seconds by watching it against the wall clock (source/Clock.*), and a button that acts on its rising edge, with Take as an XOR on the fader's meaning, are relay's.

### What a mixer is in Arena — Stoatworks genlock

<https://github.com/stoatworks-labs/genlock>  
Licence: MIT  
Copyright: Stoatworks Labs

genlock's AGENTS.md is the measured account of how Resolume Arena drives an FFGL mixer (which input is Dest, the padding, the hidden first parameter), and catest --mixer's magenta-padding sentinel is genlock's test, adapted. No code beyond the test's shape.

### The GL state guard — Stoatworks vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/GLState.h is haze's, which is radar's, which is vectrix's, which lifted it from resolume-scopes' Scopes.cpp: the host's GL state captured and put back.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Data this project ships

Measurements made by other people and carried inside the product. Each is credited the way its publisher asks, and where a licence requires its attribution to travel with the data, that text is reproduced here.

### CIE 1931 colour-matching functions, 2° observer — CIE, in the copy the Colour & Vision Research Laboratory (UCL) publishes

<https://doi.org/10.25039/CIE.DS.xvudnb9b>  
Licence: Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)  
Copyright: International Commission on Illumination (CIE)

The colour-matching functions at 1 nm from 360 to 830 nm (CIE 015:2018; CIE 018:2019), as CVRL publishes them, "CIE 1931 2-deg, XYZ CMFs" (http://www.cvrl.org/database/data/cmfs/ciexyz31_1.csv): committed unedited as tools/data/ciexyz31_1.csv (filament's copy, sha256 ccbe601d7abf2227b46007ab29742f1f6bcb8621a6413283fb5ade7626a662c5, checked before use), with a note beside it (tools/data/README.md). The plugin is built with the published numbers as written (source/Cmf.h, generated from the CSV by tools/cmf_header.py) and sums them with Planck's law into each lamp's colour; the harness reads the CSV itself at run time. The data and the table baked from it are CC BY-SA 4.0, not MIT; the code is MIT.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### The resistivity of tungsten — Forsythe and Worthing, 1925, by way of filament

<https://doi.org/10.1086/142880>

W. E. Forsythe and A. G. Worthing, "The Properties of Tungsten and the Characteristics of Tungsten Lamps", The Astrophysical Journal 61 (1925): the resistivity table from 300 to 3600 K in source/Lamp.h, filament's copy (taken from a web reproduction of the table, not checked against the paper itself).

### The heat capacity of tungsten — NIST-JANAF (Chase, 1998), by way of filament

<https://webbook.nist.gov/cgi/cbook.cgi?ID=C7440337&Mask=2>

The Shomate coefficients for solid tungsten on the NIST Chemistry WebBook: the filament's heat capacity rises with its temperature.

### Kodak's projector manuals — Eastman Kodak

Ektagraphic III Service Manual SM4530-1 (1996): the 1 s slide change, the EXR lamp, the 102 mm f/2.8 lens, the LO resistor's 10.6 V. Ektapro 320 Instruction Manual; "Servicing the KODAK EKTAGRAPHIC Slide Projector" (1971): the order of the cycle. Ektagraphic III Operating Manual: popping and mounts. Carousel 850H manual: focusing on a warm slide.

### Dissolve units and the pop, in the patents — US 3,093,030; US 3,282,155; US 4,743,109 and US 5,609,404 (Eastman Kodak)

The dissolve units' rheostat, shaped law and warm-filament minimum (US 3,093,030 and US 3,282,155), and the pop's direction and timescale (US 4,743,109 and US 5,609,404).

### Projection lamps — Ushio; Osram Sylvania

Ushio's projection-lamp datasheet (EXR, 3350 K) and Osram Sylvania 54392 (EXR, 925 lm): the lamp the model is sized for.

### How a tungsten lamp's light, colour and lag follow its voltage — IES Lighting Handbook (1947); ERCO Handbook of Lighting Design; Welch Allyn (2009); D. Klipstein; B. Cleyet

The power laws of light and colour temperature against voltage, and Levi's heating and cooling rule as B. Cleyet summarises it (the book itself was not read): what catest --lamp and --colour print beside the model's own numbers. Printed, not asserted: the sources disagree with each other by more than the model disagrees with any of them.

### Dissolve rates — Arion Mirage 701/901 and Pro Dissolve; Rollei Twin MSC 300P

The dissolve times real units offered, and Time's 2 s default.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### The two-projector slide show

Kodak Carousel and Ektagraphic projectors on a dissolve unit: the warm sag in the middle of a dissolve, the shutter's dark gap, the slide drifting into focus. Built from how the lamps, the shutter and the film behave; nothing is copied from any product or anyone's software.

## Standards and published specifications

What the implementation is measured against.

- **OpenGL Shading Language 4.10** — §4.5.1: the precision of + − ×, division, exp2, log2 and so pow, from which every tolerance in catest is built; §8.3: mix with a boolean selector.
- **IEC 61966-2-1 (sRGB)** — The decode and encode in source/Shaders.cpp, the XYZ-to-linear-sRGB matrix to its four decimals in source/Colour.cpp, and the harness's own separate copies.
- **ITU-R BT.709-6** — Item 3.2: the luminance weights the harness reads a screen's light with.
- **CIE 15** — Colorimetry by summing the colour-matching functions at 1 nm; the CIE 1960 UCS in which the harness inverts a chromaticity to a colour temperature.
- **The 2019 SI** — The exact h, c and k in Planck's law.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
