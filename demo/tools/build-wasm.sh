#!/usr/bin/env bash
#
# Build demo/carousel-core.{js,wasm}: the plugin's own C++, compiled to
# WebAssembly for the browser demo. (nipkow's script, which is fountain's and
# polyhedral's, with carousel's files and a second input.)
#
#     demo/tools/build-wasm.sh          (needs emscripten: em++ on PATH, or EMXX=...)
#
# The output is COMMITTED, because the Cloudflare deploy has no build step:
# what is in demo/ is what is served. So is demo/wasm/inputs.sha256, the hash
# of every input below and of both outputs; demo/tools/check_shaders.py (run
# by tools/verify.sh) fails when a source file has changed since this last ran,
# which is the only way a stale .wasm can be noticed without emscripten.
#
# What goes in, and none of it is edited for the browser:
#
#   source/   Controls, Lamp, Colour, Clock, Model, BlurBuffer, Diag, Shaders
#             and Carousel.cpp -- the plugin class itself, with its constructor
#             and its CFFGLPluginInfo, its clock and seconds-or-milliseconds
#             vote, the buttons' latches, the two lamps' filament ODE (RK4 in
#             double), the dissolve unit, the shutters, the slides' heat, the
#             Planck sum against the CIE table (Cmf.h, a header), the
#             Gaussian's weights, the blur buffers and every uniform it sets.
#             Everything but MixerEntry.cpp, the bundle's build stamp, which
#             nothing calls.
#   external/ffgl   the FFGL SDK's CFFGLPluginManager / CFFGLPlugin (parameter
#             bookkeeping, read back by the page as a host reads it),
#             CFFGLPluginInfo (Carousel.cpp registers one), FFGLLog, and
#             ffglex's FFGLShader, FFGLScreenQuad and the scoped bindings they
#             use.
#   demo/wasm glue.cpp (the host's calls, as C functions) and gl_shim.cpp (the
#             GL entry points WebGL2 cannot take as they are).
#
# GL itself is emscripten's WebGL2 library, bound by the page to its own
# context. `-D__linux__` is for the SDK's FFGLPlatform.h, which knows Windows,
# Linux and macOS and stops the build on anything else: told it is Linux, the
# SDK includes <GL/glew.h>, which emscripten provides. It is given only to the
# files that include the SDK.
#
# Flags follow CMakeLists.txt's Release build: -O3 -DNDEBUG, C++17. WebAssembly
# has no fused multiply-add, so where clang fuses one on arm64 the browser
# rounds twice, and its exp, expm1, pow and cbrt are musl's rather than Apple's
# libm: the lamps' doubles need not agree with the Mac's bit for bit. What
# reaches the GPU is rounded to float, as in the plugin. Pictures against
# catest: AGENTS.md, "The browser demo".
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO"

EMXX="${EMXX:-em++}"
command -v "$EMXX" >/dev/null 2>&1 || { echo "em++ not found (install emscripten, or set EMXX=)" >&2; exit 1; }
[[ -f external/ffgl/CMakeLists.txt ]] || { echo "FFGL SDK missing -- git submodule update --init --recursive" >&2; exit 1; }

SDK=external/ffgl/source/lib
PLUGIN_SOURCES="Controls Lamp Colour Clock Model BlurBuffer Diag Shaders Carousel"
# The plugin's files that include the SDK, and so need its platform switch.
SDK_USERS="BlurBuffer Carousel"
SDK_SOURCES="ffgl/FFGLPluginManager ffgl/FFGLPluginSDK ffgl/FFGLPluginInfo ffgl/FFGLPluginInfoData ffgl/FFGLLog
             ffglex/FFGLShader ffglex/FFGLScreenQuad ffglex/FFGLScopedVAOBinding ffglex/FFGLScopedBufferBinding
             ffglex/FFGLScopedShaderBinding ffglex/FFGLUtilities"

OBJ="$( mktemp -d )"
trap 'rm -rf "$OBJ"' EXIT

INCLUDES=( -Isource -I"$SDK" )
# CAROUSEL_VERSION is read only by MixerEntry.cpp, which is not built here.
BASE=( -std=c++17 -DNDEBUG "${INCLUDES[@]}" )
SDK_PLATFORM=( -D__linux__=1 )

objects=()
compile() {
	local src="$1" opt="$2"; shift 2
	local obj="$OBJ/$( echo "$src" | tr '/' '_' ).o"
	"$EMXX" "${BASE[@]}" "$opt" "$@" -c "$src" -o "$obj"
	objects+=( "$obj" )
}

for name in $PLUGIN_SOURCES; do
	if [[ " $SDK_USERS " == *" $name "* ]]; then
		compile "source/$name.cpp" -O3 "${SDK_PLATFORM[@]}"
	else
		compile "source/$name.cpp" -O3
	fi
done
for name in $SDK_SOURCES; do
	compile "$SDK/$name.cpp" -O3 "${SDK_PLATFORM[@]}"
done
compile demo/wasm/glue.cpp -O3 "${SDK_PLATFORM[@]}"
compile demo/wasm/gl_shim.cpp -O3

LINK=(
	-O3
	-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createCarousel
	-sENVIRONMENT=web
	-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576
	-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
	-sDYNAMIC_EXECUTION=0
	-sFILESYSTEM=1
	-sEXPORTED_RUNTIME_METHODS=GL,FS,UTF8ToString,HEAPF64
	-sEXPORTED_FUNCTIONS=_malloc,_free
)
"$EMXX" "${objects[@]}" "${LINK[@]}" -o demo/carousel-core.js

#---------------------------------------------------------------------------
# The manifest check_shaders.py reads. Inputs first, then outputs.
#---------------------------------------------------------------------------
{
	echo "# demo/tools/build-wasm.sh, $( "$EMXX" --version | sed -n 1p )"
	echo "# FFGL SDK $( git -C external/ffgl rev-parse HEAD )"
	for name in $PLUGIN_SOURCES; do shasum -a 256 "source/$name.cpp"; done
	for header in source/*.h; do shasum -a 256 "$header"; done
	for name in $SDK_SOURCES; do shasum -a 256 "$SDK/$name.cpp"; done
	shasum -a 256 demo/wasm/glue.cpp demo/wasm/gl_shim.cpp demo/tools/build-wasm.sh
	shasum -a 256 demo/carousel-core.js demo/carousel-core.wasm
} > demo/wasm/inputs.sha256

ls -l demo/carousel-core.js demo/carousel-core.wasm | awk '{ print "wrote " $NF ", " $5 " bytes" }'
