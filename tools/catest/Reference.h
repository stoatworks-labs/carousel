#pragma once

/**
    The harness's OWN projectors, written from SPEC-carousel.md, sharing no
    code with the plugin: catest's yardstick.

    - **The lamp in SI units.** The plugin integrates filament's equation
      divided through by the rated power (Lamp.h). This one keeps volts,
      ohms, watts and joules: R( T ) = ( V^2 / P ) rho( T ) / rho( Tr ),
      e s A and k from the design point, C( T ) = m c_p( T ), the wire
      sized by its own algebra (L = a d^2 from the resistance, then d from
      the radiated power). Its own copies of Forsythe and Worthing's table
      and the Shomate coefficients.
    - **Integrated by Dormand-Prince 5(4)**, adaptive, to a relative error
      of 1e-11 -- not the plugin's RK4. The same timeline can also be
      integrated by the harness's own fixed-step RK4 at the plugin's stated
      step (0.25 ms), with the Light curve's inverse from a table at the
      plugin's spacing (4097 points): the difference between the two is the
      plugin's METHOD error, which every lamp tolerance includes, measured,
      not guessed.
    - **Colour from the CSV, read at run time** (tools/data/ciexyz31_1.csv,
      not the plugin's generated Cmf.h), Planck summed at 1 nm as CIE 15
      defines a tristimulus value, the IEC 61966-2-1 matrix, white at the
      rated lamp per channel; and the inverse, a chromaticity back to a
      temperature on the Planckian locus in CIE 1960 (u, v).
    - **The timeline** from the spec's words: the fader's target, the
      unit's rate, the preheat floor, the two curves (the Light curve's
      inverse from its own 20 001-point table), Low's 71.4/82, the shutter's
      1 s cycle with its chosen 0.04 / 0.5 / 0.06 s, the slide's heat with
      tau = 3 s, the blur's sigma, the gain snap.
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef CAROUSEL_SOURCE_DIR
#define CAROUSEL_SOURCE_DIR "."
#endif

namespace ref
{

constexpr double kPi = 3.14159265358979323846;

//The lamp (SPEC "Sources": SM4530-1, Sylvania 54392, Ushio).
constexpr double kP  = 300.0;
constexpr double kV  = 82.0;
constexpr double kTr = 3350.0;
//filament's design point (SPEC "The lamp, from filament").
constexpr double kT0        = 298.15;
constexpr double kFc        = 0.15;
constexpr double kEps       = 0.30;
constexpr double kKappa     = 0.5;
constexpr double kSigmaSB   = 5.670374419e-8;
constexpr double kDensity   = 19250.0;
constexpr double kMolarMass = 0.18384;
constexpr double kPreheatK  = 700.0;
constexpr double kLowVolts  = 82.0 - 10.6;

//Forsythe and Worthing (1925), micro-ohm cm, 300 K to 3600 K by 100 K.
const double kRhoTable[ 34 ] = { 5.65,  8.06,  10.56, 13.23, 16.09, 19.00, 21.94, 24.93, 27.94, 30.98, 34.08, 37.19,
	                             40.36, 43.55, 46.78, 50.05, 53.35, 56.67, 60.06, 63.48, 66.91, 70.39, 73.91, 77.49,
	                             81.04, 84.70, 88.33, 92.04, 95.76, 99.54, 103.3, 107.2, 111.1, 115.0 };

inline double rho( double T )
{
	double x = ( T - 300.0 ) / 100.0;
	int i    = static_cast< int >( std::floor( x ) );
	if( i < 0 )
		i = 0;
	if( i > 32 )
		i = 32;
	return kRhoTable[ i ] + ( x - i ) * ( kRhoTable[ i + 1 ] - kRhoTable[ i ] );
}

//NIST-JANAF Shomate, J / ( mol K ).
inline double cpPerKg( double T )
{
	const double t = T / 1000.0;
	double a, b, c, d, e;
	if( T < 1900.0 )
		a = 23.95930, b = 2.639680, c = 1.257750, d = -0.254642, e = -0.048407;
	else
		a = -22.57640, b = 90.27980, c = -44.27150, d = 7.176630, e = -24.09740;
	return ( a + t * ( b + t * ( c + t * d ) ) + e / ( t * t ) ) / kMolarMass;
}

struct Filament
{
	double rHot = 0.0;     ///< ohm, V^2 / P
	double epsSigmaA = 0.0;///< W / K^4
	double kCond = 0.0;    ///< W / K
	double mass = 0.0;     ///< kg
	double d = 0.0, L = 0.0;
};

inline Filament makeFilament()
{
	Filament f;
	f.rHot             = kV * kV / kP;
	const double rhoR  = rho( kTr ) * 1e-8;
	const double span4 = std::pow( kTr, 4 ) - std::pow( kT0, 4 );
	//R = rho L / ( pi d^2 / 4 ) gives L = a d^2; the radiated power gives d.
	const double a = f.rHot * kPi / ( 4.0 * rhoR );
	f.d            = std::cbrt( ( 1.0 - kFc ) * kP / ( kKappa * kEps * kSigmaSB * kPi * a * span4 ) );
	f.L            = a * f.d * f.d;
	f.mass         = kDensity * kPi * f.d * f.d / 4.0 * f.L;
	f.epsSigmaA    = ( 1.0 - kFc ) * kP / span4;
	f.kCond        = kFc * kP / ( kTr - kT0 );
	return f;
}

inline const Filament& filament()
{
	static const Filament f = makeFilament();
	return f;
}

/// Net power into the filament, W.
inline double netPower( double T, double volts )
{
	const Filament& f = filament();
	const double R    = f.rHot * rho( T ) / rho( kTr );
	return volts * volts / R - f.epsSigmaA * ( std::pow( T, 4 ) - std::pow( kT0, 4 ) ) - f.kCond * ( T - kT0 );
}

inline double dTdt( double T, double volts )
{
	return netPower( T, volts ) / ( filament().mass * cpPerKg( T ) );
}

inline double steadyT( double volts )
{
	if( volts <= 0.0 )
		return kT0;
	double lo = kT0, hi = 4500.0;
	for( int i = 0; i < 100; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( netPower( mid, volts ) > 0.0 ? lo : hi ) = mid;
	}
	return 0.5 * ( lo + hi );
}

inline double voltsForT( double T )
{
	double lo = 0.0, hi = 2.0 * kV;
	for( int i = 0; i < 100; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( steadyT( mid ) < T ? lo : hi ) = mid;
	}
	return 0.5 * ( lo + hi );
}

//---------------------------------------------------------------------------
// Colour.
//---------------------------------------------------------------------------
struct Cmf
{
	int first = 0;
	std::vector< double > x, y, z;
	bool ok = false;
};

inline const Cmf& cmf()
{
	static const Cmf table = [] {
		Cmf c;
		std::ifstream in( CAROUSEL_SOURCE_DIR "/tools/data/ciexyz31_1.csv" );
		std::string line;
		int expected = -1;
		while( std::getline( in, line ) )
		{
			std::replace( line.begin(), line.end(), ',', ' ' );
			std::istringstream s( line );
			int nm;
			double a, b, d;
			if( !( s >> nm >> a >> b >> d ) )
				continue;
			if( expected < 0 )
				c.first = expected = nm;
			if( nm != expected++ )
				return Cmf{};
			c.x.push_back( a );
			c.y.push_back( b );
			c.z.push_back( d );
		}
		c.ok = c.x.size() == 471 && c.first == 360;
		if( !c.ok )
			std::fprintf( stderr, "catest: could not read " CAROUSEL_SOURCE_DIR "/tools/data/ciexyz31_1.csv\n" );
		return c;
	}();
	return table;
}

/// Planck x CMF summed at 1 nm, CIE 15's definition of a tristimulus value.
/// Units drop out of every ratio.
inline void planckXyz( double T, double xyz[ 3 ] )
{
	const double h = 6.62607015e-34, c = 299792458.0, k = 1.380649e-23;
	const Cmf& t   = cmf();
	xyz[ 0 ] = xyz[ 1 ] = xyz[ 2 ] = 0.0;
	const size_t n = t.x.size();
	for( size_t i = 0; i < n; ++i )
	{
		const double lam = ( t.first + static_cast< double >( i ) ) * 1e-9;
		const double B   = 2.0 * h * c * c / std::pow( lam, 5 ) / std::expm1( h * c / ( lam * k * T ) );
		xyz[ 0 ] += B * t.x[ i ];
		xyz[ 1 ] += B * t.y[ i ];
		xyz[ 2 ] += B * t.z[ i ];
	}
}

const double kM[ 3 ][ 3 ] = { { 3.2406, -1.5372, -0.4986 }, { -0.9689, 1.8758, 0.0415 }, { 0.0557, -0.2040, 1.0570 } };

inline void toRgb( const double xyz[ 3 ], double rgb[ 3 ] )
{
	for( int r = 0; r < 3; ++r )
		rgb[ r ] = kM[ r ][ 0 ] * xyz[ 0 ] + kM[ r ][ 1 ] * xyz[ 1 ] + kM[ r ][ 2 ] * xyz[ 2 ];
}

inline const double* ratedRgb()
{
	static double rgb[ 3 ];
	static bool done = false;
	if( !done )
	{
		double xyz[ 3 ];
		planckXyz( kTr, xyz );
		toRgb( xyz, rgb );
		done = true;
	}
	return rgb;
}

/// The lamp's gain per channel at T: white is the rated lamp.
inline void gains( double T, double g[ 3 ] )
{
	double xyz[ 3 ], rgb[ 3 ];
	planckXyz( T, xyz );
	toRgb( xyz, rgb );
	for( int c = 0; c < 3; ++c )
		g[ c ] = rgb[ c ] / ratedRgb()[ c ];
}

inline double lum709( const double g[ 3 ] )
{
	return 0.2126 * g[ 0 ] + 0.7152 * g[ 1 ] + 0.0722 * g[ 2 ];
}

/// Photometric Y of the lamp at T over the rated lamp's: the CMF's y-bar,
/// with no white balance in the way. For the power-law comparisons.
inline double photometric( double T )
{
	double a[ 3 ], b[ 3 ];
	planckXyz( T, a );
	planckXyz( kTr, b );
	return a[ 1 ] / b[ 1 ];
}

inline void invert3( const double m[ 3 ][ 3 ], double out[ 3 ][ 3 ] )
{
	const double det = m[ 0 ][ 0 ] * ( m[ 1 ][ 1 ] * m[ 2 ][ 2 ] - m[ 1 ][ 2 ] * m[ 2 ][ 1 ] ) - m[ 0 ][ 1 ] * ( m[ 1 ][ 0 ] * m[ 2 ][ 2 ] - m[ 1 ][ 2 ] * m[ 2 ][ 0 ] )
	                   + m[ 0 ][ 2 ] * ( m[ 1 ][ 0 ] * m[ 2 ][ 1 ] - m[ 1 ][ 1 ] * m[ 2 ][ 0 ] );
	out[ 0 ][ 0 ] = ( m[ 1 ][ 1 ] * m[ 2 ][ 2 ] - m[ 1 ][ 2 ] * m[ 2 ][ 1 ] ) / det;
	out[ 0 ][ 1 ] = ( m[ 0 ][ 2 ] * m[ 2 ][ 1 ] - m[ 0 ][ 1 ] * m[ 2 ][ 2 ] ) / det;
	out[ 0 ][ 2 ] = ( m[ 0 ][ 1 ] * m[ 1 ][ 2 ] - m[ 0 ][ 2 ] * m[ 1 ][ 1 ] ) / det;
	out[ 1 ][ 0 ] = ( m[ 1 ][ 2 ] * m[ 2 ][ 0 ] - m[ 1 ][ 0 ] * m[ 2 ][ 2 ] ) / det;
	out[ 1 ][ 1 ] = ( m[ 0 ][ 0 ] * m[ 2 ][ 2 ] - m[ 0 ][ 2 ] * m[ 2 ][ 0 ] ) / det;
	out[ 1 ][ 2 ] = ( m[ 0 ][ 2 ] * m[ 1 ][ 0 ] - m[ 0 ][ 0 ] * m[ 1 ][ 2 ] ) / det;
	out[ 2 ][ 0 ] = ( m[ 1 ][ 0 ] * m[ 2 ][ 1 ] - m[ 1 ][ 1 ] * m[ 2 ][ 0 ] ) / det;
	out[ 2 ][ 1 ] = ( m[ 0 ][ 1 ] * m[ 2 ][ 0 ] - m[ 0 ][ 0 ] * m[ 2 ][ 1 ] ) / det;
	out[ 2 ][ 2 ] = ( m[ 0 ][ 0 ] * m[ 1 ][ 1 ] - m[ 0 ][ 1 ] * m[ 1 ][ 0 ] ) / det;
}

/// CIE 1960 (u, v) of an XYZ.
inline void uv( const double xyz[ 3 ], double& u, double& v )
{
	const double den = xyz[ 0 ] + 15.0 * xyz[ 1 ] + 3.0 * xyz[ 2 ];
	u                = 4.0 * xyz[ 0 ] / den;
	v                = 6.0 * xyz[ 1 ] / den;
}

/// The temperature whose Planckian chromaticity is nearest, in (u, v), to
/// the light these gains describe: the gains times the rated lamp's linear
/// sRGB, back to XYZ. NaN if the light has no positive luminance.
inline double cctFromGains( const double g[ 3 ] )
{
	double minv[ 3 ][ 3 ];
	invert3( kM, minv );
	double rgb[ 3 ], xyz[ 3 ];
	for( int c = 0; c < 3; ++c )
		rgb[ c ] = g[ c ] * ratedRgb()[ c ];
	for( int r = 0; r < 3; ++r )
		xyz[ r ] = minv[ r ][ 0 ] * rgb[ 0 ] + minv[ r ][ 1 ] * rgb[ 1 ] + minv[ r ][ 2 ] * rgb[ 2 ];
	if( !( xyz[ 1 ] > 0.0 ) )
		return std::nan( "" );
	double u0, v0;
	uv( xyz, u0, v0 );
	auto distance = [ & ]( double T ) {
		double p[ 3 ], u, v;
		planckXyz( T, p );
		uv( p, u, v );
		return ( u - u0 ) * ( u - u0 ) + ( v - v0 ) * ( v - v0 );
	};
	//Coarse in log T, then golden section.
	double best = 1000.0, bestD = 1e300;
	for( double lt = std::log( 800.0 ); lt < std::log( 8000.0 ); lt += 0.01 )
	{
		const double d = distance( std::exp( lt ) );
		if( d < bestD )
		{
			bestD = d;
			best  = std::exp( lt );
		}
	}
	double a = best * 0.985, b = best * 1.015;
	const double gr = 0.5 * ( std::sqrt( 5.0 ) - 1.0 );
	double c1 = b - gr * ( b - a ), c2 = a + gr * ( b - a );
	double d1 = distance( c1 ), d2 = distance( c2 );
	for( int i = 0; i < 80; ++i )
	{
		if( d1 < d2 )
		{
			b  = c2;
			c2 = c1;
			d2 = d1;
			c1 = b - gr * ( b - a );
			d1 = distance( c1 );
		}
		else
		{
			a  = c1;
			c1 = c2;
			d1 = d2;
			c2 = a + gr * ( b - a );
			d2 = distance( c2 );
		}
	}
	return 0.5 * ( a + b );
}

//---------------------------------------------------------------------------
// The timeline.
//---------------------------------------------------------------------------
struct Ctl
{
	double fader = 1.0;
	double time  = 2.0;
	bool light   = false;///< Curve = Light
	bool low     = false;///< Lamp = Low
	bool glass   = false;
	double popMm = 0.4;
};

struct Out
{
	double g[ 2 ][ 3 ];///< snapped gains, shutter included
	double sigma[ 2 ]; ///< output pixels, 0 = none
	double T[ 2 ];
	double shutter[ 2 ];
	double heat[ 2 ];
	double position;
};

/// The steady picture luminance of the lamp at High against its drive, and
/// its inverse, from the harness's own model.
struct LightCurve
{
	double u0 = 0.0;
	std::vector< double > u, l;
	explicit LightCurve( int kN )
	{
		u0 = voltsForT( kPreheatK ) / kV;
		u.resize( kN );
		l.resize( kN );
		for( int i = 0; i < kN; ++i )
		{
			u[ i ] = u0 + ( 1.0 - u0 ) * i / ( kN - 1.0 );
			double g[ 3 ];
			gains( steadyT( u[ i ] * kV ), g );
			l[ i ] = lum709( g );
		}
	}
	double inverse( double light ) const
	{
		if( light <= l.front() )
			return u0;
		if( light >= l.back() )
			return 1.0;
		size_t lo = 0, hi = l.size() - 1;
		while( hi - lo > 1 )
		{
			const size_t mid = ( lo + hi ) / 2;
			( l[ mid ] <= light ? lo : hi ) = mid;
		}
		return u[ lo ] + ( light - l[ lo ] ) / ( l[ hi ] - l[ lo ] ) * ( u[ hi ] - u[ lo ] );
	}
};

/// The harness's curve: 20 001 points.
inline const LightCurve& lightCurve()
{
	static const LightCurve c( 20001 );
	return c;
}

/// The same construction at the plugin's spacing, 4097 points: for the
/// method error only.
inline const LightCurve& lightCurveCoarse()
{
	static const LightCurve c( 4097 );
	return c;
}

class Sim
{
public:
	bool rk4Method = false;///< the plugin's method: fixed RK4 at h <= 0.25 ms, a 4097-point light table

	void start( const Ctl& c, bool take )
	{
		ctl    = c;
		target = take ? 1.0 - c.fader : c.fader;
		pos    = target;
		for( int p = 0; p < 2; ++p )
		{
			T[ p ]       = steadyT( volts( p, pos ) );
			heat[ p ]    = std::min( 1.0, four( T[ p ] ) / four( tFull() ) );
			cycling[ p ] = false;
			age[ p ]     = 0.0;
		}
	}

	void apply( const Ctl& c, bool take )
	{
		ctl    = c;
		target = take ? 1.0 - c.fader : c.fader;
		if( c.time <= 0.0 )
			pos = target;
	}

	void change( int p )
	{
		if( !cycling[ p ] )
		{
			cycling[ p ] = true;
			age[ p ]     = 0.0;
		}
	}

	void advance( double dt )
	{
		dt = std::min( std::max( dt, 0.0 ), 0.25 );
		while( dt > 0.0 )
		{
			//The next point where the drive changes slope.
			double seg = dt;
			if( pos != target && ctl.time > 0.0 )
				seg = std::min( seg, std::fabs( target - pos ) * ctl.time );
			for( int p = 0; p < 2; ++p )
				if( cycling[ p ] )
					for( double edge : { 0.04, 0.5, 0.94, 1.0 } )
						if( edge > age[ p ] + 1e-15 )
						{
							seg = std::min( seg, edge - age[ p ] );
							break;
						}
			integrate( seg );
			//Advance the clocks onto the edge.
			if( pos != target && ctl.time > 0.0 )
			{
				const double step = seg / ctl.time;
				pos               = std::fabs( target - pos ) <= step * ( 1.0 + 1e-12 ) ? target : pos + ( target > pos ? step : -step );
			}
			for( int p = 0; p < 2; ++p )
				if( cycling[ p ] )
				{
					const double before = age[ p ];
					age[ p ] += seg;
					for( double edge : { 0.04, 0.5, 0.94, 1.0 } )
						if( std::fabs( age[ p ] - edge ) < 1e-12 )
							age[ p ] = edge;
					if( before < 0.5 && age[ p ] >= 0.5 )
						heat[ p ] = 0.0;
					if( age[ p ] >= 1.0 )
					{
						cycling[ p ] = false;
						age[ p ]     = 0.0;
					}
				}
			dt -= seg;
			if( dt < 1e-15 )
				break;
		}
	}

	Out now( int width ) const
	{
		Out o;
		o.position = pos;
		for( int p = 0; p < 2; ++p )
		{
			o.T[ p ]       = T[ p ];
			o.heat[ p ]    = heat[ p ];
			o.shutter[ p ] = shutter( p, age[ p ] );
			double g[ 3 ];
			gains( T[ p ], g );
			for( int c = 0; c < 3; ++c )
			{
				double v = g[ c ] * o.shutter[ p ];
				if( v < 1.0 / 16777216.0 )
					v = 0.0;
				else if( std::fabs( v - 1.0 ) < 1.0 / 16777216.0 )
					v = 1.0;
				o.g[ p ][ c ] = v;
			}
			const double s = ctl.glass ? 0.0 : ctl.popMm * std::fabs( 1.0 - heat[ p ] ) / ( 4.0 * 2.8 * 36.0 ) * width;
			o.sigma[ p ]   = s < 0.17 ? 0.0 : s;
		}
		return o;
	}

	double drive( int p ) const
	{
		return volts( p, pos ) / kV;
	}

private:
	static double four( double t )
	{
		return std::pow( t, 4 ) - std::pow( kT0, 4 );
	}

	double tFull() const
	{
		return ctl.low ? steadyTCached( true ) : kTr;
	}

	static double steadyTCached( bool low )
	{
		static const double tLow = steadyT( kLowVolts );
		return low ? tLow : kTr;
	}

	double volts( int p, double s ) const
	{
		const double c  = p == 1 ? s : 1.0 - s;
		const double v  = ctl.light ? ( rk4Method ? lightCurveCoarse() : lightCurve() ).inverse( c ) : c;
		const double sc = ctl.low ? kLowVolts / kV : 1.0;
		return kV * std::max( lightCurve().u0, v * sc );
	}

	double shutter( int p, double a ) const
	{
		if( !cycling[ p ] )
			return 1.0;
		if( a < 0.04 )
			return 1.0 - a / 0.04;
		if( a < 0.94 )
			return 0.0;
		return std::min( 1.0, ( a - 0.94 ) / 0.06 );
	}

	/// The state's derivative at tau into the current segment.
	void rhs( const double y[ 4 ], double tau, double out[ 4 ] ) const
	{
		double s = pos;
		if( pos != target && ctl.time > 0.0 )
			s = pos + ( target > pos ? 1.0 : -1.0 ) * tau / ctl.time;
		const double full = four( tFull() );
		for( int p = 0; p < 2; ++p )
		{
			out[ p ]          = dTdt( y[ p ], volts( p, s ) );
			const double a    = cycling[ p ] ? age[ p ] + tau : 0.0;
			const double q    = shutter( p, a ) * four( y[ p ] ) / full;
			out[ 2 + p ]      = ( q - y[ 2 + p ] ) / 3.0;
		}
	}

	void integrate( double length )
	{
		if( length <= 0.0 )
			return;
		double y[ 4 ] = { T[ 0 ], T[ 1 ], heat[ 0 ], heat[ 1 ] };
		if( rk4Method )
		{
			const int n    = std::max( 1, static_cast< int >( std::ceil( length / 0.00025 - 1e-9 ) ) );
			const double h = length / n;
			for( int i = 0; i < n; ++i )
			{
				double k1[ 4 ], k2[ 4 ], k3[ 4 ], k4[ 4 ], t[ 4 ];
				const double tau = h * i;
				rhs( y, tau, k1 );
				for( int j = 0; j < 4; ++j )
					t[ j ] = y[ j ] + 0.5 * h * k1[ j ];
				rhs( t, tau + 0.5 * h, k2 );
				for( int j = 0; j < 4; ++j )
					t[ j ] = y[ j ] + 0.5 * h * k2[ j ];
				rhs( t, tau + 0.5 * h, k3 );
				for( int j = 0; j < 4; ++j )
					t[ j ] = y[ j ] + h * k3[ j ];
				rhs( t, tau + h, k4 );
				for( int j = 0; j < 4; ++j )
					y[ j ] += h / 6.0 * ( k1[ j ] + 2 * k2[ j ] + 2 * k3[ j ] + k4[ j ] );
			}
		}
		else
			dormandPrince( y, length );
		T[ 0 ]    = y[ 0 ];
		T[ 1 ]    = y[ 1 ];
		heat[ 0 ] = y[ 2 ];
		heat[ 1 ] = y[ 3 ];
	}

	/// Dormand and Prince's 5(4) pair, adaptive, error per step against
	/// 1e-11 of each component's scale (kelvin; heat in units of 1).
	void dormandPrince( double y[ 4 ], double length ) const
	{
		static const double c2 = 1.0 / 5, c3 = 3.0 / 10, c4 = 4.0 / 5, c5 = 8.0 / 9;
		static const double a21 = 1.0 / 5;
		static const double a31 = 3.0 / 40, a32 = 9.0 / 40;
		static const double a41 = 44.0 / 45, a42 = -56.0 / 15, a43 = 32.0 / 9;
		static const double a51 = 19372.0 / 6561, a52 = -25360.0 / 2187, a53 = 64448.0 / 6561, a54 = -212.0 / 729;
		static const double a61 = 9017.0 / 3168, a62 = -355.0 / 33, a63 = 46732.0 / 5247, a64 = 49.0 / 176, a65 = -5103.0 / 18656;
		static const double b1 = 35.0 / 384, b3 = 500.0 / 1113, b4 = 125.0 / 192, b5 = -2187.0 / 6784, b6 = 11.0 / 84;
		static const double e1 = 71.0 / 57600, e3 = -71.0 / 16695, e4 = 71.0 / 1920, e5 = -17253.0 / 339200, e6 = 22.0 / 525, e7 = -1.0 / 40;
		double t = 0.0, h = std::min( length, 1e-4 );
		const double scale[ 4 ] = { 3000.0, 3000.0, 1.0, 1.0 };
		for( int guard = 0; t < length && guard < 2000000; ++guard )
		{
			if( t + h > length )
				h = length - t;
			double k1[ 4 ], k2[ 4 ], k3[ 4 ], k4[ 4 ], k5[ 4 ], k6[ 4 ], k7[ 4 ], s[ 4 ], y5[ 4 ];
			rhs( y, t, k1 );
			for( int j = 0; j < 4; ++j )
				s[ j ] = y[ j ] + h * a21 * k1[ j ];
			rhs( s, t + c2 * h, k2 );
			for( int j = 0; j < 4; ++j )
				s[ j ] = y[ j ] + h * ( a31 * k1[ j ] + a32 * k2[ j ] );
			rhs( s, t + c3 * h, k3 );
			for( int j = 0; j < 4; ++j )
				s[ j ] = y[ j ] + h * ( a41 * k1[ j ] + a42 * k2[ j ] + a43 * k3[ j ] );
			rhs( s, t + c4 * h, k4 );
			for( int j = 0; j < 4; ++j )
				s[ j ] = y[ j ] + h * ( a51 * k1[ j ] + a52 * k2[ j ] + a53 * k3[ j ] + a54 * k4[ j ] );
			rhs( s, t + c5 * h, k5 );
			for( int j = 0; j < 4; ++j )
				s[ j ] = y[ j ] + h * ( a61 * k1[ j ] + a62 * k2[ j ] + a63 * k3[ j ] + a64 * k4[ j ] + a65 * k5[ j ] );
			rhs( s, t + h, k6 );
			for( int j = 0; j < 4; ++j )
				y5[ j ] = y[ j ] + h * ( b1 * k1[ j ] + b3 * k3[ j ] + b4 * k4[ j ] + b5 * k5[ j ] + b6 * k6[ j ] );
			rhs( y5, t + h, k7 );
			double err = 0.0;
			for( int j = 0; j < 4; ++j )
			{
				const double e = h * ( e1 * k1[ j ] + e3 * k3[ j ] + e4 * k4[ j ] + e5 * k5[ j ] + e6 * k6[ j ] + e7 * k7[ j ] );
				err            = std::max( err, std::fabs( e ) / ( 1e-11 * scale[ j ] ) );
			}
			if( err <= 1.0 || h < 1e-9 )
			{
				t += h;
				for( int j = 0; j < 4; ++j )
					y[ j ] = y5[ j ];
			}
			const double factor = err > 0.0 ? 0.9 * std::pow( err, -0.2 ) : 4.0;
			h *= std::min( 4.0, std::max( 0.2, factor ) );
		}
	}

	Ctl ctl;
	double pos = 1.0, target = 1.0;
	double T[ 2 ]     = { kT0, kT0 };
	double heat[ 2 ]  = { 0.0, 0.0 };
	bool cycling[ 2 ] = { false, false };
	double age[ 2 ]   = { 0.0, 0.0 };
};

} // namespace ref
