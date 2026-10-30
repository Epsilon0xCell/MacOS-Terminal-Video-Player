// tvp: play a video in the terminal at the highest quality the terminal
// allows: truecolor half-block rendering (two pixels per character cell),
// or a grayscale ASCII mode.
//
// Usage:
//   tvp <file> [--ascii] [--width COLS] [--height ROWS] [--frames N]
//
// Build (see README):
//   g++ src/main.cpp src/video.cpp -Iinc/ -I/opt/homebrew/include
//       -std=c++17 -O3 -L/opt/homebrew/lib
//       -lavformat -lavcodec -lavutil -lswscale -o tvp

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/ioctl.h>
#include <unistd.h>

#include "video.h"

namespace {

void usage(const char *argv0) {
    std::fprintf(stderr,
                 "Usage: %s <file> [--ascii] [--width COLS] [--height ROWS] "
                 "[--frames N]\n",
                 argv0);
}

bool parse_int(const char *s, int &value) {
    // I check the end pointer as well as the range because strtol alone
    // would accept "80x" as 80, and a size like that is a typo I want
    // reported, not a width I silently guessed at. The upper bound keeps
    // a huge --frames or --width value from asking play() for a frame
    // buffer the terminal could never show anyway.
    char *end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 1 || v > 100000) return false;
    value = static_cast<int>(v);
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    std::string path;
    RenderMode mode = RenderMode::Color;
    int cols = 0, rows = 0, max_frames = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--ascii") {
            mode = RenderMode::Ascii;
        } else if (arg == "--width" && i + 1 < argc) {
            if (!parse_int(argv[++i], cols)) { usage(argv[0]); return 2; }
        } else if (arg == "--height" && i + 1 < argc) {
            if (!parse_int(argv[++i], rows)) { usage(argv[0]); return 2; }
        } else if (arg == "--frames" && i + 1 < argc) {
            if (!parse_int(argv[++i], max_frames)) { usage(argv[0]); return 2; }
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] != '-' && path.empty()) {
            // Only the first bare token is the file. A second one lands in
            // the else below and fails as unknown, which I prefer to
            // quietly playing the wrong file.
            path = arg;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }
    if (path.empty()) {
        usage(argv[0]);
        return 2;
    }

    if (cols == 0 || rows == 0) {
        // I enter this block when either dimension is missing, but I only
        // fill the ones still at zero, so a run like --width 120 keeps the
        // width I was given and only asks the terminal for the height.
        // Ask the terminal; fall back to a sane default when not a TTY.
        struct winsize ws {};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
            if (cols == 0) cols = ws.ws_col;
            // I give up the last row on purpose. Each rendered frame ends
            // in a newline, and using every row would scroll the picture
            // up one line on the final row of every frame.
            if (rows == 0) rows = ws.ws_row > 1 ? ws.ws_row - 1 : ws.ws_row;
        }
        // When stdout is a pipe the ioctl fails and both stay zero. I fall
        // back to 80x24 so a redirected run still renders at a fixed,
        // repeatable size instead of refusing to play at all.
        if (cols == 0) cols = 80;
        if (rows == 0) rows = 24;
    }

    VideoPlayer player(path);
    std::string error;
    if (!player.open(error)) {
        std::fprintf(stderr, "tvp: %s\n", error.c_str());
        return 1;
    }
    int rendered = player.play(cols, rows, mode, max_frames, error);
    if (rendered < 0) {
        std::fprintf(stderr, "tvp: %s\n", error.c_str());
        return 1;
    }
    std::fprintf(stderr, "tvp: rendered %d frame(s) from %dx%d video (%.1fs)\n",
                 rendered, player.width(), player.height(),
                 player.duration_seconds());
    return 0;
}
