/*
    The page's way in to the plugin: what an FFGL host does to a mixer
    instance, as C functions JavaScript can call.

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Carousel.cpp` and everything it calls, compiled
    UNMODIFIED into the same WebAssembly module as this file (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs a `Carousel`, as the SDK's factory does for a host
        (Carousel.cpp's CFFGLPluginInfo), and calls InitGL, Resize and
        DeInitGL on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTime, and
        ProcessOpenGL with TWO input textures, Dest (inputTextures[0], the
        layer below: projector A) and Src (inputTextures[1], this layer:
        projector B), as tools/catest's MixRig does.

    The read-outs (ca_convert) call Controls.cpp's own conversions, the ones
    Carousel.cpp's asked() applies, and the line under the picture reads the
    plugin's own state through the accessors its harness reads
    (InstantForTest(), ClockForTest()): the instant the last frame rendered.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the four entry points that cannot be passed
    straight through.
*/
#include "Carousel.h"
#include "Colour.h"
#include "Controls.h"
#include "Diag.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

using namespace carousel;

namespace
{
/// One plugin instance and the host-side structs it is handed.
struct Instance
{
	Carousel plugin;
	bool initialised = false;

	FFGLTextureStruct dest {};///< inputTextures[ 0 ]: the layer below, projector A
	FFGLTextureStruct src {}; ///< inputTextures[ 1 ]: this layer, projector B
	FFGLTextureStruct* inputs[ 2 ] = { &dest, &src };
	ProcessOpenGLStruct process {};

	std::string scratch;///< a string handed back to JS lives here until the next call
	double instant[ 23 ] = {};
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

void describe( FFGLTextureStruct& t, int handle, int width, int height )
{
	//The page's textures are unpadded: the hardware size is the used size.
	t.Width = t.HardwareWidth = static_cast< FFUInt32 >( width );
	t.Height = t.HardwareHeight = static_cast< FFUInt32 >( height );
	t.Handle                    = static_cast< GLuint >( handle );
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE Instance* ca_new()
{
	return new Instance();
}

EMSCRIPTEN_KEEPALIVE void ca_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int ca_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

/// FF_RESIZE: a new output size, as a host sends it when the composition's
/// changes. The plugin keeps its state (catest --resize).
EMSCRIPTEN_KEEPALIVE void ca_resize( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width  = static_cast< GLuint >( width );
	viewport.height = static_cast< GLuint >( height );
	instance->plugin.Resize( &viewport );
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ca_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* ca_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int ca_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ca_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float ca_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int ca_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ca_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float ca_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE int ca_min_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMinInputs() );
}

EMSCRIPTEN_KEEPALIVE int ca_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ca_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float ca_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

/// One frame: SetTime, then ProcessOpenGL into the framebuffer `hostFbo` (0:
/// the page's canvas) with Dest and Src, each a texture the page registered
/// with emscripten's GL tables, at its own size. 1 on success.
EMSCRIPTEN_KEEPALIVE int ca_process( Instance* instance, double hostTime, int dest, int destWidth, int destHeight, int src, int srcWidth,
                                     int srcHeight, int hostFbo )
{
	instance->plugin.SetTime( hostTime );
	describe( instance->dest, dest, destWidth, destHeight );
	describe( instance->src, src, srcWidth, srcHeight );
	instance->process.numInputTextures = 2;
	instance->process.inputTextures    = instance->inputs;
	instance->process.HostFBO          = static_cast< GLuint >( hostFbo );
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

/// The harness's hook, for demo/tools/crosscheck ONLY: declares the host
/// clock's unit as catest's MixRig does (Clock::SetScaleForTest), so a page
/// frame and a harness frame see the same intervals. The page never calls
/// it: there the plugin's own vote decides, as in a host.
EMSCRIPTEN_KEEPALIVE void ca_clock_scale_for_test( Instance* instance, double scale )
{
	instance->plugin.ClockForTest().SetScaleForTest( scale );
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one Carousel.cpp's asked() applies to it. NaN for anything with no
// conversion (a boolean, an option's index, a button).
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double ca_convert( int index, float v )
{
	switch( index )
	{
	case Carousel::PT_OPACITY: return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
	case Carousel::PT_TIME: return TimeFromParam( v );
	case Carousel::PT_POP: return PopFromParam( v );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// The inverse, for the page's presets, which are written in seconds and
/// millimetres: Controls.cpp's ParamForTime / ParamForPop. NaN for anything
/// else.
EMSCRIPTEN_KEEPALIVE double ca_param_from( int index, double x )
{
	switch( index )
	{
	case Carousel::PT_TIME: return ParamForTime( x );
	case Carousel::PT_POP: return ParamForPop( x );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

//---------------------------------------------------------------------------
// What the line under the picture reports: the instant the plugin's last
// frame rendered (Carousel::InstantForTest, Model::Now), as 23 doubles:
//
//   0 position   the dissolve unit's, 0 = A, 1 = B
//   1 target     where it is going (Opacity, or 1 - Opacity after an odd
//                number of Takes)
//   2 clock      the host clock's unit as the plugin settled it: 1 seconds,
//                0.001 ms, 0 not yet
//   3 + 10 p ... projector p (0 = A, 1 = B): kelvin, drive (V / V_rated),
//                shutter (1 open), the slide's heat, the blur's sigma in
//                output pixels, cycling (0 / 1), the gain's r, g, b, and the
//                gain's picture luminance (colour::PictureLuminance: BT.709
//                weights on linear sRGB, 1 = the rated lamp, shutter open)
//
// The pointer stays valid until the instance is deleted.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE const double* ca_instant( Instance* instance )
{
	const Instant& now = instance->plugin.InstantForTest();
	double* out        = instance->instant;
	out[ 0 ]           = now.position;
	out[ 1 ]           = now.target;
	out[ 2 ]           = instance->plugin.ClockForTest().Scale();
	for( int p = 0; p < 2; ++p )
	{
		const ProjectorNow& o = now.p[ p ];
		double* q             = out + 3 + 10 * p;
		q[ 0 ]                = o.kelvin;
		q[ 1 ]                = o.drive;
		q[ 2 ]                = o.shutter;
		q[ 3 ]                = o.heat;
		q[ 4 ]                = o.sigmaPx;
		q[ 5 ]                = o.cycling ? 1.0 : 0.0;
		q[ 6 ]                = o.gain.r;
		q[ 7 ]                = o.gain.g;
		q[ 8 ]                = o.gain.b;
		q[ 9 ]                = colour::PictureLuminance( o.gain );
	}
	return out;
}

/// Give the plugin's log (Diag.cpp) somewhere to go: a directory in the
/// page's in-memory file system, named through the override Diag.cpp reads.
/// Call before the first instance, which is when the log opens.
EMSCRIPTEN_KEEPALIVE void ca_prepare_log()
{
	setenv( "CAROUSEL_LOG_DIR", "/carousel/logs", 1 );
	mkdir( "/carousel", 0755 );
	mkdir( "/carousel/logs", 0755 );
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* ca_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

} // extern "C"
