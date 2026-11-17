#pragma once

/**
    The host's clock, and the one thing Carousel takes from it: the time
    since the last frame, in seconds, in double.

    Lamp temperatures, the dissolve's position, the shutter's cycle and the
    slides' heat all persist from frame to frame, so the plugin needs real
    elapsed time -- not a frame count (the FrameLag and SlewFrames faults)
    -- and it must survive the fleet's two clock lessons:

    - **Resolume hands over milliseconds; other hosts seconds.** The unit is
      settled by watching the host's clock against a real one for a few
      frames, relay's vote (tinsel, genlock and wipe before it): the ratio of
      the deltas names the unit. Until it is settled the wall clock's delta
      is used: right in rate.
    - **A float cannot hold the host's clock.** Only the DIFFERENCE between
      consecutive readings is ever formed, in double (a double holds integer
      milliseconds exactly to 2^53 ms, 285 000 years). Nothing absolute
      reaches the model or the GPU. relay keeps an epoch and subtracts it;
      here there is not even an epoch, so settling the unit cannot jump the
      clock.

    Tick() is called once per ProcessOpenGL, after the host's SetTime.
*/
namespace carousel
{

class Clock
{
public:
	/// From the plugin's SetTime, whatever the host said.
	void Observe( double hostTime );

	/// Seconds since the previous Tick, >= 0 (0 on the first). Unclamped:
	/// the model clamps.
	double Tick();

	/// The multiplier taking the host's unit to seconds: 0.001 for ms, 1 for
	/// s, 0 while undecided. The harness declares what it sends.
	void SetScaleForTest( double scale )
	{
		scale_ = scale;
	}

	/// The harness's wall clock, in seconds, so that the unit vote itself
	/// can be checked offline; negative restores the real one.
	void SetWallForTest( double seconds )
	{
		wallOverride_ = seconds;
	}

	/// The FloatClock negative control: the readings held in floats first.
	void SetFloatForTest( bool on )
	{
		floatReadings_ = on;
	}

	double Scale() const
	{
		return scale_;
	}

private:
	double wallNow() const;

	double raw_          = -1.0;///< the host's last reading, in its unit
	double lastRaw_      = -1.0;
	double lastWall_     = -1.0;
	double scale_        = 0.0;
	double wallOverride_ = -1.0;
	bool floatReadings_  = false;
	int secondsVotes_    = 0;
	int millisVotes_     = 0;
};

} // namespace carousel
