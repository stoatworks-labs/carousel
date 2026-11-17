#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# One copy of the tree in a temporary directory (the FFGL SDK is symlinked,
# not copied), configured once, arm64-only. For each mutant every source file
# is put back, its object and the binary deleted (make's whole-second mtimes),
# the one character changed, catest rebuilt, the binary checked to carry a
# GLSL mutant, and only the named check run (it runs at 640x360 and 320x180)
# under a time limit: a mutant can hang, and a timeout counts as caught.
#
#     tools/mutate.sh [WORDS]    WORDS: only the mutants whose description has them
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The scratch tree goes under TMPDIR when it is set (a heredoc into python
# hung once with the system's /var/folders TMPDIR: the fleet's trap).
WORK="$(mktemp -d "${TMPDIR:-/tmp}/camutate.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
export CAROUSEL_LOG_DIR="$WORK/logs"

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap) -- which is why no mutant here touches a GLSL '||'.
MUTANTS=(
	"source/Shaders.cpp|pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), greaterThan|pow( ( c + 0.055 ) / 1.055, vec3( 2.5 ) ), greaterThan|--lamp|GLSL decode: gamma 2.5, 4 -> 5"
	"source/Shaders.cpp|1.055 * pow( x, vec3( 1.0 / 2.4 ) ) - 0.055|1.055 * pow( x, vec3( 1.0 / 2.2 ) ) - 0.055|--lamp|GLSL encode: gamma 1 / 2.2, 4 -> 2"
	"source/Shaders.cpp|vec3 c = ( Fault & kFaultNoPremultiply ) != 0 ? t.rgb : t.rgb * t.a;|vec3 c = ( Fault & kFaultNoPremultiply ) != 0 ? t.rgb : t.rgb + t.a;|--alpha|GLSL texelOf: alpha added, not multiplied, * -> +"
	"source/Shaders.cpp|clamp( at, ivec2( 0 ), size - ivec2( 1 ) )|clamp( at, ivec2( 0 ), size - ivec2( 0 ) )|--mixer|GLSL texelOf: one texel of padding readable, 1 -> 0"
	"source/Shaders.cpp|if( size == OutSize )|if( size != OutSize )|--mixer|GLSL slideAt: the fetch for the other size, = -> !"
	"source/Shaders.cpp|vec2 q  = ( vec2( pixel ) + 0.5 ) * vec2( size ) / vec2( OutSize ) - 0.5;|vec2 q  = ( vec2( pixel ) + 0.6 ) * vec2( size ) / vec2( OutSize ) - 0.5;|--mixer|GLSL slideAt: the resample a tenth of a pixel off, 5 -> 6"
	"source/Shaders.cpp|for( int k = -Radius; k <= Radius; ++k )|for( int k = -Radius; k <  Radius; ++k )|--pop|GLSL blur: the horizontal kernel's last tap dropped, = -> space"
	"source/Shaders.cpp|texelFetch( BlurB, ivec2( p.x, y ), 0 )|texelFetch( BlurB, ivec2( p.y, y ), 0 )|--change|GLSL verticalB: B's blur read from the wrong column, x -> y"
	"source/Shaders.cpp|texelFetch( BlurA, ivec2( p.x, y ), 0 )|texelFetch( BlurA, ivec2( p.y, y ), 0 )|--lamp|GLSL verticalA: A's blur read from the wrong column, x -> y"
	"source/Shaders.cpp|Colour        = vec4( Linear != 0 && !gammaMix ? encode( light ) : light, 1.0 );|Colour        = vec4( Linear == 0 && !gammaMix ? encode( light ) : light, 1.0 );|--linear|GLSL screen: encoded only with Linear Light off, ! -> ="
	"source/Shaders.cpp|vec4 t = texelFetch( SlideB, p, 0 );|vec4 t = texelFetch( SlideA, p, 0 );|--unchanged|GLSL screen: B's exact path showing A's slide, B -> A"
	"source/Shaders.cpp|if( ExactA != 0 )|if( ExactA == 0 )|--unchanged|GLSL screen: A's exact path whenever it is not asked for, ! -> ="
	"source/Shaders.cpp|ivec2 sizeB = ( Fault & kFaultSizeSwap ) != 0 ? SizeA : SizeB;|ivec2 sizeB = ( Fault & kFaultSizeSwap ) == 0 ? SizeA : SizeB;|--mixer|GLSL screen: B read at A's size, ! -> ="
	"source/Shaders.cpp|vec3 a      = resolve( BlurredA != 0 ? verticalA( p ) : slideAt( SlideA, SizeA, p ) );|vec3 a      = resolve( BlurredA == 0 ? verticalA( p ) : slideAt( SlideA, SizeA, p ) );|--pop|GLSL screen: A's blur and sharp swapped, ! -> ="
	"source/Shaders.cpp|GainA * a + GainB * b;|GainA * a + GainA * b;|--lamp|GLSL screen: B's light weighted by A's gain, B -> A"
	"source/Shaders.cpp|return vec4( Linear != 0 ? decode( c ) : c, t.a );|return vec4( Linear == 0 ? decode( c ) : c, t.a );|--linear|GLSL texelOf: decoded only with Linear Light off, ! -> ="
	"source/Shaders.cpp|return ( Fault & kFaultNoPremultiply ) != 0 ? v.rgb * v.a : v.rgb;|return ( Fault & kFaultNoPremultiply ) == 0 ? v.rgb * v.a : v.rgb;|--alpha|GLSL resolve: premultiplied twice, ! -> ="
	"source/Model.cpp|out[ 2 + p ]   = ( q - y[ 2 + p ] ) / tauPop;|out[ 2 + p ]   = ( q + y[ 2 + p ] ) / tauPop;|--pop|C++: the slide's heat running away, - -> +"
	"source/Model.cpp|case 0: return std::clamp( 1.0 - a / kShutterClose, 0.0, 1.0 );|case 0: return std::clamp( 1.0 + a / kShutterClose, 0.0, 1.0 );|--change|C++: the shutter never closing on its ramp, - -> +"
	"source/Model.cpp|heat[ p ] = 0.0;//a cold slide from the tray|heat[ p ] = 1.0;//a cold slide from the tray|--pop|C++: a warm slide from the tray, 0 -> 1"
	"source/Model.cpp|slewRate = 1.0 / applied.time;|slewRate = 2.0 / applied.time;|--dissolve|C++: the dissolve twice as fast as Time, 1 -> 2"
	"source/Model.cpp|const double c     = projector == 1 ? s : 1.0 - s;|const double c     = projector == 1 ? s : 1.0 + s;|--lamp|C++: A's drive rising with B's, - -> +"
	"source/Model.cpp|( 4.0 * kFNumber * kSlideMm ) * outWidth;|( 4.0 * kFNumber * kSlideMm ) / outWidth;|--pop|C++: the blur in output widths, not pixels, * -> /"
	"source/Model.cpp|if( applied.time <= 0.0 && ( fault & kFaultHardCutRamp ) == 0 )|if( applied.time <  0.0 && ( fault & kFaultHardCutRamp ) == 0 )|--cut|C++: Time 0 no longer cuts at the instant, = -> space"
	"source/Lamp.cpp|( T2 * T2 - A2 * A2 ) / fourthPowerSpan();|( T2 * T2 + A2 * A2 ) / fourthPowerSpan();|--lamp|C++: radiation from the room added, - -> +"
	"source/Lamp.cpp|const double in       = driveSquared * rhoRated / rho;|const double in       = driveSquared * rhoRated * rho;|--lamp|C++: the power rising with resistance, / -> *"
	"source/Lamp.cpp|a[ 4 ] / ( t * t );|a[ 4 ] * ( t * t );|--lamp|C++: Shomate's last term inverted, / -> *"
	"source/Colour.cpp|Rgb g = { p.r / w.r, p.g / w.g, p.b / w.b };|Rgb g = { p.r / w.r, p.g / w.g, p.b / w.g };|--colour|C++: blue white-balanced against green, b -> g"
	"source/Colour.cpp|lambda * lambda * lambda * lambda * lambda;|lambda * lambda * lambda * lambda + lambda;|--colour|C++: Planck's lambda^5 broken, * -> +"
	"source/Clock.cpp|scale_ = millisVotes_ > secondsVotes_ ? 0.001 : 1.0;|scale_ = millisVotes_ > secondsVotes_ ? 0.002 : 1.0;|--clock|C++: a millisecond host read as 2 ms a tick, 1 -> 2"
	"source/Clock.cpp|dt = hostDelta * scale_;|dt = hostDelta / scale_;|--clock|C++: the unit divided, not multiplied, * -> /"
	"source/Controls.cpp|return kTimeMax * p * p;|return kTimeMax * p + p;|--names|C++: Time's mapping no longer squared, * -> +"
	"source/Carousel.cpp|sum += k == 0 ? w[ k ] : 2.0 * w[ k ];|sum += k == 0 ? w[ k ] : 1.0 * w[ k ];|--lamp|C++: the Gaussian's weights not normalised, 2 -> 1"
	"source/Carousel.cpp|w[ k ] = std::exp( -0.5 * k * k / ( sigma * sigma ) );|w[ k ] = std::exp( -0.6 * k * k / ( sigma * sigma ) );|--pop|C++: the Gaussian narrower than sigma, 5 -> 6"
	"source/Carousel.cpp|const bool exactB = isExactly( gB, 1.0 ) && isExactly( gA, 0.0 )|const bool exactB = isExactly( gB, 1.0 ) && isExactly( gA, 1.0 )|--unchanged|C++: B's exact path never taken, 0 -> 1"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

FILES="source/Shaders.cpp source/Model.cpp source/Lamp.cpp source/Colour.cpp source/Clock.cpp source/Controls.cpp source/Carousel.cpp"
tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
cmake --build "$tree/build" --target catest -j6 >/dev/null 2>&1

FILTER="${1:-}"
caught=0
tried=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	[[ -n "$FILTER" && "$what" != *"$FILTER"* ]] && continue
	tried=$(( tried + 1 ))
	# Every file back to the shipped text first, and its object deleted: make
	# on macOS compares whole-second mtimes, and a file put back in the same
	# second the last mutant's object was written looks up to date -- the
	# previous mutant then runs again under this one's name.
	for f in $FILES "$file"; do
		cmp -s "$REPO/$f" "$tree/$f" || cp "$REPO/$f" "$tree/$f"
		find "$tree/build" -name "$(basename "$f").o" -delete
	done
	# And the binary, so it is relinked whatever the clock says.
	rm -f "$tree/build/catest"

	python3 -I - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	if ! cmake --build "$tree/build" --target catest -j6 >"$WORK/build.log" 2>&1; then
		printf '   FAIL  the mutant does not build -- a mutant must compile to mean anything\n'
		continue
	fi
	# A GLSL mutant is a string literal in the binary: if the binary does not
	# carry it, whatever runs next is not this mutant.
	if [[ "$file" == source/Shaders.cpp ]] && ! /usr/bin/grep -qF -- "$mutant" "$tree/build/catest"; then
		printf '   FAIL  the built catest does not carry the mutant -- a stale build\n'
		continue
	fi
	status=0
	limit 300 "$tree/build/catest" "$check" >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d%s):\n' "$check" "$status" "$( [[ "$status" -eq 142 ]] && printf ', the time limit' )"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ /usr/bin/grep -E '^  FAIL' "$WORK/log" || true; } | /usr/bin/head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "$tried" "$caught"
[[ "$tried" -gt 0 && "$caught" -eq "$tried" ]]
