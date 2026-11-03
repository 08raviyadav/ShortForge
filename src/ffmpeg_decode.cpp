#include "ffmpeg_decode.hpp"

#include <stdexcept>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace sf {
namespace {

// RAII wrappers own their FFmpeg objects outright: copies are deleted and
// moves null out the source, so returning them by value can never double-free
// (a user-declared destructor alone would suppress the implicit move and
// leave a shallow copy as the fallback).

struct FmtInput {
    AVFormatContext* c = nullptr;
    ~FmtInput() {
        if (c) avformat_close_input(&c);
    }
    FmtInput() = default;
    FmtInput(const FmtInput&) = delete;
    FmtInput& operator=(const FmtInput&) = delete;
    FmtInput(FmtInput&& o) noexcept : c(o.c) { o.c = nullptr; }
    FmtInput& operator=(FmtInput&& o) noexcept {
        if (this != &o) {
            if (c) avformat_close_input(&c);
            c = o.c;
            o.c = nullptr;
        }
        return *this;
    }
};

struct CodecCtx {
    AVCodecContext* c = nullptr;
    ~CodecCtx() {
        avcodec_free_context(&c);
    }
    CodecCtx() = default;
    CodecCtx(const CodecCtx&) = delete;
    CodecCtx& operator=(const CodecCtx&) = delete;
    CodecCtx(CodecCtx&& o) noexcept : c(o.c) { o.c = nullptr; }
    CodecCtx& operator=(CodecCtx&& o) noexcept {
        if (this != &o) {
            avcodec_free_context(&c);
            c = o.c;
            o.c = nullptr;
        }
        return *this;
    }
};

struct FramePtr {
    AVFrame* f = nullptr;
    FramePtr() : f(av_frame_alloc()) {}
    ~FramePtr() {
        av_frame_free(&f);
    }
    FramePtr(const FramePtr&) = delete;
    FramePtr& operator=(const FramePtr&) = delete;
    FramePtr(FramePtr&& o) noexcept : f(o.f) { o.f = nullptr; }
    FramePtr& operator=(FramePtr&& o) noexcept {
        if (this != &o) {
            av_frame_free(&f);
            f = o.f;
            o.f = nullptr;
        }
        return *this;
    }
};

struct PacketPtr {
    AVPacket* p = nullptr;
    PacketPtr() : p(av_packet_alloc()) {}
    ~PacketPtr() {
        av_packet_free(&p);
    }
    PacketPtr(const PacketPtr&) = delete;
    PacketPtr& operator=(const PacketPtr&) = delete;
    PacketPtr(PacketPtr&& o) noexcept : p(o.p) { o.p = nullptr; }
    PacketPtr& operator=(PacketPtr&& o) noexcept {
        if (this != &o) {
            av_packet_free(&p);
            p = o.p;
            o.p = nullptr;
        }
        return *this;
    }
};

struct SwsPtr {
    SwsContext* c = nullptr;
    ~SwsPtr() {
        sws_freeContext(c);
    }
    SwsPtr() = default;
    SwsPtr(const SwsPtr&) = delete;
    SwsPtr& operator=(const SwsPtr&) = delete;
    SwsPtr(SwsPtr&& o) noexcept : c(o.c) { o.c = nullptr; }
    SwsPtr& operator=(SwsPtr&& o) noexcept {
        if (this != &o) {
            sws_freeContext(c);
            c = o.c;
            o.c = nullptr;
        }
        return *this;
    }
};

struct SwrPtr {
    SwrContext* c = nullptr;
    ~SwrPtr() {
        swr_free(&c);
    }
    SwrPtr() = default;
    SwrPtr(const SwrPtr&) = delete;
    SwrPtr& operator=(const SwrPtr&) = delete;
    SwrPtr(SwrPtr&& o) noexcept : c(o.c) { o.c = nullptr; }
    SwrPtr& operator=(SwrPtr&& o) noexcept {
        if (this != &o) {
            swr_free(&c);
            c = o.c;
            o.c = nullptr;
        }
        return *this;
    }
};

// Open a media file and return the format context plus the best stream index
// of the requested type and its decoder.
struct OpenedStream {
    FmtInput fmt;
    CodecCtx cc;
    int stream_index = -1;
    OpenedStream() = default;
    OpenedStream(const OpenedStream&) = delete;
    OpenedStream& operator=(const OpenedStream&) = delete;
    OpenedStream(OpenedStream&&) = default;
    OpenedStream& operator=(OpenedStream&&) = default;
};

OpenedStream open_stream(const std::string& path, AVMediaType type) {
    OpenedStream o;
    if (avformat_open_input(&o.fmt.c, path.c_str(), nullptr, nullptr) < 0)
        throw std::runtime_error("cannot open file: " + path);
    if (avformat_find_stream_info(o.fmt.c, nullptr) < 0)
        throw std::runtime_error("cannot read stream info: " + path);
    const AVCodec* dec = nullptr;
    o.stream_index = av_find_best_stream(o.fmt.c, type, -1, -1, &dec, 0);
    if (o.stream_index < 0 || !dec) throw std::runtime_error("no suitable stream in: " + path);
    o.cc.c = avcodec_alloc_context3(dec);
    if (!o.cc.c) throw std::runtime_error("codec context alloc failed");
    if (avcodec_parameters_to_context(o.cc.c, o.fmt.c->streams[o.stream_index]->codecpar) < 0)
        throw std::runtime_error("codec params failed: " + path);
    if (avcodec_open2(o.cc.c, dec, nullptr) < 0)
        throw std::runtime_error("cannot open decoder: " + path);
    return o;
}

}  // namespace

Image decode_image(const std::string& path) {
    OpenedStream o = open_stream(path, AVMEDIA_TYPE_VIDEO);

    FramePtr frame;
    PacketPtr pkt;
    bool got = false;
    auto receive_one = [&]() -> bool {
        av_frame_unref(frame.f);
        if (avcodec_receive_frame(o.cc.c, frame.f) == 0) {
            got = true;
            return true;
        }
        return false;
    };
    while (!got && av_read_frame(o.fmt.c, pkt.p) >= 0) {
        if (pkt.p->stream_index != o.stream_index) {
            av_packet_unref(pkt.p);
            continue;
        }
        int r = avcodec_send_packet(o.cc.c, pkt.p);
        if (r == AVERROR(EAGAIN)) {
            receive_one();  // drain one frame to make room, then retry
            r = avcodec_send_packet(o.cc.c, pkt.p);
        }
        // A failed send (e.g. corrupt packet) skips the packet, not the file.
        if (r == 0) receive_one();
        av_packet_unref(pkt.p);
    }
    if (!got) {  // flush for single-image demuxers
        avcodec_send_packet(o.cc.c, nullptr);
        FramePtr f2;
        if (avcodec_receive_frame(o.cc.c, f2.f) == 0) {
            av_frame_unref(frame.f);
            av_frame_move_ref(frame.f, f2.f);
            got = true;
        }
    }
    if (!got) throw std::runtime_error("no decodable frame in: " + path);

    SwsPtr sws;
    sws.c = sws_getContext(frame.f->width, frame.f->height,
                           static_cast<AVPixelFormat>(frame.f->format), frame.f->width,
                           frame.f->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr,
                           nullptr);
    if (!sws.c) throw std::runtime_error("scaler init failed");

    Image img;
    img.w = frame.f->width;
    img.h = frame.f->height;
    img.px.resize(static_cast<size_t>(img.w) * img.h * 3);
    uint8_t* dst[1] = {img.px.data()};
    int stride[1] = {img.w * 3};
    sws_scale(sws.c, frame.f->data, frame.f->linesize, 0, frame.f->height, dst, stride);
    return img;
}

AudioClip decode_audio(const std::string& path) {
    OpenedStream o = open_stream(path, AVMEDIA_TYPE_AUDIO);
    AVCodecContext* cc = o.cc.c;

    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    SwrPtr swr;
    if (swr_alloc_set_opts2(&swr.c, &out_layout, AV_SAMPLE_FMT_S16, 44100, &cc->ch_layout,
                            cc->sample_fmt, cc->sample_rate, 0, nullptr) < 0 ||
        !swr.c || swr_init(swr.c) < 0)
        throw std::runtime_error("resampler init failed: " + path);

    AudioClip clip;
    FramePtr frame;
    PacketPtr pkt;
    std::vector<int16_t> tmp;

    auto convert = [&](AVFrame* f) {
        const int out_samples = swr_get_out_samples(swr.c, f->nb_samples) + 256;
        tmp.resize(static_cast<size_t>(out_samples) * 2);
        uint8_t* out[1] = {reinterpret_cast<uint8_t*>(tmp.data())};
        // extended_data (not data): correct for planar layouts beyond 8
        // channels; identical to data for <= 8 channels.
        const int n = swr_convert(swr.c, out, out_samples,
                                 const_cast<const uint8_t**>(f->extended_data),
                                 f->nb_samples);
        if (n < 0) throw std::runtime_error("resample failed: " + path);
        clip.pcm.insert(clip.pcm.end(), tmp.data(), tmp.data() + static_cast<size_t>(n) * 2);
    };

    auto drain_frames = [&]() {
        int rr;
        do {
            av_frame_unref(frame.f);
            rr = avcodec_receive_frame(cc, frame.f);
            if (rr == 0) convert(frame.f);
        } while (rr == 0);
    };

    while (av_read_frame(o.fmt.c, pkt.p) >= 0) {
        if (pkt.p->stream_index != o.stream_index) {
            av_packet_unref(pkt.p);
            continue;
        }
        int r = avcodec_send_packet(cc, pkt.p);
        if (r == AVERROR(EAGAIN)) {
            drain_frames();  // make room, then retry the packet
            r = avcodec_send_packet(cc, pkt.p);
        }
        if (r == 0) drain_frames();
        av_packet_unref(pkt.p);
    }
    avcodec_send_packet(cc, nullptr);
    drain_frames();
    // Drain resampler.
    const int out_samples = swr_get_out_samples(swr.c, 0) + 256;
    tmp.resize(static_cast<size_t>(out_samples) * 2);
    uint8_t* out[1] = {reinterpret_cast<uint8_t*>(tmp.data())};
    const int n = swr_convert(swr.c, out, out_samples, nullptr, 0);
    if (n > 0) clip.pcm.insert(clip.pcm.end(), tmp.data(), tmp.data() + static_cast<size_t>(n) * 2);

    if (clip.pcm.empty()) throw std::runtime_error("no audio decoded from: " + path);
    return clip;
}

}  // namespace sf
