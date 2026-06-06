// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Media/VideoDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
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
        videoCtx_ = avcodec_alloc_context3(vCodec);
        avcodec_parameters_to_context(videoCtx_, vs->codecpar);
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
                audioCtx_ = avcodec_alloc_context3(aCodec);
                avcodec_parameters_to_context(audioCtx_, as->codecpar);
                if (avcodec_open2(audioCtx_, aCodec, nullptr) < 0)
                {
                    avcodec_free_context(&audioCtx_);
                    audioCtx_ = nullptr;
                    audioStream_ = -1;
                }
                else
                {
                    sampleRate_ = audioCtx_->sample_rate;
                    channels_   = audioCtx_->ch_layout.nb_channels;
                    audioTimeBase_ = av_q2d(as->time_base);

                    // Setup resampler: any format → packed float32
                    swr_alloc_set_opts2(
                        &swrCtx_,
                        &audioCtx_->ch_layout, AV_SAMPLE_FMT_FLT, sampleRate_,
                        &audioCtx_->ch_layout, audioCtx_->sample_fmt, sampleRate_,
                        0, nullptr);
                    if (swr_init(swrCtx_) < 0)
                    {
                        swr_free(&swrCtx_);
                        swrCtx_ = nullptr;
                    }
                }
            }
        }

        frame_ = av_frame_alloc();
        pkt_   = av_packet_alloc();

        return true;
    }

    void VideoDecoder::Close()
    {
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
    }

    void VideoDecoder::SeekToStart()
    {
        if (!fmtCtx_) return;
        av_seek_frame(fmtCtx_, -1, 0, AVSEEK_FLAG_BACKWARD);
        if (videoCtx_) avcodec_flush_buffers(videoCtx_);
        if (audioCtx_) avcodec_flush_buffers(audioCtx_);
    }

    // ---------------------------------------------------------------------------
    // YUV420P → RGBA  (BT.601 full-range)
    // ---------------------------------------------------------------------------
    void VideoDecoder::yuv420p_to_rgba(
        const uint8_t* y, int yStride,
        const uint8_t* u, int uStride,
        const uint8_t* v, int vStride,
        uint8_t* rgba, int w, int h)
    {
        for (int row = 0; row < h; ++row)
        {
            for (int col = 0; col < w; ++col)
            {
                int yv = y[row * yStride + col];
                int uv = u[(row >> 1) * uStride + (col >> 1)] - 128;
                int vv = v[(row >> 1) * vStride + (col >> 1)] - 128;

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
        if (frame_->format == AV_PIX_FMT_YUV420P ||
            frame_->format == AV_PIX_FMT_YUVJ420P)
        {
            out.resize(static_cast<std::size_t>(width_) * height_ * 4);
            yuv420p_to_rgba(
                frame_->data[0], frame_->linesize[0],
                frame_->data[1], frame_->linesize[1],
                frame_->data[2], frame_->linesize[2],
                out.data(), width_, height_);
        }
        else
        {
            generic_to_rgba(frame_, out);
        }
    }

    bool VideoDecoder::NextFrame(std::vector<uint8_t>& rgbaOut, double& ptsOut)
    {
        if (!fmtCtx_ || !videoCtx_) return false;

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
            if (ret != AVERROR(EAGAIN))
                return false; // EOF or error

            // Read packets until we find a video packet to send
            while (true)
            {
                ret = av_read_frame(fmtCtx_, pkt_);
                if (ret < 0)
                {
                    // EOF — flush codec
                    avcodec_send_packet(videoCtx_, nullptr);
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
                    avcodec_send_packet(videoCtx_, pkt_);
                    av_packet_unref(pkt_);
                    break;
                }
                av_packet_unref(pkt_);
            }
        }
    }

    void VideoDecoder::ProcessAudioPacket(AVPacket* pkt)
    {
        if (!audioCtx_ || !swrCtx_) return;

        if (avcodec_send_packet(audioCtx_, pkt) < 0) return;

        AVFrame* aFrame = av_frame_alloc();
        while (avcodec_receive_frame(audioCtx_, aFrame) == 0)
        {
            int numSamples = aFrame->nb_samples;
            // Convert to float
            std::vector<float> buf(static_cast<std::size_t>(numSamples) * channels_);
            uint8_t* outPtr = reinterpret_cast<uint8_t*>(buf.data());
            swr_convert(swrCtx_, &outPtr, numSamples,
                        const_cast<const uint8_t**>(aFrame->data), numSamples);
            // Store in pending audio buffer
            pendingAudio_.insert(pendingAudio_.end(), buf.begin(), buf.end());
        }
        av_frame_free(&aFrame);
    }

    void VideoDecoder::DrainAudio(std::vector<float>& samplesOut)
    {
        samplesOut.insert(samplesOut.end(), pendingAudio_.begin(), pendingAudio_.end());
        pendingAudio_.clear();
    }
}
