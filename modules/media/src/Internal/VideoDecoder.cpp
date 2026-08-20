// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/VideoDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace CNA::Internal::Media
{
    VideoDecoder::VideoDecoder() = default;

    VideoDecoder::~VideoDecoder()
    {
        Close();
    }

    bool VideoDecoder::Open(const std::string& path)
    {
        Close();

        if (avformat_open_input(&fmtCtx_, path.c_str(), nullptr, nullptr) < 0)
            return false;

        if (avformat_find_stream_info(fmtCtx_, nullptr) < 0)
        {
            avformat_close_input(&fmtCtx_);
            return false;
        }

        videoStream_ = av_find_best_stream(fmtCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        audioStream_ = av_find_best_stream(fmtCtx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

        if (videoStream_ < 0)
        {
            avformat_close_input(&fmtCtx_);
            return false;
        }

        // --- Video codec ---
        AVStream* vs = fmtCtx_->streams[videoStream_];
        const AVCodec* vCodec = avcodec_find_decoder(vs->codecpar->codec_id);
        if (!vCodec)
        {
            avformat_close_input(&fmtCtx_);
            return false;
        }
        videoCtx_ = AllocAndConfigureCodecContext(vCodec, vs->codecpar);
        if (!videoCtx_)
        {
            avformat_close_input(&fmtCtx_);
            return false;
        }
        if (avcodec_open2(videoCtx_, vCodec, nullptr) < 0)
        {
            avcodec_free_context(&videoCtx_);
            avformat_close_input(&fmtCtx_);
            return false;
        }

        width_  = videoCtx_->width;
        height_ = videoCtx_->height;

        if (vs->avg_frame_rate.den > 0)
            fps_ = static_cast<float>(av_q2d(vs->avg_frame_rate));
        else
            fps_ = 24.0f;

        videoTimeBase_ = av_q2d(vs->time_base);

        if (fmtCtx_->duration != AV_NOPTS_VALUE)
            durationSec_ = static_cast<double>(fmtCtx_->duration) / AV_TIME_BASE;

        // --- Audio codec ---
        if (audioStream_ >= 0)
        {
            AVStream* as = fmtCtx_->streams[audioStream_];
            const AVCodec* aCodec = avcodec_find_decoder(as->codecpar->codec_id);
            if (aCodec)
            {
                audioCtx_ = AllocAndConfigureCodecContext(aCodec, as->codecpar);
                if (audioCtx_ && avcodec_open2(audioCtx_, aCodec, nullptr) < 0)
                {
                    avcodec_free_context(&audioCtx_);
                    audioCtx_ = nullptr;
                }
                if (audioCtx_)
                {
                    // A resampler failure here is treated exactly like the avcodec_open2() failure
                    // just above -- audio simply isn't available for this track, rather than
                    // leaving audioCtx_ allocated with HasAudio() correctly reporting false (via
                    // MEDIA-160) but the codec still decoding audio into a black hole every frame
                    // (found by external code review, plans/plan_media.md MEDIA-162).
                    swrCtx_ = CreateResampler(audioCtx_);
                    if (!swrCtx_)
                    {
                        avcodec_free_context(&audioCtx_);
                        audioCtx_ = nullptr;
                    }
                }
                if (!audioCtx_)
                {
                    audioStream_ = -1;
                }
                else
                {
                    sampleRate_ = audioCtx_->sample_rate;
                    channels_   = audioCtx_->ch_layout.nb_channels;
                    audioTimeBase_ = av_q2d(as->time_base);
                }
            }
        }

        frame_ = av_frame_alloc();
        pkt_   = av_packet_alloc();
        if (!frame_ || !pkt_)
        {
            // A real (if rare) OOM path -- NextFrame()/ProcessAudioPacket() both dereference
            // frame_/pkt_ unconditionally, so leaving either null here and returning success would
            // be a guaranteed null-pointer dereference on the very first decode call
            // (plans/plan_media.md MEDIA-38/94 hardening).
            Close();
            return false;
        }

        return true;
    }

    void VideoDecoder::Close()
    {
        // av_packet_free() below unrefs any packet still retained by an in-progress EAGAIN retry
        // -- reset the flag too so a fresh Open() never starts with stale "resend on first call"
        // state left over from the previous file.
        havePendingVideoPacket_ = false;
        if (pkt_)      { av_packet_free(&pkt_);          pkt_ = nullptr; }
        if (frame_)    { av_frame_free(&frame_);          frame_ = nullptr; }
        if (swrCtx_)   { swr_free(&swrCtx_);              swrCtx_ = nullptr; }
        if (audioCtx_) { avcodec_free_context(&audioCtx_); audioCtx_ = nullptr; }
        if (videoCtx_) { avcodec_free_context(&videoCtx_); videoCtx_ = nullptr; }
        if (fmtCtx_)   { avformat_close_input(&fmtCtx_);  fmtCtx_ = nullptr; }

        videoStream_ = -1;
        audioStream_ = -1;
        width_ = height_ = 0;
        fps_ = 0.0f;
        durationSec_ = 0.0;
        // pendingAudio_ holds decoded-but-not-yet-drained samples from whatever file was open --
        // left uncleared, a caller that reuses this same VideoDecoder instance (Close() then
        // Open() again) would have its first DrainAudio() call after the new Open() return stale
        // samples from the PREVIOUS file (found by external code review, plans/plan_media.md MEDIA-155).
        pendingAudio_.clear();
    }

    void VideoDecoder::SeekToStart()
    {
        if (!fmtCtx_) return;
        // If the seek itself fails, the demuxer's read position never actually moved -- flushing
        // the codecs and discarding pending/buffered state below would build a "we're now at the
        // start" assumption on top of a position that's still wherever it was before this call,
        // continuing to decode from an inconsistent state. Leave everything untouched and let the
        // caller's next NextFrame() simply carry on from wherever the stream already was (found by
        // external code review, plans/plan_media.md MEDIA-159).
        if (av_seek_frame(fmtCtx_, -1, 0, AVSEEK_FLAG_BACKWARD) < 0) return;

        if (videoCtx_) avcodec_flush_buffers(videoCtx_);
        if (audioCtx_) avcodec_flush_buffers(audioCtx_);

        // pendingAudio_ can hold real, undrained samples decoded from BEFORE this seek -- left
        // uncleared, the next DrainAudio() call would splice stale pre-seek audio onto whatever
        // decodes after it (found by external code review, plans/plan_media.md MEDIA-159; the same class
        // of gap MEDIA-155 already fixed for Close(), now also closed here).
        pendingAudio_.clear();

        if (audioCtx_ && swrCtx_)
        {
            // SwrContext keeps its own internal delay buffer, separate from pendingAudio_ -- any
            // samples still held there from before the seek would otherwise surface intermixed
            // with the first freshly-decoded post-seek audio, an audible discontinuity artifact.
            // MEDIA-163's own attempt at this (a bounded manual drain loop) still couldn't
            // *guarantee* the delay reached zero -- it could exit via its own iteration bound or a
            // swr_convert() error with real delay left unconfirmed-drained, and silently discarded
            // a genuine negative return without distinguishing it from "nothing more produced"
            // (found by external code review, plans/plan_media.md MEDIA-167). Discarding the whole
            // resampler and building a fresh one via the same CreateResampler() every other
            // audio-open path already uses sidesteps the question entirely -- a brand-new
            // SwrContext has zero internal delay by construction, no draining required.
            swr_free(&swrCtx_);
            swrCtx_ = CreateResampler(audioCtx_);
            // A recreation failure here is a genuine (if very unlikely) degradation -- the same
            // codec context that already built a working resampler once fails to do so again.
            // Left as swrCtx_ == nullptr rather than thrown: SeekToStart() has no throwing
            // contract anywhere else, is called from VideoPlayer::GetTexture()'s isLooped_ branch
            // with no surrounding try/catch, and HasAudio() (MEDIA-160) already correctly reports
            // "no audio" for a null swrCtx_ -- matching this file's existing graceful
            // audio-unavailable degradation pattern rather than introducing a new throw path a
            // non-throwing caller isn't prepared for.
        }

        // A video packet retained by an in-progress EAGAIN retry (havePendingVideoPacket_) refers
        // to compressed data read from BEFORE this seek -- resending it to the codec context just
        // flushed above would hand a now-out-of-place packet to a decoder with no relevant prior
        // state, at best wasted work and at worst a spurious decode error. Reset both the flag and
        // the packet's own held reference so the next NextFrame() call starts clean, reading fresh
        // packets from the new (seeked) position instead (found by external code review,
        // plans/plan_media.md MEDIA-155).
        if (havePendingVideoPacket_)
        {
            av_packet_unref(pkt_);
            havePendingVideoPacket_ = false;
        }
    }

    AVCodecContext* VideoDecoder::AllocAndConfigureCodecContext(
        const AVCodec* codec, const AVCodecParameters* params)
    {
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        if (!ctx) return nullptr;
        if (avcodec_parameters_to_context(ctx, params) < 0)
        {
            avcodec_free_context(&ctx);
            return nullptr;
        }
        // MEDIA-39's own accept criterion is "a clear exception rather than garbage output" for
        // corrupted frame data -- by default, most FFmpeg decoders are lenient and silently try to
        // conceal/recover from minor stream corruption (e.g. a per-slice CRC mismatch), returning
        // a decoded-but-possibly-glitched frame with just a log warning, no error return code for
        // NextFrame() to surface. AV_EF_CRCCHECK actually enables CRC verification at all (several
        // decoders, e.g. ffv1, otherwise compute but never *act on* a checksum mismatch);
        // AV_EF_EXPLODE then turns any detected inconsistency (CRC or otherwise) into a hard
        // decode error instead of a tolerated one -- so corrupted input is never silently
        // displayed as if it decoded correctly (plans/plan_media.md MEDIA-39/40, confirmed via a real
        // corrupted-ffv1-slice fixture during external code review: AV_EF_EXPLODE alone was not
        // sufficient on its own to make a CRC mismatch fatal).
        ctx->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_BUFFER | AV_EF_EXPLODE;
        return ctx;
    }

    SwrContext* VideoDecoder::CreateResampler(AVCodecContext* ctx)
    {
        SwrContext* newSwr = nullptr;
        int ret = swr_alloc_set_opts2(
            &newSwr,
            &ctx->ch_layout, AV_SAMPLE_FMT_FLT, ctx->sample_rate,
            &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate,
            0, nullptr);
        if (ret < 0 || !newSwr || swr_init(newSwr) < 0)
        {
            if (newSwr) swr_free(&newSwr);
            return nullptr;
        }
        return newSwr;
    }

    bool VideoDecoder::OpenAudioStreamByIndex(int streamIdx)
    {
        if (!fmtCtx_ || streamIdx < 0 || streamIdx >= (int)fmtCtx_->nb_streams) return false;
        AVStream* as = fmtCtx_->streams[streamIdx];
        if (as->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) return false;

        const AVCodec* codec = avcodec_find_decoder(as->codecpar->codec_id);
        if (!codec) return false;

        AVCodecContext* newCtx = AllocAndConfigureCodecContext(codec, as->codecpar);
        if (!newCtx) return false;
        if (avcodec_open2(newCtx, codec, nullptr) < 0)
        {
            avcodec_free_context(&newCtx);
            return false;
        }

        // Build the new resampler and confirm it works BEFORE touching any existing state -- the
        // previous version committed the new codec context and destroyed the old one first, then
        // called SetupResampler() and returned true unconditionally regardless of its result. A
        // resampler-setup failure at that point still reported success while the old, fully
        // working track was already irrecoverably gone and the new one could never actually
        // produce audio. A track switch is now genuinely transactional: both the codec AND its
        // resampler must succeed before anything old is destroyed, matching this function's own
        // return-value contract ("true only if a different stream is now genuinely active") for
        // every failure mode, not just avcodec_open2()'s (found by external code review,
        // plans/plan_media.md MEDIA-162).
        SwrContext* newSwr = CreateResampler(newCtx);
        if (!newSwr)
        {
            avcodec_free_context(&newCtx);
            return false;
        }

        if (swrCtx_)   { swr_free(&swrCtx_);              swrCtx_   = nullptr; }
        if (audioCtx_) { avcodec_free_context(&audioCtx_); }

        audioCtx_      = newCtx;
        swrCtx_        = newSwr;
        audioStream_   = streamIdx;
        sampleRate_    = audioCtx_->sample_rate;
        channels_      = audioCtx_->ch_layout.nb_channels;
        audioTimeBase_ = av_q2d(as->time_base);
        pendingAudio_.clear();

        return true;
    }

    bool VideoDecoder::OpenVideoStreamByIndex(int streamIdx)
    {
        if (!fmtCtx_ || streamIdx < 0 || streamIdx >= (int)fmtCtx_->nb_streams) return false;
        AVStream* vs = fmtCtx_->streams[streamIdx];
        if (vs->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) return false;

        const AVCodec* codec = avcodec_find_decoder(vs->codecpar->codec_id);
        if (!codec) return false;

        AVCodecContext* newCtx = AllocAndConfigureCodecContext(codec, vs->codecpar);
        if (!newCtx) return false;
        if (avcodec_open2(newCtx, codec, nullptr) < 0)
        {
            avcodec_free_context(&newCtx);
            return false;
        }

        if (videoCtx_) avcodec_free_context(&videoCtx_);

        videoCtx_      = newCtx;
        videoStream_   = streamIdx;
        width_         = videoCtx_->width;
        height_        = videoCtx_->height;
        fps_           = (vs->avg_frame_rate.den > 0)
                         ? static_cast<float>(av_q2d(vs->avg_frame_rate))
                         : 24.0f;
        videoTimeBase_ = av_q2d(vs->time_base);
        return true;
    }

    bool VideoDecoder::SetAudioStream(int trackIndex)
    {
        if (!fmtCtx_ || trackIndex < 0) return false;
        int found = 0;
        for (int i = 0; i < (int)fmtCtx_->nb_streams; ++i)
        {
            if (fmtCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                if (found == trackIndex)
                {
                    // Re-selecting the already-active track used to unconditionally discard and
                    // recreate the codec context anyway, throwing away all decode state for no
                    // reason -- harmless for audio (no keyframe concept), but pointless work.
                    // Kept as a no-op for consistency with SetVideoStream()'s own fix below (found
                    // by external code review, plans/plan_media.md MEDIA-131). The bool return (found by
                    // a later external review, plans/plan_media.md MEDIA-154) lets VideoPlayer skip its
                    // own downstream reconfiguration entirely when this was a true no-op, instead
                    // of tearing down and reopening the audio stream for a switch that never happened.
                    // Propagate OpenAudioStreamByIndex()'s own result rather than assuming success
                    // (found by external code review, plans/plan_media.md MEDIA-158) -- it builds the new
                    // codec context before touching audioCtx_/audioStream_, so on failure (bad
                    // codec, alloc failure) the old stream is genuinely still active, but the old
                    // unconditional `return true` told VideoPlayer a switch happened anyway,
                    // triggering a needless/destructive reconfigure for a track that never changed.
                    if (i == audioStream_) return false;
                    return OpenAudioStreamByIndex(i);
                }
                ++found;
            }
        }
        return false; // trackIndex out of range -- also a true no-op.
    }

    bool VideoDecoder::SetVideoStream(int trackIndex)
    {
        if (!fmtCtx_ || trackIndex < 0) return false;
        int found = 0;
        for (int i = 0; i < (int)fmtCtx_->nb_streams; ++i)
        {
            if (fmtCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                if (found == trackIndex)
                {
                    // Re-selecting the already-active track used to unconditionally discard and
                    // recreate the codec context, throwing away all decode state -- a real,
                    // observable bug for video specifically (unlike audio): the demuxer's read
                    // position has already moved past the stream's first keyframe by the time any
                    // later SetVideoTrackEXT() call happens, so the freshly-reset codec context
                    // has no reference frame to decode the very next (non-keyframe) packet
                    // against, and (correctly, now that avcodec_send_packet()'s return is checked
                    // -- plans/plan_media.md MEDIA-39/128) throws "Cannot decode non-keyframe without
                    // valid keyframe" instead of silently producing a garbage frame. Found by
                    // external code review testing exactly this "re-select the same track"
                    // scenario, plans/plan_media.md MEDIA-131. See SetAudioStream() above for why this
                    // now returns bool (plans/plan_media.md MEDIA-154) and why that bool must be
                    // OpenVideoStreamByIndex()'s own real result, not an assumed `true`
                    // (plans/plan_media.md MEDIA-158, found by external code review).
                    if (i == videoStream_) return false;
                    return OpenVideoStreamByIndex(i);
                }
                ++found;
            }
        }
        return false; // trackIndex out of range -- also a true no-op.
    }

    // ---------------------------------------------------------------------------
    // 8-bit planar YUV → RGBA  (BT.601 full-range), any chroma subsampling
    // ---------------------------------------------------------------------------
    void VideoDecoder::yuv_planar8_to_rgba(
        const uint8_t* y, int yStride,
        const uint8_t* u, int uStride,
        const uint8_t* v, int vStride,
        uint8_t* rgba, int w, int h,
        int hChromaShift, int vChromaShift)
    {
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* uRow = u + (row >> vChromaShift) * uStride;
            const uint8_t* vRow = v + (row >> vChromaShift) * vStride;
            for (int col = 0; col < w; ++col)
            {
                int yv = y[row * yStride + col];
                int uv = uRow[col >> hChromaShift] - 128;
                int vv = vRow[col >> hChromaShift] - 128;

                int r = yv + ((359 * vv) >> 8);
                int g = yv - ((88 * uv + 183 * vv) >> 8);
                int b = yv + ((454 * uv) >> 8);

                uint8_t* px = rgba + (row * w + col) * 4;
                px[0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                px[1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                px[2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                px[3] = 255;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // 10-/12-bit (16-bit-packed LE) planar YUV → RGBA, any chroma subsampling.
    // Downshifts each sample to its 8-bit equivalent before reusing the same integer
    // YUV math as the 8-bit path -- a simple, correct (if not full-precision-HDR)
    // approach; see plans/plan_media.md MEDIA-36.
    // ---------------------------------------------------------------------------
    void VideoDecoder::yuv_planar16_to_rgba(
        const uint8_t* y, int yStride,
        const uint8_t* u, int uStride,
        const uint8_t* v, int vStride,
        uint8_t* rgba, int w, int h,
        int hChromaShift, int vChromaShift, int bitDepth)
    {
        const int downshift = bitDepth - 8;
        for (int row = 0; row < h; ++row)
        {
            const auto* yRow = reinterpret_cast<const uint16_t*>(y + row * yStride);
            const auto* uRow = reinterpret_cast<const uint16_t*>(u + (row >> vChromaShift) * uStride);
            const auto* vRow = reinterpret_cast<const uint16_t*>(v + (row >> vChromaShift) * vStride);
            for (int col = 0; col < w; ++col)
            {
                int yv = yRow[col] >> downshift;
                int uv = (uRow[col >> hChromaShift] >> downshift) - 128;
                int vv = (vRow[col >> hChromaShift] >> downshift) - 128;

                int r = yv + ((359 * vv) >> 8);
                int g = yv - ((88 * uv + 183 * vv) >> 8);
                int b = yv + ((454 * uv) >> 8);

                uint8_t* px = rgba + (row * w + col) * 4;
                px[0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                px[1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                px[2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                px[3] = 255;
            }
        }
    }

    void VideoDecoder::generic_to_rgba(AVFrame* src, std::vector<uint8_t>& out)
    {
        // Slow fallback: copy pixel-by-pixel using av_read_image_line2.
        // Handles most packed formats (e.g. RGB24, BGR24, RGBA, etc.).
        const int stride = src->width * 4;
        out.resize(static_cast<std::size_t>(src->width) * src->height * 4, 0);

        // For packed RGB/RGBA formats just memcpy lines if possible
        if (src->format == AV_PIX_FMT_RGBA)
        {
            for (int row = 0; row < src->height; ++row)
                std::memcpy(out.data() + row * stride,
                            src->data[0] + row * src->linesize[0], stride);
            return;
        }
        if (src->format == AV_PIX_FMT_RGB24)
        {
            for (int row = 0; row < src->height; ++row)
            {
                const uint8_t* src_row = src->data[0] + row * src->linesize[0];
                uint8_t*       dst_row = out.data()   + row * stride;
                for (int col = 0; col < src->width; ++col)
                {
                    dst_row[col * 4 + 0] = src_row[col * 3 + 0];
                    dst_row[col * 4 + 1] = src_row[col * 3 + 1];
                    dst_row[col * 4 + 2] = src_row[col * 3 + 2];
                    dst_row[col * 4 + 3] = 255;
                }
            }
            return;
        }
        // NV12 (Y plane + interleaved UV)
        if (src->format == AV_PIX_FMT_NV12)
        {
            for (int row = 0; row < src->height; ++row)
            {
                for (int col = 0; col < src->width; ++col)
                {
                    int yv = src->data[0][row * src->linesize[0] + col];
                    int uv = src->data[1][(row >> 1) * src->linesize[1] + (col & ~1)]     - 128;
                    int vv = src->data[1][(row >> 1) * src->linesize[1] + (col & ~1) + 1] - 128;

                    int r = yv + ((359 * vv) >> 8);
                    int g = yv - ((88 * uv + 183 * vv) >> 8);
                    int b = yv + ((454 * uv) >> 8);

                    uint8_t* px = out.data() + (row * src->width + col) * 4;
                    px[0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                    px[1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                    px[2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                    px[3] = 255;
                }
            }
            return;
        }
        // Unsupported format: fill magenta so it's obvious
        std::fill(out.begin(), out.end(), 0);
        for (std::size_t i = 0; i < out.size(); i += 4)
        {
            out[i + 0] = 255; out[i + 2] = 255; out[i + 3] = 255;
        }
    }

    void VideoDecoder::ConvertFrameToRGBA(std::vector<uint8_t>& out)
    {
        // hChromaShift/vChromaShift: 0 = full resolution, 1 = halved (plans/plan_media.md MEDIA-35).
        struct PlanarFormat { int hShift; int vShift; int bitDepth; };
        auto planarFormat = [](int fmt) -> const PlanarFormat*
        {
            static const PlanarFormat p420_8  {1, 1, 8};
            static const PlanarFormat p422_8  {1, 0, 8};
            static const PlanarFormat p444_8  {0, 0, 8};
            static const PlanarFormat p420_10 {1, 1, 10};
            static const PlanarFormat p422_10 {1, 0, 10};
            static const PlanarFormat p444_10 {0, 0, 10};
            static const PlanarFormat p420_12 {1, 1, 12};
            static const PlanarFormat p422_12 {1, 0, 12};
            static const PlanarFormat p444_12 {0, 0, 12};
            switch (fmt)
            {
                case AV_PIX_FMT_YUV420P:    case AV_PIX_FMT_YUVJ420P:    return &p420_8;
                case AV_PIX_FMT_YUV422P:    case AV_PIX_FMT_YUVJ422P:    return &p422_8;
                case AV_PIX_FMT_YUV444P:    case AV_PIX_FMT_YUVJ444P:    return &p444_8;
                case AV_PIX_FMT_YUV420P10LE: return &p420_10;
                case AV_PIX_FMT_YUV422P10LE: return &p422_10;
                case AV_PIX_FMT_YUV444P10LE: return &p444_10;
                case AV_PIX_FMT_YUV420P12LE: return &p420_12;
                case AV_PIX_FMT_YUV422P12LE: return &p422_12;
                case AV_PIX_FMT_YUV444P12LE: return &p444_12;
                default: return nullptr;
            }
        };

        if (const PlanarFormat* pf = planarFormat(frame_->format))
        {
            out.resize(static_cast<std::size_t>(width_) * height_ * 4);
            if (pf->bitDepth == 8)
            {
                yuv_planar8_to_rgba(
                    frame_->data[0], frame_->linesize[0],
                    frame_->data[1], frame_->linesize[1],
                    frame_->data[2], frame_->linesize[2],
                    out.data(), width_, height_, pf->hShift, pf->vShift);
            }
            else
            {
                yuv_planar16_to_rgba(
                    frame_->data[0], frame_->linesize[0],
                    frame_->data[1], frame_->linesize[1],
                    frame_->data[2], frame_->linesize[2],
                    out.data(), width_, height_, pf->hShift, pf->vShift, pf->bitDepth);
            }
        }
        else
        {
            generic_to_rgba(frame_, out);
        }
    }

    bool VideoDecoder::NextFrame(std::vector<uint8_t>& rgbaOut, double& ptsOut)
    {
        if (!fmtCtx_ || !videoCtx_) return false;

        // havePendingVideoPacket_ is a class member (see VideoDecoder.hpp) -- a video packet
        // retained here after send_packet() returns EAGAIN must survive not just this call's loop
        // but potentially several NextFrame() calls: the very next receive_frame() below almost
        // always yields a buffered frame immediately, returning to the caller before the pending
        // packet is resent (found by external code review, plans/plan_media.md MEDIA-146; supersedes the
        // MEDIA-128 fix, which used a function-local flag that lost this state across calls).
        while (true)
        {
            // Try to receive a decoded frame from codec
            int ret = avcodec_receive_frame(videoCtx_, frame_);
            if (ret == 0)
            {
                ptsOut = (frame_->best_effort_timestamp != AV_NOPTS_VALUE)
                         ? frame_->best_effort_timestamp * videoTimeBase_
                         : 0.0;
                ConvertFrameToRGBA(rgbaOut);
                return true;
            }
            if (ret == AVERROR_EOF)
                return false; // clean end of stream
            if (ret != AVERROR(EAGAIN))
            {
                // A genuine decode error (corrupt/truncated frame data reaching the codec, an
                // unsupported bitstream feature, etc.) -- previously merged with the EOF case
                // above, silently treating a decode failure as "video finished normally"
                // (plans/plan_media.md MEDIA-39/40).
                char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errBuf, sizeof(errBuf));
                throw std::runtime_error(
                    std::string("VideoDecoder: video decode error: ") + errBuf);
            }

            if (havePendingVideoPacket_)
            {
                // receive_frame (just above) has now had a chance to drain the codec's internal
                // buffer -- retry sending the packet that got EAGAIN before reading anything new.
                int sendRet = avcodec_send_packet(videoCtx_, pkt_);
                if (sendRet == 0)
                {
                    av_packet_unref(pkt_);
                    havePendingVideoPacket_ = false;
                }
                else if (sendRet != AVERROR(EAGAIN))
                {
                    av_packet_unref(pkt_);
                    havePendingVideoPacket_ = false;
                    char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                    av_strerror(sendRet, errBuf, sizeof(errBuf));
                    throw std::runtime_error(
                        std::string("VideoDecoder: failed to send packet to decoder: ") + errBuf);
                }
                // else: still EAGAIN -- loop back to receive_frame again without reading a new packet.
                continue;
            }

            // Read packets until we find a video packet to send
            while (true)
            {
                ret = av_read_frame(fmtCtx_, pkt_);
                if (ret < 0)
                {
                    if (ret != AVERROR_EOF)
                    {
                        // A genuine I/O/demux error, not a clean end-of-stream -- surface it
                        // distinctly rather than silently treating a corrupt/truncated file as
                        // "video finished normally" (plans/plan_media.md MEDIA-40).
                        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                        av_strerror(ret, errBuf, sizeof(errBuf));
                        throw std::runtime_error(
                            std::string("VideoDecoder: I/O error while reading frame: ") + errBuf);
                    }
                    // Clean EOF — flush both codecs. A flush's own return is checked too (found
                    // by external code review, plans/plan_media.md MEDIA-128/MEDIA-39's "at each site"):
                    // in practice this can only fail if the codec is already closed/flushed, but
                    // silently ignoring that would let a later avcodec_receive_frame() call
                    // proceed against a codec state this code didn't actually confirm was ready.
                    int flushRet = avcodec_send_packet(videoCtx_, nullptr);
                    if (flushRet < 0 && flushRet != AVERROR_EOF)
                    {
                        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                        av_strerror(flushRet, errBuf, sizeof(errBuf));
                        throw std::runtime_error(
                            std::string("VideoDecoder: failed to flush decoder at EOF: ") + errBuf);
                    }
                    // The audio codec was never sent a flush packet at all -- any audio samples
                    // still buffered inside its own internal decode state (not yet emitted via
                    // avcodec_receive_frame) were silently lost at EOF (found by external code
                    // review, plans/plan_media.md MEDIA-130). ProcessAudioPacket(nullptr) is FFmpeg's
                    // own documented flush contract (avcodec_send_packet accepts a null packet to
                    // mean "no more input, drain what you have") -- reuses the exact same,
                    // already-hardened decode/resample/error-propagation logic as every other
                    // audio packet.
                    if (audioCtx_)
                    {
                        ProcessAudioPacket(nullptr);
                    }
                    break;
                }
                if (pkt_->stream_index == audioStream_ && audioCtx_)
                {
                    ProcessAudioPacket(pkt_);
                    av_packet_unref(pkt_);
                    continue;
                }
                if (pkt_->stream_index == videoStream_)
                {
                    int sendRet = avcodec_send_packet(videoCtx_, pkt_);
                    if (sendRet == 0)
                    {
                        av_packet_unref(pkt_);
                    }
                    else if (sendRet == AVERROR(EAGAIN))
                    {
                        // Keep pkt_ alive (do NOT unref) -- retried at the top of the outer loop,
                        // right after the receive_frame call that should relieve the backpressure
                        // this EAGAIN is reporting, instead of silently dropping this video frame.
                        havePendingVideoPacket_ = true;
                    }
                    else
                    {
                        av_packet_unref(pkt_);
                        // A malformed/corrupt packet the codec outright rejects -- surface it
                        // distinctly rather than silently discarding it and looping as if nothing
                        // happened (plans/plan_media.md MEDIA-39/40).
                        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                        av_strerror(sendRet, errBuf, sizeof(errBuf));
                        throw std::runtime_error(
                            std::string("VideoDecoder: failed to send packet to decoder: ") + errBuf);
                    }
                    break;
                }
                av_packet_unref(pkt_);
            }
        }
    }

    void VideoDecoder::ProcessAudioPacket(AVPacket* pkt)
    {
        if (!audioCtx_ || !swrCtx_) return;

        AVFrame* aFrame = av_frame_alloc();
        if (!aFrame)
        {
            throw std::runtime_error("VideoDecoder: av_frame_alloc() failed for audio frame.");
        }

        // plans/plan_media.md MEDIA-39/128 (found incomplete by external code review): every FFmpeg
        // call in this function is now checked and a genuine error propagated as a thrown
        // exception, matching the same "surface it, don't silently produce garbage/skip" standard
        // NextFrame()'s own video-decode path already applies -- a corrupted *audio* packet is no
        // less a real decode failure than a corrupted video one, and MEDIA-39's own task text
        // explicitly calls out this function's swr_convert/avcodec_send_packet sites by name.
        //
        // avcodec_send_packet() returning EAGAIN means "the codec's internal output queue is full
        // -- drain via receive_frame, then resend this exact packet" (FFmpeg's documented
        // contract). This used to be tolerated without throwing but never actually retried -- the
        // caller (NextFrame()'s packet-reading loop) unconditionally av_packet_unref()s pkt_
        // regardless of whether it was ever accepted, silently discarding real audio data on the
        // rare occasions this path is taken. Retried here, mirroring the same fix NextFrame()
        // already applies on the video side via havePendingVideoPacket_ (MEDIA-146) -- found by
        // external code review, plans/plan_media.md MEDIA-164.
        bool needsResend = true;
        while (needsResend)
        {
            int sendRet = avcodec_send_packet(audioCtx_, pkt);
            needsResend = (sendRet == AVERROR(EAGAIN));
            if (sendRet < 0 && !needsResend)
            {
                char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(sendRet, errBuf, sizeof(errBuf));
                av_frame_free(&aFrame);
                throw std::runtime_error(
                    std::string("VideoDecoder: failed to send audio packet to decoder: ") + errBuf);
            }

            while (true)
            {
                int recvRet = avcodec_receive_frame(audioCtx_, aFrame);
                if (recvRet != 0)
                {
                    if (recvRet != AVERROR(EAGAIN) && recvRet != AVERROR_EOF)
                    {
                        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                        av_strerror(recvRet, errBuf, sizeof(errBuf));
                        av_frame_free(&aFrame);
                        throw std::runtime_error(
                            std::string("VideoDecoder: audio decode error: ") + errBuf);
                    }
                    break; // EAGAIN (no more frames from this packet yet) or EOF -- both expected, not errors
                }

                int numSamples = aFrame->nb_samples;
                // Convert to float
                std::vector<float> buf(static_cast<std::size_t>(numSamples) * channels_);
                uint8_t* outPtr = reinterpret_cast<uint8_t*>(buf.data());
                int converted = swr_convert(swrCtx_, &outPtr, numSamples,
                            const_cast<const uint8_t**>(aFrame->data), numSamples);
                // plans/plan_media.md MEDIA-39: swr_convert's return is the ACTUAL number of samples
                // produced, which can be less than numSamples (trim to what was actually produced) or
                // negative on a genuine resample error, which -- like every other site in this
                // function -- is now surfaced rather than silently swallowed.
                if (converted < 0)
                {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                    av_strerror(converted, errBuf, sizeof(errBuf));
                    av_frame_free(&aFrame);
                    throw std::runtime_error(
                        std::string("VideoDecoder: audio resample error: ") + errBuf);
                }
                if (converted > 0)
                {
                    buf.resize(static_cast<std::size_t>(converted) * channels_);
                    pendingAudio_.insert(pendingAudio_.end(), buf.begin(), buf.end());
                }
            }
        }

        if (!pkt)
        {
            // pkt == nullptr is the EOF flush call -- the audio *codec's* own buffered frames were
            // just drained above via avcodec_receive_frame(), but SwrContext keeps a separate
            // internal buffer of its own (format/rate-conversion delay). FFmpeg's documented flush
            // contract for that is a null source pointer/zero count; skipping it silently drops
            // whatever the resampler was still holding at end-of-stream (found by external code
            // review, plans/plan_media.md MEDIA-147).
            int delaySamples = static_cast<int>(swr_get_delay(swrCtx_, audioCtx_->sample_rate));
            if (delaySamples > 0)
            {
                std::vector<float> buf(static_cast<std::size_t>(delaySamples) * channels_);
                uint8_t* outPtr = reinterpret_cast<uint8_t*>(buf.data());
                int converted = swr_convert(swrCtx_, &outPtr, delaySamples, nullptr, 0);
                if (converted < 0)
                {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                    av_strerror(converted, errBuf, sizeof(errBuf));
                    av_frame_free(&aFrame);
                    throw std::runtime_error(
                        std::string("VideoDecoder: audio resampler flush error: ") + errBuf);
                }
                if (converted > 0)
                {
                    buf.resize(static_cast<std::size_t>(converted) * channels_);
                    pendingAudio_.insert(pendingAudio_.end(), buf.begin(), buf.end());
                }
            }
        }

        av_frame_free(&aFrame);
    }

    void VideoDecoder::DrainAudio(std::vector<float>& samplesOut)
    {
        samplesOut.insert(samplesOut.end(), pendingAudio_.begin(), pendingAudio_.end());
        pendingAudio_.clear();
    }
}
