#pragma once

/**
    The projection lamp as numbers, in double, for the CPU: filament's model
    of a tungsten filament, sized for a 300 W, 82 V EXR.

    **The ODE** (filament's, unchanged in form):

        C( T ) dT/dt = V^2 / R( T ) - e s A ( T^4 - T0^4 ) - k ( T - T0 )

    The lamp is a filament designed to dissipate kWatts at kVolts with its
    wire at kRatedKelvin in an ambient of kAmbient, with kConductionFraction
    of that power conducted (the leads, the supports, the fill gas) and the
    rest radiated. That design point fixes both loss terms:

        e s A = ( 1 - fc ) P / ( Tr^4 - T0^4 ),   k = fc P / ( Tr - T0 )

    and R( T ) = R( Tr ) rho( T ) / rho( Tr ). Divided by the heat capacity,
    with u = V / V_rated:

        dT/dt = ( P / m ) [ u^2 rho( Tr ) / rho( T )
                            - ( 1 - fc )( T^4 - T0^4 ) / ( Tr^4 - T0^4 )
                            - fc ( T - T0 ) / ( Tr - T0 ) ] / c_p( T )

    **The wire**, from P and V: a straight wire of diameter d and length L
    with resistance V^2 / P hot, radiating from kCoilRadiatingFraction of
    its surface at kEmissivity:

        d^3 = 4 rho P ( 1 - fc ) P / ( kappa e s pi^2 V^2 ( Tr^4 - T0^4 ) )

    For the EXR this is d = 165.5 um, L = 0.458 m, m = 189.6 mg (model
    figures, not data). Nothing is tuned: the three stated constants are
    filament's assumptions.

    **What differs from filament** (SPEC-carousel.md): the lamp (300 W, 82 V,
    3350 K -- Ushio's EXR colour temperature -- where filament runs mains
    bulbs at 2800 K); no mains waveform (the drive is its RMS); two lamps on
    the CPU in double, not a texture of bulbs in float.

    rho( T ) is Forsythe and Worthing (1925), 300 to 3600 K by 100 K, linear
    between points; c_p( T ) the NIST-JANAF Shomate fit. Both as filament.
*/
namespace carousel::lamp
{

inline constexpr double kPi = 3.14159265358979323846;

/// The lamp: ANSI EXR, 82 V 300 W (Kodak SM4530-1; Osram Sylvania 54392),
/// 3350 K (Ushio's projection lamp datasheet, JCR82V-300W).
inline constexpr double kWatts       = 300.0;
inline constexpr double kVolts       = 82.0;
inline constexpr double kRatedKelvin = 3350.0;

/// filament's design-point assumptions, unchanged.
inline constexpr double kAmbient               = 298.15;
inline constexpr double kConductionFraction    = 0.15;
inline constexpr double kEmissivity            = 0.30;
inline constexpr double kCoilRadiatingFraction = 0.5;
inline constexpr double kStefanBoltzmann       = 5.670374419e-8;
inline constexpr double kTungstenDensity       = 19250.0;///< kg / m^3
inline constexpr double kTungstenMolarMass     = 0.18384;///< kg / mol
inline constexpr double kMeltingKelvin         = 3695.0;

/// The dark lamp's floor: US 3,282,155 keeps "a minimum voltage on both
/// lamps at all times ... sufficient to warm the lamp filaments without
/// incandescence". 700 K is CHOSEN (no source gives a figure); its light is
/// 2e-12 of rated, which the output rounds to exactly 0.
inline constexpr double kPreheatKelvin = 700.0;

/// The Ektagraphic's LO LAMP position: a 3.0 ohm dropping resistor with
/// "10.6 V AC drop across" it (Kodak SM4530-1), so the lamp sees 71.4 of
/// 82 V.
inline constexpr double kLowFraction = ( 82.0 - 10.6 ) / 82.0;

/// Forsythe and Worthing (1925), tungsten's resistivity in micro-ohm cm at
/// 300, 400, ... 3600 K (filament's table).
inline constexpr int kRhoCount    = 34;
inline constexpr double kRhoFirst = 300.0;
inline constexpr double kRhoStep  = 100.0;
inline constexpr double kRho[ kRhoCount ] = {
	5.65,  8.06,  10.56, 13.23, 16.09, 19.00, 21.94, 24.93, 27.94, 30.98, 34.08, 37.19, 40.36, 43.55, 46.78, 50.05, 53.35,
	56.67, 60.06, 63.48, 66.91, 70.39, 73.91, 77.49, 81.04, 84.70, 88.33, 92.04, 95.76, 99.54, 103.3, 107.2, 111.1, 115.0,
};

/// NIST-JANAF (Chase 1998) Shomate coefficients for solid tungsten, c_p in
/// J / ( mol K ) with t = T / 1000: A + B t + C t^2 + D t^3 + E / t^2.
inline constexpr double kShomateLow[ 5 ]  = { 23.95930, 2.639680, 1.257750, -0.254642, -0.048407 }; ///< 298-1900 K
inline constexpr double kShomateHigh[ 5 ] = { -22.57640, 90.27980, -44.27150, 7.176630, -24.09740 };///< 1900-3680 K
inline constexpr double kShomateSplit     = 1900.0;

/// The integrator's longest step. RK4's error at 1 ms was 2.5e-6 of the
/// light early in a rise from 700 K (the resistivity table's kinks cost it
/// its order); at 0.25 ms it is below the GPU's own rounding. A frame is
/// at most 0.25 s, so at most 1000 steps.
inline constexpr double kMaxStep = 0.00025;

/// Negative-control bits for the lamp (the plugin's Fault enum maps onto
/// these). 0 in the shipped plugin.
enum Perturb : int
{
	kPerturbConstantR  = 1,///< R held at R( Tr ): no inrush -- --lamp must fail
	kPerturbLinearCool = 2,///< radiation linear in T, matched at Tr -- --lamp must fail
};

double Resistivity( double kelvin );///< micro-ohm cm
double HeatCapacity( double kelvin );///< J / ( kg K )

struct Wire
{
	double diameter   = 0.0;///< m
	double length     = 0.0;///< m
	double mass       = 0.0;///< kg
	double wattsPerKg = 0.0;///< P / m: the whole lamp, as the ODE sees it
};

/// The EXR's wire, sized from P and V at Tr.
Wire MakeWire();

/// dT/dt in K/s at temperature `kelvin` under a drive u^2 = ( V / V_rated )^2.
double Derivative( const Wire& wire, double kelvin, double driveSquared, int perturb );

/// Where power in balances power out under a constant drive u^2. Bisection
/// to the double's resolution.
double SteadyKelvin( double driveSquared, int perturb );

/// The drive u that holds the filament at `kelvin` in the steady state.
double DriveForKelvin( double kelvin, int perturb );

} // namespace carousel::lamp
