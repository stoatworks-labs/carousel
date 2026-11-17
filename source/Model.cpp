#include "Model.h"

#include <algorithm>
#include <cmath>

namespace carousel
{
namespace
{
constexpr double kPhaseEnd[ 4 ] = { kShutterClose, kSlideDrop, kCycleSeconds - kShutterOpen, kCycleSeconds };

/// The steady light curve at High: the picture luminance of the lamp's gain
/// at the steady temperature for drive u, on a grid uniform in u from the
/// preheat floor to 1. Built once per process (about 4000 bisections and
/// Planck sums), shared by every instance. Linear interpolation between
/// 4097 points: the curve's second derivative bounds the error at 1e-7 of
/// the light near rated (AGENTS.md).
struct LightTable
{
	static constexpr int kPoints = 4097;
	std::vector< double > u, light;

	LightTable()
	{
		const double u0 = lamp::DriveForKelvin( lamp::kPreheatKelvin, 0 );
		u.resize( kPoints );
		light.resize( kPoints );
		for( int i = 0; i < kPoints; ++i )
		{
			u[ i ]     = u0 + ( 1.0 - u0 ) * i / ( kPoints - 1 );
			light[ i ] = colour::PictureLuminance( colour::Gain( lamp::SteadyKelvin( u[ i ] * u[ i ], 0 ) ) );
		}
		light[ kPoints - 1 ] = 1.0;
	}

	double Invert( double l ) const
	{
		if( l <= light.front() )
			return u.front();
		if( l >= 1.0 )
			return 1.0;
		const auto it = std::upper_bound( light.begin(), light.end(), l );
		const size_t i = static_cast< size_t >( std::distance( light.begin(), it ) ) - 1;
		const double t = ( l - light[ i ] ) / ( light[ i + 1 ] - light[ i ] );
		return u[ i ] + t * ( u[ i + 1 ] - u[ i ] );
	}
};

const LightTable& lightTable()
{
	static const LightTable table;
	return table;
}

int lampPerturb( int fault )
{
	return ( ( fault & kFaultConstantR ) ? lamp::kPerturbConstantR : 0 ) | ( ( fault & kFaultLinearCool ) ? lamp::kPerturbLinearCool : 0 );
}

int colourPerturb( int fault )
{
	return ( ( fault & kFaultNoWhiteBalance ) ? colour::kPerturbNoWhiteBalance : 0 )
	       | ( ( fault & kFaultColourHot ) ? colour::kPerturbColourHot : 0 );
}

double fourthAbove( double kelvin )
{
	const double t2 = kelvin * kelvin, a2 = lamp::kAmbient * lamp::kAmbient;
	return t2 * t2 - a2 * a2;
}

double snap( double g )
{
	if( g < kGainSnap )
		return 0.0;
	if( std::fabs( g - 1.0 ) < kGainSnap )
		return 1.0;
	return g;
}
} // namespace

Model::Model()
{
	wire              = lamp::MakeWire();
	preheatU          = lightTable().u.front();
	fullKelvin[ 0 ]   = lamp::SteadyKelvin( 1.0, 0 );
	fullKelvin[ 1 ]   = lamp::SteadyKelvin( lamp::kLowFraction * lamp::kLowFraction, 0 );
}

double Model::DriveForLight( double light ) const
{
	return lightTable().Invert( light );
}

double Model::driveFor( int projector, double s ) const
{
	const double c     = projector == 1 ? s : 1.0 - s;
	const bool shaped  = applied.curve == CurveKind::Light && ( fault & kFaultDissolveCurve ) == 0;
	const double v     = shaped ? lightTable().Invert( c ) : c;
	const double scale = applied.lamp == LampKind::Low ? lamp::kLowFraction : 1.0;
	return std::max( preheatU, v * scale );
}

double Model::fullFourth() const
{
	return fourthAbove( fullKelvin[ applied.lamp == LampKind::Low ? 1 : 0 ] );
}

double Model::shutterAt( int projector, double a ) const
{
	if( !cycling[ projector ] )
		return 1.0;
	switch( phase[ projector ] )
	{
	case 0: return std::clamp( 1.0 - a / kShutterClose, 0.0, 1.0 );
	case 1:
	case 2: return 0.0;
	case 3: return std::clamp( ( a - ( kCycleSeconds - kShutterOpen ) ) / kShutterOpen, 0.0, 1.0 );
	default: return 1.0;
	}
}

void Model::Start( const Asked& asked, bool takeLatched )
{
	applied  = asked;
	take     = takeLatched;
	target   = take ? 1.0 - applied.fader : applied.fader;
	position = target;
	const int perturb = lampPerturb( fault );
	for( int p = 0; p < 2; ++p )
	{
		const double u = driveFor( p, position );
		kelvin[ p ]    = lamp::SteadyKelvin( u * u, perturb );
		heat[ p ]      = std::min( 1.0, fourthAbove( kelvin[ p ] ) / fullFourth() );
		cycling[ p ]   = false;
		phase[ p ]     = 0;
		age[ p ]       = 0.0;
	}
	started = true;
}

void Model::Apply( const Asked& asked, bool takeLatched )
{
	applied = asked;
	take    = takeLatched;
	target  = take ? 1.0 - applied.fader : applied.fader;
	//At Time 0 the unit cuts, here, at this frame's instant. The filaments
	//cannot follow it within the instant: the frame shows the same light.
	if( applied.time <= 0.0 && ( fault & kFaultHardCutRamp ) == 0 )
		position = target;
	if( fault & kFaultNoLag )
	{
		const int perturb = lampPerturb( fault );
		for( int p = 0; p < 2; ++p )
		{
			const double u = driveFor( p, position );
			kelvin[ p ]    = lamp::SteadyKelvin( u * u, perturb );
		}
	}
}

void Model::Change( int projector )
{
	if( projector < 0 || projector > 1 || cycling[ projector ] )
		return;
	cycling[ projector ] = true;
	phase[ projector ]   = 0;
	age[ projector ]     = 0.0;
}

void Model::derivatives( const double y[ 4 ], double tau, double out[ 4 ] ) const
{
	const double s      = segPosition + segSlew * tau;
	const int perturb   = lampPerturb( fault );
	const double tauPop = ( fault & kFaultPopTau ) ? 1.0 : kPopTau;
	const double full   = fullFourth();
	for( int p = 0; p < 2; ++p )
	{
		const double u = driveFor( p, s );
		out[ p ]       = ( fault & kFaultNoLag ) ? 0.0 : segLampRate * lamp::Derivative( wire, y[ p ], u * u, perturb );
		const double q = shutterAt( p, segAge[ p ] + segCycleRate * tau ) * fourthAbove( y[ p ] ) / full;
		out[ 2 + p ]   = ( q - y[ 2 + p ] ) / tauPop;
	}
}

void Model::Advance( double dt )
{
	if( !started )
		return;
	dt = std::clamp( dt, 0.0, kMaxFrame );
	if( dt <= 0.0 )
		return;

	//The frame-counting faults: each component's clock runs at 1/60 s per
	//frame, whatever the frame's length.
	const double perFrame = ( 1.0 / 60.0 ) / dt;
	segLampRate           = ( fault & kFaultFrameLag ) ? perFrame : 1.0;
	segCycleRate          = ( fault & kFaultShutterLong ) ? perFrame : 1.0;
	double slewRate       = 0.0;
	if( position != target )
	{
		if( applied.time > 0.0 )
			slewRate = 1.0 / applied.time;
		else
			slewRate = std::fabs( target - position ) / dt;//only under kFaultHardCutRamp
		if( fault & kFaultSlewFrames )
			slewRate *= perFrame;
	}

	double remaining = dt;
	for( int guard = 0; remaining > 0.0 && guard < 64; ++guard )
	{
		//The segment: up to the next point where the drive changes slope.
		double seg      = remaining;
		int limiter     = -1;//-1 none, 0/1 a projector's phase end, 2 the slew's end
		const double gap = std::fabs( target - position );
		if( gap > 0.0 && slewRate > 0.0 && gap / slewRate <= seg )
		{
			seg     = gap / slewRate;
			limiter = 2;
		}
		for( int p = 0; p < 2; ++p )
			if( cycling[ p ] )
			{
				const double toEnd = ( kPhaseEnd[ phase[ p ] ] - age[ p ] ) / segCycleRate;
				if( toEnd <= seg )
				{
					seg     = std::max( toEnd, 0.0 );
					limiter = p;
				}
			}

		segPosition = position;
		segSlew     = gap > 0.0 ? ( target > position ? slewRate : -slewRate ) : 0.0;
		segAge[ 0 ] = age[ 0 ];
		segAge[ 1 ] = age[ 1 ];

		if( seg > 0.0 )
		{
			const int n    = std::max( 1, static_cast< int >( std::ceil( seg / lamp::kMaxStep - 1e-9 ) ) );
			const double h = seg / n;
			double y[ 4 ]  = { kelvin[ 0 ], kelvin[ 1 ], heat[ 0 ], heat[ 1 ] };
			for( int i = 0; i < n; ++i )
			{
				const double t0 = h * i;
				double k1[ 4 ], k2[ 4 ], k3[ 4 ], k4[ 4 ], tmp[ 4 ];
				derivatives( y, t0, k1 );
				for( int j = 0; j < 4; ++j )
					tmp[ j ] = y[ j ] + 0.5 * h * k1[ j ];
				derivatives( tmp, t0 + 0.5 * h, k2 );
				for( int j = 0; j < 4; ++j )
					tmp[ j ] = y[ j ] + 0.5 * h * k2[ j ];
				derivatives( tmp, t0 + 0.5 * h, k3 );
				for( int j = 0; j < 4; ++j )
					tmp[ j ] = y[ j ] + h * k3[ j ];
				derivatives( tmp, t0 + h, k4 );
				for( int j = 0; j < 4; ++j )
					y[ j ] += h / 6.0 * ( k1[ j ] + 2.0 * k2[ j ] + 2.0 * k3[ j ] + k4[ j ] );
				if( fault & kFaultNoLag )
				{
					const double s = segPosition + segSlew * ( t0 + h );
					for( int p = 0; p < 2; ++p )
					{
						const double u = driveFor( p, s );
						y[ p ]         = lamp::SteadyKelvin( u * u, lampPerturb( fault ) );
					}
				}
			}
			kelvin[ 0 ] = y[ 0 ];
			kelvin[ 1 ] = y[ 1 ];
			heat[ 0 ]   = y[ 2 ];
			heat[ 1 ]   = y[ 3 ];
		}

		//Move the clocks to the segment's end, exactly onto the limiting point.
		if( limiter == 2 )
			position = target;
		else if( gap > 0.0 )
		{
			position += segSlew * seg;
			if( ( segSlew > 0.0 && position > target ) || ( segSlew < 0.0 && position < target ) )
				position = target;
		}
		for( int p = 0; p < 2; ++p )
		{
			if( !cycling[ p ] )
				continue;
			if( limiter == p )
			{
				age[ p ] = kPhaseEnd[ phase[ p ] ];
				if( phase[ p ] == 1 && ( fault & kFaultPopNoReset ) == 0 )
					heat[ p ] = 0.0;//a cold slide from the tray
				if( ++phase[ p ] > 3 )
				{
					cycling[ p ] = false;
					phase[ p ]   = 0;
					age[ p ]     = 0.0;
				}
			}
			else
				age[ p ] += segCycleRate * seg;
		}
		remaining -= seg;
		if( limiter < 0 )
			break;
	}
}

Instant Model::Now( int outWidth ) const
{
	Instant now;
	now.position      = position;
	now.target        = target;
	const int perturb = colourPerturb( fault );
	for( int p = 0; p < 2; ++p )
	{
		ProjectorNow& o = now.p[ p ];
		o.kelvin        = kelvin[ p ];
		o.drive         = driveFor( p, position );
		o.shutter       = shutterAt( p, age[ p ] );
		o.heat          = heat[ p ];
		o.cycling       = cycling[ p ];
		const colour::Rgb g = colour::Gain( kelvin[ p ], perturb );
		o.gain          = { snap( g.r * o.shutter ), snap( g.g * o.shutter ), snap( g.b * o.shutter ) };
		if( applied.mounts == MountKind::Card )
		{
			const double sigma = applied.popMm * std::fabs( 1.0 - heat[ p ] ) / ( 4.0 * kFNumber * kSlideMm ) * outWidth;
			o.sigmaPx          = sigma < kBlurFloor ? 0.0 : sigma;
		}
	}
	return now;
}

} // namespace carousel
