/**
    catest -- render SW Carousel offline, and measure the projectors.

    The mixer gets TWO inputs: `inputTextures[0]` is Dest, the layer below --
    projector A (`--input-a`); `[1]` is Src, this layer -- projector B
    (`--input-b`). They may be different sizes, with different hardware
    padding, rendered to a third.

        catest --out /tmp/f.png      a picture, through the real plugin
        catest --list                every parameter, for the sweep
        catest --names               names, the sacrificial index 0, Opacity, the buttons
        catest --mixer               the guards; two inputs, two sizes; padding never read
        catest --unchanged           at rest, one projector alone is its slide, to the bit
        catest --lamp                each lamp's light, frame by frame, against the ODE
        catest --colour              the colour temperature recovered from the picture
        catest --dissolve            the sag, the warm middle, the Light curve, the ramp's length
        catest --cut                 Time 0: the cut frame unchanged, then the filaments' lag
        catest --change              the shutter's cycle, frame by frame
        catest --pop                 the blur measured from an edge; tau fitted from the picture
        catest --alpha               transparent is black film; premultiplied before any blur
        catest --linear              Linear Light adds light; off adds code values
        catest --resize              a resize keeps the lamps, the dissolve and the drift
        catest --rates               24, 59.94, 60 and 144 fps agree with continuous time
        catest --clock               any host clock origin, ms or s; the unit vote
        catest --mutation            one character of the shipped GLSL fails a check
        catest --bench               ms/frame at 720p, 1080p and 4K
        catest --pipe                raw frames in, raw frames out (two inputs)

    CATEST_RENDERER=software renders on Apple's software renderer.

    Every check renders through the REAL plugin class in a headless CGL
    context on a synthetic clock and measures the property out of the
    picture -- a gain is the output's decoded level over the slide's, where
    the other slide is black; a blur is an edge's second moment; a colour
    temperature is a chromaticity inverted on the Planckian locus -- against
    the harness's own model (Reference.h), at 640x360 and 320x180, with
    negative controls that build the wrong answer into the real plugin
    (`SetFaultForTest`). Every tolerance is derived from a stated error
    source; AGENTS.md lists them.
*/

#include "Harness.h"
#include "Reference.h"

#include <limits>

namespace
{

struct Raster
{
	int w, h;
};
constexpr Raster kRasters[ 2 ] = { { 640, 360 }, { 320, 180 } };

template< typename F >
void atBothRasters( F&& check, std::initializer_list< std::pair< int, const char* > > faults )
{
	for( const Raster& r : kRasters )
	{
		std::printf( "\n %dx%d\n", r.w, r.h );
		check( r.w, r.h, kFaultNone );
		for( const auto& f : faults )
		{
			const int caught = failuresOf( [ & ] { check( r.w, r.h, f.first ); } );
			Negative( caught > 0, std::string( f.second ) + fmt( " fails %.0f assertion(s)", caught ) );
		}
	}
}

/// A non-round clock origin, so nothing lines up with a round second.
constexpr double kStart = 1.2345678;

//---------------------------------------------------------------------------
// The lockstep: the real plugin and the harness's model, driven by the same
// cues at the same instants. The model reads back what each control really
// got (a slider's float) through Controls, never its predictions.
//---------------------------------------------------------------------------
struct Lockstep
{
	MixRig& rig;
	ref::Sim sim, simRk4;
	ref::Ctl ctl;
	bool take    = false;
	bool started = false;
	bool pending[ 2 ] = { false, false };
	std::vector< std::string > pressed;

	explicit Lockstep( MixRig& r ) : rig( r )
	{
		simRk4.rk4Method = true;
	}

	void readBack()
	{
		const Asked a = rig.plugin.AskedForTest();
		ctl.fader     = a.fader;
		ctl.time      = a.time;
		ctl.light     = a.curve == CurveKind::Light;
		ctl.low       = a.lamp == LampKind::Low;
		ctl.glass     = a.mounts == MountKind::Glass;
		ctl.popMm     = a.popMm;
	}

	void set( const std::string& name, float value )
	{
		rig.Set( name, value );
		readBack();
	}

	void press( const std::string& name )
	{
		rig.Press( name );
		pressed.push_back( name );
		if( name == "Take" )
			take = !take;
		else if( name == "Change A" )
			pending[ 0 ] = true;
		else if( name == "Change B" )
			pending[ 1 ] = true;
	}

	bool frame()
	{
		readBack();
		const int k   = rig.frame;
		const bool ok = rig.Step();
		for( ref::Sim* s : { &sim, &simRk4 } )
		{
			if( !started )
				s->start( ctl, take );
			else
			{
				//The plugin's dt: consecutive host readings, times the unit.
				const double dt = ( rig.SecondsOf( k ) / rig.unitScale - rig.SecondsOf( k - 1 ) / rig.unitScale ) * rig.unitScale;
				s->advance( dt );
				s->apply( ctl, take );
			}
			for( int p = 0; p < 2; ++p )
				if( pending[ p ] )
					s->change( p );
		}
		started = true;
		pending[ 0 ] = pending[ 1 ] = false;
		for( const std::string& n : pressed )
			rig.Release( n );
		pressed.clear();
		return ok;
	}

	ref::Out expect() const
	{
		return sim.now( rig.out.width );
	}

	/// The plugin's method error at this instant, per projector and channel:
	/// the harness's RK4 at the plugin's step against its Dormand-Prince.
	double methodError( int p, int c ) const
	{
		return std::fabs( sim.now( rig.out.width ).g[ p ][ c ] - simRk4.now( rig.out.width ).g[ p ][ c ] );
	}
};

//---------------------------------------------------------------------------
// Reading gains out of the picture.
//---------------------------------------------------------------------------
double decodeSlope( double v )
{
	return v <= 0.04045 ? 1.0 / 12.92 : 2.4 / 1.055 * std::pow( ( v + 0.055 ) / 1.055, 1.4 );
}

double encodeSlope( double x )
{
	return x <= 0.0031308 ? 12.92 : 1.055 / 2.4 * std::pow( x, 1.0 / 2.4 - 1.0 );
}

/// Kept clear of a band's edges and the frame's: the widest blur any check
/// draws, ceil( 4 sigma ) taps at Pop 1 mm, plus 2.
int marginFor( int W )
{
	const double sigma = 1.0 * W / ( 4.0 * 2.8 * 36.0 );
	return std::min( 48, static_cast< int >( std::ceil( 4.0 * sigma ) ) ) + 2;
}

/// The taps each side for a sigma, by the spec's rule; 0 for none.
int radiusFor( double sigma )
{
	return sigma > 0.0 ? std::min( 48, static_cast< int >( std::ceil( 4.0 * sigma ) ) ) : 0;
}

struct Measured
{
	bool linear = true;
	double g[ 2 ][ 3 ]     = {};///< per projector, per channel
	double out[ 2 ][ 3 ]   = {};///< the mean output each was read from, as stored
	double slide[ 2 ][ 3 ] = {};///< the slide's transmittance there
	double band2[ 3 ]      = {};///< both lit: the mean output, decoded
	double out2[ 3 ]       = {};
	int pixels = 0;
};

/// Each projector's gain from the split card: band 0 is A's alone, band 1
/// B's. The mean of the decoded output over the band's interior, over the
/// slide's decoded code.
Measured measureSplit( const ImageF& img, int W, int H, bool linear, int margin )
{
	Measured m;
	m.linear             = linear;
	double sum[ 3 ][ 3 ] = {};
	int count[ 3 ]       = {};
	for( int y = margin; y < H - margin; ++y )
		for( int x = 0; x < W; ++x )
		{
			const int b  = bandOf( x, W );
			const int x0 = b * W / 3, x1 = ( b + 1 ) * W / 3;
			if( x < x0 + margin || x >= x1 - margin )
				continue;
			for( int c = 0; c < 3; ++c )
				sum[ b ][ c ] += img[ at4( W, x, y ) + static_cast< size_t >( c ) ];
			++count[ b ];
		}
	m.pixels = std::min( count[ 0 ], count[ 1 ] );
	for( int b = 0; b < 3; ++b )
		for( int c = 0; c < 3; ++c )
		{
			const double mean = count[ b ] ? sum[ b ][ c ] / count[ b ] : std::nan( "" );
			const double lin  = linear ? srgbToLinear( mean ) : mean;
			if( b < 2 )
			{
				const Rgb card        = b == 0 ? kSplitA[ 0 ] : kSplitB[ 1 ];
				const unsigned char v = c == 0 ? card.r : c == 1 ? card.g : card.b;
				m.slide[ b ][ c ]     = linear ? srgbToLinear( v / 255.0 ) : v / 255.0;
				m.g[ b ][ c ]         = lin / m.slide[ b ][ c ];
				m.out[ b ][ c ]       = mean;
			}
			else
			{
				m.band2[ c ] = lin;
				m.out2[ c ]  = mean;
			}
		}
	return m;
}

/// A slide's transmittance through the GPU's float arithmetic, relative:
/// the bilinear resample (two mixes, 2 ULP each), and when blurred, two
/// passes of 2R + 1 multiply-adds (the recursive-summation bound, 2R + 1
/// ULP, plus the products' 1) and the weights' rounding to float (1 ULP of
/// their sum, each pass).
double transmittanceUlps( int R )
{
	return 4.0 + ( R > 0 ? 2.0 * ( 2.0 * R + 3.0 ) : 0.0 );
}

/// The absolute bound on a stored output `o` (encoded with Linear Light on)
/// carrying light `light`: the GLSL sRGB pair's kPowBound (AGENTS.md:
/// encode( decode( c ) L ), absolute), plus the light's own float error --
/// the transmittance's, the gain's multiply and the sum's (3 ULP) --
/// carried through encode.
double outputBound( double light, int R, bool linear )
{
	const double rel = ( transmittanceUlps( R ) + 3.0 ) * kFloatUlp;
	return linear ? kPowBound + encodeSlope( std::max( light, 1e-12 ) ) * light * rel : light * rel;
}

/// The bound on a gain read from band p, three times the sum of: the
/// output's bound carried back through the harness's decode and divided by
/// the slide; the gain's own rounding to a float uniform (1 ULP); and the
/// snap, which moves a gain by at most 2^-24.
double readBound( const Measured& m, int p, int c, double g, int R )
{
	const double slide = m.slide[ p ][ c ];
	const double light = g * slide;
	const double o     = m.out[ p ][ c ];
	const double back  = m.linear ? decodeSlope( o + outputBound( light, R, true ) ) : 1.0;
	return 3.0 * ( back * outputBound( light, R, m.linear ) / slide + 2.0 * kFloatUlp * std::max( g, 1.0 ) );
}

/// Two double implementations of one ODE drift apart by their rounding: at
/// most 4 ULP of 4000 K (4 x 4.5e-13 K) a step, 4000 steps a second, for at
/// most 20 s of a check's run, is 1.5e-7 K; a gain moves by at most 3e-3 of
/// itself per kelvin (Planck's c2 / lambda T^2 at 450 nm, 2000 K) -- 4.4e-10.
/// And the reference's gains and the plugin's are each two 471-term double
/// sums: 2 x 471 x 2^-53, 1.1e-13. Three times their sum.
constexpr double kImplementationBound = 3.0 * ( 4.4e-10 + 1.1e-13 );

/// The model's side of a gain's tolerance: three times the plugin's measured
/// method error, and the two implementations' rounding.
double modelBound( const Lockstep& l, int p, int c )
{
	return 3.0 * l.methodError( p, c ) + kImplementationBound;
}

/// Compare one frame's gains, both projectors, every channel. Returns the
/// worst ratio of error to tolerance (NaN-propagating).
double compareGains( const Lockstep& l, const Measured& m, double& worstErr, double& worstTol )
{
	const ref::Out e  = l.expect();
	const ref::Out e4 = l.simRk4.now( l.rig.out.width );
	double worst      = 0.0;
	for( int p = 0; p < 2; ++p )
	{
		const int R = radiusFor( std::max( e.sigma[ p ], e4.sigma[ p ] ) );
		for( int c = 0; c < 3; ++c )
		{
			const double err = std::fabs( m.g[ p ][ c ] - e.g[ p ][ c ] );
			const double tol = readBound( m, p, c, e.g[ p ][ c ], R ) + modelBound( l, p, c );
			const double r   = std::isnan( err ) ? err : err / tol;
			if( std::isnan( r ) || r > worst )
			{
				worstErr = err;
				worstTol = tol;
			}
			worst = worse( worst, r );
		}
	}
	return worst;
}

/// Run a timeline on the split card, comparing every frame. `cue( k, l )` is
/// called before frame k renders, `after( k, l, m )` after it is measured.
template< typename Cue, typename After >
int splitTimeline( MixRig& rig, Lockstep& l, int frames, Cue&& cue, After&& after, const std::string& what, bool linear = true )
{
	double worst = 0.0, worstErr = 0.0, worstTol = 0.0;
	int compared = 0, worstFrame = -1;
	for( int k = 0; k < frames; ++k )
	{
		cue( k, l );
		if( !l.frame() )
		{
			Check( false, what + ": ProcessOpenGL failed" );
			return 0;
		}
		const Measured m = measureSplit( rig.PixelsF(), rig.out.width, rig.out.height, linear, marginFor( rig.out.width ) );
		after( k, l, m );
		if( m.pixels <= 0 )
			continue;
		double e = 0.0, t = 0.0;
		const double r = compareGains( l, m, e, t );
		if( std::isnan( r ) || r > worst )
		{
			worstErr   = e;
			worstTol   = t;
			worstFrame = k;
		}
		worst = worse( worst, r );
		++compared;
	}
	Check( compared == frames && worst <= 1.0,
	       what + fmt( ": %.0f frames, worst gain error %.3g against %.3g (frame %.0f)", compared, worstErr, worstTol, worstFrame ) );
	return compared;
}

template< typename Cue >
int splitTimeline( MixRig& rig, Lockstep& l, int frames, Cue&& cue, const std::string& what, bool linear = true )
{
	return splitTimeline( rig, l, frames, cue, []( int, Lockstep&, const Measured& ) {}, what, linear );
}

//---------------------------------------------------------------------------
// --list, --names
//---------------------------------------------------------------------------
int runList()
{
	Carousel plugin;
	std::printf( "%-4s %-22s %-9s %10s   %-16s %s\n", "id", "name", "kind", "value", "range", "elements" );
	for( unsigned int id = 0; id < Carousel::PT_COUNT; ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( id >= Carousel::PT_ABOUT_FIRST )
		{
			std::printf( "%-4u %-22s %-9s %10s   %-16s %s\n", id, name ? name : "", "about", "-", "-", "the Stoatworks About block; not swept" );
			continue;
		}
		const char* kind = "standard";
		switch( plugin.GetParamType( id ) )
		{
		case FF_TYPE_BOOLEAN: kind = "boolean"; break;
		case FF_TYPE_EVENT: kind = "event"; break;
		case FF_TYPE_INTEGER: kind = "integer"; break;
		case FF_TYPE_OPTION: kind = "option"; break;
		case FF_TYPE_TEXT: kind = "text"; break;
		default: break;
		}
		RangeStruct range = plugin.GetParamRange( id );
		std::string elements;
		if( plugin.GetParamType( id ) == FF_TYPE_OPTION )
		{
			const unsigned n = std::max( 1u, plugin.GetNumParamElements( id ) );
			range.min        = 0.0f;
			range.max        = static_cast< float >( n ) - 1.0f;
			for( unsigned e = 0; e < n; ++e )
				elements += std::string( e ? " | " : "" ) + plugin.GetParamElementName( id, e );
		}
		char rangeText[ 32 ] = {};
		std::snprintf( rangeText, sizeof( rangeText ), "[%g .. %g]", range.min, range.max );
		std::printf( "%-4u %-22s %-9s %10.4f   %-16s %s\n", id, name ? name : "", kind, plugin.GetFloatParameter( id ), rangeText, elements.c_str() );
	}
	return 0;
}

int runNames()
{
	std::printf( "names: sixteen bytes at most, none twice, and the mixer's load-bearing ones\n\n" );
	Carousel mixer;
	int over = 0;
	std::vector< std::string > seen;
	for( unsigned int id = 0; id < Carousel::PT_COUNT; ++id )
	{
		const char* name = mixer.GetParamName( id );
		if( name == nullptr )
			continue;
		if( std::strlen( name ) > 16 || std::find( seen.begin(), seen.end(), std::string( name ) ) != seen.end() )
		{
			std::printf( "  %-3u  %-28s %zu bytes%s\n", id, name, std::strlen( name ), std::strlen( name ) > 16 ? "" : ", duplicate" );
			++over;
		}
		seen.push_back( name );
	}
	Check( over == 0, fmt( "%.0f parameter names over 16 bytes or duplicated, of %.0f", over, Carousel::PT_COUNT ) );

	const std::string id = names::kMixerId, shown = names::kMixerName;
	Check( id == "CA01" && shown == "SW Carousel" && shown.size() <= 16, "the plugin is " + shown + fmt( " (%.0f bytes), id ", shown.size() ) + id );

	const char* first = mixer.GetParamName( 0 );
	Check( first != nullptr && std::strcmp( first, "Linear Light" ) == 0 && mixer.GetParamType( 0 ) == FF_TYPE_BOOLEAN && mixer.GetFloatParameter( 0 ) == 1.0f,
	       std::string( "parameter 0, which Arena hides, is " ) + ( first ? first : "(none)" ) + " (boolean, default on)" );
	const char* opacity = mixer.GetParamName( Carousel::PT_OPACITY );
	Check( opacity != nullptr && std::strcmp( opacity, "Opacity" ) == 0 && mixer.GetParamType( Carousel::PT_OPACITY ) == FF_TYPE_STANDARD
	           && mixer.GetFloatParameter( Carousel::PT_OPACITY ) == 1.0f,
	       "the dissolve fader is a standard parameter named Opacity (Arena binds it to the layer fader), default 1 (projector B)" );
	Check( mixer.GetParamType( Carousel::PT_TAKE ) == FF_TYPE_EVENT && mixer.GetParamType( Carousel::PT_CHANGE_A ) == FF_TYPE_EVENT
	           && mixer.GetParamType( Carousel::PT_CHANGE_B ) == FF_TYPE_EVENT,
	       "Take, Change A and Change B are events (buttons)" );
	const bool options = std::strcmp( mixer.GetParamElementName( Carousel::PT_CURVE, 1 ), "Light" ) == 0
	                     && std::strcmp( mixer.GetParamElementName( Carousel::PT_LAMP, 1 ), "Low" ) == 0
	                     && std::strcmp( mixer.GetParamElementName( Carousel::PT_MOUNTS, 1 ), "Glass" ) == 0;
	Check( options, "the options are Curve Voltage|Light, Lamp High|Low, Mounts Card|Glass" );
	const char* about = mixer.GetParamName( Carousel::PT_ABOUT_FIRST );
	Check( about != nullptr && std::strcmp( about, "About" ) == 0 && Carousel::PT_COUNT - Carousel::PT_ABOUT_FIRST == stoatworks::about::kParamCount,
	       "the About block is last" );
	Check( std::fabs( TimeFromParam( ParamForTime( kTimeDefault ) ) - kTimeDefault ) < 1e-6 && TimeFromParam( 0.0f ) == 0.0
	           && std::fabs( TimeFromParam( 1.0f ) - 16.0 ) < 1e-12 && std::fabs( PopFromParam( ParamForPop( kPopDefaultMm ) ) - kPopDefaultMm ) < 1e-6
	           && OptionIndex( 1.0f, 2 ) == 1 && OptionIndex( 7.0f, 2 ) == 1,
	       "Time (0 .. 16 s) and Pop invert to their defaults; options map by index, clamped" );
	Check( mixer.GetFloatParameter( Carousel::PT_CURVE ) == 0.0f && mixer.GetFloatParameter( Carousel::PT_LAMP ) == 0.0f
	           && mixer.GetFloatParameter( Carousel::PT_MOUNTS ) == 0.0f,
	       "defaults: Curve Voltage, Lamp High, Mounts Card" );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --mixer: the guards; two inputs of two sizes with two paddings; the GL
// state handed back.
//---------------------------------------------------------------------------
void mixerCheck( int outW, int outH, int fault )
{
	//The same frames rendered twice, the padding magenta in one and green in
	//the other: if the padding is never read, not one byte differs.
	const InputSpec a = InputSpec::Padded( 200, 120, 256, 256 );
	const InputSpec b = InputSpec::Padded( 96, 70, 128, 128 );
	MixRig magenta, green;
	green.padding = { 0, 255, 0 };
	if( !magenta.Init( outW, outH, a, b, false, fault ) || !green.Init( outW, outH, a, b, false, fault ) )
	{
		Check( false, "InitGL" );
		return;
	}
	for( MixRig* r : { &magenta, &green } )
	{
		r->startSeconds = kStart;
		r->UploadA( slideCard( 200, 120, false ) );
		r->UploadB( slideCard( 96, 70, true ) );
		//Mid-dissolve, so both are read, glass-mounted, so both are
		//resampled straight from the inputs; then card-mounted and B changed,
		//so both are blurred.
		r->Set( "Curve", 1.0f );
		r->Set( "Time", 0.0f );
		r->Set( "Opacity", 0.5f );
		r->Set( "Pop", 1.0f );
		r->Set( "Mounts", 1.0f );
	}
	size_t differ = 0;
	int frames = 0, sharpBoth = 0, blurredBoth = 0;
	for( int k = 0; k < 130; ++k )
	{
		for( MixRig* r : { &magenta, &green } )
		{
			if( k == 30 )
				r->Set( "Mounts", 0.0f );
			if( k == 32 )
				r->Press( "Change B" );
			if( k == 33 )
				r->Release( "Change B" );
		}
		if( !magenta.Step() || !green.Step() )
			break;
		++frames;
		differ += differingBytes( magenta.Pixels(), green.Pixels() );
		const Instant& in = magenta.plugin.InstantForTest();
		const bool lit    = in.p[ 0 ].shutter == 1.0 && in.p[ 1 ].shutter == 1.0;
		sharpBoth += lit && in.p[ 0 ].sigmaPx == 0.0 && in.p[ 1 ].sigmaPx == 0.0;
		blurredBoth += lit && in.p[ 0 ].sigmaPx > 0.0 && in.p[ 1 ].sigmaPx > 0.0;
	}
	Check( frames == 130 && differ == 0 && sharpBoth >= 20 && blurredBoth >= 20,
	       fmt( "Dest 200x120 of 256x256, Src 96x70 of 128x128, both lit: magenta padding against green, %.0f bytes differ over %.0f frames (%.0f both in focus, %.0f both blurred)",
	            differ, frames, sharpBoth, blurredBoth ) );

	//The state handed back after a frame with both blurs.
	magenta.Step();
	GLint bound[ 4 ] = {}, active = 0, fbo = 0, viewport[ 4 ] = {};
	glGetIntegerv( GL_ACTIVE_TEXTURE, &active );
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
	glGetIntegerv( GL_VIEWPORT, viewport );
	for( int u = 0; u < 4; ++u )
	{
		glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound[ u ] );
	}
	glActiveTexture( GL_TEXTURE0 );
	Check( bound[ 0 ] == 0 && bound[ 1 ] == 0 && bound[ 2 ] == 0 && bound[ 3 ] == 0 && active == GL_TEXTURE0,
	       fmt( "after a frame, texture units 0-3 hold %.0f %.0f %.0f %.0f (0 is none), unit 0 active", bound[ 0 ], bound[ 1 ], bound[ 2 ], bound[ 3 ] ) );
	Check( static_cast< GLuint >( fbo ) == magenta.out.fbo && viewport[ 0 ] == 0 && viewport[ 1 ] == 0 && viewport[ 2 ] == outW && viewport[ 3 ] == outH,
	       "the host's framebuffer and viewport are as they were" );
}

/// An input at another size fills the frame: output pixel p samples it at
/// ( p + 0.5 ) size / out - 0.5, bilinear over the four texels' decoded,
/// premultiplied light, clamped to the used size; the padding never read.
/// Each projector alone at rated, glass-mounted (no blur), its slide at 0.6
/// and at 1.37 of the output, padded, against the harness's own resample.
void resampleCheck( int W, int H, int fault )
{
	for( int which = 0; which < 2; ++which )
		for( double scale : { 0.6, 1.37 } )
		{
			const int w = static_cast< int >( std::lround( W * scale ) ), h = static_cast< int >( std::lround( H * scale ) );
			MixRig rig;
			const InputSpec mine = InputSpec::Padded( w, h, w + 16, h + 16 ), full = InputSpec::Exact( W, H );
			if( !rig.Init( W, H, which == 0 ? mine : full, which == 1 ? mine : full, true, fault ) )
				return;
			rig.startSeconds = kStart;
			const Image slide = slideCard( w, h, which == 1, true );
			const Image other = slideCard( W, H, which == 0, true );
			rig.UploadA( which == 0 ? slide : other );
			rig.UploadB( which == 1 ? slide : other );
			rig.Set( "Opacity", static_cast< float >( which ) );
			rig.Set( "Mounts", 1.0f );
			for( int k = 0; k < 4; ++k )
				rig.Step();
			const ImageF out = rig.PixelsF();
			//The GPU's sample position: ( p + 0.5 ) x size exact, a division
			//(2.5 ULP) and a subtraction (1 ULP), at a position up to w.
			const double dq = 3.5 * std::ldexp( 1.0, static_cast< int >( std::floor( std::log2( std::max( w, h ) ) ) ) - 23 );
			auto texel = [ & ]( int x, int y, int c ) {
				x                = std::clamp( x, 0, w - 1 );
				y                = std::clamp( y, 0, h - 1 );
				const size_t i   = at4( w, x, y );
				const double rgb = slide[ i + static_cast< size_t >( c ) ] / 255.0 * ( slide[ i + 3 ] / 255.0 );
				return srgbToLinear( rgb );
			};
			double worst = 0.0;
			for( int y = 0; y < H; ++y )
				for( int x = 0; x < W; ++x )
				{
					const double qx = ( x + 0.5 ) * w / W - 0.5, qy = ( y + 0.5 ) * h / H - 0.5;
					const int x0 = static_cast< int >( std::floor( qx ) ), y0 = static_cast< int >( std::floor( qy ) );
					const double fx = qx - x0, fy = qy - y0;
					for( int c = 0; c < 3; ++c )
					{
						const double t00 = texel( x0, y0, c ), t10 = texel( x0 + 1, y0, c ), t01 = texel( x0, y0 + 1, c ), t11 = texel( x0 + 1, y0 + 1, c );
						const double v   = ( t00 + ( t10 - t00 ) * fx ) * ( 1.0 - fy ) + ( t01 + ( t11 - t01 ) * fx ) * fy;
						const double hi  = std::max( std::max( t00, t10 ), std::max( t01, t11 ) );
						const double lo  = std::min( std::min( t00, t10 ), std::min( t01, t11 ) );
						//The output's bound, the position's (the spread of the
						//four texels times its error, each axis), and the
						//harness's double against the GPU's float rgb x a.
						const double dv  = 2.0 * ( hi - lo ) * dq + 3.0 * kFloatUlp * hi * 2.4;
						const double tol = 3.0 * ( outputBound( v, 0, true ) + encodeSlope( std::max( v, 1e-12 ) ) * dv );
						const double got = out[ at4( W, x, y ) + static_cast< size_t >( c ) ];
						worst            = worse( worst, std::fabs( got - linearToSrgb( v ) ) / tol );
					}
				}
			Check( worst <= 1.0, std::string( which ? "B" : "A" ) + fmt( " at %.0fx%.0f, padded, alone at rated: every pixel is the bilinear resample, worst %.3f of its tolerance", w, h, worst ) );
		}
}

void guardCheck()
{
	MixRig rig;
	if( !rig.Init( 320, 180 ) )
		return;
	rig.startSeconds = kStart;
	Check( rig.RenderBroken( 2, -2 ) == FF_FAIL, "no input array: FF_FAIL" );
	Check( rig.RenderBroken( 0, -1 ) == FF_FAIL, "no inputs: FF_FAIL" );
	Check( rig.RenderBroken( 1, -1 ) == FF_FAIL, "one input (oxbow, an effect host): FF_FAIL" );
	Check( rig.RenderBroken( 2, 0 ) == FF_FAIL, "a null Dest: FF_FAIL" );
	Check( rig.RenderBroken( 2, 1 ) == FF_FAIL, "a null Src: FF_FAIL" );
	Check( rig.RenderBroken( 2, -1, true ) == FF_FAIL, "a zero-sized input: FF_FAIL" );
	Check( rig.RenderBroken( 2, -1, false, true ) == FF_FAIL, "a zero viewport: FF_FAIL" );
	Check( rig.Step(), "and then a whole frame renders" );
}

int runMixer()
{
	std::printf( "the guards, two inputs at two sizes with padding, the GL state\n" );
	guardCheck();
	atBothRasters(
		[]( int w, int h, int f ) {
			mixerCheck( w, h, f );
			resampleCheck( w, h, f );
		},
		{ { kFaultLeaveBound, "a texture left bound" }, { kFaultSizeSwap, "B read at A's size" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --unchanged: at rest, one projector alone at rated is its slide over black,
// byte for byte and float for float. "Float for float" is against THIS
// rasteriser's own unorm8 decode (unormDecode): the plugin multiplies what
// its fetch returns by the alpha and nothing else, and the fetch is the
// rasteriser's. Apple's GPU decodes c/255 correctly rounded; its software
// renderer is 1 ulp off for some values, and that is printed, not hidden.
//---------------------------------------------------------------------------
void unchangedCheck( int W, int H, int fault )
{
	static bool decodeReported = false;
	for( int which = 0; which < 2; ++which )
		for( int alpha = 0; alpha < 2; ++alpha )
			for( int floatOut = 0; floatOut < 2; ++floatOut )
			{
				MixRig rig;
				if( !rig.Init( W, H, floatOut != 0, fault ) )
					return;
				rig.startSeconds = kStart;
				const Image a = slideCard( W, H, false, alpha != 0 ), b = slideCard( W, H, true, alpha != 0 );
				rig.UploadA( a );
				rig.UploadB( b );
				rig.Set( "Opacity", static_cast< float >( which ) );
				for( int k = 0; k < 4; ++k )
					rig.Step();
				const Image& slide = which ? b : a;
				size_t differ      = 0;
				if( floatOut )
				{
					const std::vector< float > decode = unormDecode();
					if( decode.size() != 256 )
					{
						Check( false, "the unorm8 decode probe ran" );
						return;
					}
					if( !decodeReported )
					{
						int off        = 0;
						double offUlps = 0.0;
						for( int c = 1; c < 256; ++c )
						{
							const float exact = static_cast< float >( c ) / 255.0f;
							off += decode[ static_cast< size_t >( c ) ] != exact;
							offUlps = worse( offUlps, std::fabs( static_cast< double >( decode[ static_cast< size_t >( c ) ] ) - exact ) / ulpAt( exact ) );
						}
						off += decode[ 0 ] != 0.0f;
						std::printf( "  (this rasteriser's unorm8 decode: %d of 256 values differ from c/255 correctly rounded, worst %.3g ulp)\n", off, offUlps );
						decodeReported = true;
					}
					const ImageF out = rig.PixelsF();
					for( size_t i = 0; i < out.size(); i += 4 )
					{
						//rgb x a in float, as the shader forms it from its fetch.
						const float af = decode[ slide[ i + 3 ] ];
						for( int c = 0; c < 3; ++c )
						{
							const float v = decode[ slide[ i + static_cast< size_t >( c ) ] ] * af;
							differ += std::memcmp( &out[ i + static_cast< size_t >( c ) ], &v, sizeof( float ) ) != 0;
						}
						differ += out[ i + 3 ] != 1.0f;
					}
				}
				else
				{
					const Image out = rig.Pixels();
					for( size_t i = 0; i < out.size(); i += 4 )
					{
						const float af = static_cast< float >( slide[ i + 3 ] ) / 255.0f;
						for( int c = 0; c < 3; ++c )
						{
							const float v = ( static_cast< float >( slide[ i + static_cast< size_t >( c ) ] ) / 255.0f ) * af;
							differ += out[ i + static_cast< size_t >( c ) ] != static_cast< unsigned char >( std::lround( v * 255.0f ) );
						}
						differ += out[ i + 3 ] != 255;
					}
				}
				Check( differ == 0, std::string( which ? "B" : "A" ) + " alone at rest, " + ( alpha ? "with alpha" : "opaque" ) + ", "
				                        + ( floatOut ? "float" : "8-bit" ) + fmt( " target: %.0f values differ from rgb x a over black, alpha 1", differ ) );
			}
}

int runUnchanged()
{
	std::printf( "at rest, a projector alone at rated is its slide over black, to the bit\n" );
	atBothRasters( []( int w, int h, int f ) { unchangedCheck( w, h, f ); }, { { kFaultNoWhiteBalance, "raw Planck sRGB, not white-balanced" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --lamp: a cut and a slow dissolve, each lamp's gain frame by frame against
// the harness's ODE. Then the 90% rise and 10% fall, from the picture.
//---------------------------------------------------------------------------
struct Crossings
{
	double rise90 = std::nan( "" ), fall10 = std::nan( "" ), rise50 = std::nan( "" );
};

void lampCheck( int W, int H, int fault )
{
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return;
	rig.startSeconds = kStart;
	rig.UploadA( splitCard( W, H, false ) );
	rig.UploadB( splitCard( W, H, true ) );
	Lockstep l( rig );
	l.set( "Time", 0.0f );
	l.set( "Opacity", 1.0f );
	splitTimeline(
		rig, l, 300,
		[]( int k, Lockstep& s ) {
			if( k == 13 )
				s.set( "Opacity", 0.0f );
			if( k == 130 )
			{
				s.set( "Time", ParamForTime( 2.0 ) );
				s.set( "Opacity", 1.0f );
			}
		},
		"a cut to A at frame 13, then a 2 s dissolve back to B at frame 130: both lamps, every frame" );
}

/// The crossings, from the picture alone, at 1 ms frames so that no model is
/// needed to interpolate: a cut from B to A, A rising from the preheat floor
/// and B falling from rated.
Crossings crossingsFromPicture( int W, int H )
{
	Crossings c;
	MixRig rig;
	if( !rig.Init( W, H, true ) )
		return c;
	rig.startSeconds = kStart;
	rig.fps          = 1000.0;
	rig.UploadA( splitCard( W, H, false ) );
	rig.UploadB( splitCard( W, H, true ) );
	rig.Set( "Time", 0.0f );
	rig.Set( "Opacity", 1.0f );
	double prevA = 0.0, prevB = 1.0;
	for( int k = 0; k < 700; ++k )
	{
		if( k == 1 )
			rig.Set( "Opacity", 0.0f );
		rig.Step();
		const Measured m = measureSplit( rig.PixelsF(), W, H, true, marginFor( W ) );
		const double a   = ref::lum709( m.g[ 0 ] ), b = ref::lum709( m.g[ 1 ] );
		const double t   = ( k - 1 ) / 1000.0;
		if( k > 1 && std::isnan( c.rise50 ) && a >= 0.5 )
			c.rise50 = t - 0.001 * ( a - 0.5 ) / ( a - prevA );
		if( k > 1 && std::isnan( c.rise90 ) && a >= 0.9 )
			c.rise90 = t - 0.001 * ( a - 0.9 ) / ( a - prevA );
		if( k > 1 && std::isnan( c.fall10 ) && b <= 0.1 )
			c.fall10 = t - 0.001 * ( 0.1 - b ) / ( prevB - b );
		prevA = a;
		prevB = b;
	}
	return c;
}

int runLamp()
{
	std::printf( "each lamp's light, frame by frame, against the harness's own ODE (Dormand-Prince, SI units)\n" );
	atBothRasters( []( int w, int h, int f ) { lampCheck( w, h, f ); },
	               { { kFaultConstantR, "resistance fixed at its hot value" },
	                 { kFaultLinearCool, "radiation linear in T" },
	                 { kFaultNoLag, "the filament at its steady T instantly" } } );
	const Crossings c = crossingsFromPicture( 320, 180 );
	//Levi via Cleyet (Phys-L, 2005), not read at source: heat 0.15 I0^0.7 s
	//(from cold), cool 0.07 I0^0.7 s.
	const double i0 = 300.0 / 82.0;
	std::printf( "\n  from the picture at 1 ms frames, light as BT.709 luminance of the gains:\n" );
	std::printf( "    A from the 700 K preheat to 50%% in %.4f s, to 90%% in %.4f s; B from rated to 10%% in %.4f s\n", c.rise50, c.rise90, c.fall10 );
	std::printf( "    Levi's rule of thumb at I0 = %.2f A: heat %.3f s (from cold), cool %.3f s -- printed, not asserted\n", i0, 0.15 * std::pow( i0, 0.7 ),
	             0.07 * std::pow( i0, 0.7 ) );
	Check( c.rise90 > c.fall10 && c.fall10 > 0.0, "a filament heats slower than it cools (both crossings measured)" );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --colour: the colour temperature recovered from the picture.
//---------------------------------------------------------------------------
void colourCheck( int W, int H, int fault, bool print = false )
{
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return;
	rig.startSeconds = kStart;
	rig.UploadA( splitCard( W, H, false ) );
	rig.UploadB( splitCard( W, H, true ) );
	Lockstep l( rig );
	l.set( "Time", 0.0f );
	const double levels[] = { 1.0, 0.95, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4 };
	int measured = 0, bad = 0;
	double worstRatio = 0.0, worstErr = 0.0, worstTol = 0.0;
	std::vector< std::pair< double, double > > curve;//( u, CCT ), High
	for( int low = 0; low < 2; ++low )
	{
		l.set( "Lamp", static_cast< float >( low ) );
		for( double level : levels )
		{
			if( low && level != 1.0 )
				continue;
			l.set( "Opacity", static_cast< float >( level ) );
			for( int k = 0; k < 150; ++k )
				l.frame();
			const Measured m = measureSplit( rig.PixelsF(), W, H, true, marginFor( W ) );
			const ref::Out e = l.expect();
			const double cct = ref::cctFromGains( m.g[ 1 ] );
			//The tolerance: each channel's gain bound carried through the
			//locus inversion (a finite difference, summed over channels), and
			//the inversion's own error, measured on the model's own gains.
			const int R = radiusFor( e.sigma[ 1 ] );
			double tol  = 0.0;
			for( int c = 0; c < 3; ++c )
			{
				const double dg = readBound( m, 1, c, e.g[ 1 ][ c ], R ) + modelBound( l, 1, c );
				double g2[ 3 ]  = { m.g[ 1 ][ 0 ], m.g[ 1 ][ 1 ], m.g[ 1 ][ 2 ] };
				g2[ c ] += dg;
				tol += std::fabs( ref::cctFromGains( g2 ) - cct );
			}
			tol += 3.0 * std::fabs( ref::cctFromGains( e.g[ 1 ] ) - e.T[ 1 ] );
			const double err   = std::fabs( cct - e.T[ 1 ] );
			const double ratio = err / tol;
			if( !( err <= tol ) )
				++bad;
			if( !( ratio <= worstRatio ) )
			{
				worstRatio = ratio;
				worstErr   = err;
				worstTol   = tol;
			}
			++measured;
			if( !low )
				curve.emplace_back( l.sim.drive( 1 ), cct );
			if( print )
				std::printf( "    %s u %.4f: %.3f K from the picture, the filament %.3f K (|diff| %.2g K, tolerance %.2g K)\n", low ? "Low " : "High",
				             l.sim.drive( 1 ), cct, e.T[ 1 ], err, tol );
		}
	}
	Check( measured == 9 && bad == 0,
	       fmt( "%.0f states (High at 8 voltages, then Low): the colour temperature from the picture is the filament's; worst |diff| %.2g K against %.2g K", measured,
	            worstErr, worstTol ) );
	bool warmer = curve.size() == 8;
	for( size_t i = 1; i < curve.size(); ++i )
		warmer = warmer && curve[ i ].second < curve[ i - 1 ].second;
	Check( warmer, "warmer at every step down the voltage" );
	if( print && curve.size() >= 3 )
	{
		const double x = std::log( curve[ 2 ].second / curve[ 0 ].second ) / std::log( curve[ 2 ].first / curve[ 0 ].first );
		std::printf( "    from the picture, colour temperature ~ V^%.3f between 0.9 and 1.0 of rated (Klipstein 0.376, ERCO 0.4): printed, not asserted\n", x );
	}
}

int runColour()
{
	std::printf( "the colour temperature, recovered from the picture's gains through the harness's own Planck and CMFs\n" );
	colourCheck( 640, 360, kFaultNone, true );
	atBothRasters( []( int w, int h, int f ) { colourCheck( w, h, f ); },
	               { { kFaultColourHot, "the colour of the rated lamp at every T" }, { kFaultNoWhiteBalance, "raw Planck sRGB, not white-balanced" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --dissolve: the sag and the warm middle (Voltage), the flat Light curve,
// and the ramp lasting exactly Time.
//---------------------------------------------------------------------------
double luminanceBound( const Lockstep& l, const Measured& m, int p )
{
	const ref::Out e = l.expect();
	const int R      = radiusFor( e.sigma[ p ] );
	double t         = 0.0;
	for( int c = 0; c < 3; ++c )
		t += ( c == 0 ? 0.2126 : c == 1 ? 0.7152 : 0.0722 ) * ( readBound( m, p, c, e.g[ p ][ c ], R ) + modelBound( l, p, c ) );
	return t;
}

void dissolveCheck( int W, int H, int fault, bool print = false )
{
	//(1) Voltage, 2 s, A to B, at 48 fps: every frame follows the model; the
	//screen's light at the model's lowest frame.
	{
		MixRig rig;
		if( !rig.Init( W, H, true, fault ) )
			return;
		rig.startSeconds = kStart;
		rig.fps          = 48.0;
		rig.UploadA( splitCard( W, H, false ) );
		rig.UploadB( splitCard( W, H, true ) );
		Lockstep l( rig );
		l.set( "Time", ParamForTime( 2.0 ) );
		l.set( "Opacity", 0.0f );
		double refMin = 1e9, measAtMin = 0.0, tolAtMin = 0.0, cctAtMin = 0.0, refCctAtMin = 0.0;
		int frameAtMin = -1;
		splitTimeline(
			rig, l, 170, []( int k, Lockstep& s ) {
				if( k == 10 )
					s.set( "Opacity", 1.0f );
			},
			[ & ]( int k, Lockstep& s, const Measured& m ) {
				const ref::Out e    = s.expect();
				const double sumRef = ref::lum709( e.g[ 0 ] ) + ref::lum709( e.g[ 1 ] );
				if( k > 10 && sumRef < refMin )
				{
					refMin     = sumRef;
					measAtMin  = ref::lum709( m.g[ 0 ] ) + ref::lum709( m.g[ 1 ] );
					tolAtMin   = luminanceBound( s, m, 0 ) + luminanceBound( s, m, 1 );
					frameAtMin = k;
					double both[ 3 ], bothRef[ 3 ];
					for( int c = 0; c < 3; ++c )
					{
						both[ c ]    = m.g[ 0 ][ c ] + m.g[ 1 ][ c ];
						bothRef[ c ] = e.g[ 0 ][ c ] + e.g[ 1 ][ c ];
					}
					cctAtMin    = ref::cctFromGains( both );
					refCctAtMin = ref::cctFromGains( bothRef );
				}
			},
			"Voltage, a 2 s dissolve A to B at 48 fps" );
		Check( std::fabs( measAtMin - refMin ) <= tolAtMin && measAtMin < 0.5,
		       fmt( "the middle sags: at frame %.0f the screen's light is %.4f of one lamp's (the model's %.4f, tolerance %.2g)", frameAtMin, measAtMin, refMin, tolAtMin ) );
		if( print )
			std::printf( "    and warm: %.0f K from the picture there (the model's %.0f K), against 3350 K at either end\n", cctAtMin, refCctAtMin );
	}

	//(2) The ramp lasts Time: the same picture against models whose Time
	//is 0.9 and 1.1 of it must NOT fit.
	{
		MixRig rig;
		if( !rig.Init( W, H, true, fault ) )
			return;
		rig.startSeconds = kStart;
		rig.fps          = 48.0;
		rig.UploadA( splitCard( W, H, false ) );
		rig.UploadB( splitCard( W, H, true ) );
		Lockstep l( rig );
		l.set( "Time", ParamForTime( 1.5 ) );
		l.set( "Opacity", 0.0f );
		ref::Sim other[ 2 ];
		const double factor[ 2 ] = { 0.9, 1.1 };
		double worstOther[ 2 ]   = { 0.0, 0.0 };
		splitTimeline(
			rig, l, 120, []( int k, Lockstep& s ) {
				if( k == 5 )
					s.set( "Opacity", 1.0f );
			},
			[ & ]( int k, Lockstep& s, const Measured& m ) {
				for( int i = 0; i < 2; ++i )
				{
					ref::Ctl c = s.ctl;
					c.time *= factor[ i ];
					if( k == 0 )
						other[ i ].start( c, false );
					else
					{
						other[ i ].advance( s.rig.SecondsOf( k ) - s.rig.SecondsOf( k - 1 ) );
						other[ i ].apply( c, false );
					}
					const ref::Out o = other[ i ].now( W );
					const ref::Out e = s.expect();
					for( int c = 0; c < 3; ++c )
					{
						const double tol = readBound( m, 1, c, e.g[ 1 ][ c ], radiusFor( e.sigma[ 1 ] ) ) + modelBound( s, 1, c );
						worstOther[ i ]  = worse( worstOther[ i ], std::fabs( m.g[ 1 ][ c ] - o.g[ 1 ][ c ] ) / tol );
					}
				}
			},
			"a 1.5 s dissolve A to B at 48 fps fits a 1.5 s ramp" );
		Check( worstOther[ 0 ] > 1.0 && worstOther[ 1 ] > 1.0,
		       fmt( "and not a ramp of 0.9 or 1.1 of it: off by %.0f and %.0f times the tolerance", worstOther[ 0 ], worstOther[ 1 ] ) );
	}

	//(3) Light: at the midpoint, at rest, the two lamps' light sums to one
	//lamp's. Start puts both at their steady state; 400 frames follow.
	{
		MixRig rig;
		if( !rig.Init( W, H, true, fault ) )
			return;
		rig.startSeconds = kStart;
		rig.UploadA( splitCard( W, H, false ) );
		rig.UploadB( splitCard( W, H, true ) );
		Lockstep l( rig );
		l.set( "Curve", 1.0f );
		l.set( "Time", 0.0f );
		l.set( "Opacity", 0.5f );
		for( int k = 0; k < 400; ++k )
			l.frame();
		const Measured m    = measureSplit( rig.PixelsF(), W, H, true, marginFor( W ) );
		const ref::Out e    = l.expect();
		const double sum    = ref::lum709( m.g[ 0 ] ) + ref::lum709( m.g[ 1 ] );
		const double sumRef = ref::lum709( e.g[ 0 ] ) + ref::lum709( e.g[ 1 ] );
		//The picture's bound and the model's, and how far the model itself
		//is from the definition (its table and its integration).
		const double tol = luminanceBound( l, m, 0 ) + luminanceBound( l, m, 1 ) + 3.0 * std::fabs( sumRef - 1.0 );
		double both[ 3 ];
		for( int c = 0; c < 3; ++c )
			both[ c ] = m.g[ 0 ][ c ] + m.g[ 1 ][ c ];
		Check( std::fabs( sum - 1.0 ) <= tol, fmt( "Light, at the midpoint, at rest: the two lamps' light sums to %.7f of one lamp's (|1 - sum| %.2g against %.2g), at %.0f K",
		                                           sum, std::fabs( sum - 1.0 ), tol, ref::cctFromGains( both ) ) );
		if( print )
			std::printf( "    each lamp at u = %.5f of its voltage (the harness's L^-1( 0.5 ) = %.5f)\n", rig.plugin.InstantForTest().p[ 1 ].drive,
			             ref::lightCurve().inverse( 0.5 ) );
	}
}

int runDissolve()
{
	std::printf( "the dissolve: the sag and the warm middle, the Light curve, the ramp's length\n" );
	atBothRasters( []( int w, int h, int f ) { dissolveCheck( w, h, f, f == kFaultNone ); },
	               { { kFaultDissolveCurve, "the Light curve driving voltage" }, { kFaultSlewFrames, "the dissolve advanced per frame, not per second" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --cut: Time 0. The frame at the cut is the frame before it, bitwise; every
// later frame follows the ODE from an instantaneous step.
//---------------------------------------------------------------------------
void cutCheck( int W, int H, int fault, bool print = false )
{
	for( int low = 0; low < 2; ++low )
		for( int floatOut = 0; floatOut < 2; ++floatOut )
		{
			MixRig rig;
			if( !rig.Init( W, H, floatOut != 0, fault ) )
				return;
			rig.startSeconds = kStart;
			rig.UploadA( splitCard( W, H, false ) );
			rig.UploadB( splitCard( W, H, true ) );
			Lockstep l( rig );
			l.set( "Time", 0.0f );
			l.set( "Opacity", 1.0f );
			l.set( "Lamp", static_cast< float >( low ) );
			Image before8;
			ImageF beforeF;
			for( int k = 0; k < 11; ++k )
				l.frame();
			before8 = rig.Pixels();
			beforeF = rig.PixelsF();
			l.set( "Opacity", 0.0f );
			l.frame();
			const size_t d = floatOut ? differingFloats( beforeF, rig.PixelsF() ) : differingBytes( before8, rig.Pixels() );
			const std::string what = std::string( low ? "Low" : "High" ) + ", " + ( floatOut ? "float" : "8-bit" ) + " target: ";
			Check( d == 0, what + fmt( "the frame at the cut is the frame before it, %.0f values differ (the voltage steps at that instant; the filament cannot)", d ) );
			if( !floatOut )
				continue;
			//The lag, from the picture: A's light against its final value.
			double finalA = 0.0;
			{
				double g[ 3 ];
				ref::gains( ref::steadyT( low ? ref::kLowVolts : ref::kV ), g );
				finalA = ref::lum709( g );
			}
			int half = -1;
			double first = -1.0, firstB = -1.0;
			splitTimeline(
				rig, l, 60, []( int, Lockstep& ) {},
				[ & ]( int k, Lockstep&, const Measured& m ) {
					const double a = ref::lum709( m.g[ 0 ] ) / finalA;
					if( k == 0 )
					{
						first  = a;
						firstB = ref::lum709( m.g[ 1 ] );
					}
					if( half < 0 && a >= 0.5 )
						half = k + 1;
				},
				what + "the next 60 frames follow the ODE from a step at the cut's instant" );
			if( print )
				std::printf( "    the lag at 60 fps (%s): one frame after the cut, A is at %.3g of its final light and B at %.3g of its light before; A passes half at frame %d after the cut\n",
				             low ? "Low" : "High", first, firstB / finalA, half );
		}
}

int runCut()
{
	std::printf( "a cut at Time 0, exactly\n" );
	atBothRasters( []( int w, int h, int f ) { cutCheck( w, h, f, f == kFaultNone ); },
	               { { kFaultHardCutRamp, "Time 0 as a one-frame ramp" }, { kFaultNoLag, "the filament at its steady T instantly" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --change: the shutter, frame by frame, at 59.94 fps from an odd origin.
//---------------------------------------------------------------------------
void changeCheck( int W, int H, int fault, bool print = false )
{
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return;
	rig.startSeconds = 37.123456789;
	rig.fps          = 60000.0 / 1001.0;
	rig.UploadA( splitCard( W, H, false ) );
	rig.UploadB( splitCard( W, H, true ) );
	Lockstep l( rig );
	int dark = 0, darkExpected = 0, inRamp = 0;
	splitTimeline(
		rig, l, 150,
		[]( int k, Lockstep& s ) {
			if( k == 7 || k == 30 )
				s.press( "Change B" );
		},
		[ & ]( int, Lockstep& s, const Measured& m ) {
			const ref::Out e = s.expect();
			darkExpected += e.shutter[ 1 ] == 0.0;
			inRamp += e.shutter[ 1 ] > 0.0 && e.shutter[ 1 ] < 1.0;
			dark += m.g[ 1 ][ 0 ] == 0.0 && m.g[ 1 ][ 1 ] == 0.0 && m.g[ 1 ][ 2 ] == 0.0;
		},
		"Change B at frame 7 (and at 30, mid-cycle, ignored), 59.94 fps: both projectors every frame" );
	Check( dark == darkExpected && dark > 0 && inRamp >= 4,
	       fmt( "the dark gap: %.0f frames black in the picture, %.0f in the model; %.0f frames caught on the shutter's ramps", dark, darkExpected, inRamp ) );
	if( print )
		std::printf( "    the gap at 59.94 fps: %d frames (%.3f s) fully dark, of a 1 s cycle\n", dark, dark * 1001.0 / 60000.0 );
}

int runChange()
{
	std::printf( "a slide change: the shutter's cycle, frame by frame\n" );
	atBothRasters( []( int w, int h, int f ) { changeCheck( w, h, f, f == kFaultNone ); }, { { kFaultShutterLong, "the cycle timed in 60 frames, not 1 s" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --pop: the blur, measured from an edge, against sigma( t ) from the model.
//---------------------------------------------------------------------------
/// The variance of a discrete Gaussian kernel by the spec's rule (radius
/// ceil( 4 sigma ), at most 48, normalised): the harness's own.
double kernelVariance( double sigma )
{
	if( sigma <= 0.0 )
		return 0.0;
	const int R = radiusFor( sigma );
	double s = 0.0, s2 = 0.0;
	for( int k = -R; k <= R; ++k )
	{
		const double w = std::exp( -0.5 * k * k / ( sigma * sigma ) );
		s += w;
		s2 += w * k * k;
	}
	return s2 / s;
}

double kernelSpread( double sigma )
{
	return std::sqrt( kernelVariance( sigma ) );
}

/// The edge's line spread in the middle row, from the float picture, in
/// linear light: its second moment about the unblurred step (between
/// columns W/2 - 1 and W/2), in pixels. Exactly 0 for an unblurred edge.
struct Spread
{
	double sigma = std::nan( "" ), mean = 0.0;
};

Spread measureSpread( const ImageF& img, int W, int H )
{
	Spread s;
	const int y   = H / 2;
	const double p0 = W / 2 - 0.5;
	double m0 = 0.0, m1 = 0.0, m2 = 0.0;
	for( int x = W / 4; x < 3 * W / 4; ++x )
	{
		const double l = srgbToLinear( img[ at4( W, x + 1, y ) + 1 ] ) - srgbToLinear( img[ at4( W, x, y ) + 1 ] );
		const double d = x + 0.5 - p0;
		m0 += l;
		m1 += l * d;
		m2 += l * d * d;
	}
	if( !( m0 > 0.0 ) )
		return s;
	s.mean  = m1 / m0;
	s.sigma = std::sqrt( std::max( 0.0, m2 / m0 - s.mean * s.mean ) );
	return s;
}

/// The bound on a measured spread: each difference in the line spread is
/// off by at most twice the per-pixel bound (the output's, through decode,
/// over the edge's step), and only the 2R + 3 around the edge are not
/// exactly 0; carried into the variance through its moments, plus the
/// kernel's weights' own float rounding (2 ( 2R + 1 ) ULP of the variance).
/// Then to sigma. Three times it.
double spreadBound( double sigma, double g, double mean, int R )
{
	const double lo = srgbToLinear( kEdgeB[ 0 ].g / 255.0 ) * g, hi = srgbToLinear( kEdgeB[ 1 ].g / 255.0 ) * g;
	const double oHi = linearToSrgb( hi );
	const double eps = decodeSlope( oHi + kPowBound ) * outputBound( hi, R, true ) / ( hi - lo );
	const double r1 = R + 1.0;
	const double sumD2 = 2.0 * r1 * ( r1 + 1.0 ) * ( 2.0 * r1 + 1.0 ) / 6.0, sumD = r1 * ( r1 + 1.0 ), n = 2.0 * R + 3.0;
	const double var  = sigma * sigma;
	const double dm0  = 2.0 * eps * n;
	const double dvar = 2.0 * eps * sumD2 + var * dm0 + 2.0 * std::fabs( mean ) * 2.0 * eps * sumD + mean * mean * dm0 + var * 2.0 * ( 2.0 * R + 1.0 ) * kFloatUlp;
	return 3.0 * ( sigma > 0.0 ? dvar / ( 2.0 * sigma ) : std::sqrt( dvar ) );
}

/// sigma from a measured spread, through the discrete kernel, by bisection.
double sigmaFromSpread( double spread )
{
	double lo = 0.17, hi = 30.0;
	for( int i = 0; i < 100; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( kernelSpread( mid ) < spread ? lo : hi ) = mid;
	}
	return 0.5 * ( lo + hi );
}

/// One frame's edge against the model: the spread measured from the picture
/// and the model's, with its bound. False while projector B's shutter is
/// not open (the edge's step would not be the card's).
struct SpreadFrame
{
	Spread s;
	double expect = 0.0, err = 0.0, tol = 0.0;
	bool atFloor  = false;
};

bool spreadFrame( const Lockstep& l, MixRig& rig, SpreadFrame& f )
{
	const int W = rig.out.width, H = rig.out.height;
	const ref::Out e  = l.expect();
	const ref::Out e4 = l.simRk4.now( W );
	if( e.shutter[ 1 ] < 1.0 )
		return false;
	f.s         = measureSpread( rig.PixelsF(), W, H );
	const int R = radiusFor( std::max( e.sigma[ 1 ], e4.sigma[ 1 ] ) );
	//The model's sigma and its method error, measured; plus the two
	//implementations' rounding of the heat (kImplementationBound is far
	//above it), as pixels.
	f.expect            = kernelSpread( e.sigma[ 1 ] );
	const double method = 3.0 * std::fabs( f.expect - kernelSpread( e4.sigma[ 1 ] ) ) + kImplementationBound * W;
	f.tol               = spreadBound( std::max( f.s.sigma, f.expect ), e.g[ 1 ][ 1 ], f.s.mean, R ) + method;
	f.err               = std::fabs( f.s.sigma - f.expect );
	//At the 0.17 px floor either side is right if the method error reaches
	//it.
	const double raw = l.ctl.glass ? 0.0 : l.ctl.popMm * W / ( 4.0 * 2.8 * 36.0 ) * std::fabs( 1.0 - e.heat[ 1 ] );
	f.atFloor        = !l.ctl.glass && std::fabs( raw - 0.17 ) <= 3.0 * std::fabs( e.sigma[ 1 ] - e4.sigma[ 1 ] ) + kImplementationBound * W;
	if( f.atFloor )
		f.err = std::min( f.err, std::fabs( f.s.sigma - kernelSpread( raw >= 0.17 ? 0.0 : 0.17 ) ) );
	return true;
}

void popCheck( int W, int H, int fault, bool print = false )
{
	for( int glass = 0; glass < 2; ++glass )
	{
		MixRig rig;
		if( !rig.Init( W, H, true, fault ) )
			return;
		rig.startSeconds = kStart;
		rig.UploadA( edgeCard( W, H, false ) );
		rig.UploadB( edgeCard( W, H, true ) );
		Lockstep l( rig );
		l.set( "Pop", 1.0f );
		l.set( "Mounts", static_cast< float >( glass ) );
		const int pressAt = 5, secondAt = 540, frames = 640;
		double worst = 0.0, wErr = 0.0, wTol = 0.0, maxSigma = 0.0, sharpAfter = -1.0;
		int measured = 0, straddled = 0;
		//For tau: ( t, ln sigma, its bound ) while the drift is a pure
		//exponential -- the cycle over, the lamp at rated, before the second
		//change, sigma at least 0.5 px where the discrete kernel is well
		//conditioned.
		struct Point
		{
			double t, y, e;
		};
		std::vector< Point > fit;
		for( int k = 0; k < frames; ++k )
		{
			if( k == pressAt || ( k == secondAt && !glass ) )
				l.press( "Change B" );
			if( !l.frame() )
				break;
			SpreadFrame f;
			if( !spreadFrame( l, rig, f ) )
				continue;
			straddled += f.atFloor;
			const double r = std::isnan( f.err ) ? f.err : f.err / f.tol;
			if( std::isnan( r ) || r > worst )
			{
				wErr = f.err;
				wTol = f.tol;
			}
			worst = worse( worst, r );
			++measured;
			maxSigma = std::max( maxSigma, f.s.sigma );
			//The drift's end, from the picture: the first frame after the
			//change whose edge is a step again.
			if( !glass && k > pressAt + 60 && k < secondAt && sharpAfter < 0.0 && f.s.sigma == 0.0 )
				sharpAfter = rig.SecondsOf( k ) - rig.SecondsOf( pressAt + 60 );
			if( !glass && k > pressAt + 60 && k < secondAt && l.expect().sigma[ 1 ] >= 0.5 )
			{
				const double sigma = sigmaFromSpread( f.s.sigma );
				const double slope = ( kernelSpread( sigma + 1e-6 ) - kernelSpread( sigma - 1e-6 ) ) / 2e-6;
				fit.push_back( { rig.SecondsOf( k ), std::log( sigma ), f.tol / slope / sigma } );
			}
		}
		const std::string what = glass ? "Glass, Pop 1 mm: " : "Card, Pop 1 mm: ";
		Check( measured > 100 && worst <= 1.0, what + fmt( "%.0f frames with the shutter open, the edge's spread from the picture is the model's (worst %.2g px against %.2g)",
		                                                   measured, wErr, wTol ) );
		if( glass )
		{
			Check( maxSigma == 0.0, what + fmt( "the edge never spreads (largest second moment %.3g px)", maxSigma ) );
			continue;
		}
		Check( maxSigma > 0.5 && sharpAfter > 0.0,
		       what + fmt( "the freshly changed slide is out of focus (%.3f px when the shutter opens) and sharp again %.2f s later", maxSigma, sharpAfter ) );
		if( fit.size() < 20 )
		{
			Check( false, fmt( "tau: only %.0f frames to fit", static_cast< double >( fit.size() ) ) );
			continue;
		}
		//Least squares, and its slope's bound by linear propagation of each
		//point's: | d slope | <= sum | t_i - mean t | e_i / sum ( t_i - mean t )^2.
		double mt = 0.0, my = 0.0;
		for( const Point& p : fit )
		{
			mt += p.t;
			my += p.y;
		}
		mt /= static_cast< double >( fit.size() );
		my /= static_cast< double >( fit.size() );
		double stt = 0.0, sty = 0.0, se = 0.0;
		for( const Point& p : fit )
		{
			stt += ( p.t - mt ) * ( p.t - mt );
			sty += ( p.t - mt ) * ( p.y - my );
			se += std::fabs( p.t - mt ) * p.e;
		}
		const double slope  = sty / stt;
		const double tau    = -1.0 / slope;
		const double tolTau = tau * tau * se / stt;
		Check( std::fabs( tau - 3.0 ) <= tolTau,
		       fmt( "tau fitted from %.0f frames of the picture: %.5f s against the stated 3 s (tolerance %.2g s)", static_cast< double >( fit.size() ), tau, tolTau ) );
		if( print )
			std::printf( "    %d frame(s) within the method error of the 0.17 px floor\n", straddled );
	}
}

int runPop()
{
	std::printf( "the pop: a cold card-mounted slide out of focus, drifting in as it warms\n" );
	atBothRasters( []( int w, int h, int f ) { popCheck( w, h, f, f == kFaultNone ); },
	               { { kFaultPopNoReset, "the slide's heat kept across a change" }, { kFaultPopTau, "the drift's time constant 1 s" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --alpha: a transparent pixel is opaque black film, whatever is under it,
// through resampling and blur alike; a half-transparent one is decode( rgb a ).
//---------------------------------------------------------------------------
Image paintUnderTransparent( const Image& in, int width, Rgb c, int extraOpaque )
{
	Image out  = in;
	int opaque = 0;
	for( size_t i = 0; i < out.size(); i += 4 )
		if( out[ i + 3 ] == 0 )
		{
			out[ i ]     = c.r;
			out[ i + 1 ] = c.g;
			out[ i + 2 ] = c.b;
		}
		else if( opaque < extraOpaque && out[ i + 3 ] == 255 && ( ( i / 4 ) % static_cast< size_t >( width ) ) % 7 == 3 )
		{
			out[ i ] = out[ i + 1 ] = out[ i + 2 ] = 255;
			++opaque;
		}
	return out;
}

void alphaCheck( int W, int H, int fault )
{
	struct Case
	{
		const char* what;
		int w, h;
		bool blurred;
	};
	const Case cases[] = { { "at the output's size", W, H, false }, { "resampled from 0.6 of the output", W * 3 / 5, H * 3 / 5, false }, { "blurred", W, H, true } };
	for( const Case& cs : cases )
		for( int which = 0; which < 2; ++which )
		{
			Image outs[ 3 ];
			bool wasBlurred = false;
			for( int variant = 0; variant < 3; ++variant )
			{
				MixRig rig;
				const InputSpec mine  = InputSpec::Exact( cs.w, cs.h );
				const InputSpec other = InputSpec::Exact( W, H );
				if( !rig.Init( W, H, which == 0 ? mine : other, which == 1 ? mine : other, false, fault ) )
					return;
				rig.startSeconds = kStart;
				Image card       = slideCard( cs.w, cs.h, which == 1, true );
				if( variant == 1 )
					card = paintUnderTransparent( card, cs.w, kWhite, 0 );
				if( variant == 2 )
					card = paintUnderTransparent( card, cs.w, kWhite, 200 );
				const Image dark = slideCard( W, H, which == 0, true );
				rig.UploadA( which == 0 ? card : dark );
				rig.UploadB( which == 1 ? card : dark );
				//This projector lit, at Low so that the gain is not 1 and the
				//general path runs; the other dark.
				rig.Set( "Opacity", static_cast< float >( which ) );
				rig.Set( "Lamp", 1.0f );
				rig.Set( "Pop", 1.0f );
				for( int k = 0; k < 4; ++k )
					rig.Step();
				if( cs.blurred )
				{
					const char* button = which ? "Change B" : "Change A";
					rig.Press( button );
					for( int k = 0; k < 66; ++k )
					{
						rig.Step();
						rig.Release( button );
					}
					wasBlurred = rig.plugin.InstantForTest().p[ which ].sigmaPx > 0.0;
				}
				outs[ variant ] = rig.Pixels();
			}
			const std::string what = std::string( which ? "B " : "A " ) + cs.what + ": ";
			Check( differingBytes( outs[ 0 ], outs[ 1 ] ) == 0 && ( !cs.blurred || wasBlurred ),
			       what + fmt( "white painted under every transparent pixel changes %.0f bytes", differingBytes( outs[ 0 ], outs[ 1 ] ) ) );
			Check( differingBytes( outs[ 0 ], outs[ 2 ] ) > 0,
			       what + fmt( "200 opaque pixels painted white change %.0f (the check can see)", differingBytes( outs[ 0 ], outs[ 2 ] ) ) );
		}

	//A half-transparent pixel: encode( gain x decode( rgb x a ) ), at Low.
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return;
	rig.startSeconds = kStart;
	Image b = splitCard( W, H, true );
	for( int y = 0; y < H / 2; ++y )
		for( int x = 0; x < W; ++x )
			if( bandOf( x, W ) == 1 )
				b[ at4( W, x, y ) + 3 ] = 128;
	rig.UploadA( splitCard( W, H, false ) );
	rig.UploadB( b );
	rig.Set( "Lamp", 1.0f );
	rig.Set( "Mounts", 1.0f );//glass: no blur across the alpha step
	for( int k = 0; k < 4; ++k )
		rig.Step();
	const ImageF out = rig.PixelsF();
	const int x      = W / 2;
	double worst = 0.0, worstTol = 0.0;
	for( int c = 0; c < 3; ++c )
	{
		const unsigned char v = c == 0 ? kSplitB[ 1 ].r : c == 1 ? kSplitB[ 1 ].g : kSplitB[ 1 ].b;
		const double slide    = srgbToLinear( v / 255.0 );
		const double oOpaque  = out[ at4( W, x, 3 * H / 4 ) + static_cast< size_t >( c ) ];
		const double gain     = srgbToLinear( oOpaque ) / slide;
		const double tau      = srgbToLinear( ( v / 255.0 ) * ( 128.0 / 255.0 ) );
		const double y        = gain * tau;
		const double expect   = linearToSrgb( y );
		const double got      = out[ at4( W, x, H / 4 ) + static_cast< size_t >( c ) ];
		//got's own bound, and the gain's (read from the opaque pixel)
		//carried through encode.
		const double dGain = decodeSlope( oOpaque + kPowBound ) * outputBound( gain * slide, 0, true ) / slide;
		const double tol   = 3.0 * ( outputBound( y, 0, true ) + encodeSlope( y ) * tau * dGain );
		const double r     = std::fabs( got - expect ) / tol;
		if( !( r <= worst ) )
			worstTol = tol;
		worst = worse( worst, r );
	}
	Check( worst <= 1.0, fmt( "a pixel at alpha 128 is encode( gain x decode( rgb x a ) ): worst %.3f of the tolerance (%.2g)", worst, worstTol ) );
}

int runAlpha()
{
	std::printf( "alpha: a transparent pixel is opaque black film; premultiplied before resampling or blur\n" );
	atBothRasters( []( int w, int h, int f ) { alphaCheck( w, h, f ); }, { { kFaultNoPremultiply, "resampling and blur on straight colour" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --linear: Linear Light adds the two projectors' light; off adds code values.
//---------------------------------------------------------------------------
void linearCheck( int W, int H, int fault )
{
	for( int linear = 1; linear >= 0; --linear )
	{
		MixRig rig;
		if( !rig.Init( W, H, true, fault ) )
			return;
		rig.startSeconds = kStart;
		rig.UploadA( splitCard( W, H, false ) );
		rig.UploadB( splitCard( W, H, true ) );
		Lockstep l( rig );
		l.set( "Linear Light", static_cast< float >( linear ) );
		l.set( "Curve", 1.0f );
		l.set( "Time", 0.0f );
		l.set( "Opacity", 0.35f );
		for( int k = 0; k < 30; ++k )
			l.frame();
		const Measured m = measureSplit( rig.PixelsF(), W, H, linear != 0, marginFor( W ) );
		const ref::Out e = l.expect();
		double worst = 0.0, worstTol = 0.0;
		for( int c = 0; c < 3; ++c )
		{
			const unsigned char va = c == 0 ? kSplitA[ 2 ].r : c == 1 ? kSplitA[ 2 ].g : kSplitA[ 2 ].b;
			const unsigned char vb = c == 0 ? kSplitB[ 2 ].r : c == 1 ? kSplitB[ 2 ].g : kSplitB[ 2 ].b;
			const double ta        = linear ? srgbToLinear( va / 255.0 ) : va / 255.0;
			const double tb        = linear ? srgbToLinear( vb / 255.0 ) : vb / 255.0;
			const double expect    = m.g[ 0 ][ c ] * ta + m.g[ 1 ][ c ] * tb;
			//Band 2's own bound, and each gain's (read from bands 0 and 1)
			//times its transmittance.
			const int R       = radiusFor( std::max( e.sigma[ 0 ], e.sigma[ 1 ] ) );
			const double back = linear ? decodeSlope( m.out2[ c ] + kPowBound ) : 1.0;
			const double tol  = 3.0 * back * outputBound( expect, R, linear != 0 ) + ta * readBound( m, 0, c, e.g[ 0 ][ c ], R )
			                   + tb * readBound( m, 1, c, e.g[ 1 ][ c ], R );
			const double r = std::fabs( m.band2[ c ] - expect ) / tol;
			if( !( r <= worst ) )
				worstTol = tol;
			worst = worse( worst, r );
		}
		Check( m.pixels > 0 && worst <= 1.0, std::string( linear ? "Linear Light on, in light: " : "Linear Light off, in code values: " )
		                                         + fmt( "the band lit by both is g_A t_A + g_B t_B, worst %.3f of the tolerance (%.2g)", worst, worstTol ) );
	}
}

int runLinear()
{
	std::printf( "the two projectors' light adds\n" );
	atBothRasters( []( int w, int h, int f ) { linearCheck( w, h, f ); }, { { kFaultGammaMix, "Linear Light's sum on code values" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --resize: the state survives a resize.
//---------------------------------------------------------------------------
void resizeCheck( int W, int H, int fault )
{
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return;
	rig.startSeconds = kStart;
	rig.UploadA( splitCard( W, H, false ) );
	rig.UploadB( splitCard( W, H, true ) );
	Lockstep l( rig );
	l.set( "Time", ParamForTime( 2.0 ) );
	l.set( "Opacity", 0.0f );
	const int ow = W == 640 ? 320 : 640, oh = W == 640 ? 180 : 360;
	splitTimeline(
		rig, l, 140,
		[ & ]( int k, Lockstep& s ) {
			if( k == 10 )
				s.set( "Opacity", 1.0f );
			if( k == 40 )
				s.press( "Change A" );
			if( k == 50 )
			{
				//Mid-dissolve and mid-change: a new output size, and inputs at
				//a third.
				rig.ResizeOutput( ow, oh );
				rig.MakeInputs( InputSpec::Exact( 480, 270 ), InputSpec::Exact( 480, 270 ) );
				rig.UploadA( splitCard( 480, 270, false ) );
				rig.UploadB( splitCard( 480, 270, true ) );
			}
			if( k == 100 )
			{
				rig.ResizeOutput( W, H );
				rig.MakeInputs( InputSpec::Exact( W, H ), InputSpec::Exact( W, H ) );
				rig.UploadA( splitCard( W, H, false ) );
				rig.UploadB( splitCard( W, H, true ) );
			}
		},
		fmt( "a dissolve and a change, resized at frame 50 to %.0fx%.0f with 480x270 inputs and back at 100: every frame", ow, oh ) );
}

/// The slide's heat across a resize: B's edge drifting into focus, resized
/// to the other raster mid-drift and back, every frame's spread against the
/// model's at the size it is drawn.
void resizeDriftCheck( int W, int H, int fault )
{
	MixRig rig;
	if( !rig.Init( W, H, true, fault ) )
		return;
	rig.startSeconds = kStart;
	rig.UploadA( edgeCard( W, H, false ) );
	rig.UploadB( edgeCard( W, H, true ) );
	Lockstep l( rig );
	l.set( "Pop", 1.0f );
	const int ow = W == 640 ? 320 : 640, oh = W == 640 ? 180 : 360;
	double worst = 0.0, wErr = 0.0, wTol = 0.0;
	int measured = 0, blurredAfter = 0;
	for( int k = 0; k < 300; ++k )
	{
		if( k == 5 )
			l.press( "Change B" );
		const int size[ 2 ][ 2 ] = { { ow, oh }, { W, H } };
		if( k == 120 || k == 200 )
		{
			const int* to = size[ k == 120 ? 0 : 1 ];
			rig.ResizeOutput( to[ 0 ], to[ 1 ] );
			rig.MakeInputs( InputSpec::Exact( to[ 0 ], to[ 1 ] ), InputSpec::Exact( to[ 0 ], to[ 1 ] ) );
			rig.UploadA( edgeCard( to[ 0 ], to[ 1 ], false ) );
			rig.UploadB( edgeCard( to[ 0 ], to[ 1 ], true ) );
		}
		if( !l.frame() )
			break;
		SpreadFrame f;
		if( !spreadFrame( l, rig, f ) )
			continue;
		const double r = std::isnan( f.err ) ? f.err : f.err / f.tol;
		if( std::isnan( r ) || r > worst )
		{
			wErr = f.err;
			wTol = f.tol;
		}
		worst = worse( worst, r );
		++measured;
		blurredAfter += k >= 120 && f.s.sigma > 0.0;
	}
	Check( measured > 200 && blurredAfter > 30 && worst <= 1.0,
	       fmt( "B's slide drifting into focus, resized at frame 120 to %.0fx%.0f and back at 200: every frame's spread is the model's (worst %.2g px against %.2g)", ow, oh,
	            wErr, wTol ) );
}

int runResize()
{
	std::printf( "a resize keeps the lamps, the dissolve, the shutter and the slides' heat\n" );
	atBothRasters(
		[]( int w, int h, int f ) {
			resizeCheck( w, h, f );
			resizeDriftCheck( w, h, f );
		},
		{ { kFaultResizeResets, "a resize restarts the projectors at rest" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --rates: the same timeline at four frame rates, against continuous time.
//---------------------------------------------------------------------------
void ratesCheck( int W, int H, int fault )
{
	const double rates[] = { 24.0, 60000.0 / 1001.0, 60.0, 144.0 };
	for( double fps : rates )
	{
		MixRig rig;
		if( !rig.Init( W, H, true, fault ) )
			return;
		rig.startSeconds = kStart;
		rig.fps          = fps;
		rig.UploadA( splitCard( W, H, false ) );
		rig.UploadB( splitCard( W, H, true ) );
		Lockstep l( rig );
		l.set( "Time", 0.0f );
		const int n = static_cast< int >( std::ceil( 4.0 * fps ) );
		const int cut = static_cast< int >( std::ceil( 0.3 * fps ) ), change = static_cast< int >( std::ceil( 1.1 * fps ) ),
		          dissolve = static_cast< int >( std::ceil( 2.4 * fps ) );
		splitTimeline(
			rig, l, n,
			[ & ]( int k, Lockstep& s ) {
				if( k == cut )
					s.set( "Opacity", 0.0f );
				if( k == change )
					s.press( "Change A" );
				if( k == dissolve )
				{
					s.set( "Time", ParamForTime( 1.3 ) );
					s.set( "Opacity", 1.0f );
				}
			},
			fmt( "%.3f fps: a cut, a change, a 1.3 s dissolve, 4 s", fps ) );
	}
}

int runRates()
{
	std::printf( "frame-rate independence: 24, 59.94, 60, 144 fps\n" );
	atBothRasters( []( int w, int h, int f ) { ratesCheck( w, h, f ); },
	               { { kFaultFrameLag, "the lamps stepped 1/60 s per frame" }, { kFaultSlewFrames, "the dissolve advanced per frame" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --clock: any origin, either unit; and the unit vote with a wall clock.
//---------------------------------------------------------------------------
void clockCheck( int W, int H, int fault )
{
	struct Origin
	{
		const char* what;
		double seconds, scale;
		bool vote;
	};
	const Origin origins[] = {
		{ "0 ms", 0.0, 0.001, false },
		{ "4.1e6 ms (1.1 hours)", 4.1e3, 0.001, false },
		{ "4.99e8 ms (5.8 days)", 4.99e5, 0.001, false },
		{ "3.2e10 ms (a year)", 3.2e7, 0.001, false },
		{ "4.99e5 s", 4.99e5, 1.0, false },
		{ "4.99e8 ms, the unit by vote", 4.99e5, 0.001, true },
		{ "4.99e5 s, the unit by vote", 4.99e5, 1.0, true },
	};
	for( const Origin& o : origins )
	{
		MixRig rig;
		if( !rig.Init( W, H, true, fault ) )
			return;
		rig.startSeconds = o.seconds;
		rig.unitScale    = o.scale;
		rig.declareUnit  = !o.vote;
		rig.wallFollows  = o.vote;
		rig.UploadA( splitCard( W, H, false ) );
		rig.UploadB( splitCard( W, H, true ) );
		Lockstep l( rig );
		l.set( "Time", ParamForTime( 1.0 ) );
		splitTimeline(
			rig, l, 120,
			[]( int k, Lockstep& s ) {
				if( k == 9 )
					s.set( "Opacity", 0.0f );
				if( k == 70 )
					s.press( "Change A" );
			},
			std::string( "from " ) + o.what + ": a 1 s dissolve and a change" );
		if( o.vote )
			Check( rig.plugin.ClockForTest().Scale() == o.scale, fmt( "the vote settled on the host's unit, %.3g s", rig.plugin.ClockForTest().Scale() ) );
	}
}

int runClock()
{
	std::printf( "the host's clock: any origin, ms or s\n" );
	atBothRasters( []( int w, int h, int f ) { clockCheck( w, h, f ); }, { { kFaultFloatClock, "the host's readings held in floats" } } );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --mutation: the hook compiles the text it is given; one character changed
// in the shipped screen shader fails a model check.
//---------------------------------------------------------------------------
void steadyCheck( const char* fragment )
{
	MixRig rig;
	if( !rig.Init( 320, 180, InputSpec::Exact( 320, 180 ), InputSpec::Exact( 320, 180 ), true, kFaultNone, fragment ) )
	{
		Check( false, "InitGL" );
		return;
	}
	rig.startSeconds = kStart;
	rig.UploadA( splitCard( 320, 180, false ) );
	rig.UploadB( splitCard( 320, 180, true ) );
	Lockstep l( rig );
	l.set( "Curve", 1.0f );
	l.set( "Time", 0.0f );
	l.set( "Opacity", 0.35f );
	splitTimeline( rig, l, 20, []( int, Lockstep& ) {}, "both lit, unequal (Light, 0.35): every frame against the model" );
}

int runMutation()
{
	std::printf( "the shader hook, and one character of the shipped screen shader\n\n" );
	const std::string shipped = AssembleFragment( kScreenFragment );
	const std::string from    = "GainA * a + GainB * b;";
	const std::string to      = "GainA * a + GainA * b;";
	const size_t at           = shipped.find( from );
	Check( at != std::string::npos && shipped.find( from, at + 1 ) == std::string::npos, "the line to mutate occurs exactly once in the shipped shader" );
	if( at == std::string::npos )
		return 1;
	std::string mutated = shipped;
	mutated.replace( at, from.size(), to );
	int differ = 0;
	for( size_t i = 0; i < shipped.size(); ++i )
		differ += shipped[ i ] != mutated[ i ];
	Check( differ == 1, fmt( "the mutation is one character (%.0f differ): B's light weighted by A's gain", differ ) );

	Image defaultBytes, hookBytes;
	for( int pass = 0; pass < 2; ++pass )
	{
		MixRig rig;
		if( !rig.Init( 320, 180, InputSpec::Exact( 320, 180 ), InputSpec::Exact( 320, 180 ), false, kFaultNone, pass ? shipped.c_str() : nullptr ) )
			return 1;
		rig.UploadA( slideCard( 320, 180, false ) );
		rig.UploadB( slideCard( 320, 180, true ) );
		rig.Set( "Opacity", 0.35f );
		rig.Set( "Curve", 1.0f );
		for( int k = 0; k < 3; ++k )
			rig.Step();
		( pass ? hookBytes : defaultBytes ) = rig.Pixels();
	}
	Check( differingBytes( defaultBytes, hookBytes ) == 0, "the shipped text through the hook renders the default path's bytes" );
	const int shippedFails = failuresOf( [ & ] { steadyCheck( shipped.c_str() ); } );
	Check( shippedFails == 0, "the model check passes with the shipped text through the hook" );
	const int mutatedFails = failuresOf( [ & ] { steadyCheck( mutated.c_str() ); } );
	Negative( mutatedFails > 0, fmt( "and fails the mutated shader (%.0f assertion(s))", mutatedFails ) );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --bench
//---------------------------------------------------------------------------
constexpr int kBenchWarmUp = 60;

/// ms per frame. Mode 0: one projector lit at rated, in focus (the exact
/// path: a fetch). 1: both lit (Light, midpoint), glass-mounted: both
/// fetched, decoded and added, unblurred. 2: both lit and both slides changed a
/// second earlier at Pop 1 mm -- the widest blur the controls allow, both
/// projectors: the worst case.
double benchAt( int width, int height, int frames, int mode )
{
	MixRig rig;
	if( !rig.Init( width, height, false ) )
		return -1.0;
	rig.UploadA( slideCard( width, height, false ) );
	rig.UploadB( slideCard( width, height, true ) );
	if( mode >= 1 )
	{
		rig.Set( "Curve", 1.0f );
		rig.Set( "Time", 0.0f );
		rig.Set( "Opacity", 0.5f );
		rig.Set( "Mounts", mode == 1 ? 1.0f : 0.0f );
		rig.Set( "Pop", 1.0f );
	}
	if( mode == 2 )
	{
		rig.Press( "Change A" );
		rig.Press( "Change B" );
	}
	for( int frame = 0; frame < kBenchWarmUp + 7; ++frame )
	{
		rig.Step();
		rig.Release( "Change A" );
		rig.Release( "Change B" );
	}
	glFinish();
	const auto start = std::chrono::steady_clock::now();
	for( int frame = 0; frame < frames; ++frame )
		rig.Step();
	glFinish();
	const auto end   = std::chrono::steady_clock::now();
	const Instant& i = rig.plugin.InstantForTest();
	const bool both  = i.p[ 0 ].sigmaPx > 0.0 && i.p[ 1 ].sigmaPx > 0.0;
	if( ( mode == 2 ) != both )
		std::fprintf( stderr, "bench: mode %d's blur is not as described\n", mode );
	return std::chrono::duration< double >( end - start ).count() * 1000.0 / frames;
}

int runBench( int frames )
{
	struct Size
	{
		const char* name;
		int width, height;
	};
	const Size sizes[] = { { "1280x720 ", 1280, 720 }, { "1920x1080", 1920, 1080 }, { "3840x2160", 3840, 2160 } };
	std::printf( "%d frames each after a %d-frame warm-up, glFinish both sides; both inputs at the output size, 8-bit.\n", frames, kBenchWarmUp );
	std::printf( "one lit: one projector at rated, in focus (a fetch). both lit: Light at the midpoint, glass: both fetched, decoded and added.\n" );
	std::printf( "both blurred: both lit and both slides just changed at Pop 1 mm, the worst case.\n\n" );
	std::printf( "resolution    one lit ms   both lit ms   both blurred ms   (%% of a 60 fps frame)\n" );
	for( const Size& size : sizes )
	{
		double ms[ 3 ];
		for( int mode = 0; mode < 3; ++mode )
			if( ( ms[ mode ] = benchAt( size.width, size.height, frames, mode ) ) < 0.0 )
				return 1;
		std::printf( "%s     %7.3f      %7.3f        %7.3f         (%.1f%% / %.1f%% / %.1f%%)\n", size.name, ms[ 0 ], ms[ 1 ], ms[ 2 ], ms[ 0 ] / 16.667 * 100.0,
		             ms[ 1 ] / 16.667 * 100.0, ms[ 2 ] / 16.667 * 100.0 );
	}
	return 0;
}

//---------------------------------------------------------------------------
// --set, strictly: a value is a number, all of it, or an option's element
// name. Anything else is refused, never read as 0.
//---------------------------------------------------------------------------
bool applyOne( Carousel& plugin, const std::string& setting )
{
	const size_t equals = setting.find( '=' );
	if( equals == std::string::npos )
	{
		std::fprintf( stderr, "--set %s: expected Name=Value\n", setting.c_str() );
		return false;
	}
	const std::string name = setting.substr( 0, equals ), text = setting.substr( equals + 1 );
	const int id           = paramId( plugin, name );
	if( id < 0 )
	{
		std::fprintf( stderr, "--set %s: no parameter called '%s' (try --list)\n", setting.c_str(), name.c_str() );
		return false;
	}
	char* end         = nullptr;
	const float value = std::strtof( text.c_str(), &end );
	if( !text.empty() && end != nullptr && *end == '\0' && std::isfinite( value ) )
	{
		plugin.SetFloatParameter( static_cast< unsigned >( id ), value );
		return true;
	}
	if( plugin.GetParamType( static_cast< unsigned >( id ) ) == FF_TYPE_OPTION )
		for( unsigned e = 0; e < plugin.GetNumParamElements( static_cast< unsigned >( id ) ); ++e )
			if( text == plugin.GetParamElementName( static_cast< unsigned >( id ), e ) )
			{
				plugin.SetFloatParameter( static_cast< unsigned >( id ), static_cast< float >( e ) );
				return true;
			}
	std::fprintf( stderr, "--set %s: '%s' is not a number%s\n", setting.c_str(), text.c_str(),
	              plugin.GetParamType( static_cast< unsigned >( id ) ) == FF_TYPE_OPTION ? " or one of the option's names (try --list)" : "" );
	return false;
}

bool applySettings( Carousel& plugin, const std::vector< std::string >& settings )
{
	for( const std::string& s : settings )
		if( !applyOne( plugin, s ) )
			return false;
	return true;
}

//---------------------------------------------------------------------------
// --pipe: the fleet's frame format, with a second input, in relay's,
// lenticular's and penumbra's shape.
//
//   * stdin is DEST, inputTextures[0], the layer below: projector A.
//     Output-sized, raw RGBA, top row first.
//   * `--pipe-src PATH` is SRC, inputTextures[1], this layer: projector B.
//     Raw RGBA, top row first, at `--src-size` (default: the output size).
//     A FIFO will do. Without it the `--input-b` card is held.
//
// The clock is 60 fps from 0 (`--fps` to change it). `--script` is the cue
// sheet: `frame Name value`, value in host units, '#' a comment; a STANDARD
// parameter ramps between keys, the rest step (a button is pressed by a
// step to 1 and released by a step to 0). Unknown names are refused before
// a frame is read. SIGPIPE is ignored: a closed stdout is exit 1.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return tracks;
	}
	std::string line;
	int lineNumber = 0;
	while( std::getline( file, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream in( line );
		int frame = 0;
		if( !( in >> frame ) )
			continue;
		std::vector< std::string > words;
		std::string word;
		while( in >> word )
			words.push_back( word );
		if( words.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		char* end         = nullptr;
		const float value = std::strtof( words.back().c_str(), &end );
		if( end == nullptr || *end != '\0' )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": '" + words.back() + "' is not a number";
			return {};
		}
		words.pop_back();
		std::string name = words.front();
		for( size_t i = 1; i < words.size(); ++i )
			name += " " + words[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::sort( entry.second.begin(), entry.second.end() );
	return tracks;
}

/// A track's value at a frame; NaN before a STEPPED track's first key, so a
/// button cued at frame 1 is not pressed at frame 0. A ramped track holds
/// its first key before it.
float valueAt( const Track& track, int frame, bool ramps )
{
	if( track.empty() )
		return std::nanf( "" );
	if( !ramps && frame < track.front().first )
		return std::nanf( "" );
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 1; i < track.size(); ++i )
	{
		if( frame < track[ i ].first )
		{
			const auto& a = track[ i - 1 ];
			const auto& b = track[ i ];
			if( !ramps )
				return a.second;
			const float span = static_cast< float >( b.first - a.first );
			const float t    = span > 0.0f ? ( static_cast< float >( frame - a.first ) / span ) : 1.0f;
			return a.second + ( b.second - a.second ) * t;
		}
		if( frame == track[ i ].first )
			return track[ i ].second;
	}
	return track.back().second;
}

struct Automation
{
	unsigned int id;
	bool ramps;
	Track track;
};

bool loadAutomation( Carousel& plugin, const std::string& scriptPath, std::vector< Automation >& automation )
{
	if( scriptPath.empty() )
		return true;
	std::string error;
	const std::map< std::string, Track > tracks = loadScript( scriptPath, error );
	if( !error.empty() )
	{
		std::fprintf( stderr, "%s\n", error.c_str() );
		return false;
	}
	for( const auto& entry : tracks )
	{
		const int id = paramId( plugin, entry.first );
		if( id < 0 )
		{
			std::fprintf( stderr, "script names '%s', which is not a parameter (try --list)\n", entry.first.c_str() );
			return false;
		}
		const bool ramps = plugin.GetParamType( static_cast< unsigned >( id ) ) == FF_TYPE_STANDARD;
		automation.push_back( { static_cast< unsigned >( id ), ramps, entry.second } );
	}
	return true;
}

bool readFrame( int fd, Image& frame )
{
	size_t filled = 0;
	while( filled < frame.size() )
	{
		const ssize_t got = read( fd, frame.data() + filled, frame.size() - filled );
		if( got <= 0 )
			return false;
		filled += static_cast< size_t >( got );
	}
	return true;
}

bool writeAll( int fd, const Image& frame )
{
	size_t written = 0;
	while( written < frame.size() )
	{
		const ssize_t n = write( fd, frame.data() + written, frame.size() - written );
		if( n <= 0 )
			return false;
		written += static_cast< size_t >( n );
	}
	return true;
}

void flipRows( const Image& in, Image& out, int width, int height )
{
	const size_t row = static_cast< size_t >( width ) * 4;
	for( int y = 0; y < height; ++y )
		std::memcpy( out.data() + static_cast< size_t >( height - 1 - y ) * row, in.data() + static_cast< size_t >( y ) * row, row );
}

int runPipe( int width, int height, int srcWidth, int srcHeight, double fps, const std::string& scriptPath, const std::string& srcPath, const std::string& inputB,
             const std::vector< std::string >& settings )
{
	signal( SIGPIPE, SIG_IGN );
	MixRig rig;
	if( !rig.Init( width, height, InputSpec::Exact( width, height ), InputSpec::Exact( srcWidth, srcHeight ), false ) )
		return 1;
	rig.fps = fps;
	if( !applySettings( rig.plugin, settings ) )
		return 2;
	std::vector< Automation > automation;
	if( !loadAutomation( rig.plugin, scriptPath, automation ) )
		return 2;

	int srcFd = -1;
	if( !srcPath.empty() )
	{
		srcFd = open( srcPath.c_str(), O_RDONLY );
		if( srcFd < 0 )
		{
			std::fprintf( stderr, "cannot open --pipe-src %s\n", srcPath.c_str() );
			return 2;
		}
	}
	else
		rig.UploadB( generate( inputB, srcWidth, srcHeight, true ) );

	Image destIn( static_cast< size_t >( width ) * height * 4 ), destUp( destIn.size() );
	Image srcIn( static_cast< size_t >( srcWidth ) * srcHeight * 4 ), srcUp( srcIn.size() );
	Image out( destIn.size() );
	int index  = 0;
	int result = 0;
	for( ;; ++index )
	{
		if( !readFrame( STDIN_FILENO, destIn ) )
			break;
		if( srcFd >= 0 )
		{
			if( !readFrame( srcFd, srcIn ) )
				break;
			flipRows( srcIn, srcUp, srcWidth, srcHeight );
			rig.UploadB( srcUp );
		}
		flipRows( destIn, destUp, width, height );
		rig.UploadA( destUp );
		for( const Automation& track : automation )
		{
			const float v = valueAt( track.track, index, track.ramps );
			if( !std::isnan( v ) )
				rig.plugin.SetFloatParameter( track.id, v );
		}
		if( !rig.Step() )
		{
			std::fprintf( stderr, "ProcessOpenGL failed at frame %d\n", index );
			result = 1;
			break;
		}
		flipRows( rig.Pixels(), out, width, height );
		if( !writeAll( STDOUT_FILENO, out ) )
		{
			std::fprintf( stderr, "catest --pipe: stdout closed at frame %d\n", index );
			result = 1;
			break;
		}
	}
	if( srcFd >= 0 )
		close( srcFd );
	std::fprintf( stderr, "catest --pipe: %d frames\n", index );
	return result;
}

//---------------------------------------------------------------------------
void usage()
{
	std::printf(
		"catest -- render and measure SW Carousel (FFGL mixer)\n"
		"\n"
		"  --out PATH        render through the plugin (default carousel.png)\n"
		"  --input-a NAME    DEST, projector A's slide (default slide)\n"
		"  --input-b NAME    SRC, projector B's slide (default slide)\n"
		"                    %s\n"
		"  --src-size WxH    Src's size (default: the output size)\n"
		"  --size WxH        picture size (default 1280x720)\n"
		"  --frames N        frames to render for --out at --fps (the last is written), or --bench's count\n"
		"  --fps F           the synthetic clock's rate for --out and --pipe (default 60)\n"
		"  --set \"Name=V\"    a parameter by display name, in host units, or an option by its\n"
		"                    element name (\"Curve=Light\"). Repeatable. Unparsable values are refused.\n"
		"  --script PATH     cues for --out and --pipe: 'frame Name value'; standard parameters ramp, the rest step\n"
		"  --list            every parameter, its kind, default and range\n"
		"  --names --mixer --unchanged --lamp --colour --dissolve --cut --change --pop --alpha\n"
		"  --linear --resize --rates --clock --mutation\n"
		"                    the checks (see the top of tools/catest/main.cpp)\n"
		"  --bench           ms/frame at 720p, 1080p and 4K (--frames N, default 30)\n"
		"  --pipe            raw RGBA Dest frames on stdin, raw RGBA frames on stdout\n"
		"  --pipe-src PATH   raw RGBA Src frames for --pipe (a file or FIFO); default: --input-b, held\n"
		"  --help\n",
		kCardNames );
}

bool parseSize( const std::string& text, int& w, int& h )
{
	const size_t x = text.find( 'x' );
	if( x == std::string::npos )
		return false;
	char* end = nullptr;
	w         = static_cast< int >( std::strtol( text.substr( 0, x ).c_str(), &end, 10 ) );
	if( end == nullptr || *end != '\0' )
		return false;
	h = static_cast< int >( std::strtol( text.substr( x + 1 ).c_str(), &end, 10 ) );
	return end != nullptr && *end == '\0' && w > 0 && h > 0 && w <= 16384 && h <= 16384;
}
} // namespace

int main( int argc, char** argv )
{
	std::string outPath = "carousel.png";
	std::string inputA = "slide", inputB = "slide";
	int width = 1280, height = 720, srcWidth = 0, srcHeight = 0, frames = 0;
	double fps    = 60.0;
	bool wantList = false, wantBench = false, wantPipe = false;
	std::string scriptPath, srcPath, check;
	std::vector< std::string > settings;
	static const char* checks[] = { "--names", "--mixer", "--unchanged", "--lamp", "--colour", "--dissolve", "--cut", "--change",
		                            "--pop", "--alpha", "--linear", "--resize", "--rates", "--clock", "--mutation" };

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		bool isCheck               = false;
		for( const char* c : checks )
			isCheck = isCheck || argument == c;
		if( argument == "--help" )
		{
			usage();
			return 0;
		}
		else if( isCheck )
			check = argument;
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( ( argument == "--input-a" || argument == "--input-b" ) && hasNext )
		{
			const std::string name = argv[ ++i ];
			if( !isCard( name ) )
			{
				std::fprintf( stderr, "%s: no card called '%s' (%s)\n", argument.c_str(), name.c_str(), kCardNames );
				return 2;
			}
			( argument == "--input-a" ? inputA : inputB ) = name;
		}
		else if( ( argument == "--size" || argument == "--src-size" ) && hasNext )
		{
			if( !parseSize( argv[ ++i ], argument == "--size" ? width : srcWidth, argument == "--size" ? height : srcHeight ) )
			{
				std::fprintf( stderr, "%s wants a positive WxH\n", argument.c_str() );
				return 2;
			}
		}
		else if( argument == "--frames" && hasNext )
		{
			char* end        = nullptr;
			const long value = std::strtol( argv[ ++i ], &end, 10 );
			if( end == nullptr || *end != '\0' || value <= 0 || value > 1000000 )
			{
				std::fprintf( stderr, "--frames wants a positive whole number\n" );
				return 2;
			}
			frames = static_cast< int >( value );
		}
		else if( argument == "--fps" && hasNext )
		{
			char* end = nullptr;
			fps       = std::strtod( argv[ ++i ], &end );
			if( end == nullptr || *end != '\0' || !( fps > 0.0 ) || fps > 100000.0 )
			{
				std::fprintf( stderr, "--fps wants a positive number\n" );
				return 2;
			}
		}
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--list" )
			wantList = true;
		else if( argument == "--bench" )
			wantBench = true;
		else if( argument == "--pipe" )
			wantPipe = true;
		else if( argument == "--pipe-src" && hasNext )
			srcPath = argv[ ++i ];
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else
		{
			std::fprintf( stderr, "unknown argument: %s\n", argument.c_str() );
			usage();
			return 2;
		}
	}
	if( srcWidth == 0 )
	{
		srcWidth  = width;
		srcHeight = height;
	}

	if( wantList )
		return runList();
	if( check == "--names" )
		return runNames();

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL context\n" );
		return 1;
	}

	int result = 0;
	if( check == "--mixer" )
		result = runMixer();
	else if( check == "--unchanged" )
		result = runUnchanged();
	else if( check == "--lamp" )
		result = runLamp();
	else if( check == "--colour" )
		result = runColour();
	else if( check == "--dissolve" )
		result = runDissolve();
	else if( check == "--cut" )
		result = runCut();
	else if( check == "--change" )
		result = runChange();
	else if( check == "--pop" )
		result = runPop();
	else if( check == "--alpha" )
		result = runAlpha();
	else if( check == "--linear" )
		result = runLinear();
	else if( check == "--resize" )
		result = runResize();
	else if( check == "--rates" )
		result = runRates();
	else if( check == "--clock" )
		result = runClock();
	else if( check == "--mutation" )
		result = runMutation();
	else if( wantBench )
		result = runBench( frames > 0 ? frames : 30 );
	else if( wantPipe )
		result = runPipe( width, height, srcWidth, srcHeight, fps, scriptPath, srcPath, inputB, settings );
	else
	{
		MixRig rig;
		if( !rig.Init( width, height, InputSpec::Exact( width, height ), InputSpec::Exact( srcWidth, srcHeight ), false ) )
			return 1;
		rig.fps = fps;
		rig.UploadA( generate( inputA, width, height, false ) );
		rig.UploadB( generate( inputB, srcWidth, srcHeight, true ) );
		if( !applySettings( rig.plugin, settings ) )
			return 2;
		std::vector< Automation > automation;
		if( !loadAutomation( rig.plugin, scriptPath, automation ) )
			return 2;
		bool ok = true;
		for( int f = 0; f < std::max( 1, frames ); ++f )
		{
			for( const Automation& track : automation )
			{
				const float v = valueAt( track.track, f, track.ramps );
				if( !std::isnan( v ) )
					rig.plugin.SetFloatParameter( track.id, v );
			}
			ok = rig.Step() && ok;
		}
		if( !ok || !writePng( outPath, width, height, rig.Pixels() ) )
		{
			std::fprintf( stderr, "could not render or write %s\n", outPath.c_str() );
			return 1;
		}
		std::printf( "wrote %s\n", outPath.c_str() );
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	if( result != 0 )
		std::printf( "\n%d check(s) FAILED\n", failures > 0 ? failures : result );
	return result;
}
