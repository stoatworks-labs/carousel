#include "Lamp.h"

#include <algorithm>
#include <cmath>

namespace carousel::lamp
{

double Resistivity( double kelvin )
{
	//filament's rule: the segment index clamped to the table, the fraction
	//not, so the end segments extrapolate.
	const double x = ( kelvin - kRhoFirst ) / kRhoStep;
	const int i    = std::clamp( static_cast< int >( std::floor( x ) ), 0, kRhoCount - 2 );
	const double t = x - i;
	return kRho[ i ] + ( kRho[ i + 1 ] - kRho[ i ] ) * t;
}

double HeatCapacity( double kelvin )
{
	const double* a      = kelvin < kShomateSplit ? kShomateLow : kShomateHigh;
	const double t       = kelvin / 1000.0;
	const double perMole = a[ 0 ] + a[ 1 ] * t + a[ 2 ] * t * t + a[ 3 ] * t * t * t + a[ 4 ] / ( t * t );
	return perMole / kTungstenMolarMass;
}

namespace
{
double fourthPowerSpan()
{
	const double r2 = kRatedKelvin * kRatedKelvin, a2 = kAmbient * kAmbient;
	return r2 * r2 - a2 * a2;
}
} // namespace

Wire MakeWire()
{
	Wire wire;
	const double rhoRated = Resistivity( kRatedKelvin ) * 1e-8;//ohm m
	const double radiated = ( 1.0 - kConductionFraction ) * kWatts;
	const double fourth   = fourthPowerSpan();
	const double d3       = 4.0 * rhoRated * radiated * kWatts
	                  / ( kCoilRadiatingFraction * kEmissivity * kStefanBoltzmann * kPi * kPi * kVolts * kVolts * fourth );
	wire.diameter   = std::cbrt( d3 );
	wire.length     = radiated / ( kCoilRadiatingFraction * kEmissivity * kStefanBoltzmann * kPi * wire.diameter * fourth );
	wire.mass       = kTungstenDensity * kPi * wire.diameter * wire.diameter / 4.0 * wire.length;
	wire.wattsPerKg = kWatts / wire.mass;
	return wire;
}

double Derivative( const Wire& wire, double kelvin, double driveSquared, int perturb )
{
	const double T        = kelvin;
	const double rhoRated = Resistivity( kRatedKelvin );
	const double rho      = ( perturb & kPerturbConstantR ) ? rhoRated : Resistivity( T );
	const double in       = driveSquared * rhoRated / rho;

	double radiated;
	if( perturb & kPerturbLinearCool )
		radiated = ( 1.0 - kConductionFraction ) * ( T - kAmbient ) / ( kRatedKelvin - kAmbient );
	else
	{
		const double T2 = T * T, A2 = kAmbient * kAmbient;
		radiated = ( 1.0 - kConductionFraction ) * ( T2 * T2 - A2 * A2 ) / fourthPowerSpan();
	}
	const double conducted = kConductionFraction * ( T - kAmbient ) / ( kRatedKelvin - kAmbient );
	return wire.wattsPerKg * ( in - radiated - conducted ) / HeatCapacity( T );
}

double SteadyKelvin( double driveSquared, int perturb )
{
	//Power balance only: the heat capacity does not matter.
	if( driveSquared <= 0.0 )
		return kAmbient;
	const Wire wire = MakeWire();
	double lo = kAmbient, hi = kMeltingKelvin + 500.0;
	for( int i = 0; i < 200 && hi - lo > 0.0; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		if( mid <= lo || mid >= hi )
			break;
		if( Derivative( wire, mid, driveSquared, perturb ) > 0.0 )
			lo = mid;
		else
			hi = mid;
	}
	return 0.5 * ( lo + hi );
}

double DriveForKelvin( double kelvin, int perturb )
{
	double lo = 0.0, hi = 2.0;
	for( int i = 0; i < 200; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		if( mid <= lo || mid >= hi )
			break;
		if( SteadyKelvin( mid * mid, perturb ) < kelvin )
			lo = mid;
		else
			hi = mid;
	}
	return 0.5 * ( lo + hi );
}

} // namespace carousel::lamp
