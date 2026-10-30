# macos-terminal-video-high-quality

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
