#include "Colour.h"

#include "Cmf.h"
#include "Lamp.h"

#include <cmath>

namespace carousel::colour
{
namespace
{
//2019 SI: exact by definition.
constexpr double kPlanck    = 6.62607015e-34;
constexpr double kLight     = 299792458.0;
constexpr double kBoltzmann = 1.380649e-23;

//IEC 61966-2-1:1999, XYZ to linear sRGB, to the four decimals it states.
constexpr double kXyzToRgb[ 3 ][ 3 ] = {
	{ 3.2406, -1.5372, -0.4986 },
	{ -0.9689, 1.8758, 0.0415 },
	{ 0.0557, -0.2040, 1.0570 },
};

Rgb rated()
{
	static const Rgb r = PlanckRgb( lamp::kRatedKelvin );
	return r;
}
} // namespace

Rgb PlanckRgb( double kelvin )
{
	double xyz[ 3 ] = { 0.0, 0.0, 0.0 };
	if( kelvin > 0.0 )
	{
		const double c1 = 2.0 * kPlanck * kLight * kLight;
		const double c2 = kPlanck * kLight / kBoltzmann;
		for( int i = 0; i < cmf::kCount; ++i )
		{
			const double lambda = ( cmf::kFirstNm + i ) * 1e-9;
			const double l5     = lambda * lambda * lambda * lambda * lambda;
			const double B      = c1 / ( l5 * std::expm1( c2 / ( lambda * kelvin ) ) );
			for( int c = 0; c < 3; ++c )
				xyz[ c ] += cmf::kXyz[ i ][ c ] * B;
		}
	}
	Rgb out;
	out.r = kXyzToRgb[ 0 ][ 0 ] * xyz[ 0 ] + kXyzToRgb[ 0 ][ 1 ] * xyz[ 1 ] + kXyzToRgb[ 0 ][ 2 ] * xyz[ 2 ];
	out.g = kXyzToRgb[ 1 ][ 0 ] * xyz[ 0 ] + kXyzToRgb[ 1 ][ 1 ] * xyz[ 1 ] + kXyzToRgb[ 1 ][ 2 ] * xyz[ 2 ];
	out.b = kXyzToRgb[ 2 ][ 0 ] * xyz[ 0 ] + kXyzToRgb[ 2 ][ 1 ] * xyz[ 1 ] + kXyzToRgb[ 2 ][ 2 ] * xyz[ 2 ];
	return out;
}

double PictureLuminance( const Rgb& g )
{
	return 0.2126 * g.r + 0.7152 * g.g + 0.0722 * g.b;
}

Rgb Gain( double kelvin, int perturb )
{
	const Rgb p = PlanckRgb( kelvin );
	const Rgb w = rated();
	if( perturb & kPerturbNoWhiteBalance )
	{
		//Normalised to the rated lamp's luminance, not per channel.
		const double y = PictureLuminance( w );
		return { p.r / y, p.g / y, p.b / y };
	}
	Rgb g = { p.r / w.r, p.g / w.g, p.b / w.b };
	if( perturb & kPerturbColourHot )
	{
		const double y = PictureLuminance( g );
		g              = { y, y, y };
	}
	return g;
}

} // namespace carousel::colour
