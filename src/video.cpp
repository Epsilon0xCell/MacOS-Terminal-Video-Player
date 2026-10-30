#include "video.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

// Set by the SIGINT/SIGTERM handler so play() can stop, restore the
// terminal (cursor and colors), and return instead of the process dying
// mid-frame with the cursor hidden.
volatile std::sig_atomic_t g_stop_requested = 0;

void request_stop(int) { g_stop_requested = 1; }

}  // namespace

namespace {

std::string av_error(int errnum) {
    // av_strerror needs a caller owned buffer and can itself fail on an
    // error code it does not know, so the fallback below names the raw
    // number instead of returning an empty message.
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    if (av_strerror(errnum, buf, sizeof(buf)) == 0) return std::string(buf);
    return "unknown FFmpeg error " + std::to_string(errnum);
}

const char *kAsciiRamp = " .:-=+*#%@";  // dark to bright

// Render an RGB24 frame of size w x h (h is even) using half blocks:
// each character cell shows the top pixel as foreground and the bottom
// pixel as background color of U+2580.
void render_color(const AVFrame *frame, int w, int h, std::string &out) {
    out += "\033[H";
    const uint8_t *data = frame->data[0];
    const int stride = frame->linesize[0];
    char esc[64];
    for (int y = 0; y + 1 < h; y += 2) {
        // last_fg and last_bg restart on every row because the row ends
        // with a full color reset. Carrying them across rows would skip an
        // escape the reset already cancelled and the row would start in
        // the wrong colors. Suppressing repeats inside a row is the win
        // here: flat areas of a frame would otherwise emit two color
        // sequences per cell.
        int last_fg = -1, last_bg = -1;
        for (int x = 0; x < w; ++x) {
            const uint8_t *top = data + y * stride + x * 3;
            const uint8_t *bot = data + (y + 1) * stride + x * 3;
            const int fg = (top[0] << 16) | (top[1] << 8) | top[2];
            const int bg = (bot[0] << 16) | (bot[1] << 8) | bot[2];
            if (fg != last_fg || bg != last_bg) {
                std::snprintf(esc, sizeof(esc),
                              "\033[38;2;%d;%d;%d;48;2;%d;%d;%dm",
                              (fg >> 16) & 255, (fg >> 8) & 255, fg & 255,
                              (bg >> 16) & 255, (bg >> 8) & 255, bg & 255);
                out += esc;
                last_fg = fg;
                last_bg = bg;
            }
            out += "\xE2\x96\x80";  // U+2580 upper half block
        }
        out += "\033[0m\n";
    }
}

// Render a GRAY8 frame of size w x h (h is even): each character is the
// average brightness of a vertical pixel pair, mapped onto the ASCII ramp.
// Both renderers step two source rows at a time and drop a final odd
// row. That is safe only because play() always scales to rows * 2, an
// even height, so no caller can hand them an odd h through play().
void render_ascii(const AVFrame *frame, int w, int h, std::string &out) {
    out += "\033[H";
    const uint8_t *data = frame->data[0];
    const int stride = frame->linesize[0];
    const int levels = static_cast<int>(std::strlen(kAsciiRamp));
    for (int y = 0; y + 1 < h; y += 2) {
        for (int x = 0; x < w; ++x) {
            const int avg = (data[y * stride + x] + data[(y + 1) * stride + x]) / 2;
            int idx = avg * levels / 256;
            if (idx >= levels) idx = levels - 1;
            out += kAsciiRamp[idx];
        }
        out += '\n';
    }
}

}  // namespace

VideoPlayer::VideoPlayer(std::string path) : path_(std::move(path)) {}

VideoPlayer::~VideoPlayer() {
    // Free order matters to me here: the scaler was built from the codec
    // context's format, and the format context owns the stream data the
    // codec parameters came from, so I tear them down in reverse order
    // of how open() and play() built them.
    if (sws_ctx_) sws_freeContext(sws_ctx_);
    if (codec_ctx_) avcodec_free_context(&codec_ctx_);
    if (fmt_ctx_) avformat_close_input(&fmt_ctx_);
}

bool VideoPlayer::open(std::string &error) {
    int ret = avformat_open_input(&fmt_ctx_, path_.c_str(), nullptr, nullptr);
    if (ret < 0) {
        error = "cannot open '" + path_ + "': " + av_error(ret);
        return false;
    }
    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0) {
        error = "cannot read stream info: " + av_error(ret);
        return false;
    }
    // I let FFmpeg pick the video stream instead of taking stream 0.
    // Plenty of files put an audio or subtitle stream first, and this
    // player ignores audio entirely by design.
    const AVCodec *decoder = nullptr;
    video_stream_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (video_stream_ < 0 || decoder == nullptr) {
        error = "no video stream found in '" + path_ + "'";
        return false;
    }
    codec_ctx_ = avcodec_alloc_context3(decoder);
    if (!codec_ctx_) {
        error = "out of memory allocating codec context";
        return false;
    }
    ret = avcodec_parameters_to_context(codec_ctx_,
                                        fmt_ctx_->streams[video_stream_]->codecpar);
    if (ret < 0) {
        error = "cannot copy codec parameters: " + av_error(ret);
        return false;
    }
    ret = avcodec_open2(codec_ctx_, decoder, nullptr);
    if (ret < 0) {
        error = "cannot open decoder: " + av_error(ret);
        return false;
    }
    return true;
}

double VideoPlayer::duration_seconds() const {
    // Some containers carry no duration. I report 0 rather than letting
    // the AV_NOPTS_VALUE sentinel through, where it would print as a
    // huge negative number of seconds in main().
    if (!fmt_ctx_ || fmt_ctx_->duration == AV_NOPTS_VALUE) return 0.0;
    return static_cast<double>(fmt_ctx_->duration) / AV_TIME_BASE;
}

int VideoPlayer::play(int cols, int rows, RenderMode mode, int max_frames,
                      std::string &error) {
    if (!fmt_ctx_ || !codec_ctx_) {
        error = "player is not open";
        return -1;
    }
    if (cols < 1 || rows < 1) {
        error = "invalid terminal size";
        return -1;
    }
    const int px_w = cols;
    const int px_h = rows * 2;  // two vertical pixels per character cell
    const AVPixelFormat target =
        (mode == RenderMode::Color) ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_GRAY8;

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *scaled = av_frame_alloc();
    if (!packet || !frame || !scaled) {
        error = "out of memory allocating frames";
        return -1;
    }
    auto cleanup = [&]() {
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&scaled);
    };

    scaled->format = target;
    scaled->width = px_w;
    scaled->height = px_h;
    // Buffer owned by the frame (av_frame_free releases it). Do not free
    // scaled->data manually: sws_scale_frame manages its own references.
    // The 32 byte alignment is for the scaler's vector paths; an
    // unaligned buffer can fail or fall back on some builds.
    int ret = av_frame_get_buffer(scaled, 32);
    if (ret < 0) {
        error = "cannot allocate scaled frame: " + av_error(ret);
        cleanup();
        return -1;
    }

    sws_ctx_ = sws_getContext(codec_ctx_->width, codec_ctx_->height,
                              codec_ctx_->pix_fmt, px_w, px_h, target,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        error = "cannot create scaler (unsupported pixel format?)";
        cleanup();
        return -1;
    }

    AVStream *stream = fmt_ctx_->streams[video_stream_];
    const AVRational tb = stream->time_base;
    const double fps = av_q2d(av_guess_frame_rate(fmt_ctx_, stream, nullptr));

    // I build each frame in one string and write it in a single fputs so
    // a slow terminal never shows a half drawn frame. The reserve is a
    // rough floor, not a promise: a noisy frame with a color change in
    // every cell outgrows it and the string reallocates, which is rare
    // enough that I accept it.
    std::string out;
    out.reserve(static_cast<size_t>(px_w) * px_h * 4);
    std::printf("\033[?25l");  // hide cursor
    std::fflush(stdout);

    // Stop cleanly on Ctrl+C or SIGTERM so the restore sequence below
    // (colors reset, cursor shown) always runs.
    // I clear the flag before installing my handlers, not after: if a
    // previous play() call in the same process was stopped, a leftover 1
    // would make this run quit before its first frame.
    g_stop_requested = 0;
    struct sigaction sa {};
    sa.sa_handler = request_stop;
    sigemptyset(&sa.sa_mask);
    struct sigaction old_int {}, old_term {};
    sigaction(SIGINT, &sa, &old_int);
    sigaction(SIGTERM, &sa, &old_term);

    int rendered = 0;
    bool done = false;
    bool flushed = false;
    // t0 starts before the first decode, so time spent decoding counts
    // against the pacing budget instead of being added on top of it.
    auto t0 = std::chrono::steady_clock::now();
    int64_t first_pts = AV_NOPTS_VALUE;

    auto handle_frame = [&](AVFrame *f) {
        // Frame pacing: wait until the frame's presentation time.
        int64_t pts = f->best_effort_timestamp;
        double target_s;
        if (pts != AV_NOPTS_VALUE) {
            // I anchor on the first frame's timestamp instead of assuming
            // the stream starts at zero. Files cut from a longer recording
            // can start minutes in, and without this subtraction the first
            // frame would sleep for that whole offset.
            if (first_pts == AV_NOPTS_VALUE) first_pts = pts;
            target_s = static_cast<double>(pts - first_pts) * av_q2d(tb);
        } else if (fps > 0) {
            // No timestamp at all: I pace by frames already shown. Using
            // rendered rather than a decode counter keeps a frame the
            // decoder produced early from pulling every later frame
            // forward with it.
            target_s = rendered / fps;
        } else {
            target_s = 0.0;
        }
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - t0).count();
        if (target_s > elapsed) {
            // Sleep in short slices so a stop request interrupts pacing
            // promptly instead of waiting out a long frame gap.
            double remaining = target_s - elapsed;
            while (remaining > 0 && !g_stop_requested) {
                double slice = remaining < 0.01 ? remaining : 0.01;
                std::this_thread::sleep_for(std::chrono::duration<double>(slice));
                remaining -= slice;
            }
        }
        if (g_stop_requested) { done = true; return; }
        int scale_ret = sws_scale_frame(sws_ctx_, scaled, f);
        if (scale_ret < 0) {
            error = "cannot scale frame: " + av_error(scale_ret);
            rendered = -1;
            done = true;
            return;
        }
        out.clear();
        if (mode == RenderMode::Color) render_color(scaled, px_w, px_h, out);
        else render_ascii(scaled, px_w, px_h, out);
        std::fputs(out.c_str(), stdout);
        std::fflush(stdout);
        ++rendered;
        if (max_frames > 0 && rendered >= max_frames) done = true;
    };

    while (!done) {
        ret = av_read_frame(fmt_ctx_, packet);
        if (ret == AVERROR_EOF) {
            // End of file is not end of pictures. A decoder with B-frames
            // can still be holding finished frames, so I send the null
            // flush packet once and keep receiving until it says EOF too.
            // The flushed flag stops me sending that null packet again on
            // every pass through this branch.
            if (!flushed) {
                avcodec_send_packet(codec_ctx_, nullptr);  // flush decoder
                flushed = true;
            }
            ret = avcodec_receive_frame(codec_ctx_, frame);
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) break;
            if (ret < 0) {
                error = "decode error: " + av_error(ret);
                rendered = -1;
                break;
            }
            handle_frame(frame);
            av_frame_unref(frame);
            continue;
        }
        if (ret < 0) {
            error = "read error: " + av_error(ret);
            rendered = -1;
            break;
        }
        if (packet->stream_index != video_stream_) {
            av_packet_unref(packet);
            continue;
        }
        ret = avcodec_send_packet(codec_ctx_, packet);
        // I unref before checking ret on purpose: send_packet does not
        // take ownership of the packet, so success or failure the buffer
        // is still mine to release, and the next av_read_frame reuses it.
        av_packet_unref(packet);
        if (ret < 0) {
            error = "decode error: " + av_error(ret);
            rendered = -1;
            break;
        }
        while (!done) {
            ret = avcodec_receive_frame(codec_ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                error = "decode error: " + av_error(ret);
                rendered = -1;
                done = true;
                break;
            }
            handle_frame(frame);
            av_frame_unref(frame);
        }
    }

    // This restore line is the whole reason the signal handler only sets
    // a flag: every exit from the loop above, error, flush, frame limit,
    // or stop request, funnels through here with the cursor still hidden.
    std::printf("\033[0m\033[?25h\n");  // reset colors, show cursor
    std::fflush(stdout);
    // I hand the old handlers back only after the restore is printed, so
    // a second Ctrl+C during cleanup still cannot kill the process
    // between hiding and showing the cursor.
    sigaction(SIGINT, &old_int, nullptr);
    sigaction(SIGTERM, &old_term, nullptr);
    cleanup();
    return rendered;
}
