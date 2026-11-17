#pragma once

/**
    A filament's light as linear sRGB, in double, every frame.

    filament's path, computed directly instead of read from a table: Planck's
    law at T (2019 SI h, c, k), a grey body, summed at 1 nm against the CIE
    1931 2-degree colour-matching functions (Cmf.h, generated from CVRL's
    ciexyz31_1.csv), to XYZ, to linear sRGB by the IEC 61966-2-1 matrix to
    four decimals. 471 exponentials per lamp per frame.

    **White** is the lamp at its rated voltage: each channel is divided by
    the same sum at kRatedKelvin, so a lamp at rated passes the slide
    unchanged and everything else -- dimmer, warmer -- is relative to it.
*/
namespace carousel::colour
{

struct Rgb
{
	double r = 0.0, g = 0.0, b = 0.0;
};

/// Negative-control bits (the plugin's Fault enum maps onto these).
enum Perturb : int
{
	kPerturbNoWhiteBalance = 1,///< divided by Y( Tr ), not per channel: the rated lamp is not white
	kPerturbColourHot      = 2,///< the colour of Tr always, at the picture luminance of T
};

/// M . XYZ( T ), unnormalised (W sr^-1 m^-3 x nm, summed).
Rgb PlanckRgb( double kelvin );

/// The lamp's gain per channel at T: PlanckRgb( T ) / PlanckRgb( Tr ).
Rgb Gain( double kelvin, int perturb = 0 );

/// The picture's luminance for a gain: BT.709's weights on linear sRGB
/// (ITU-R BT.709-6 item 3.2; the sRGB primaries are BT.709's).
double PictureLuminance( const Rgb& gain );

} // namespace carousel::colour
