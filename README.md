![macos-terminal-video-high-quality](banner.png)

# MacOS-Terminal-Video-Player

Play video in the terminal at the highest quality the terminal allows:
truecolor rendering with Unicode half blocks (two pixels per character
cell), correctly paced to the video's own timestamps. An ASCII mode is
included for monochrome terminals.

Built on FFmpeg (libavformat / libavcodec / libswscale): any format and
codec FFmpeg can read, the player can play. Audio tracks are ignored; the
best video stream is selected automatically.

## Install FFmpeg

```
brew install ffmpeg
```

## Compile Command

```
g++ src/main.cpp src/video.cpp -Iinc/ -I/opt/homebrew/include -std=c++17 -O3 -L/opt/homebrew/lib -lavformat -lavcodec -lavutil -lswscale -o tvp
```

On Linux, point `-I` / `-L` at your FFmpeg development headers and
libraries instead (packages: libavformat-dev, libavcodec-dev,
libavutil-dev, libswscale-dev). The code uses the stable FFmpeg API
surface common to releases 6.x through 8.x.

## Usage

```
./tvp <file> [--ascii] [--width COLS] [--height ROWS] [--frames N]
```

- With no size options, the current terminal size is used (80x24 fallback
  when output is not a TTY).
- `--ascii` renders a grayscale ASCII ramp instead of truecolor blocks.
- `--frames N` stops after N frames (useful for testing).
- Pressing Ctrl+C (or sending SIGTERM) stops playback cleanly: the player
  restores the cursor and resets colors before exiting.

Examples:

```
./tvp movie.mp4
./tvp clip.mkv --ascii
./tvp trailer.webm --width 120 --height 40
```

## Layout

- `inc/video.h` - the VideoPlayer class interface
- `src/video.cpp` - decoding, scaling, pacing, and terminal rendering
- `src/main.cpp` - command line handling and terminal size detection

## Tests

test/run_tests.sh builds the player, generates a short test clip with
the ffmpeg tool, and checks the behavior above end to end: both render
modes, the frame limit, all frames rendered at end of file, playback
paced to the timestamps, a clean Ctrl-C stop with the cursor restored,
and the command line error exits (19 checks). It needs the same FFmpeg
development files as the build itself. The include and library flags
default to the Homebrew layout; the script header shows how to
override them for other layouts.

```
bash test/run_tests.sh
```
