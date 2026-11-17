#include "Clock.h"

#include <chrono>

namespace carousel
{
namespace
{
/// Frames that must agree before the host's clock unit is settled (relay's).
constexpr int kVotes = 4;
} // namespace

double Clock::wallNow() const
{
	if( wallOverride_ >= 0.0 )
		return wallOverride_;
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

void Clock::Observe( double hostTime )
{
	raw_ = floatReadings_ ? static_cast< double >( static_cast< float >( hostTime ) ) : hostTime;
}

double Clock::Tick()
{
	const double wall = wallNow();
	double dt         = 0.0;
	const bool both   = raw_ >= 0.0 && lastRaw_ >= 0.0;
	const double hostDelta = both ? raw_ - lastRaw_ : 0.0;
	const double wallDelta = lastWall_ >= 0.0 ? wall - lastWall_ : 0.0;

	//The vote: a paused host, a looping clip or a stalled frame says nothing.
	if( scale_ == 0.0 && both && hostDelta > 0.0 && wallDelta >= 0.0005 )
	{
		const double ratio = hostDelta / wallDelta;
		if( ratio > 0.1 && ratio < 10.0 )
			++secondsVotes_;
		else if( ratio > 100.0 && ratio < 10000.0 )
			++millisVotes_;
		if( secondsVotes_ >= kVotes || millisVotes_ >= kVotes )
			scale_ = millisVotes_ > secondsVotes_ ? 0.001 : 1.0;
	}

	if( both && scale_ != 0.0 )
		dt = hostDelta * scale_;
	else if( raw_ < 0.0 || scale_ == 0.0 )
		dt = wallDelta;//no host clock yet, or its unit undecided: right in rate

	if( raw_ >= 0.0 )
		lastRaw_ = raw_;
	lastWall_ = wall;
	return dt > 0.0 ? dt : 0.0;
}

} // namespace carousel
