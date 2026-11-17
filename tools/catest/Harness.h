#pragma once

/**
    catest's plumbing: checks, pictures, a headless GL context and the
    two-input rig with a synthetic clock. The harness's OWN model of the
    projectors is Reference.h; nothing here predicts anything.
*/

#include "Carousel.h"
#include "Controls.h"
#include "Model.h"
#include "Names.h"
#include "Shaders.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace carousel;

namespace
{
int failures = 0;
bool quiet   = false;

void Check( bool ok, const std::string& what )
{
	if( !quiet )
		std::printf( "  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str() );
	if( !ok )
		++failures;
}

/// A negative control: something the check must be able to REJECT. Passes
/// when `rejected` is true.
void Negative( bool rejected, const std::string& what )
{
	if( !quiet )
		std::printf( "  %s  negative control: %s\n", rejected ? "ok  " : "FAIL", what.c_str() );
	if( !rejected )
		++failures;
}

/// EVERY conversion must be a floating-point one.
__attribute__( ( format( printf, 1, 0 ) ) ) std::string fmt( const char* format, double a, double b = 0.0, double c = 0.0, double d = 0.0 )
{
	char buffer[ 320 ];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	std::snprintf( buffer, sizeof( buffer ), format, a, b, c, d );
#pragma clang diagnostic pop
	return buffer;
}

/// Run a check silently and return how many of its assertions failed,
/// leaving the global count as it was: how a negative control asks "did the
/// check reject this?".
template< typename F >
int failuresOf( F&& run )
{
	const int before = failures;
	const bool was   = quiet;
	quiet            = true;
	run();
	quiet            = was;
	const int caught = failures - before;
	failures         = before;
	return caught;
}

constexpr double kFloatUlp = 1.0 / 16777216.0;///< 2^-24, float32's unit roundoff at 1

/// One float ULP at the magnitude of x.
double ulpAt( double x )
{
	const double m = std::max( std::fabs( x ), 1e-30 );
	return std::ldexp( 1.0, static_cast< int >( std::floor( std::log2( m ) ) ) - 23 );
}

/// The sRGB pair's budget, absolute, on values in 0..1 -- a bound, derived in
/// AGENTS.md ("Tolerances") from GLSL 4.10 4.5.1: pow is exp2( y log2 x ),
/// exp2 good to ( 3 + 2|x| ) ULP and log2 to 3 ULP (absolute 2^-21 in
/// [0.5, 2]), + - * correctly rounded, / 2.5 ULP. decode's relative error is
/// at most 5.1e-6 (at its knee, where exp2's argument is largest) and
/// encode's absolute error at most 6.8e-7; encode scales a relative error in
/// its input by y encode'(y) = 0.44 y^(1/2.4), which is 0.04 at the knee, so
/// encode( decode( rgb / a ) L ) a is good to 1.9e-6 absolute over the whole
/// range (summed worst cases, evaluated on a grid on 2026-11-16). 4e-6 is
/// used; the tolerance is three times it.
constexpr double kPowBound = 4e-6;
constexpr double kPowTol   = 3.0 * kPowBound;

/// The sRGB pair, IEC 61966-2-1:1999 (amended 2003): the harness's own copy.
/// The plugin's (Controls.cpp) decodes the lamps' colours, so it must not
/// also be the yardstick -- a mutant there would move both sides at once.
double srgbToLinear( double v )
{
	if( v <= 0.04045 )
		return v / 12.92;
	return std::pow( ( v + 0.055 ) / 1.055, 2.4 );
}

double linearToSrgb( double v )
{
	if( v <= 0.0031308 )
		return v * 12.92;
	return 1.055 * std::pow( v, 1.0 / 2.4 ) - 0.055;
}

//---------------------------------------------------------------------------
// PNG writer. zlib ships with the OS. Rows BOTTOM-UP in, top-down out.
//---------------------------------------------------------------------------
using Image  = std::vector< unsigned char >;
using ImageF = std::vector< float >;

void putU32( std::vector< unsigned char >& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

bool writePng( const std::string& path, int width, int height, const Image& bottomUp )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		const unsigned char* row = bottomUp.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	std::vector< unsigned char > ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.push_back( 8 );
	ihdr.push_back( 6 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	FILE* file = fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = fwrite( png.data(), 1, png.size(), file );
	fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// Pictures. Bottom-up, GL's orientation: row 0 is the BOTTOM. RGBA,
// premultiplied (the fleet's assumption about what a host hands a mixer).
//---------------------------------------------------------------------------
struct Rgb
{
	unsigned char r, g, b;
};

unsigned char toByte( double v )
{
	return static_cast< unsigned char >( std::lround( std::clamp( v, 0.0, 1.0 ) * 255.0 ) );
}

constexpr Rgb kBlack    = { 0, 0, 0 };
constexpr Rgb kWhite    = { 255, 255, 255 };
constexpr Rgb kSentinel = { 0xFF, 0x00, 0xFF };///< padding: a colour that appears nowhere else
constexpr int kSentinelNear = 135;

size_t at4( int width, int x, int y )
{
	return ( static_cast< size_t >( y ) * width + x ) * 4;
}

void put( Image& image, int width, int x, int y, Rgb c, unsigned char a = 255 )
{
	const size_t at = at4( width, x, y );
	image[ at + 0 ] = c.r;
	image[ at + 1 ] = c.g;
	image[ at + 2 ] = c.b;
	image[ at + 3 ] = a;
}

Rgb pixelAt( const Image& image, int width, int x, int y )
{
	const size_t at = at4( width, x, y );
	return { image[ at + 0 ], image[ at + 1 ], image[ at + 2 ] };
}

int l1( Rgb a, Rgb b )
{
	return std::abs( a.r - b.r ) + std::abs( a.g - b.g ) + std::abs( a.b - b.b );
}

Image flatField( int width, int height, Rgb c, unsigned char alpha = 255 )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
			put( image, width, x, y, c, alpha );
	return image;
}

void hsv( double h, double s, double v, double& r, double& g, double& b )
{
	h           = h - std::floor( h );
	const int i = static_cast< int >( h * 6.0 ) % 6;
	const double f = h * 6.0 - std::floor( h * 6.0 );
	const double p = v * ( 1 - s ), q = v * ( 1 - f * s ), t = v * ( 1 - ( 1 - f ) * s );
	switch( i )
	{
	case 0: r = v, g = t, b = p; break;
	case 1: r = q, g = v, b = p; break;
	case 2: r = p, g = v, b = t; break;
	case 3: r = p, g = q, b = v; break;
	case 4: r = t, g = p, b = v; break;
	default: r = v, g = p, b = q; break;
	}
}

/// The screen: a clip, light enough that a shadow on it reads. Hue across,
/// a soft brightness wave down, a grid of thin lines.
float hashUnit( uint32_t n )
{
	uint32_t state = n * 747796405u + 2891336453u;
	uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	word           = ( word >> 22u ) ^ word;
	return static_cast< float >( word >> 8 ) / 16777216.0f;
}

/// Random premultiplied RGBA floats: rgb <= a, a anywhere in 0..1.
bool softwareRenderer()
{
	const char* renderer = std::getenv( "CATEST_RENDERER" );
	return renderer != nullptr && std::strcmp( renderer, "software" ) == 0;
}

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	//CATEST_RENDERER=software asks for Apple's software renderer by id, on a
	//Mac that has a GPU: what a GPU-less runner falls back to.
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	if( softwareRenderer() )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		std::fprintf( stderr, "catest: CATEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
		if( CGLChoosePixelFormat( software, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

GLuint makeTexture( int width, int height, bool floatFormat, const void* pixels )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	if( floatFormat )
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	else
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

struct InputSpec
{
	int usedW = 0, usedH = 0;
	int hwW = 0, hwH = 0;
	static InputSpec Exact( int w, int h )
	{
		return { w, h, w, h };
	}
	static InputSpec Padded( int w, int h, int hw, int hh )
	{
		return { w, h, hw, hh };
	}
};


/// The parameter called `name`, or -1.
int paramId( Carousel& plugin, const std::string& name )
{
	for( unsigned int i = 0; i < Carousel::PT_ABOUT_FIRST; ++i )
	{
		const char* declared = plugin.GetParamName( i );
		if( declared != nullptr && name == declared )
			return static_cast< int >( i );
	}
	return -1;
}

/// Set a parameter by display name, in host units.
bool setByName( Carousel& plugin, const std::string& name, float value )
{
	const int id = paramId( plugin, name );
	if( id < 0 )
	{
		std::fprintf( stderr, "no parameter called '%s'\n", name.c_str() );
		return false;
	}
	plugin.SetFloatParameter( static_cast< unsigned >( id ), value );
	return true;
}

/// The output framebuffer, its texture, and reading it back.
struct Target
{
	int width = 0, height = 0;
	GLuint texture = 0, fbo = 0;

	void Init( int w, int h, bool floatOutput )
	{
		width   = w;
		height  = h;
		texture = makeTexture( w, h, floatOutput, nullptr );
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
	}

	void Begin()
	{
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, width, height );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
	}

	Image Pixels()
	{
		Image pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		return pixels;
	}

	ImageF PixelsF()
	{
		ImageF pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}

	void Release()
	{
		if( fbo )
			glDeleteFramebuffers( 1, &fbo );
		if( texture )
			glDeleteTextures( 1, &texture );
		fbo = texture = 0;
	}
};

//---------------------------------------------------------------------------
// The mixer's rig: SW Carousel, TWO input textures and an output, each at its
// own size, and a synthetic clock. inputTextures[0] is Dest (projector A),
// [1] is Src (projector B). Padding is the sentinel, opaque.
//
// The clock: frame k is at host time start + k / fps seconds, handed over in
// the host's unit (milliseconds unless told otherwise), with the unit
// DECLARED to the plugin (Clock::SetScaleForTest) -- a harness renders far
// faster than real time, so the plugin's own vote would read seconds as
// milliseconds. `--clock` checks the vote separately, with a wall clock it
// controls.
//---------------------------------------------------------------------------
struct MixRig
{
	Carousel plugin;
	Target out;
	InputSpec aSpec, bSpec;
	bool aFloat = false;
	GLuint aTexture = 0, bTexture = 0;
	FFGLTextureStruct aStruct = {}, bStruct = {};
	FFGLTextureStruct* inputs[ 2 ] = { nullptr, nullptr };
	ProcessOpenGLStruct process    = {};
	bool ready                     = false;

	double fps          = 60.0;
	double startSeconds = 0.0;
	double unitScale    = 0.001;///< host unit to seconds
	bool declareUnit    = true;
	bool wallFollows    = false;///< feed the plugin a wall clock that keeps pace with the host's
	int frame           = 0;
	Rgb padding         = kSentinel;///< the inputs' hardware padding, opaque

	bool Init( int outW, int outH, InputSpec a, InputSpec b, bool floatOutput = true, int fault = 0, const char* fragment = nullptr,
	           bool floatA = false )
	{
		plugin.SetFaultForTest( fault );
		plugin.SetFragmentShaderForTest( fragment );
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( outW );
		viewport.height             = static_cast< FFUInt32 >( outH );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see the diagnostics log for the shader\n" );
			return false;
		}
		aFloat = floatA;
		MakeInputs( a, b );
		out.Init( outW, outH, floatOutput );
		process.numInputTextures = 2;
		process.inputTextures    = inputs;
		process.HostFBO          = out.fbo;
		ready                    = true;
		return true;
	}

	bool Init( int outW, int outH, bool floatOutput = true, int fault = 0, const char* fragment = nullptr )
	{
		return Init( outW, outH, InputSpec::Exact( outW, outH ), InputSpec::Exact( outW, outH ), floatOutput, fault, fragment );
	}

	void MakeInputs( InputSpec a, InputSpec b )
	{
		if( aTexture )
			glDeleteTextures( 1, &aTexture );
		if( bTexture )
			glDeleteTextures( 1, &bTexture );
		aSpec = a;
		bSpec = b;
		if( aFloat )
		{
			ImageF pad( static_cast< size_t >( a.hwW ) * a.hwH * 4 );
			for( size_t i = 0; i < pad.size(); i += 4 )
				pad[ i ] = pad[ i + 2 ] = pad[ i + 3 ] = 1.0f;
			aTexture = makeTexture( a.hwW, a.hwH, true, pad.data() );
		}
		else
			aTexture = makeTexture( a.hwW, a.hwH, false, flatField( a.hwW, a.hwH, padding ).data() );
		bTexture  = makeTexture( b.hwW, b.hwH, false, flatField( b.hwW, b.hwH, padding ).data() );
		auto fill = []( FFGLTextureStruct& t, const InputSpec& s, GLuint handle ) {
			t.Width          = static_cast< FFUInt32 >( s.usedW );
			t.Height         = static_cast< FFUInt32 >( s.usedH );
			t.HardwareWidth  = static_cast< FFUInt32 >( s.hwW );
			t.HardwareHeight = static_cast< FFUInt32 >( s.hwH );
			t.Handle         = handle;
		};
		fill( aStruct, a, aTexture );
		fill( bStruct, b, bTexture );
		inputs[ 0 ] = &aStruct;
		inputs[ 1 ] = &bStruct;
	}

	/// A new output size, as a host's FF_RESIZE.
	void ResizeOutput( int outW, int outH, bool floatOutput = true )
	{
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( outW );
		viewport.height             = static_cast< FFUInt32 >( outH );
		plugin.Resize( &viewport );
		out.Release();
		out.Init( outW, outH, floatOutput );
		process.HostFBO = out.fbo;
	}

	void UploadA( const Image& bottomUp )
	{
		glBindTexture( GL_TEXTURE_2D, aTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, aSpec.usedW, aSpec.usedH, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void UploadAF( const ImageF& bottomUp )
	{
		glBindTexture( GL_TEXTURE_2D, aTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, aSpec.usedW, aSpec.usedH, GL_RGBA, GL_FLOAT, bottomUp.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void UploadB( const Image& bottomUp )
	{
		glBindTexture( GL_TEXTURE_2D, bTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, bSpec.usedW, bSpec.usedH, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	bool Set( const std::string& name, float value )
	{
		return setByName( plugin, name, value );
	}

	/// A button's press: the event parameter to 1 now; Release() before the
	/// next press. The plugin acts on the rising edge, at the next frame.
	void Press( const std::string& name )
	{
		Set( name, 1.0f );
	}
	void Release( const std::string& name )
	{
		Set( name, 0.0f );
	}

	/// The host time of frame k, in seconds.
	double SecondsOf( int k ) const
	{
		return startSeconds + k / fps;
	}

	/// Render the next frame at its clock time.
	bool Step()
	{
		if( frame == 0 && declareUnit )
			plugin.ClockForTest().SetScaleForTest( unitScale );
		const double seconds = SecondsOf( frame );
		if( wallFollows )
			plugin.ClockForTest().SetWallForTest( seconds - startSeconds + 1.0 );
		plugin.SetTime( seconds / unitScale );
		out.Begin();
		const bool ok = plugin.ProcessOpenGL( &process ) == FF_SUCCESS;
		++frame;
		return ok;
	}

	/// Render the current instant again (dt = 0: the state does not move).
	bool Again()
	{
		plugin.SetTime( SecondsOf( frame - 1 ) / unitScale );
		out.Begin();
		return plugin.ProcessOpenGL( &process ) == FF_SUCCESS;
	}

	FFResult RenderBroken( int numInputs, int nullIndex, bool zeroSize = false, bool zeroViewport = false )
	{
		FFGLTextureStruct* saved[ 2 ]  = { inputs[ 0 ], inputs[ 1 ] };
		const FFGLTextureStruct savedA = aStruct;
		if( nullIndex >= 0 && nullIndex < 2 )
			inputs[ nullIndex ] = nullptr;
		if( nullIndex == -2 )
			process.inputTextures = nullptr;
		if( zeroSize )
			aStruct.HardwareWidth = 0;
		FFGLViewportStruct viewport = {};
		if( zeroViewport )
			plugin.Resize( &viewport );
		process.numInputTextures = static_cast< FFUInt32 >( numInputs );
		out.Begin();
		const FFResult result = plugin.ProcessOpenGL( &process );
		if( zeroViewport )
		{
			viewport.width  = static_cast< FFUInt32 >( out.width );
			viewport.height = static_cast< FFUInt32 >( out.height );
			plugin.Resize( &viewport );
		}
		inputs[ 0 ]              = saved[ 0 ];
		inputs[ 1 ]              = saved[ 1 ];
		aStruct                  = savedA;
		process.inputTextures    = inputs;
		process.numInputTextures = 2;
		return result;
	}

	Image Pixels()
	{
		return out.Pixels();
	}

	ImageF PixelsF()
	{
		return out.PixelsF();
	}

	~MixRig()
	{
		if( !ready )
			return;
		plugin.DeInitGL();
		out.Release();
		glDeleteTextures( 1, &aTexture );
		glDeleteTextures( 1, &bTexture );
	}
};

size_t differingBytes( const Image& a, const Image& b )
{
	if( a.size() != b.size() )
		return std::max( a.size(), b.size() );
	size_t n = 0;
	for( size_t i = 0; i < a.size(); ++i )
		n += a[ i ] != b[ i ];
	return n;
}

size_t differingFloats( const ImageF& a, const ImageF& b )
{
	if( a.size() != b.size() )
		return std::max( a.size(), b.size() );
	size_t n = 0;
	for( size_t i = 0; i < a.size(); ++i )
		n += std::memcmp( &a[ i ], &b[ i ], sizeof( float ) ) != 0;
	return n;
}

/// The worst of a set, NaN-propagating: std::max drops a NaN, and a NaN in
/// a measurement must fail the check, never vanish from it.
double worse( double a, double b )
{
	if( std::isnan( a ) || std::isnan( b ) )
		return std::numeric_limits< double >::quiet_NaN();
	return a > b ? a : b;
}

//---------------------------------------------------------------------------
// Cards. Each has colour, covers the frame, uses exact 8-bit codes, and A's
// and B's differ.
//
// The split card: three vertical bands. Band 0 is lit on A only, band 1 on
// B only, band 2 on both, so a projector's gain is read where the other's
// slide is black, and band 2 is the sum.
//---------------------------------------------------------------------------
constexpr Rgb kSplitA[ 3 ] = { { 232, 168, 96 }, { 0, 0, 0 }, { 150, 170, 120 } };
constexpr Rgb kSplitB[ 3 ] = { { 0, 0, 0 }, { 96, 176, 224 }, { 120, 160, 180 } };

int bandOf( int x, int width )
{
	return std::min( 2, x * 3 / width );
}

Image splitCard( int width, int height, bool forB )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
			put( image, width, x, y, ( forB ? kSplitB : kSplitA )[ bandOf( x, width ) ] );
	return image;
}

/// The edge card: a vertical edge at the middle column, two colours, for the
/// blur's line spread. A gets its own (distinct) pair.
constexpr Rgb kEdgeB[ 2 ] = { { 40, 60, 90 }, { 220, 200, 140 } };
constexpr Rgb kEdgeA[ 2 ] = { { 180, 60, 40 }, { 60, 140, 60 } };

Image edgeCard( int width, int height, bool forB )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
			put( image, width, x, y, ( forB ? kEdgeB : kEdgeA )[ x >= width / 2 ? 1 : 0 ] );
	return image;
}

/// A picture with detail everywhere and some alpha: hue across, value down,
/// a grid, a half-transparent band and a fully transparent window with
/// garbage under it.
Image slideCard( int width, int height, bool forB, bool withAlpha = false )
{
	Image image( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			double r, g, b;
			hsv( ( forB ? 0.55 : 0.05 ) + u * 0.5, 0.55, 0.45 + 0.4 * v, r, g, b );
			const bool line = ( x % std::max( 8, width / 12 ) ) == 0 || ( y % std::max( 8, height / 7 ) ) == 0;
			if( line )
				r = g = b = 0.9;
			unsigned char a = 255;
			if( withAlpha )
			{
				if( v > 0.62 && v < 0.8 )
					a = 128;
				if( u > 0.6 && u < 0.9 && v > 0.15 && v < 0.45 )
				{
					a = 0;
					r = hashUnit( static_cast< uint32_t >( x * 7 + y * 131 ) );
					g = 0.3;
					b = 1.0 - r;
				}
			}
			put( image, width, x, y, { toByte( r ), toByte( g ), toByte( b ) }, a );
		}
	return image;
}

Image generate( const std::string& name, int width, int height, bool forB )
{
	if( name == "split" )
		return splitCard( width, height, forB );
	if( name == "edge" )
		return edgeCard( width, height, forB );
	if( name == "slide" )
		return slideCard( width, height, forB );
	if( name == "slide-alpha" )
		return slideCard( width, height, forB, true );
	if( name == "black" )
		return flatField( width, height, kBlack );
	if( name == "white" )
		return flatField( width, height, kWhite );
	return flatField( width, height, { 0x80, 0x80, 0x80 } );//"grey"
}

const char* const kCardNames = "slide | slide-alpha | split | edge | black | white | grey";

bool isCard( const std::string& name )
{
	static const char* names[] = { "slide", "slide-alpha", "split", "edge", "black", "white", "grey" };
	for( const char* n : names )
		if( name == n )
			return true;
	return false;
}

} // namespace
