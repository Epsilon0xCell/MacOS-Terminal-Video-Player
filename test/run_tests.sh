#!/bin/bash
# Behavioral tests for tvp, the terminal video player.
#
# Builds the player from source, generates a short test clip with the
# ffmpeg command line tool, and checks the documented behavior end to
# end: both render modes, the frame limit, end of file draining,
# timestamp pacing, the Ctrl-C stop path, and the command line error
# exits. Needs g++, the FFmpeg development headers and libraries (the
# same requirement as building the player at all), and the ffmpeg tool.
#
# The include and library flags default to the Homebrew layout from
# the README. Override them for other layouts, for example:
#   TVP_INCLUDE=-I/path/to/ffmpeg/include \
#   TVP_LIBS="-l:libavformat.so.62 -l:libavcodec.so.62 -l:libavutil.so.60 -l:libswscale.so.9" \
#   bash test/run_tests.sh
set -u
cd "$(dirname "$0")/.."

TVP_INCLUDE="${TVP_INCLUDE:--I/opt/homebrew/include}"
TVP_LIBDIR="${TVP_LIBDIR:--L/opt/homebrew/lib}"
TVP_LIBS="${TVP_LIBS:--lavformat -lavcodec -lavutil -lswscale}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
TVP="$WORK/tvp"
CLIP="$WORK/clip.mp4"
CLIP3="$WORK/clip3.mp4"

pass=0
fail=0
check() { # check <name> <condition...>
    local name="$1"; shift
    if "$@" > /dev/null 2>&1; then
        pass=$((pass + 1)); echo "ok - $name"
    else
        fail=$((fail + 1)); echo "FAIL - $name"
    fi
}
contains() { grep -qF -- "$1" "$2"; }

echo "building tvp..."
if ! g++ src/main.cpp src/video.cpp -Iinc/ $TVP_INCLUDE -std=c++17 -O2 \
        $TVP_LIBDIR $TVP_LIBS -o "$TVP" 2> "$WORK/build.log"; then
    echo "FAIL - build (see below)"; cat "$WORK/build.log"; exit 1
fi
pass=$((pass + 1)); echo "ok - build"

ffmpeg -v error -f lavfi -i testsrc=duration=1:size=160x90:rate=10 \
    -c:v libx264 -pix_fmt yuv420p "$CLIP" -y || { echo "FAIL - clip generation"; exit 1; }
ffmpeg -v error -f lavfi -i testsrc=duration=3:size=160x90:rate=10 \
    -c:v libx264 -pix_fmt yuv420p "$CLIP3" -y || { echo "FAIL - clip generation"; exit 1; }

"$TVP" "$CLIP" --frames 3 > "$WORK/color.txt" 2>&1
check "truecolor run exits 0" test $? -eq 0
check "truecolor output uses 24-bit color escapes" contains '38;2;' "$WORK/color.txt"
check "truecolor run renders the requested 3 frames" contains 'rendered 3 frame(s)' "$WORK/color.txt"
check "run restores the cursor at the end" contains "$(printf '\033[?25h')" "$WORK/color.txt"

"$TVP" "$CLIP" --ascii --frames 3 > "$WORK/ascii.txt" 2>&1
check "ascii run exits 0" test $? -eq 0
if grep -qF '38;2;' "$WORK/ascii.txt"; then
    fail=$((fail + 1)); echo "FAIL - ascii output carries no color escapes"
else
    pass=$((pass + 1)); echo "ok - ascii output carries no color escapes"
fi
check "ascii run renders the requested 3 frames" contains 'rendered 3 frame(s)' "$WORK/ascii.txt"

"$TVP" "$CLIP" > "$WORK/full.txt" 2>&1
check "full run exits 0" test $? -eq 0
check "full run renders all 10 frames (decoder drained at EOF)" contains 'rendered 10 frame(s)' "$WORK/full.txt"

start=$(date +%s%N)
"$TVP" "$CLIP3" > "$WORK/paced.txt" 2>&1
elapsed_ms=$(( ($(date +%s%N) - start) / 1000000 ))
if [ "$elapsed_ms" -ge 2500 ]; then
    pass=$((pass + 1)); echo "ok - playback is paced to the timestamps (${elapsed_ms}ms for a 3s clip)"
else
    fail=$((fail + 1)); echo "FAIL - playback is paced to the timestamps (only ${elapsed_ms}ms for a 3s clip)"
fi

"$TVP" "$CLIP3" > "$WORK/int.txt" 2>&1 &
pid=$!
sleep 0.6
kill -INT "$pid"
wait "$pid"
check "Ctrl-C stop exits 0" test $? -eq 0
check "Ctrl-C stop restores the cursor" contains "$(printf '\033[?25h')" "$WORK/int.txt"

"$TVP" > "$WORK/noargs.txt" 2>&1
check "no arguments exits 2" test $? -eq 2
check "no arguments prints the usage line" contains 'Usage:' "$WORK/noargs.txt"

"$TVP" /nonexistent.mp4 > "$WORK/missing.txt" 2>&1
check "missing file exits 1" test $? -eq 1
check "missing file names the problem" contains 'cannot open' "$WORK/missing.txt"

"$TVP" "$CLIP" --width abc > "$WORK/badwidth.txt" 2>&1
check "a non-numeric width exits 2" test $? -eq 2

"$TVP" "$CLIP" --width 0 > "$WORK/zerowidth.txt" 2>&1
check "a zero width exits 2" test $? -eq 2

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
