#pragma once

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

// How decoded frames are drawn to the terminal.
enum class RenderMode {
    Color,  // truecolor ANSI half-blocks (two pixels per character cell)
    Ascii,  // grayscale ASCII ramp
};

// A minimal FFmpeg-backed video player that renders frames as terminal text.
//
// Usage:
//   VideoPlayer player(path);
//   std::string error;
//   if (!player.open(error)) { /* error describes the failure */ }
//   player.play(cols, rows, RenderMode::Color, 0, error);
class VideoPlayer {
public:
    explicit VideoPlayer(std::string path);
    ~VideoPlayer();

    VideoPlayer(const VideoPlayer &) = delete;
    VideoPlayer &operator=(const VideoPlayer &) = delete;

    // Open the file and prepare the decoder. Returns false and fills
    // `error` with a human-readable message on failure.
    bool open(std::string &error);

    // Decode and render. `cols`/`rows` are terminal character cells.
    // `max_frames` <= 0 plays to the end of the stream.
    // Returns the number of frames rendered, or -1 on error (`error` set).
    int play(int cols, int rows, RenderMode mode, int max_frames, std::string &error);

    int width() const { return codec_ctx_ ? codec_ctx_->width : 0; }
    int height() const { return codec_ctx_ ? codec_ctx_->height : 0; }
    double duration_seconds() const;

private:
    std::string path_;
    AVFormatContext *fmt_ctx_ = nullptr;
    AVCodecContext *codec_ctx_ = nullptr;
    SwsContext *sws_ctx_ = nullptr;
    int video_stream_ = -1;
};
