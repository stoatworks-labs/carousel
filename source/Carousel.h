#pragma once

#include <FFGLSDK.h>

//AFTER the SDK: this header names FFUInt32 and does not pull the SDK in
//itself, so an include placed above it fails with "unknown type name" errors
//that point at the About block rather than at the include order.
#include "StoatworksAboutParams.h"
#include "BlurBuffer.h"
#include "Clock.h"
#include "Model.h"

#include <string>

/**
    Carousel -- two Kodak slide projectors on a dissolve unit. An FFGL
    **mixer** for Resolume.

    Dest, the layer below, is projector A's slide; Src, the layer whose blend
    mode this is, is projector B's. Each projector's light is its lamp's
    filament temperature, which follows the dissolve unit's voltage through
    its thermal lag; a slide change is a shutter's dark gap; a card-mounted
    slide drifts into focus as it warms. See Model.h, Lamp.h, AGENTS.md.

    **A mixer.** Read the mixer section of genlock's AGENTS.md before
    changing `ProcessOpenGL`.

    **State persists**: both lamps, the dissolve's position, the shutters'
    cycles and the slides' heat, all on the CPU in double (Model). A resize
    keeps it (`--resize`). The GPU holds only the two blur buffers, each
    RGBA32F at the output's size, made the first time a slide is out of
    focus and kept until DeInitGL.
*/
class Carousel : public CFFGLPlugin
{
public:
	Carousel();

	//CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

	/// The host's clock: milliseconds from Resolume, seconds elsewhere.
	FFResult SetTime( double time ) override;

	/// Changes no pixel. It exists so that ONE session in front of Resolume
	/// leaves a log that says which host and which bundle.
	void SetHostInfo( const char* hostname, const char* version ) override;

	char* GetTextParameter( unsigned int index ) override;

	/// Declared only so the About line can accept its own default:
	/// instantiateGL pushes every declared default back through the setters
	/// and deletes the whole instance if one fails, and CFFGLPlugin's
	/// SetTextParameter is a stub that returns exactly that failure.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	/// A four-character label for a host with no room for "SW Carousel".
	const char* GetShortName() override
	{
		return "Crsl";
	}

	/// What the last rendered frame used, for the harness.
	const carousel::Instant& InstantForTest() const
	{
		return lastInstant;
	}

	/// What the parameters ask for, in physical units, through Controls.
	carousel::Asked AskedForTest() const
	{
		return asked();
	}

	carousel::Clock& ClockForTest()
	{
		return clock;
	}

	const carousel::Model& ModelForTest() const
	{
		return model;
	}

	/// The negative controls: a bitmask of carousel::Fault, 0 in the shipped
	/// plugin, reachable only from here.
	void SetFaultForTest( int mask );

	/// The mutation test: compile this screen-pass fragment shader instead of
	/// the shipped one. Call before InitGL. Null restores the shipped text.
	void SetFragmentShaderForTest( const char* text )
	{
		fragmentOverride = text;
	}

	/// In the order the host shows them.
	///
	/// **Index 0 is sacrificial.** Resolume Arena 7.27.1 does not expose a
	/// mixer's FIRST parameter (measured on genlock, wipe, relay, lenticular
	/// and pepper), so it is stuck at its default. Linear Light, on, is how
	/// two projectors' light adds on a screen: right if nobody can reach it.
	enum ParamID : FFUInt32
	{
		//Screen
		PT_LINEAR_LIGHT,

		//Dissolve
		PT_OPACITY,///< the dissolve fader: Arena binds it to the LAYER's fader
		PT_TAKE,
		PT_TIME,
		PT_CURVE,

		//Lamps
		PT_LAMP,

		//Slides
		PT_MOUNTS,
		PT_POP,
		PT_CHANGE_A,
		PT_CHANGE_B,

		//About. Last in the enum so nothing before it ever moves.
		PT_ABOUT_FIRST,
		PT_COUNT = PT_ABOUT_FIRST + stoatworks::about::kParamCount
	};

private:
	carousel::Asked asked() const;
	void LogGuard( unsigned bit, const char* what );

	ffglex::FFGLShader blurShader;
	ffglex::FFGLShader screenShader;
	ffglex::FFGLScreenQuad quad;
	carousel::BlurBuffer blur[ 2 ];

	carousel::Clock clock;
	carousel::Model model;
	carousel::Instant lastInstant;

	float params[ PT_COUNT ] = {};

	//The buttons: a press is the rising edge (relay's latch); the release is
	//ignored. Take toggles the fader's meaning; a change waits for the frame.
	bool takeLatch      = false;
	bool held[ 3 ]      = { false, false, false };///< Take, Change A, Change B
	bool pendingChange[ 2 ] = { false, false };

	int fault                    = 0;
	const char* fragmentOverride = nullptr;
	int lastOutW = 0, lastOutH = 0;

	//The host-session log: what reached the mixer. Nothing renders from it.
	unsigned long frames  = 0;
	int instance          = 0;
	int opacityLines      = 0;
	float loggedOpacity   = -1.0f;
	unsigned guardsLogged = 0;

	/// GetTextParameter hands the host a bare pointer, so the string has to
	/// outlive the call.
	std::string aboutText;
};
