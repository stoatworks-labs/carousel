#pragma once

/// What a host reads: the four-byte id and the sixteen-byte display name.
/// One place, so the registration, the harness's --names and verify.sh's
/// oxbow probe all say the same thing.
namespace carousel::names
{
inline constexpr const char* kMixerId   = "CA01";
inline constexpr const char* kMixerName = "SW Carousel";
} // namespace carousel::names
