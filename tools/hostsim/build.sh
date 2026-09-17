#!/bin/sh
# Build the host sim. Compiles the REAL vector.cpp/text.cpp and the face under
# test against a fake DAC, so what is measured is what the tube would draw
# rather than a reimplementation that can drift from it.
#
# UBSan is on deliberately: it is what caught the globe's view vector
# overflowing int32 (two Q16 values multiplied is 2^32), which no amount of
# looking at the picture would have shown.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
SRC="$ROOT/display-teensy/src"

CXX="${CXX:-c++}"
$CXX -std=c++17 -O1 -g -fsanitize=undefined,address \
  -I "$HERE" -I "$HERE/fake" \
  -I "$ROOT/display-teensy/include" -I "$ROOT/shared" \
  -o "$HERE/sizeface" \
  "$HERE/sizeface.cpp" "$HERE/sim.cpp" \
  "$SRC/vector.cpp" "$SRC/text.cpp" \
  "$SRC/drawlist.cpp" "$SRC/faces_now.cpp" "$SRC/faces_audio.cpp" \
  "$SRC/radar.cpp" "$SRC/gauges.cpp" "$SRC/nowplaying.cpp" "$SRC/audiofx.cpp"

echo "built $HERE/sizeface"

# The thumbnail generator links EVERY face, which needs the whole face set plus
# the small stores the host-fed ones read from. hal::midi is stubbed inside
# seed.cpp; it is the only HAL a face touches, and seed.cpp also carries the
# host-fed fixtures the faces draw from.
$CXX -std=c++17 -O2 -g \
  -I "$HERE" -I "$HERE/fake" \
  -I "$ROOT/display-teensy/include" -I "$ROOT/shared" \
  -o "$HERE/thumbs" \
  "$HERE/thumbs.cpp" "$HERE/sim.cpp" "$HERE/seed.cpp" \
  "$SRC/vector.cpp" "$SRC/text.cpp" "$SRC/drawlist.cpp" "$SRC/faces.cpp" \
  "$SRC"/faces_*.cpp \
  "$SRC/gauges.cpp" "$SRC/nowplaying.cpp" "$SRC/hostdata.cpp" "$SRC/zones.cpp" \
  "$SRC/radar.cpp" "$SRC/audiofx.cpp"

echo "built $HERE/thumbs"
