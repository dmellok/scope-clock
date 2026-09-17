#!/bin/sh
# Build the viewer as a static page: the same render path, compiled to
# WebAssembly, running in whoever's browser opens it. Nothing to install at
# their end and no server at ours — which is the difference between "here is a
# repo you can build" and "here is a link".
#
# Needs emscripten (brew install emscripten, or emsdk). The GitHub Action in
# .github/workflows/pages.yml runs exactly this, so CI is not a second recipe.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
SRC="$ROOT/display-teensy/src"
SIM="$ROOT/tools/hostsim"
OUT="$HERE/dist"

# The face table again, from the bridge, exactly as the native build does it.
python3 "$HERE/gen_names.py" "$ROOT/bridge-esp32/src/main.cpp" > "$HERE/names.h"

rm -rf "$OUT"; mkdir -p "$OUT"

# MODULARIZE gives a factory the page awaits rather than a global that has to
# exist before the page script runs; ALLOW_MEMORY_GROWTH because a dense face
# is a couple of hundred KB of captured beam path and the default heap is not
# generous. -O3: this is the thing rendering 60 times a second.
em++ -std=c++17 -O3 \
  -I "$HERE" -I "$SIM" -I "$SIM/fake" \
  -I "$ROOT/display-teensy/include" -I "$ROOT/shared" \
  -o "$OUT/websim.js" \
  "$HERE/wasm.cpp" "$HERE/core.cpp" "$SIM/sim.cpp" "$SIM/seed.cpp" \
  "$SRC/vector.cpp" "$SRC/text.cpp" "$SRC/drawlist.cpp" "$SRC/faces.cpp" \
  "$SRC"/faces_*.cpp \
  "$SRC/gauges.cpp" "$SRC/nowplaying.cpp" "$SRC/hostdata.cpp" "$SRC/zones.cpp" \
  "$SRC/radar.cpp" "$SRC/audiofx.cpp" \
  -s MODULARIZE=1 -s EXPORT_NAME=createWebsim \
  -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT=web \
  -s EXPORTED_FUNCTIONS='["_ws_init","_ws_faces_json","_ws_frame","_ws_frame_len"]' \
  -s EXPORTED_RUNTIME_METHODS='["UTF8ToString","HEAPU8"]'

# One page, two transports: it uses the wasm module if the factory is there and
# falls back to fetching from the native server if it is not. So the only
# difference in the published copy is that the module gets loaded.
sed 's|<script>|<script src="websim.js"></script>\
<script>|' "$HERE/index.html" > "$OUT/index.html"

# Pages serves what it is given; without this Jekyll would eat it.
touch "$OUT/.nojekyll"

echo "built $OUT ->" && ls -la "$OUT"
