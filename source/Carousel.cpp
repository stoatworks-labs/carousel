#include "Carousel.h"

#include "Controls.h"
#include "Diag.h"
#include "GLState.h"
#include "Names.h"
#include "Shaders.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace ffglex;
using namespace carousel;

//---------------------------------------------------------------------------
// The eighth argument is the plugin TYPE, and it is the only thing in the
// whole repo that makes this a mixer rather than an effect. The input count
// is a SEPARATE declaration, in the constructor. See genlock's AGENTS.md.
//---------------------------------------------------------------------------
static CFFGLPluginInfo PluginInfo(
	PluginFactory< Carousel >,// Create method
	names::kMixerId,          // Plugin unique ID of maximum length 4.
	names::kMixerName,        // Plugin name
	2,                        // API major version number
	1,                        // API minor version number
	0,                        // Plugin major version number
	1,                        // Plugin minor version number
	FF_MIXER,                 // Plugin type
	"Two Kodak slide projectors on a dissolve unit. The layer below is projector A's slide, this layer projector B's. Each lamp is a 300 W tungsten filament that follows the dissolve unit's voltage through its thermal lag, so a dissolve sags and turns amber in the middle and even a cut dips through a warm half-second. Change A and Change B run a one-second slide change: the shutter's dark gap, then a cold card-mounted slide drifting into focus as it warms. The layer's opacity is the dissolve fader; Take dissolves to the other projector in Time.\n\nThis is a MIXER: set it as projector B's layer blend mode, over projector A.",
	"Carousel FFGL mixer" );

static_assert( Carousel::PT_COUNT - Carousel::PT_ABOUT_FIRST == stoatworks::about::kParamCount,
               "the About block's size changed with the generated header" );

namespace
{
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

std::string sizeText( const FFGLTextureStruct& t )
{
	return std::to_string( t.Width ) + "x" + std::to_string( t.Height ) + " of " + std::to_string( t.HardwareWidth ) + "x"
	       + std::to_string( t.HardwareHeight );
}

/// How many Opacity changes the log records, per instance: a whole layer
/// transition frame by frame, not a line a frame for a whole show.
constexpr int kOpacityLines = 400;

/// Instances are numbered in creation order, so a host session's log can tell
/// the layer's mixer from a transition's (Arena makes separate instances).
int gInstances = 0;

bool compile( FFGLShader& shader, const std::string& fragment, const char* what )
{
	if( shader.Compile( AssembleVertex(), fragment ) )
		return true;
	//Returning FF_FAIL from InitGL is invisible to the operator: the mixer
	//simply does nothing in Resolume. These lines are the only record.
	diag::error( std::string( "the " ) + what + " shader failed to compile - the mixer will do nothing" );
	FFGLLog::LogToHost( ( std::string( "Carousel: " ) + what + " shader failed to compile" ).c_str() );
	return false;
}

/// The Gaussian's half-kernel, normalised over -radius .. radius.
int gaussian( double sigma, float weights[ kMaxTaps + 1 ] )
{
	const int radius = std::min( kMaxTaps, static_cast< int >( std::ceil( 4.0 * sigma ) ) );
	double w[ kMaxTaps + 1 ] = {};
	double sum               = 0.0;
	for( int k = 0; k <= radius; ++k )
	{
		w[ k ] = std::exp( -0.5 * k * k / ( sigma * sigma ) );
		sum += k == 0 ? w[ k ] : 2.0 * w[ k ];
	}
	for( int k = 0; k <= kMaxTaps; ++k )
		weights[ k ] = k <= radius ? static_cast< float >( w[ k ] / sum ) : 0.0f;
	return radius;
}

bool isExactly( const colour::Rgb& g, double v )
{
	return g.r == v && g.g == v && g.b == v;
}
} // namespace

Carousel::Carousel()
{
	//A mixer takes exactly two. This is a separate declaration from the type
	//above, read by the host through different function codes entirely.
	SetMinInputs( 2 );
	SetMaxInputs( 2 );
	//Without this the host never calls SetTime, and every lamp would run on
	//the wall clock.
	SetTimeSupported( true );

	//---------------------------------------------------------------------
	// Defaults. SetParamInfof reads each one back out of GetFloatParameter,
	// so these assignments are what the host is told the defaults are.
	//---------------------------------------------------------------------
	params[ PT_LINEAR_LIGHT ] = 1.0f;
	params[ PT_OPACITY ]      = 1.0f;
	params[ PT_TAKE ]         = 0.0f;
	params[ PT_TIME ]         = ParamForTime( kTimeDefault );
	params[ PT_CURVE ]        = static_cast< float >( CurveKind::Voltage );
	params[ PT_LAMP ]         = static_cast< float >( LampKind::High );
	params[ PT_MOUNTS ]       = static_cast< float >( MountKind::Card );
	params[ PT_POP ]          = ParamForPop( kPopDefaultMm );
	params[ PT_CHANGE_A ]     = 0.0f;
	params[ PT_CHANGE_B ]     = 0.0f;

	//FIRST, deliberately: Arena hides a mixer's parameter 0.
	SetParamInfo( PT_LINEAR_LIGHT, "Linear Light", FF_TYPE_BOOLEAN, true );

	//Named Opacity, and the name is load-bearing: Resolume binds a mixer
	//parameter called Opacity to the LAYER's opacity fader, and a layer
	//transition ramps it (measured on genlock, wipe and relay in Arena
	//7.27.1). So the layer fader is the dissolve fader.
	SetParamInfof( PT_OPACITY, "Opacity", FF_TYPE_STANDARD );
	SetParamInfo( PT_TAKE, "Take", FF_TYPE_EVENT, false );
	SetParamInfof( PT_TIME, "Time", FF_TYPE_STANDARD );
	SetOptionParamInfo( PT_CURVE, "Curve", static_cast< unsigned >( CurveKind::Count ), params[ PT_CURVE ] );
	SetParamElementInfo( PT_CURVE, 0, "Voltage", 0.0f );
	SetParamElementInfo( PT_CURVE, 1, "Light", 1.0f );

	SetOptionParamInfo( PT_LAMP, "Lamp", static_cast< unsigned >( LampKind::Count ), params[ PT_LAMP ] );
	SetParamElementInfo( PT_LAMP, 0, "High", 0.0f );
	SetParamElementInfo( PT_LAMP, 1, "Low", 1.0f );

	SetOptionParamInfo( PT_MOUNTS, "Mounts", static_cast< unsigned >( MountKind::Count ), params[ PT_MOUNTS ] );
	SetParamElementInfo( PT_MOUNTS, 0, "Card", 0.0f );
	SetParamElementInfo( PT_MOUNTS, 1, "Glass", 1.0f );
	SetParamInfof( PT_POP, "Pop", FF_TYPE_STANDARD );
	SetParamInfo( PT_CHANGE_A, "Change A", FF_TYPE_EVENT, false );
	SetParamInfo( PT_CHANGE_B, "Change B", FF_TYPE_EVENT, false );

	// Groups, the way Resolume shows them: each group one contiguous run.
	SetParamGroup( PT_LINEAR_LIGHT, "Screen" );
	for( FFUInt32 i = PT_OPACITY; i <= PT_CURVE; ++i )
		SetParamGroup( i, "Dissolve" );
	SetParamGroup( PT_LAMP, "Lamps" );
	for( FFUInt32 i = PT_MOUNTS; i <= PT_CHANGE_B; ++i )
		SetParamGroup( i, "Slides" );

	// The About block. Declared inline rather than through a helper, because
	// SetParamInfo is protected on CFFGLPlugin.
	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_FIRST; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );

	FFGLLog::LogToHost( "Created Carousel mixer" );

	diag::init();
	instance = ++gInstances;
}

void Carousel::SetFaultForTest( int mask )
{
	fault = mask;
	model.SetFault( mask );
	clock.SetFloatForTest( ( mask & kFaultFloatClock ) != 0 );
}

//---------------------------------------------------------------------------
FFResult Carousel::InitGL( const FFGLViewportStruct* vp )
{
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	const std::string screen = fragmentOverride ? std::string( fragmentOverride ) : AssembleFragment( kScreenFragment );
	if( !compile( blurShader, AssembleFragment( kBlurFragment ), "blur" ) || !compile( screenShader, screen, "screen" ) )
	{
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	diag::info( "initialised, viewport " + std::to_string( vp ? vp->width : 0 ) + "x" + std::to_string( vp ? vp->height : 0 )
	            + " (instance #" + std::to_string( instance ) + ")" );

	//Use base-class init as the success result so it retains the viewport.
	return CFFGLPlugin::InitGL( vp );
}

void Carousel::LogGuard( unsigned bit, const char* what )
{
	if( guardsLogged & bit )
		return;
	guardsLogged |= bit;
	diag::warn( std::string( "guard: called with " ) + what + " -- returned FF_FAIL (logged once)" );
}

Asked Carousel::asked() const
{
	Asked a;
	a.linear = params[ PT_LINEAR_LIGHT ] > 0.5f;
	a.fader  = std::clamp( static_cast< double >( params[ PT_OPACITY ] ), 0.0, 1.0 );
	a.time   = TimeFromParam( params[ PT_TIME ] );
	a.curve  = static_cast< CurveKind >( OptionIndex( params[ PT_CURVE ], static_cast< int >( CurveKind::Count ) ) );
	a.lamp   = static_cast< LampKind >( OptionIndex( params[ PT_LAMP ], static_cast< int >( LampKind::Count ) ) );
	a.mounts = static_cast< MountKind >( OptionIndex( params[ PT_MOUNTS ], static_cast< int >( MountKind::Count ) ) );
	a.popMm  = PopFromParam( params[ PT_POP ] );
	return a;
}

//---------------------------------------------------------------------------
FFResult Carousel::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	//The SDK's Add example guards on the input count and on each pointer: a
	//host may call a mixer with one input while the operator is patching.
	//oxbow, an effect host, hands it one input: refused here.
	if( pGL == nullptr || pGL->inputTextures == nullptr )
	{
		LogGuard( 1u, "no input array" );
		return FF_FAIL;
	}
	if( pGL->numInputTextures < 2 )
	{
		LogGuard( 2u, ( std::to_string( pGL->numInputTextures ) + " input(s)" ).c_str() );
		return FF_FAIL;
	}
	if( pGL->inputTextures[ 0 ] == nullptr || pGL->inputTextures[ 1 ] == nullptr )
	{
		LogGuard( 4u, "a null input" );
		return FF_FAIL;
	}

	const FFGLTextureStruct& dest = *pGL->inputTextures[ 0 ];//the layer below: projector A
	const FFGLTextureStruct& src  = *pGL->inputTextures[ 1 ];//this layer: projector B
	if( dest.Width == 0 || dest.Height == 0 || dest.HardwareWidth == 0 || dest.HardwareHeight == 0 || src.Width == 0 || src.Height == 0
	    || src.HardwareWidth == 0 || src.HardwareHeight == 0 )
	{
		LogGuard( 8u, "a zero-sized input" );
		return FF_FAIL;
	}

	const int outW = static_cast< int >( currentViewport.width );
	const int outH = static_cast< int >( currentViewport.height );
	if( outW <= 0 || outH <= 0 )
	{
		LogGuard( 16u, "a zero viewport" );
		return FF_FAIL;
	}
	if( !screenShader.IsReady() || !blurShader.IsReady() )
		return FF_FAIL;

	++frames;
	if( frames == 1 )
		diag::info( "first frame: Dest " + sizeText( dest ) + ", Src " + sizeText( src ) + ", out " + std::to_string( outW ) + "x"
		            + std::to_string( outH ) );
	if( params[ PT_OPACITY ] != loggedOpacity && opacityLines < kOpacityLines )
	{
		diag::info( "Opacity " + std::to_string( params[ PT_OPACITY ] ) + " at frame " + std::to_string( frames ) + " (instance #"
		            + std::to_string( instance ) + ")" );
		loggedOpacity = params[ PT_OPACITY ];
		++opacityLines;
	}

	//-----------------------------------------------------------------
	// The projectors, on the CPU, in double: integrate up to this frame's
	// instant under the last frame's controls, then take this frame's.
	//-----------------------------------------------------------------
	const double dt = clock.Tick();
	const Asked now = asked();
	if( !model.Started() )
		model.Start( now, takeLatch );
	else
	{
		model.Advance( dt );
		if( ( fault & kFaultResizeResets ) && ( outW != lastOutW || outH != lastOutH ) )
			model.Start( now, takeLatch );
		model.Apply( now, takeLatch );
	}
	lastOutW = outW;
	lastOutH = outH;
	for( int p = 0; p < 2; ++p )
		if( pendingChange[ p ] )
		{
			model.Change( p );
			pendingChange[ p ] = false;
		}
	lastInstant = model.Now( outW );
	const Instant& inst = lastInstant;

	const int sizeA[ 2 ] = { static_cast< int >( dest.Width ), static_cast< int >( dest.Height ) };
	const int sizeB[ 2 ] = { static_cast< int >( src.Width ), static_cast< int >( src.Height ) };
	const FFGLTextureStruct* slides[ 2 ] = { &dest, &src };

	//Everything below changes GL state; all of it goes back on the way out,
	//whichever way out it is.
	ScopedGLState state;
	glDisable( GL_BLEND );

	//The blur buffers, before anything is bound for the frame. A dark
	//projector's blur would be multiplied by zero: skip it.
	bool blurred[ 2 ]               = { false, false };
	int radius[ 2 ]                 = { 0, 0 };
	float weights[ 2 ][ kMaxTaps + 1 ] = {};
	for( int p = 0; p < 2; ++p )
	{
		const ProjectorNow& o = inst.p[ p ];
		if( o.sigmaPx <= 0.0 || isExactly( o.gain, 0.0 ) )
			continue;
		if( !blur[ p ].Ensure( outW, outH ) )
		{
			LogGuard( 32u, ( "a blur buffer of " + std::to_string( outW ) + "x" + std::to_string( outH ) + " the GL would not make" ).c_str() );
			glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
			return FF_FAIL;
		}
		blurred[ p ] = true;
		radius[ p ]  = gaussian( o.sigmaPx, weights[ p ] );
	}

	//-----------------------------------------------------------------
	// 1. The horizontal half of each out-of-focus slide's blur.
	//-----------------------------------------------------------------
	if( blurred[ 0 ] || blurred[ 1 ] )
	{
		glUseProgram( blurShader.GetGLID() );
		blurShader.Set( "Slide", 0 );
		blurShader.Set( "Fault", fault );
		blurShader.Set( "Linear", now.linear ? 1 : 0 );
		glUniform2i( blurShader.FindUniform( "OutSize" ), outW, outH );
		for( int p = 0; p < 2; ++p )
		{
			if( !blurred[ p ] )
				continue;
			bindUnit( 0, slides[ p ]->Handle );
			const int* size = p == 0 ? sizeA : sizeB;
			glUniform2i( blurShader.FindUniform( "SlideSize" ), size[ 0 ], size[ 1 ] );
			blurShader.Set( "Radius", radius[ p ] );
			glUniform1fv( blurShader.FindUniform( "Weight" ), kMaxTaps + 1, weights[ p ] );
			glBindFramebuffer( GL_FRAMEBUFFER, blur[ p ].Framebuffer() );
			glViewport( 0, 0, outW, outH );
			quad.Draw();
		}
	}

	//-----------------------------------------------------------------
	// 2. The screen, into the host's framebuffer at the host's viewport.
	// ScopedFBOBinding is not used: it does not restore the viewport.
	//-----------------------------------------------------------------
	glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
	glViewport( state.saved.viewport[ 0 ], state.saved.viewport[ 1 ], state.saved.viewport[ 2 ], state.saved.viewport[ 3 ] );
	glUseProgram( screenShader.GetGLID() );
	bindUnit( 0, dest.Handle );
	bindUnit( 1, src.Handle );
	//Every declared sampler gets a texture, blurred or not: Apple's driver
	//warns "texture unloadable" for a sampler with nothing bound.
	bindUnit( 2, blurred[ 0 ] ? blur[ 0 ].Texture() : dest.Handle );
	bindUnit( 3, blurred[ 1 ] ? blur[ 1 ].Texture() : src.Handle );
	screenShader.Set( "SlideA", 0 );
	screenShader.Set( "SlideB", 1 );
	screenShader.Set( "BlurA", 2 );
	screenShader.Set( "BlurB", 3 );
	screenShader.Set( "Fault", fault );
	screenShader.Set( "Linear", now.linear ? 1 : 0 );
	glUniform2i( screenShader.FindUniform( "OutSize" ), outW, outH );
	glUniform2i( screenShader.FindUniform( "SizeA" ), sizeA[ 0 ], sizeA[ 1 ] );
	glUniform2i( screenShader.FindUniform( "SizeB" ), sizeB[ 0 ], sizeB[ 1 ] );
	screenShader.Set( "BlurredA", blurred[ 0 ] ? 1 : 0 );
	screenShader.Set( "BlurredB", blurred[ 1 ] ? 1 : 0 );
	screenShader.Set( "RadiusA", radius[ 0 ] );
	screenShader.Set( "RadiusB", radius[ 1 ] );
	glUniform1fv( screenShader.FindUniform( "WeightA" ), kMaxTaps + 1, weights[ 0 ] );
	glUniform1fv( screenShader.FindUniform( "WeightB" ), kMaxTaps + 1, weights[ 1 ] );
	const colour::Rgb& gA = inst.p[ 0 ].gain;
	const colour::Rgb& gB = inst.p[ 1 ].gain;
	screenShader.Set( "GainA", static_cast< float >( gA.r ), static_cast< float >( gA.g ), static_cast< float >( gA.b ) );
	screenShader.Set( "GainB", static_cast< float >( gB.r ), static_cast< float >( gB.g ), static_cast< float >( gB.b ) );
	const bool exactB = isExactly( gB, 1.0 ) && isExactly( gA, 0.0 ) && !blurred[ 1 ] && sizeB[ 0 ] == outW && sizeB[ 1 ] == outH;
	const bool exactA = isExactly( gA, 1.0 ) && isExactly( gB, 0.0 ) && !blurred[ 0 ] && sizeA[ 0 ] == outW && sizeA[ 1 ] == outH;
	screenShader.Set( "ExactA", exactA ? 1 : 0 );
	screenShader.Set( "ExactB", exactB ? 1 : 0 );

	quad.Draw();

	//Leave nothing bound: after DeInitGL a blur buffer left on a unit is a
	//deleted texture in the host's context.
	if( fault & kFaultLeaveBound )
		glActiveTexture( GL_TEXTURE0 );
	else
		unbindTextureUnits( 4 );
	glUseProgram( 0 );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult Carousel::DeInitGL()
{
	if( frames > 0 )
		diag::info( "DeInitGL after " + std::to_string( frames ) + " frames, last Opacity " + std::to_string( params[ PT_OPACITY ] )
		            + " (instance #" + std::to_string( instance ) + ")" );
	blurShader.FreeGLResources();
	screenShader.FreeGLResources();
	quad.Release();
	blur[ 0 ].Destroy();
	blur[ 1 ].Destroy();
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult Carousel::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( index - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;

	//The buttons act on the press, the rising edge; the release is ignored
	//(relay's latch). Each acts at the next frame's instant.
	const int button = index == PT_TAKE ? 0 : index == PT_CHANGE_A ? 1 : index == PT_CHANGE_B ? 2 : -1;
	if( button >= 0 )
	{
		const bool down = value > 0.5f;
		if( down && !held[ button ] )
		{
			if( button == 0 )
				takeLatch = !takeLatch;
			else
				pendingChange[ button - 1 ] = true;
		}
		held[ button ] = down;
	}

	params[ index ] = value;
	return FF_SUCCESS;
}

float Carousel::GetFloatParameter( unsigned int index )
{
	if( index >= PT_COUNT )
		return 0.0f;

	return params[ index ];
}

FFResult Carousel::SetTime( double time )
{
	clock.Observe( time );
	return CFFGLPlugin::SetTime( time );
}

//---------------------------------------------------------------------------
char* Carousel::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_FIRST )
	{
		aboutText = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutText.c_str() );
	}

	return CFFGLPlugin::GetTextParameter( index );
}

FFResult Carousel::SetTextParameter( unsigned int index, const char* value )
{
	// See the declaration: the base class fails, and a failed default deletes
	// the instance. The About line is display-only, so there is genuinely
	// nothing to store -- but it has to say so successfully.
	if( index == PT_ABOUT_FIRST )
		return FF_SUCCESS;

	return CFFGLPlugin::SetTextParameter( index, value );
}

//---------------------------------------------------------------------------
void Carousel::SetHostInfo( const char* hostname, const char* version )
{
	CFFGLPlugin::SetHostInfo( hostname, version );
	diag::info( std::string( "host=" ) + ( hostname ? hostname : "?" ) + " version=" + ( version ? version : "?" ) + " loaded from "
	            + diag::modulePath() );
}
