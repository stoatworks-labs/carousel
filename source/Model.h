#pragma once

#include "Colour.h"
#include "Controls.h"
#include "Lamp.h"

#include <vector>

/**
    Two slide projectors on a dissolve unit, as state that persists from one
    frame to the next, in double, on the CPU.

    Per projector: the lamp's filament temperature (lamp::Derivative), the
    shutter's cycle and the slide's heat. Between them: the dissolve unit's
    position. The GPU is handed, per projector, a gain per channel (the lamp's
    colour and level times the shutter) and a blur width; see Carousel.cpp.

    **Time.** `Advance( dt )` integrates from the last frame's instant to this
    one's with the controls the last frame applied; `Apply` then takes this
    frame's controls and buttons AT this instant; `Now` reports this instant.
    So a change of control takes effect at the time of the first frame that
    sees it, and every frame shows its own instant. dt is clamped to 0..0.25 s
    (a stall integrates at most a quarter second; no floor).

    **Integration.** The interval is cut wherever the drive changes slope --
    the dissolve reaching its target, each shutter edge, the slide dropping
    in -- and each piece is stepped by classical RK4 in steps of at most
    lamp::kMaxStep, with the drive evaluated at each stage's own time. State:
    both temperatures and both slides' heat, together.

    **The shutter** (SM4530-1 and the Ektapro 320: one cycle per change, 1 s;
    the 1971 service manual: the shutter closes, the slide is ejected, the
    next drops in, the shutter opens) closes linearly over kShutterClose, the
    new slide drops at kSlideDrop, the shutter opens linearly over the last
    kShutterOpen. Those three are CHOSEN: no source gives them.

    **The slide's heat** h: dh/dt = ( q - h ) / kPopTau, q = shutter x
    ( T^4 - T0^4 ) / ( Tfull^4 - T0^4 ), Tfull the filament at this Lamp
    setting's full voltage. The lens was focused on a warm slide (h = 1), so
    the film is Pop x |1 - h| from focus; the blur circle on the slide is
    that over the f-number, drawn as a Gaussian with the disc's second
    moment, sigma = diameter / 4.
*/
namespace carousel
{

inline constexpr double kCycleSeconds = 1.0;  ///< SM4530-1, Ektapro 320
inline constexpr double kShutterClose = 0.04; ///< chosen
inline constexpr double kSlideDrop    = 0.5;  ///< chosen
inline constexpr double kShutterOpen  = 0.06; ///< chosen
inline constexpr double kPopTau       = 3.0;  ///< chosen: US 5,609,404 gives 3-10 s WITH forced hot air
inline constexpr double kMaxFrame     = 0.25; ///< seconds integrated per frame, at most

/// The Gaussian's taps each side, at most: 4 sigma up to sigma = 12 px.
inline constexpr int kMaxTaps = 48;
/// Below this sigma, in output pixels, there is no blur: the first tap's
/// weight, exp( -1 / ( 2 sigma^2 ) ), is under 2^-24.
inline constexpr double kBlurFloor = 0.17;
/// A gain below this is 0, within it of 1 is 1.
inline constexpr double kGainSnap = 1.0 / 16777216.0;

/// The negative controls: a bitmask, 0 in the shipped plugin, reachable only
/// from the harness. Each builds the wrong answer to a check into the real
/// plugin. The shader's bits are repeated there as GLSL constants.
enum Fault : int
{
	kFaultNone           = 0,
	kFaultConstantR      = 1 << 0, ///< R held at R( Tr ) -- --lamp
	kFaultLinearCool     = 1 << 1, ///< radiation linear in T -- --lamp
	kFaultNoLag          = 1 << 2, ///< the filament at its steady T instantly -- --cut, --lamp
	kFaultFloatClock     = 1 << 3, ///< the host's readings held in floats -- --clock
	kFaultFrameLag       = 1 << 4, ///< the lamps stepped 1/60 s per frame, whatever the frame -- --rates
	kFaultSlewFrames     = 1 << 5, ///< the dissolve advanced per frame, not per second -- --rates, --dissolve
	kFaultNoWhiteBalance = 1 << 6, ///< raw Planck sRGB over Y( Tr ) -- --unchanged, --colour
	kFaultColourHot      = 1 << 7, ///< the colour of Tr at every T -- --colour
	kFaultDissolveCurve  = 1 << 8, ///< the Light curve drives voltage -- --dissolve
	kFaultHardCutRamp    = 1 << 9, ///< Time 0 as a one-frame ramp -- --cut
	kFaultShutterLong    = 1 << 10,///< the cycle timed in 60 frames, not 1 s -- --change
	kFaultPopNoReset     = 1 << 11,///< the slide's heat kept across a change -- --pop
	kFaultPopTau         = 1 << 12,///< the drift's time constant 1 s -- --pop
	kFaultNoPremultiply  = 1 << 13,///< shader: resampling and blur on straight colour -- --alpha
	kFaultResizeResets   = 1 << 14,///< a resize restarts the projectors at rest -- --resize
	kFaultGammaMix       = 1 << 15,///< shader: Linear Light's sum on code values -- --linear
	kFaultLeaveBound     = 1 << 16,///< a texture left bound -- --mixer
	kFaultSizeSwap       = 1 << 17,///< shader: B read at A's size -- --mixer
	kFaultKernelMass     = 1 << 18,///< the Gaussian's outermost taps zeroed after normalising -- --pop
};

/// What the parameters ask for, in physical units.
struct Asked
{
	bool linear      = true;
	double fader     = 1.0;///< Opacity: 0 = A, 1 = B
	double time      = kTimeDefault;
	CurveKind curve  = CurveKind::Voltage;
	LampKind lamp    = LampKind::High;
	MountKind mounts = MountKind::Card;
	double popMm     = kPopDefaultMm;
};

/// One projector at one instant.
struct ProjectorNow
{
	colour::Rgb gain;     ///< lamp colour and level x shutter, snapped
	double sigmaPx  = 0.0;///< the defocus blur, output pixels; 0 = none
	double kelvin   = 0.0;
	double drive    = 0.0;///< u = V / V_rated
	double shutter  = 1.0;///< 1 open, 0 shut
	double heat     = 0.0;///< the slide's
	bool cycling    = false;
};

struct Instant
{
	ProjectorNow p[ 2 ];///< 0 = A (Dest), 1 = B (Src)
	double position = 0.0;
	double target   = 0.0;
};

class Model
{
public:
	Model();

	void SetFault( int mask )
	{
		fault = mask;
	}

	bool Started() const
	{
		return started;
	}

	/// At rest for these controls: both lamps at their steady temperature
	/// for the fader's position, the slides' heat at their steady value, no
	/// cycle running. The first frame, and the ResizeResets fault.
	void Start( const Asked& asked, bool take );

	/// Integrate dt seconds (clamped to 0..kMaxFrame) under the controls the
	/// last Apply set.
	void Advance( double dt );

	/// This frame's controls, at this instant. At Time 0 the unit cuts here.
	void Apply( const Asked& asked, bool take );

	/// Start a slide change on projector 0 (A) or 1 (B) at this instant,
	/// unless one is already running: the mechanism ignores a press mid-cycle.
	void Change( int projector );

	/// This instant, for an output `outWidth` pixels wide.
	Instant Now( int outWidth ) const;

	/// The steady light curve's inverse: the drive whose steady picture
	/// luminance is `light` of the rated lamp's (High). For the harness's
	/// read-back only.
	double DriveForLight( double light ) const;

	/// The dark lamp's floor, u.
	double PreheatDrive() const
	{
		return preheatU;
	}

private:
	double driveFor( int projector, double position ) const;
	double shutterAt( int projector, double age ) const;
	double fullFourth() const;
	void derivatives( const double y[ 4 ], double tau, double out[ 4 ] ) const;

	lamp::Wire wire;
	double preheatU = 0.0;
	double fullKelvin[ 2 ] = { lamp::kRatedKelvin, lamp::kRatedKelvin };

	Asked applied;
	bool take       = false;
	bool started    = false;
	double position = 1.0, target = 1.0;
	double kelvin[ 2 ] = { lamp::kAmbient, lamp::kAmbient };
	double heat[ 2 ]   = { 0.0, 0.0 };
	bool cycling[ 2 ]  = { false, false };
	int phase[ 2 ]     = { 0, 0 };///< 0 closing, 1 shut before the drop, 2 shut after, 3 opening
	double age[ 2 ]    = { 0.0, 0.0 };
	int fault          = 0;

	//The segment being integrated: rates and its start, for derivatives().
	double segSlew      = 0.0;///< position units per second, signed
	double segCycleRate = 1.0;///< cycle seconds per second
	double segLampRate  = 1.0;
	double segPosition  = 0.0;
	double segAge[ 2 ]  = { 0.0, 0.0 };
};

} // namespace carousel
