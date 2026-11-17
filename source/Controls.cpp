#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace carousel
{
namespace
{
double unit( float value )
{
	return std::clamp( static_cast< double >( value ), 0.0, 1.0 );
}

float toParam( double p )
{
	return static_cast< float >( std::clamp( p, 0.0, 1.0 ) );
}
} // namespace

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

double TimeFromParam( float value )
{
	const double p = unit( value );
	return kTimeMax * p * p;
}

float ParamForTime( double seconds )
{
	return toParam( std::sqrt( std::max( seconds, 0.0 ) / kTimeMax ) );
}

double PopFromParam( float value )
{
	return kPopMaxMm * unit( value );
}

float ParamForPop( double mm )
{
	return toParam( mm / kPopMaxMm );
}

double DecodeSrgb( double c )
{
	return c <= 0.04045 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.4 );
}

double EncodeSrgb( double x )
{
	return x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow( x, 1.0 / 2.4 ) - 0.055;
}

} // namespace carousel
