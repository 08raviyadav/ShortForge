#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "bitmap_font.hpp"
#include "ffmpeg_decode.hpp"
#include "hwaccel.hpp"
#include "kenburns.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace sf {
namespace {

// RAII wrappers own their FFmpeg objects outright: copies are deleted and
// moves null out the source, so returning them by value can never double-free.

struct FmtOut {
    AVFormatContext* c = nullptr;
    ~FmtOut() {
        if (c) {
            if (!(c->oformat->flags & AVFMT_NOFILE)) avio_closep(&c->pb);
            avformat_free_context(c);
        }
    }
    FmtOut() = default;
    FmtOut(const FmtOut&) = delete;
    FmtOut& operator=(const FmtOut&) = delete;
    FmtOut(FmtOut&& o) noexcept : c(o.c) { o.c = nullptr; }
    FmtOut& operator=(FmtOut&& o) noexcept {
        if (this != &o) {
            if (c) {
                if (!(c->oformat->flags & AVFMT_NOFILE)) avio_closep(&c->pb);
                avformat_free_context(c);
            }
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
struct FramePtr {
    AVFrame* f = nullptr;
    explicit FramePtr(bool alloc = true) {
        if (alloc) f = av_frame_alloc();
    }
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
    void reset() { av_packet_unref(p); }
};

void encode_packet(AVCodecContext* cc, AVFrame* frame, AVStream* st, AVFormatContext* oc) {
    if (avcodec_send_frame(cc, frame) < 0)
        throw std::runtime_error("encoder rejected frame");
    PacketPtr pkt;
    while (true) {
        const int r = avcodec_receive_packet(cc, pkt.p);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
        if (r < 0) throw std::runtime_error("encoding failed");
        av_packet_rescale_ts(pkt.p, cc->time_base, st->time_base);
        pkt.p->stream_index = st->index;
        if (av_interleaved_write_frame(oc, pkt.p) < 0)
            throw std::runtime_error("muxing failed");
        pkt.reset();
    }
}

// Cover-scale an image so it fills W x H (like CSS object-fit: cover),
// keeping up to (W*max_zoom) x (H*max_zoom) native detail so Ken Burns
// zoom-ins never upscale from a downscaled buffer. Never upscales past the
// native source resolution.
Image cover_scale(const Image& src, int W, int H, double max_zoom) {
    const double s_cover = std::max(W / static_cast<double>(src.w), H / static_cast<double>(src.h));
    const double s_need =
        std::max(W * max_zoom / src.w, H * max_zoom / src.h);
    const double s = std::max(s_cover, std::min(s_need, 1.0));
    const int bw = std::max(W, static_cast<int>(std::ceil(src.w * s)));
    const int bh = std::max(H, static_cast<int>(std::ceil(src.h * s)));
    SwsPtr sws;
    sws.c = sws_getContext(src.w, src.h, AV_PIX_FMT_RGB24, bw, bh, AV_PIX_FMT_RGB24,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws.c) throw std::runtime_error("cover scaler init failed");
    Image out;
    out.w = bw;
    out.h = bh;
    out.px.resize(static_cast<size_t>(bw) * bh * 3);
    const uint8_t* sp[1] = {src.px.data()};
    int sstr[1] = {src.stride()};
    uint8_t* dp[1] = {out.px.data()};
    int dstr[1] = {out.stride()};
    sws_scale(sws.c, sp, sstr, 0, src.h, dp, dstr);
    return out;
}

int fit_scale(const std::string& text, int scale, int max_w) {
    while (scale > 4 && text_width(text, scale) > max_w) --scale;
    return scale;
}

void draw_caption(uint8_t* rgb, int W, int H, const std::string& title,
                  const std::string& subtitle) {
    if (title.empty() && subtitle.empty()) return;
    int ts = 9, ss = 6;
    if (!title.empty()) ts = fit_scale(title, ts, W - 120);
    if (!subtitle.empty()) ss = fit_scale(subtitle, ss, W - 120);
    const int ty = H * 14 / 100;
    int block = 0;
    if (!title.empty()) block += 7 * ts;
    if (!subtitle.empty()) block += 28 + 7 * ss;
    draw_bar(rgb, W, H, 0, ty - 28, W, ty + block + 28, 0, 0, 0, 110);
    int y = ty;
    if (!title.empty()) y = draw_text(rgb, W, H, title, W / 2, y, ts, 255, 255, 255);
    if (!subtitle.empty()) draw_text(rgb, W, H, subtitle, W / 2, y + 28, ss, 255, 232, 150);
}

// Try encoders in order until one opens successfully.
// global_header must be decided BEFORE avcodec_open2: H.264 encoders read
// AV_CODEC_FLAG_GLOBAL_HEADER at open time to decide whether SPS/PPS goes
// into extradata (avcC box) or repeats in-band.
CodecCtx open_video_encoder(const std::string& override_name, const Project& p,
                            std::string& used_name, bool global_header) {
    std::vector<std::string> tries;
    if (!override_name.empty()) {
        // Explicit choice: try only it, fail loudly if it can't be used.
        tries.push_back(override_name);
    } else {
        // Auto-pick: fall through the preference list until one opens.
        for (const char* n : {"h264_nvenc", "libx264", "libopenh264", "mpeg4"})
            tries.emplace_back(n);
    }

    CodecCtx cc;
    for (const auto& name : tries) {
        const AVCodec* enc = avcodec_find_encoder_by_name(name.c_str());
        if (!enc && name == "mpeg4") enc = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        if (!enc) {
            if (!override_name.empty())
                throw std::runtime_error("encoder not available: " + name +
                                         " (see `shortforge probe`)");
            continue;
        }
        cc.c = avcodec_alloc_context3(enc);
        if (!cc.c) throw std::runtime_error("codec context alloc failed");
        cc.c->width = p.width;
        cc.c->height = p.height;
        cc.c->time_base = AVRational{1, p.fps};
        cc.c->framerate = AVRational{p.fps, 1};
        cc.c->pix_fmt = AV_PIX_FMT_YUV420P;
        cc.c->bit_rate = 8000000;
        cc.c->gop_size = p.fps * 2;
        cc.c->max_b_frames = 2;
        if (global_header) cc.c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (name == "libx264") av_opt_set(cc.c->priv_data, "preset", "veryfast", 0);
        if (avcodec_open2(cc.c, enc, nullptr) == 0) {
            used_name = name;
            return cc;
        }
        avcodec_free_context(&cc.c);
        if (!override_name.empty())
            throw std::runtime_error("encoder '" + name +
                                     "' is present but failed to open on this machine");
        // Auto-pick: this build/machine can't use it; try next.
    }
    throw std::runtime_error("no usable video encoder (tried: h264_nvenc, libx264, mpeg4)");
}

}  // namespace

RenderStats render_project(const Project& p, const std::string& out_path,
                           const std::string& encoder_override) {
    const int W = p.width, H = p.height, fps = p.fps;

    // 1. Validate every scene image up front (fail fast, before the muxer
    // starts), then decode lazily per scene inside the render loop so peak
    // RAM stays at one scene instead of the whole project.
    std::vector<int> scene_frames;
    int total_frames = 0;
    for (const auto& sc : p.scenes) {
        Image probe = decode_image(sc.image);  // throws on unreadable input
        if (probe.empty()) throw std::runtime_error("empty image: " + sc.image);
        const int n = std::max(1, static_cast<int>(std::lround(sc.duration * fps)));
        scene_frames.push_back(n);
        total_frames += n;
    }
    std::vector<Image> bases(p.scenes.size());
    std::fprintf(stderr, "Validated %zu scenes -> %d frames @ %dfps\n", p.scenes.size(),
                 total_frames, fps);

    // 2. Audio (optional; warn and continue without it on failure).
    AudioClip audio;
    bool has_audio = false;
    if (!p.audio.empty()) {
        try {
            audio = decode_audio(p.audio);
            has_audio = true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Warning: audio skipped (%s)\n", e.what());
        }
    }

    // 3. Output format + streams.
    FmtOut oc;
    if (avformat_alloc_output_context2(&oc.c, nullptr, "mp4", out_path.c_str()) < 0 || !oc.c)
        throw std::runtime_error("cannot create mp4 muxer");

    std::string enc_name;
    const bool global_header = (oc.c->oformat->flags & AVFMT_GLOBALHEADER) != 0;
    CodecCtx vcc = open_video_encoder(encoder_override, p, enc_name, global_header);
    AVStream* vst = avformat_new_stream(oc.c, nullptr);
    if (!vst) throw std::runtime_error("cannot create video stream");
    vst->time_base = vcc.c->time_base;
    if (avcodec_parameters_from_context(vst->codecpar, vcc.c) < 0)
        throw std::runtime_error("video stream params failed");

    CodecCtx acc;
    AVStream* ast = nullptr;
    SwrPtr aswr;
    AVSampleFormat aac_fmt = AV_SAMPLE_FMT_FLTP;
    const AVChannelLayout stereo_layout = AV_CHANNEL_LAYOUT_STEREO;
    if (has_audio) {
        const AVCodec* aenc = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (aenc) {
            acc.c = avcodec_alloc_context3(aenc);
            acc.c->sample_rate = 44100;
            av_channel_layout_copy(&acc.c->ch_layout, &stereo_layout);
            acc.c->sample_fmt =
                aenc->sample_fmts ? aenc->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
            aac_fmt = acc.c->sample_fmt;
            acc.c->bit_rate = 128000;
            acc.c->time_base = AVRational{1, 44100};
            if (oc.c->oformat->flags & AVFMT_GLOBALHEADER)
                acc.c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (avcodec_open2(acc.c, aenc, nullptr) == 0) {
                ast = avformat_new_stream(oc.c, nullptr);
                ast->time_base = acc.c->time_base;
                if (avcodec_parameters_from_context(ast->codecpar, acc.c) < 0)
                    throw std::runtime_error("audio stream params failed");
                if (swr_alloc_set_opts2(&aswr.c, &acc.c->ch_layout, aac_fmt, 44100,
                                        &stereo_layout,
                                        AV_SAMPLE_FMT_S16, 44100, 0, nullptr) < 0 ||
                    !aswr.c || swr_init(aswr.c) < 0)
                    throw std::runtime_error("audio resampler init failed");
            } else {
                avcodec_free_context(&acc.c);
                has_audio = false;
                std::fprintf(stderr, "Warning: AAC encoder unavailable, audio skipped\n");
            }
        } else {
            has_audio = false;
            std::fprintf(stderr, "Warning: AAC encoder missing, audio skipped\n");
        }
    }

    if (!(oc.c->oformat->flags & AVFMT_NOFILE) &&
        avio_open(&oc.c->pb, out_path.c_str(), AVIO_FLAG_WRITE) < 0)
        throw std::runtime_error("cannot open output: " + out_path);
    if (avformat_write_header(oc.c, nullptr) < 0)
        throw std::runtime_error("cannot write mp4 header");

    // 4. Frame buffers + scalers.
    std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3);
    SwsPtr crop_sws;  // cached, reallocates when crop size changes
    SwsPtr yuv_sws;
    yuv_sws.c = sws_getContext(W, H, AV_PIX_FMT_RGB24, W, H, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                               nullptr, nullptr, nullptr);
    if (!yuv_sws.c) throw std::runtime_error("yuv scaler init failed");

    FramePtr yuv(false);
    yuv.f = av_frame_alloc();
    yuv.f->format = AV_PIX_FMT_YUV420P;
    yuv.f->width = W;
    yuv.f->height = H;
    if (av_frame_get_buffer(yuv.f, 32) < 0) throw std::runtime_error("frame buffer alloc failed");

    FramePtr aframe(false);
    const int a_frame_size = (has_audio && acc.c->frame_size > 0) ? acc.c->frame_size : 1024;
    std::vector<int16_t> a_s16;  // resampler input scratch (s16 stereo)
    if (has_audio) {
        aframe.f = av_frame_alloc();
        aframe.f->format = aac_fmt;
        aframe.f->sample_rate = 44100;
        av_channel_layout_copy(&aframe.f->ch_layout, &stereo_layout);
        aframe.f->nb_samples = a_frame_size;
        if (av_frame_get_buffer(aframe.f, 0) < 0)
            throw std::runtime_error("audio frame alloc failed");
        a_s16.resize(static_cast<size_t>(a_frame_size) * 2);
    }

    // 5. Interleaved encode loop.
    const double total_sec = total_frames / static_cast<double>(fps);
    const int64_t need_audio = has_audio ? static_cast<int64_t>(std::ceil(total_sec * 44100)) : 0;
    int v_next = 0, scene_idx = 0, scene_start = 0;
    int64_t a_next = 0;
    int last_pct = -1;

    auto progress = [&]() {
        const int pct = static_cast<int>(100.0 * v_next / total_frames);
        if (pct != last_pct) {
            last_pct = pct;
            std::fprintf(stderr, "\rRendering %d/%d frames (%d%%)", v_next, total_frames, pct);
            std::fflush(stderr);
        }
    };

    while (v_next < total_frames || (has_audio && a_next < need_audio)) {
        const double vt = v_next / static_cast<double>(fps);
        const double at = a_next / 44100.0;
        if (has_audio && a_next < need_audio && at <= vt) {
            // --- audio frame: never encode past what the video needs ---
            const int take =
                static_cast<int>(std::min<int64_t>(a_frame_size, need_audio - a_next));
            for (int i = 0; i < take; ++i) {
                const int64_t s = a_next + i;
                int16_t l = 0, r = 0;
                if (s * 2 + 1 < static_cast<int64_t>(audio.pcm.size())) {
                    l = audio.pcm[static_cast<size_t>(s) * 2];
                    r = audio.pcm[static_cast<size_t>(s) * 2 + 1];
                }
                a_s16[static_cast<size_t>(i) * 2] = l;
                a_s16[static_cast<size_t>(i) * 2 + 1] = r;
            }
            if (av_frame_make_writable(aframe.f) < 0)
                throw std::runtime_error("audio frame not writable");
            const uint8_t* in[1] = {reinterpret_cast<const uint8_t*>(a_s16.data())};
            // Convert straight into the frame's planes: no temp buffer, no
            // alignment guessing. nb_samples follows the actual output count.
            const int got =
                swr_convert(aswr.c, aframe.f->extended_data, a_frame_size, in, take);
            if (got < 0) throw std::runtime_error("audio resample failed");
            aframe.f->nb_samples = got;
            aframe.f->pts = a_next;
            encode_packet(acc.c, aframe.f, ast, oc.c);
            a_next += got;
        } else {
            // --- video frame ---
            while (scene_idx + 1 < static_cast<int>(scene_frames.size()) &&
                   v_next >= scene_start + scene_frames[scene_idx]) {
                scene_start += scene_frames[scene_idx];
                ++scene_idx;
            }
            const Scene& sc = p.scenes[scene_idx];
            if (bases[scene_idx].empty()) {
                // Lazy per-scene decode: peak RAM stays at one scene.
                Image img = decode_image(sc.image);
                const double zmax = std::max(sc.zoom_from, sc.zoom_to);
                bases[scene_idx] = cover_scale(img, W, H, zmax);
            }
            const Image& base = bases[scene_idx];
            const int n = scene_frames[scene_idx];
            const int local = v_next - scene_start;
            const double t = n > 1 ? local / static_cast<double>(n - 1) : 0.0;

            CropRect cr = kenburns_crop(t, base.w, base.h, W, H, sc.zoom_from, sc.zoom_to,
                                        sc.pan_x0, sc.pan_y0, sc.pan_x1, sc.pan_y1);
            const int cwi = std::max(1, static_cast<int>(std::lround(cr.w)));
            const int chi = std::max(1, static_cast<int>(std::lround(cr.h)));
            const int cxi =
                std::min(std::max(0, static_cast<int>(std::lround(cr.x))), base.w - cwi);
            const int cyi =
                std::min(std::max(0, static_cast<int>(std::lround(cr.y))), base.h - chi);

            crop_sws.c = sws_getCachedContext(crop_sws.c, cwi, chi, AV_PIX_FMT_RGB24, W, H,
                                              AV_PIX_FMT_RGB24, SWS_BICUBIC, nullptr, nullptr,
                                              nullptr);
            if (!crop_sws.c) throw std::runtime_error("crop scaler failed");
            const uint8_t* sp[1] = {base.px.data() +
                                    static_cast<size_t>(cyi) * base.stride() + cxi * 3};
            int sstr[1] = {base.stride()};
            uint8_t* dp[1] = {rgb.data()};
            int dstr[1] = {W * 3};
            sws_scale(crop_sws.c, sp, sstr, 0, chi, dp, dstr);

            draw_caption(rgb.data(), W, H, sc.title, sc.subtitle);

            if (av_frame_make_writable(yuv.f) < 0)
                throw std::runtime_error("video frame not writable");
            sws_scale(yuv_sws.c, dp, dstr, 0, H, yuv.f->data, yuv.f->linesize);
            yuv.f->pts = v_next;
            encode_packet(vcc.c, yuv.f, vst, oc.c);
            if (local == n - 1) bases[scene_idx] = Image{};  // release scene pixels
            ++v_next;
            progress();
        }
    }

    // Drain resampler delay so the audio tail isn't cut off (capped at the
    // video duration).
    if (has_audio) {
        while (a_next < need_audio) {
            if (av_frame_make_writable(aframe.f) < 0)
                throw std::runtime_error("audio frame not writable");
            const int max_out =
                static_cast<int>(std::min<int64_t>(a_frame_size, need_audio - a_next));
            const int got =
                swr_convert(aswr.c, aframe.f->extended_data, max_out, nullptr, 0);
            if (got <= 0) break;
            aframe.f->nb_samples = got;
            aframe.f->pts = a_next;
            encode_packet(acc.c, aframe.f, ast, oc.c);
            a_next += got;
        }
    }

    // 6. Flush + trailer.
    encode_packet(vcc.c, nullptr, vst, oc.c);
    if (has_audio) encode_packet(acc.c, nullptr, ast, oc.c);
    if (av_write_trailer(oc.c) < 0) throw std::runtime_error("cannot write trailer");
    std::fprintf(stderr, "\n");

    RenderStats st;
    st.encoder = enc_name;
    st.frames = total_frames;
    st.seconds = total_sec;
    std::error_code ec;
    st.bytes = std::filesystem::file_size(out_path, ec);
    return st;
}

}  // namespace sf
