#pragma once

#include <string>

/**
    The passes, as GLSL 4.10. Two of them.

    1. **Blur** (only for a projector whose slide is out of focus): the
       horizontal half of a separable Gaussian, at the output's size, into
       that projector's RGBA32F buffer. Each tap is the slide resampled to
       the output pixel.
    2. **Screen**: per projector, the vertical half from its buffer, or the
       slide resampled directly when it is in focus; then the screen,
       encode( clamp( gain_A t_A + gain_B t_B ) ), alpha 1.

    **A slide's transmittance** is its texel composited over black and then
    decoded: t = decode( rgb x a ) (rgb x a with Linear Light off). It is
    premultiplied before anything weights it -- the bilinear resampling, which
    is four texelFetches weighted by hand, and both halves of the blur -- so a
    transparent texel's colour can never bleed (the NoPremultiply fault).

    Every position is `floor( gl_FragCoord.xy )`, an integer; every input is
    read with texelFetch at integer coordinates clamped to its USED size, so
    padding is never read and no MaxUV is involved. The weights and gains are
    computed on the CPU: no exp, sin or spectral work in GLSL, whose accuracy
    the spec leaves to the driver. The one transcendental is the sRGB pair.
*/

namespace carousel
{

extern const char* const kVersion;
extern const char* const kVertex;
extern const char* const kCommon;
extern const char* const kBlurFragment;
extern const char* const kScreenFragment;

/// kVersion + kCommon + the pass, the order every fragment shader is built in.
std::string AssembleFragment( const char* pass );
std::string AssembleVertex();

} // namespace carousel
