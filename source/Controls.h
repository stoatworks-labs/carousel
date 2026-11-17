#pragma once

/**
    Host parameters are 0..1; these are what they mean.

    Every ranged parameter is a plain FF_TYPE_STANDARD float in 0..1:
    `SetParamInfo` clamps a standard default into 0..1 before `SetParamRange`
    could be called, so a parameter in seconds could not declare a default
    in seconds. The conversions live here, in one file the plugin and the
    harness both use for reading back what they asked for -- never for the
    harness's predictions.
*/
namespace carousel
{

/// Time: the dissolve unit's rate, the seconds for one full travel of the
/// fader, 0 to 16 s (the Arion Mirage 901's longest), as 16 p^2 so the
/// short times get the slider. 0 cuts.
inline constexpr double kTimeMax     = 16.0;
inline constexpr double kTimeDefault = 2.0;///< Rollei Twin MSC 300P's default; US 3,282,155's "slow dissolve"

/// Pop: how far a warmed card-mounted slide bows from the flat, cold one,
/// 0 to 1 mm, linear. No published magnitude was found; the default is the
/// smallest that halves a freshly changed demo clip's detail at 1080p
/// (tools/clips.py, AGENTS.md "Defaults").
inline constexpr double kPopMaxMm     = 1.0;
inline constexpr double kPopDefaultMm = 0.4;

/// The lens: the Ektagraphic's 102 mm f/2.8 (Kodak SM4530-1; the 850H's
/// EKTANAR C), and the slide's long side, 36 mm (the 24 x 36 mm aperture),
/// which spans the output's width.
inline constexpr double kFNumber    = 2.8;
inline constexpr double kSlideMm    = 36.0;

enum class CurveKind : int
{
	Voltage,///< a rheostat: each lamp's voltage linear in the position
	Light,  ///< shaped: each lamp's steady light linear in the position
	Count
};

enum class LampKind : int
{
	High,///< the lamp at its rated 82 V
	Low, ///< the LO position: 71.4 V
	Count
};

enum class MountKind : int
{
	Card, ///< pops as it warms; focus drifts
	Glass,///< held flat: no drift
	Count
};

/// An option parameter's value is its element index, but its declared range
/// reads back 0..1 whatever the element count (the fleet's trap): map by
/// index, rounded and clamped.
int OptionIndex( float value, int count );

double TimeFromParam( float value );///< seconds
float ParamForTime( double seconds );

double PopFromParam( float value );///< mm
float ParamForPop( double mm );

/// IEC 61966-2-1:1999 (amended 2003), in double.
double DecodeSrgb( double c );
double EncodeSrgb( double x );

} // namespace carousel
