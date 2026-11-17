#!/usr/bin/env bash
#
# Everything, in the order that fails fastest.
#
# Each step answers a question none of the others can:
#
#   source        GLSL reserved words declared as identifiers, M_PI, the
#                 built-ins GLSL ES 3.00 lacks, the FFGL SDK at the fleet's
#                 pin, and source/Cmf.h still the CIE table it claims to be
#   shaders       do the three shaders (the vertex shader, the blur and the
#                 screen pass) compile through a real GLSL compiler
#                 (tools/glslc.sh). REQUIRED: a skipped compile is not a pass.
#   build         a fresh universal Release build, which is what would ship;
#                 no warning in this repo's own code
#   checks        the plugin's claims, each measured at TWO rasters (640x360
#                 and 320x180) with negative controls built into the real
#                 plugin
#   software      the same checks on Apple's software renderer, what a
#                 GPU-less runner gets: a check calibrated on this Mac's GPU
#                 fails here first. Slow: most of verify's time.
#   pipe          the filming mode: N frames in, N out, two inputs; a
#                 standard parameter ramps and an option steps; bad cues
#                 and bad --set values refused; a closed stdout is exit 1
#   bundle        plugMain exported, lipo (universal), the plist, an ad-hoc
#                 signature on a copy, the version in two places
#   oxbow         the name, id, TYPE and input count a host reads, parameter
#                 0 (which Arena hides on a mixer), the Opacity name, and the
#                 refusal of the one input an effect host hands it.
#                 REQUIRED: set OXBOW= to a built oxbow; without one this
#                 FAILS, it does not skip.
#   mutants       tools/mutate.sh: one-character mutants, each caught
#   sweep         does every control change the picture (tools/sweep.py),
#                 and with --bare, exactly the controls that need a context
#                 to come alive are dead
#   clips         the demo clips: Pop's default is still what they measure,
#                 and white under transparent pixels changes nothing
#   bench         the render cost at 720p, 1080p and 4K, for the record. Not
#                 pass/fail.
#
#     OXBOW=/path/to/oxbow tools/verify.sh [BUILD_DIR]
#
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

BUILD="${1:-$REPO/build-verify}"
OXBOW="${OXBOW:-$HOME/Projects/resolume/oxbow/build/oxbow}"
passes=0
failures=0
negatives=0
LOGS="$( mktemp -d "${TMPDIR:-/tmp}/caverify.XXXXXX" )"

# Every harness run instantiates the real plugin, and the plugin writes a
# log. Keep those lines out of the log an Arena session would be read from.
export CAROUSEL_LOG_DIR="${CAROUSEL_LOG_DIR:-$LOGS/carousel-logs}"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
pass() { printf '   \033[32mok\033[0m   %s\n' "$1"; passes=$(( passes + 1 )); }
fail() { printf '   \033[31mFAIL\033[0m %s\n' "$1"; failures=$(( failures + 1 )); }
# macOS has no timeout(1). A limit here only stops a hang; it is not a verdict.
limit() { perl -e 'alarm shift; exec @ARGV' "$@"; }

#---------------------------------------------------------------------------
step "source"
#---------------------------------------------------------------------------
# GLSL 4.10's reserved words, declared as identifiers: a declaration of one
# compiles on some drivers and not on others (`packed` passed Apple's compiler
# and glslc and failed only Mesa: atrac; noise1-4 are built-ins a user
# function is silently shadowed by: dice). Only declarations -- a type, then
# the word -- since calling a built-in such as round() is what it is for.
if python3 -I - <<'PY'
import re, sys, pathlib
words = ( "patch sample input output filter common active half layout flat packed smooth round "
          "noise1 noise2 noise3 noise4 superp partition resource namespace using cast external "
          "interface long short fixed unsigned inline goto asm class union enum typedef template "
          "this volatile public static extern sizeof near far precise noperspective" ).split()
types = r"(?:float|int|uint|bool|[iuhdf]?vec[234]|mat[234]|u?sampler2D|void|const)"
pattern = re.compile( r"\b" + types + r"\s+(" + "|".join( words ) + r")\b" )
text = pathlib.Path( "source/Shaders.cpp" ).read_text()
glsl = "".join( re.findall( r'R"\((.*?)\)"', text, re.S ) )
glsl = re.sub( r"//[^\n]*", "", glsl )# comments are free to use any word
if len( glsl ) < 1000:
	sys.exit( "   found almost no GLSL in source/Shaders.cpp: the raw-string pattern is stale" )
hits = sorted( set( m.group( 1 ) for m in pattern.finditer( glsl ) ) )
if hits:
	print( "   declared in GLSL: " + ", ".join( hits ) )
	sys.exit( 1 )
if "M_PI" in glsl:
	print( "   M_PI in GLSL: no GLSL defines it" )
	sys.exit( 1 )
# The check must be able to see one.
if not pattern.search( "float sample = 1.0;" ):
	sys.exit( "   the reserved-word pattern matches nothing" )
PY
then
	pass "no GLSL reserved word declared as an identifier; no M_PI"
else
	fail "a GLSL reserved word is declared as an identifier, or M_PI is used"
fi
# Built-ins that arrived in GLSL 4.00 / ES 3.10: they compile everywhere here
# but not in WebGL2, where the fleet's browser demos run the shaders unedited.
# Carousel has no demo; the rule is kept so one can be made.
if /usr/bin/grep -nE '(^|[^A-Za-z_])(findMSB|findLSB|bitCount|bitfieldExtract|bitfieldInsert|bitfieldReverse|uaddCarry|usubBorrow|umulExtended|imulExtended|frexp|ldexp|fma|textureGather|textureQueryLevels|packUnorm4x8|unpackUnorm4x8)[[:space:]]*\(' source/Shaders.cpp | /usr/bin/grep -vE '^[0-9]+:[[:space:]]*//' >"$LOGS/webgl.txt"; then
	sed 's/^/   /' "$LOGS/webgl.txt"
	fail "a built-in GLSL ES 3.00 lacks is in the GLSL"
else
	pass "no built-in GLSL ES 3.00 lacks"
fi
pin=$( git -C external/ffgl rev-parse --short=7 HEAD 2>/dev/null )
if [ "$pin" = "b1afaf9" ]; then
	pass "external/ffgl at the fleet's pin, b1afaf9"
else
	fail "external/ffgl is at '$pin', not the fleet's b1afaf9"
fi
if python3 -I tools/cmf_header.py --check >"$LOGS/cmf.txt" 2>&1; then
	pass "source/Cmf.h is the CIE 1931 table, as tools/cmf_header.py writes it ($( sed -n 's/.*(\(.*\)).*/\1/p' "$LOGS/cmf.txt" ))"
else
	sed 's/^/   /' "$LOGS/cmf.txt"
	fail "source/Cmf.h is not what tools/cmf_header.py writes"
fi

#---------------------------------------------------------------------------
step "shaders"
#---------------------------------------------------------------------------
if GLSLC_REQUIRED=1 tools/glslc.sh >"$LOGS/glslc.txt" 2>&1; then
	pass "every shader compiles through glslc ($( tail -1 "$LOGS/glslc.txt" ))"
else
	sed 's/^/   /' "$LOGS/glslc.txt" | /usr/bin/head -20
	fail "a shader does not compile, or glslc is missing (brew install shaderc)"
fi

#---------------------------------------------------------------------------
step "build"
#---------------------------------------------------------------------------
# A fresh universal Release build -- the one that would ship. The dev build
# is arm64 only and is not what any of the binary checks below should see.
# Fresh only when it is verify's own directory: a BUILD_DIR passed in is
# someone's, and lipo below catches one that is not universal.
[ "$BUILD" = "$REPO/build-verify" ] && rm -rf "$BUILD"
if cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >"$LOGS/configure.txt" 2>&1 \
	&& cmake --build "$BUILD" --parallel >"$LOGS/build.txt" 2>&1; then
	pass "universal Release build in $( basename "$BUILD" )"
else
	tail -20 "$LOGS/build.txt" "$LOGS/configure.txt" 2>/dev/null | sed 's/^/   /'
	fail "build failed"
	printf '\n\033[31mverify: stopped at the build\033[0m\n'
	exit 1
fi
# Our own warnings only: the SDK's FFGLUtilities.cpp warns about RAND_MAX.
if /usr/bin/grep -E "warning:" "$LOGS/build.txt" | /usr/bin/grep -v "external/ffgl" >"$LOGS/warnings.txt"; then
	sed 's/^/   /' "$LOGS/warnings.txt" | /usr/bin/head -10
	fail "the build warns in this repo's own code"
else
	pass "no warnings in this repo's own code"
fi
CATEST="$BUILD/catest"

#---------------------------------------------------------------------------
step "checks (this Mac's GPU, 640x360 and 320x180)"
#---------------------------------------------------------------------------
# Every claim the README makes. Each GL check runs at both rasters itself and
# carries its own negative controls; --names needs no GL.
CHECKS="names mixer unchanged lamp colour dissolve cut change pop alpha linear resize rates clock mutation"
for check in $CHECKS; do
	if out=$( limit 1200 "$CATEST" --$check 2>&1 ); then
		n=$( printf '%s\n' "$out" | /usr/bin/grep -c '^  ok' )
		neg=$( printf '%s\n' "$out" | /usr/bin/grep -c '^  ok    negative control' )
		negatives=$(( negatives + neg ))
		pass "catest --$check: $n assertions, $neg of them negative controls"
	else
		printf '%s\n' "$out" | /usr/bin/grep -E 'FAIL' | sed 's/^/      /'
		fail "catest --$check"
	fi
done

#---------------------------------------------------------------------------
step "software renderer (both rasters)"
#---------------------------------------------------------------------------
# All of them on Apple's software renderer: many minutes, most of it --mixer.
# Run nothing else heavy alongside: the software renderer is not
# bit-identical under load.
for check in $CHECKS; do
	[ "$check" = names ] && continue
	if out=$( CATEST_RENDERER=software limit 3600 "$CATEST" --$check 2>&1 ); then
		pass "catest --$check (software): $( printf '%s\n' "$out" | /usr/bin/grep -c '^  ok' ) assertions"
	else
		printf '%s\n' "$out" | /usr/bin/grep -E 'FAIL' | sed 's/^/      /'
		fail "catest --$check on the software renderer -- run: CATEST_RENDERER=software $CATEST --$check"
	fi
done

#---------------------------------------------------------------------------
step "pipe"
#---------------------------------------------------------------------------
# The fleet's --pipe frame format, with the mixer's second input: stdin is
# Dest (projector A's slide), --pipe-src is Src (projector B's), raw RGBA, top
# row first. At start both lamps sit at rest for the fader.
W=64; H=36; FRAME=$(( W * H * 4 ))
tmp="$LOGS/pipe"; mkdir -p "$tmp"
# frames N R G B A > file: N flat frames.
frames() {
	python3 -I -c "import sys; sys.stdout.buffer.write(bytes([$2, $3, $4, $5]) * ($W * $H * $1))"
}
bytes_of() { wc -c < "$1" | tr -d ' '; }
frames 3 200 120 40 255 > "$tmp/amber3.rgba"
frames 3 0 0 0 255 > "$tmp/black3.rgba"
frames 2 0 0 0 255 > "$tmp/black2.rgba"
/usr/bin/head -c $(( FRAME * 5 / 2 )) "$tmp/amber3.rgba" > "$tmp/amber2half.rgba"

status=0
"$CATEST" --pipe --size ${W}x${H} --set Opacity=0 --pipe-src "$tmp/black3.rgba" < "$tmp/amber3.rgba" > "$tmp/out.rgba" 2>/dev/null || status=$?
if [ "$status" -eq 0 ] && cmp -s "$tmp/out.rgba" "$tmp/amber3.rgba"; then
	pass "3 frames in, 3 out; at Opacity 0, projector A alone gives Dest back, byte for byte"
else
	fail "3 frames at Opacity 0: exit $status, $( bytes_of "$tmp/out.rgba" ) bytes, or not Dest"
fi
status=0
"$CATEST" --pipe --size ${W}x${H} --set Opacity=1 --pipe-src "$tmp/black3.rgba" < "$tmp/amber3.rgba" > "$tmp/out.rgba" 2>/dev/null || status=$?
if [ "$status" -eq 0 ] && cmp -s "$tmp/out.rgba" "$tmp/black3.rgba"; then
	pass "at Opacity 1, projector B alone shows --pipe-src's black slide, not Dest"
else
	fail "at Opacity 1 with a black Src: exit $status, or the picture is not Src"
fi
status=0
"$CATEST" --pipe --size ${W}x${H} --pipe-src "$tmp/black2.rgba" < "$tmp/amber3.rgba" > "$tmp/out.rgba" 2>/dev/null || status=$?
got=$( bytes_of "$tmp/out.rgba" )
if [ "$status" -eq 0 ] && [ "$got" -eq $(( FRAME * 2 )) ]; then
	pass "3 Dest frames over 2 Src frames: 2 out -- the stream ends with either input"
else
	fail "3 Dest frames over 2 Src frames gave $got bytes, exit $status"
fi
status=0
"$CATEST" --pipe --size ${W}x${H} --input-b black < "$tmp/amber2half.rgba" > "$tmp/out.rgba" 2>/dev/null || status=$?
got=$( bytes_of "$tmp/out.rgba" )
if [ "$status" -eq 0 ] && [ "$got" -eq $(( FRAME * 2 )) ]; then
	pass "2.5 frames in, exactly 2 out: a partial frame ends the stream"
else
	fail "2.5 frames in gave $got bytes (want $(( FRAME * 2 ))), exit $status"
fi
# A standard parameter ramps between keys. Time 0 makes the drive follow the
# fader at once, so only the lamps' lag stands between a cue and the picture:
# Opacity keyed 0 at frame 0 and 1 at frame 8 has A dimming from frame 1 if
# it ramps; a STEPPED Opacity would hold frames 0-7 identical.
frames 10 200 200 200 255 > "$tmp/grey10.rgba"
frames 10 0 0 0 255 > "$tmp/black10.rgba"
printf '0 Opacity 0\n8 Opacity 1\n' > "$tmp/ramp.txt"
status=0
"$CATEST" --pipe --size ${W}x${H} --set Time=0 --pipe-src "$tmp/black10.rgba" --script "$tmp/ramp.txt" < "$tmp/grey10.rgba" > "$tmp/ramp.rgba" 2>/dev/null || status=$?
# An option steps: Lamp keyed High at 0 and Low at 4 holds frames 0-3 at rest
# (identical), then A's lamp cools: 3 differs from 4, and 4 is brighter than
# 5. A RAMPED Lamp would be 0.5 at frame 2.
printf '0 Lamp 0\n4 Lamp 1\n' > "$tmp/step.txt"
s2=0
"$CATEST" --pipe --size ${W}x${H} --set Opacity=0 --pipe-src "$tmp/black10.rgba" --script "$tmp/step.txt" < "$tmp/grey10.rgba" > "$tmp/step.rgba" 2>/dev/null || s2=$?
verdict=$( python3 -I - "$tmp/ramp.rgba" "$tmp/step.rgba" $FRAME <<'PY'
import sys
size = int( sys.argv[ 3 ] )
def frames( path ):
	data = open( path, "rb" ).read()
	return [ data[ i * size:( i + 1 ) * size ] for i in range( len( data ) // size ) ]
r, s = frames( sys.argv[ 1 ] ), frames( sys.argv[ 2 ] )
if len( r ) != 10 or len( s ) != 10:
	print( f"bad {len( r )} and {len( s )} frames" ); sys.exit()
mean = lambda f: sum( f[ 0::4 ] ) / ( size / 4 )
mr, ms = [ mean( f ) for f in r ], [ mean( f ) for f in s ]
ramp = mr[ 0 ] > mr[ 1 ] > mr[ 2 ] > mr[ 3 ]
step = s[ 0 ] == s[ 1 ] == s[ 2 ] == s[ 3 ] and s[ 3 ] != s[ 4 ] and ms[ 4 ] > ms[ 5 ]
print( ( "ok " if ramp and step else "bad " ) + "ramp " + " ".join( f"{v:.1f}" for v in mr[ :5 ] ) + "; step " + " ".join( f"{v:.1f}" for v in ms[ :6 ] ) )
PY
)
case "$status:$s2:$verdict" in
	0:0:ok*) pass "cues: Opacity ramps, Lamp steps (mean red, ${verdict#ok })" ;;
	*) fail "cues gave exits $status and $s2, '${verdict#bad }': want A dimming from frame 1, and frames 0-3 equal then a step at 4" ;;
esac
printf '0 No Such Control 0.5\n' > "$tmp/bad.txt"
status=0
"$CATEST" --pipe --size ${W}x${H} --script "$tmp/bad.txt" < "$tmp/amber3.rgba" >/dev/null 2>&1 || status=$?
[ "$status" -eq 2 ] && pass "a cue naming no parameter is refused (exit 2)" || fail "a cue naming no parameter gave exit $status, not 2"
status=0
"$CATEST" --out "$tmp/x.png" --size ${W}x${H} --set "Curve=Bogus" >/dev/null 2>&1 || status=$?
s2=0
"$CATEST" --out "$tmp/x.png" --size ${W}x${H} --set "Opacity=0.5x" >/dev/null 2>&1 || s2=$?
s3=0
"$CATEST" --out "$tmp/x.png" --size ${W}x${H} --set "Curve=Light" >/dev/null 2>&1 || s3=$?
s4=0
"$CATEST" --out "$tmp/x.png" --size ${W}x${H} --set "Nothing=1" >/dev/null 2>&1 || s4=$?
if [ "$status" -eq 2 ] && [ "$s2" -eq 2 ] && [ "$s3" -eq 0 ] && [ "$s4" -eq 2 ]; then
	pass "--set refuses 'Curve=Bogus', 'Opacity=0.5x' and 'Nothing=1' (exit 2), takes 'Curve=Light'"
else
	fail "--set: no such option gave $status, a half number $s2, no such name $s4, a real option $s3"
fi
# Read from a file, not a pipe: a writer killed by SIGPIPE would fail the
# pipeline whatever catest did.
frames 20 128 128 128 255 > "$tmp/grey20.rgba"
"$CATEST" --pipe --size ${W}x${H} < "$tmp/grey20.rgba" 2>/dev/null | /usr/bin/head -c 1 >/dev/null
status=${PIPESTATUS[0]}
if [ "$status" -eq 1 ]; then
	pass "catest --pipe | head -c 1 exits 1 (SIGPIPE ignored), not 141"
else
	fail "a closed stdout gave exit $status, expected 1"
fi

#---------------------------------------------------------------------------
step "Carousel.bundle"
#---------------------------------------------------------------------------
bundle="$BUILD/Carousel.bundle"
bin="$bundle/Contents/MacOS/Carousel"
declared=$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | /usr/bin/head -1 )
if [ -n "$declared" ] && /usr/bin/grep -q "versionFallback = \"v$declared\"" source/StoatworksAbout.h; then
	pass "CMakeLists and StoatworksAbout.h agree: v$declared"
else
	fail "StoatworksAbout.h's versionFallback is not v$declared"
fi
# `nm ... | grep -q X` FAILS when grep FINDS its match under pipefail: grep
# exits at once, nm takes SIGPIPE, and the pipeline reports that failure.
syms=$( nm -gU "$bin" 2>/dev/null )
case "$syms" in
	*_plugMain*) pass "exports plugMain" ;;
	*) fail "no plugMain -- the bundle contains no plugin" ;;
esac
archs=$( lipo -archs "$bin" 2>/dev/null )
case "$archs" in *arm64*x86_64*|*x86_64*arm64*) pass "universal: $archs" ;; *) fail "not universal (got: $archs)" ;; esac
plist="$bundle/Contents/Info.plist"
read_plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null; }
exe=$( read_plist CFBundleExecutable )
if [ -n "$exe" ] && [ -f "$bundle/Contents/MacOS/$exe" ] && [ "$( read_plist CFBundleIdentifier )" = "com.stoatworks.ffgl.carousel" ] \
	&& [ "$( read_plist CFBundlePackageType )" = "BNDL" ] && [ "$( read_plist CFBundleVersion )" = "$declared" ]; then
	pass "plist: CFBundleExecutable $exe on disk, com.stoatworks.ffgl.carousel, BNDL, v$declared"
else
	fail "plist: executable '$exe', id '$( read_plist CFBundleIdentifier )', type '$( read_plist CFBundlePackageType )', version '$( read_plist CFBundleVersion )'"
fi
copy="$LOGS/sign"; mkdir -p "$copy"; cp -R "$bundle" "$copy/"
if codesign --force --sign - --timestamp=none "$copy/Carousel.bundle" >/dev/null 2>&1 \
	&& codesign --verify --deep --strict "$copy/Carousel.bundle" >/dev/null 2>&1; then
	pass "ad-hoc signs and verifies (on a copy)"
else
	fail "ad-hoc signing failed"
fi

#---------------------------------------------------------------------------
step "oxbow"
#---------------------------------------------------------------------------
if [ ! -x "$OXBOW" ]; then
	fail "no oxbow at $OXBOW -- set OXBOW= to a built oxbow; this step does not skip"
else
	probe=$( limit 120 "$OXBOW" probe "$bundle" 2>&1 )
	printf '%s\n' "$probe" > "$LOGS/probe.txt"
	case "$probe" in *"name:        SW Carousel"$'\n'*) pass "a host reads the name SW Carousel" ;; *) fail "wrong or missing name -- see: $OXBOW probe $bundle" ;; esac
	case "$probe" in *"id:          CA01"*) pass "a host reads the id CA01" ;; *) fail "wrong or missing id (want CA01)" ;; esac
	case "$probe" in *"type:        mixer"*) pass "a host reads the type mixer" ;; *) fail "wrong plugin type (want mixer) -- the 8th argument of CFFGLPluginInfo" ;; esac
	case "$probe" in *"inputs:      2..2"*) pass "a host is told 2..2 inputs" ;; *) fail "wrong input count (want 2..2) -- SetMinInputs/SetMaxInputs are separate from the type" ;; esac
	# Arena does not expose a mixer's parameter 0 (genlock, wipe, relay,
	# lenticular, pepper), so index 0 is a control whose default is right for
	# ever: Linear Light, on.
	case "$probe" in
		*"[ 0] Linear Light     type=0   default=1 "*) pass "parameter 0, which Arena hides, is Linear Light, a boolean, on" ;;
		*) fail "parameter 0 is not Linear Light (boolean, on) -- Arena hides a mixer's first parameter" ;;
	esac
	# Arena binds a mixer parameter named Opacity to the layer's fader: the
	# dissolve fader.
	case "$probe" in
		*"] Opacity          type=10"*) pass "the dissolve fader is a standard parameter named Opacity" ;;
		*) fail "no standard parameter named Opacity -- Arena binds the layer fader by that name" ;;
	esac
	# oxbow hosts effects: it hands a plugin one input. A mixer must refuse
	# that (its guard, FF_FAIL), not draw from an unbound texture unit.
	selftest=$( limit 120 "$OXBOW" selftest "$bundle" 2>&1 )
	printf '%s\n' "$selftest" > "$LOGS/selftest.txt"
	case "$selftest" in
		*"FF_PROCESS_OPENGL failed on frame 0"*) pass "one input, as an effect host gives, is refused (FF_FAIL on frame 0)" ;;
		*) fail "the mixer did not refuse a single input" ;;
	esac
fi

#---------------------------------------------------------------------------
step "mutants"
#---------------------------------------------------------------------------
if tools/mutate.sh >"$LOGS/mutate.txt" 2>&1; then
	pass "$( tail -1 "$LOGS/mutate.txt" )"
else
	/usr/bin/grep -E '^== mutant|FAIL' "$LOGS/mutate.txt" | /usr/bin/grep -B1 'FAIL' | sed 's/^/   /'
	fail "a mutant survived: $( tail -1 "$LOGS/mutate.txt" )"
fi

#---------------------------------------------------------------------------
step "sweep"
#---------------------------------------------------------------------------
# The only thing that catches a uniform whose name does not match the C++.
if out=$( python3 -I tools/sweep.py --build "$BUILD" 2>&1 ); then
	pass "$( printf '%s\n' "$out" | tail -1 )"
else
	printf '%s\n' "$out" | sed 's/^/   /'
	fail "a dead control"
fi
# --bare: the defaults only, no context. These, and only these, need one:
# Linear Light (one projector lit is exact either way), Time and Curve (no
# dissolve is running), Mounts and Pop (no slide has changed), Change A (A is
# dark at the default Opacity 1). A control dead here that is not on the
# list, or alive that is, means the defaults moved.
want="Change A, Curve, Linear Light, Mounts, Pop, Time"
out=$( python3 -I tools/sweep.py --build "$BUILD" --bare 2>&1 )
got=$( printf '%s\n' "$out" | sed -n 's/^  DEAD \(.*[^ ]\) *both ends identical$/\1/p' | sed 's/ *$//' | sort | paste -sd, - | sed 's/,/, /g' )
if [ "$got" = "$want" ] && ! printf '%s\n' "$out" | /usr/bin/grep -q '^  WEAK'; then
	pass "--bare: exactly the six controls that need a context are dead ($got)"
else
	printf '%s\n' "$out" | sed 's/^/   /'
	fail "--bare: dead '$got', want '$want'"
fi

#---------------------------------------------------------------------------
step "clips"
#---------------------------------------------------------------------------
# Resolume's own demo clips, through --pipe: Pop's default is still what the
# spec's rule measures, and the alpha path ignores colour under transparency.
if out=$( python3 -I tools/clips.py --build "$BUILD" --pop --alpha --check 2>&1 ); then
	pass "$( printf '%s\n' "$out" | /usr/bin/grep -E 'median over' | sed 's/^ *//' ); declared the same"
	printf '%s\n' "$out" | /usr/bin/grep -E 'changes nothing' | sed 's/^ */   /'
else
	printf '%s\n' "$out" | sed 's/^/   /'
	fail "tools/clips.py --check --alpha"
fi

#---------------------------------------------------------------------------
step "bench"
#---------------------------------------------------------------------------
"$CATEST" --bench --frames 30 2>&1 | sed 's/^/   /'

printf '\n'
if [ "$failures" -eq 0 ]; then
	printf '\033[32mverify: all %d checks passed\033[0m (%d negative controls; logs in %s)\n' "$passes" "$negatives" "$LOGS"
else
	printf '\033[31mverify: %d of %d checks failed\033[0m (logs in %s)\n' "$failures" $(( passes + failures )) "$LOGS"
fi
exit $(( failures > 0 ? 1 : 0 ))
