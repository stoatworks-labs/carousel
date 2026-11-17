#include "Shaders.h"

namespace carousel
{

const char* const kVersion = R"(#version 410 core
)";

//---------------------------------------------------------------------------
// One vertex shader for both passes. It passes nothing but the position:
// both passes work from the fragment's integer position.
//---------------------------------------------------------------------------
const char* const kVertex = R"(
layout( location = 0 ) in vec4 vPosition;

void main()
{
	gl_Position = vPosition;
}
)";

//---------------------------------------------------------------------------
// The sRGB pair, IEC 61966-2-1:1999 (amended 2003). mix() with a boolean
// selector picks a component without arithmetic, so pow() of a value outside
// its branch never reaches the result (GLSL 4.10 8.3, mix with genBType).
//
// Then the slide: one texel as transmittance, and the slide resampled to an
// output pixel.
//---------------------------------------------------------------------------
const char* const kCommon = R"(
//The harness's negative controls (Model.h's Fault). 0 in the shipped plugin.
const int kFaultNoPremultiply = 8192;
const int kFaultGammaMix      = 32768;
const int kFaultSizeSwap      = 131072;

uniform int Fault;
uniform int Linear;    //1: transmittance in linear light
uniform ivec2 OutSize; //the output, pixels

vec3 decode( vec3 c )
{
	return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), greaterThan( c, vec3( 0.04045 ) ) );
}

vec3 encode( vec3 x )
{
	return mix( x * 12.92, 1.055 * pow( x, vec3( 1.0 / 2.4 ) ) - 0.055, greaterThan( x, vec3( 0.0031308 ) ) );
}

//One texel: rgb its transmittance, composited over black and decoded --
//premultiplied -- and a its alpha. At a texel position clamped to the
//slide's USED size: the padding is never read.
vec4 texelOf( sampler2D slide, ivec2 size, ivec2 at )
{
	vec4 t = texelFetch( slide, clamp( at, ivec2( 0 ), size - ivec2( 1 ) ), 0 );
	vec3 c = ( Fault & kFaultNoPremultiply ) != 0 ? t.rgb : t.rgb * t.a;
	return vec4( Linear != 0 ? decode( c ) : c, t.a );
}

//The slide at an output pixel: fetched when it is the output's size, else
//bilinear by hand over four premultiplied texels (the slide fills the frame).
vec4 slideAt( sampler2D slide, ivec2 size, ivec2 pixel )
{
	if( size == OutSize )
		return texelOf( slide, size, pixel );
	vec2 q  = ( vec2( pixel ) + 0.5 ) * vec2( size ) / vec2( OutSize ) - 0.5;
	vec2 q0 = floor( q );
	vec2 f  = q - q0;
	ivec2 i = ivec2( q0 );
	vec4 a  = texelOf( slide, size, i );
	vec4 b  = texelOf( slide, size, i + ivec2( 1, 0 ) );
	vec4 c  = texelOf( slide, size, i + ivec2( 0, 1 ) );
	vec4 d  = texelOf( slide, size, i + ivec2( 1, 1 ) );
	return mix( mix( a, b, f.x ), mix( c, d, f.x ), f.y );
}

//A weighted sum of texelOf()s, as the transmittance. Premultiplied, it is
//its rgb; the fault's straight sum is multiplied by its alpha afterwards.
vec3 resolve( vec4 v )
{
	return ( Fault & kFaultNoPremultiply ) != 0 ? v.rgb * v.a : v.rgb;
}
)";

//---------------------------------------------------------------------------
// 1. Blur: the horizontal half of the Gaussian, for one projector.
//---------------------------------------------------------------------------
const char* const kBlurFragment = R"(
uniform sampler2D Slide;
uniform ivec2 SlideSize;   //its used size
uniform int Radius;        //taps each side
uniform float Weight[ 49 ];//normalised, by |offset|

out vec4 Blurred;

void main()
{
	ivec2 p  = ivec2( floor( gl_FragCoord.xy ) );
	vec4 sum = vec4( 0.0 );
	for( int k = -Radius; k <= Radius; ++k )
	{
		int x = p.x + k;
		//Beyond the frame is the mount: opaque, black.
		if( x < 0 || x >= OutSize.x )
			continue;
		sum += Weight[ abs( k ) ] * slideAt( Slide, SlideSize, ivec2( x, p.y ) );
	}
	Blurred = sum;
}
)";

//---------------------------------------------------------------------------
// 2. Screen: both projectors' light, added.
//---------------------------------------------------------------------------
const char* const kScreenFragment = R"(
//inputTextures[0] -- Dest, the layer below: projector A.
uniform sampler2D SlideA;
//inputTextures[1] -- Src, this layer: projector B.
uniform sampler2D SlideB;
//Each projector's horizontally blurred slide, when it is out of focus.
uniform sampler2D BlurA;
uniform sampler2D BlurB;

uniform ivec2 SizeA;
uniform ivec2 SizeB;
uniform int BlurredA;
uniform int BlurredB;
uniform int RadiusA;
uniform int RadiusB;
uniform float WeightA[ 49 ];
uniform float WeightB[ 49 ];
uniform vec3 GainA;  //lamp colour and level x shutter, per channel
uniform vec3 GainB;
uniform int ExactA;  //1: A alone, at rated, in focus, at the output's size
uniform int ExactB;

out vec4 Colour;

vec4 verticalA( ivec2 p )
{
	vec4 sum = vec4( 0.0 );
	for( int k = -RadiusA; k <= RadiusA; ++k )
	{
		int y = p.y + k;
		if( y < 0 || y >= OutSize.y )
			continue;
		sum += WeightA[ abs( k ) ] * texelFetch( BlurA, ivec2( p.x, y ), 0 );
	}
	return sum;
}

vec4 verticalB( ivec2 p )
{
	vec4 sum = vec4( 0.0 );
	for( int k = -RadiusB; k <= RadiusB; ++k )
	{
		int y = p.y + k;
		if( y < 0 || y >= OutSize.y )
			continue;
		sum += WeightB[ abs( k ) ] * texelFetch( BlurB, ivec2( p.x, y ), 0 );
	}
	return sum;
}

void main()
{
	ivec2 p = ivec2( floor( gl_FragCoord.xy ) );

	//One projector alone at rated, in focus, at the output's size: its slide
	//over black, to the bit -- a branch and a fetch.
	if( ExactB != 0 )
	{
		vec4 t = texelFetch( SlideB, p, 0 );
		Colour = vec4( t.rgb * t.a, 1.0 );
		return;
	}
	if( ExactA != 0 )
	{
		vec4 t = texelFetch( SlideA, p, 0 );
		Colour = vec4( t.rgb * t.a, 1.0 );
		return;
	}

	ivec2 sizeB = ( Fault & kFaultSizeSwap ) != 0 ? SizeA : SizeB;
	vec3 a      = resolve( BlurredA != 0 ? verticalA( p ) : slideAt( SlideA, SizeA, p ) );
	vec3 b      = resolve( BlurredB != 0 ? verticalB( p ) : slideAt( SlideB, sizeB, p ) );

	//The screen: the two projectors' light, added, in linear light.
	bool gammaMix = Linear != 0 && ( Fault & kFaultGammaMix ) != 0;
	vec3 light    = gammaMix ? GainA * encode( a ) + GainB * encode( b ) : GainA * a + GainB * b;
	light         = clamp( light, 0.0, 1.0 );
	Colour        = vec4( Linear != 0 && !gammaMix ? encode( light ) : light, 1.0 );
}
)";

std::string AssembleFragment( const char* pass )
{
	return std::string( kVersion ) + kCommon + pass;
}

std::string AssembleVertex()
{
	return std::string( kVersion ) + kVertex;
}

} // namespace carousel
