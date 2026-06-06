// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Forward-declare FFmpeg types so this header stays clean of extern "C"
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace CNA::Internal::Media
{
    /// Internal FFmpeg-based video/audio decoder used by VideoPlayer.
    ///
    /// Owns the FFmpeg state for one open video file. Not thread-safe — all
    /// methods must be called from the same thread.
    class VideoDecoder
    {
    public:
        VideoDecoder();
        ~VideoDecoder();

        VideoDecoder(const VideoDecoder&)            = delete;
        VideoDecoder& operator=(const VideoDecoder&) = delete;

        /// Opens the file and reads stream metadata.
        /// @return true on success, false if the file cannot be opened or
        ///         contains no usable video stream.
        bool Open(const std::string& path);

        /// Closes all streams and frees FFmpeg resources.
        void Close();

        [[nodiscard]] bool IsOpen()          const { return fmtCtx_ != nullptr; }
        [[nodiscard]] int  GetWidth()        const { return width_; }
        [[nodiscard]] int  GetHeight()       const { return height_; }
        [[nodiscard]] float GetFPS()         const { return fps_; }
        [[nodiscard]] double GetDuration()   const { return durationSec_; }
        [[nodiscard]] bool HasAudio()        const { return audioCtx_ != nullptr; }
        [[nodiscard]] int GetSampleRate()    const { return sampleRate_; }
        [[nodiscard]] int GetChannels()      const { return channels_; }

        /// Seeks to the beginning of the stream.
        void SeekToStart();

        /// Switches the active audio stream to the trackIndex-th audio stream (0-based).
        void SetAudioStream(int trackIndex);

        /// Switches the active video stream to the trackIndex-th video stream (0-based).
        void SetVideoStream(int trackIndex);

        /// Decodes the next video frame into RGBA output buffer.
        /// @param rgbaOut  output buffer; resized to width*height*4 bytes.
        /// @param ptsOut   presentation timestamp in seconds.
        /// @return false on EOF or decode error.
        bool NextFrame(std::vector<uint8_t>& rgbaOut, double& ptsOut);

        /// Drains decoded audio into a float32 interleaved buffer.
        /// Should be called alongside NextFrame to keep audio in sync.
        /// @param samplesOut  appended to (not replaced).
        void DrainAudio(std::vector<float>& samplesOut);

    private:
        bool SendPackets();          // read & send packets to codec
        bool ReceiveVideoFrame();    // receive one decoded video frame
        void ConvertFrameToRGBA(std::vector<uint8_t>& out);
        void ProcessAudioPacket(AVPacket* pkt);
        bool OpenAudioStreamByIndex(int streamIdx);
        bool OpenVideoStreamByIndex(int streamIdx);

        // BT.601 YUV420P → RGBA (no libswscale needed)
        static void yuv420p_to_rgba(
            const uint8_t* y, int yStride,
            const uint8_t* u, int uStride,
            const uint8_t* v, int vStride,
            uint8_t* rgba, int w, int h);

        // Generic planar/packed YUV → RGBA fallback via libavutil
        void generic_to_rgba(AVFrame* src, std::vector<uint8_t>& out);

        AVFormatContext* fmtCtx_   = nullptr;
        AVCodecContext*  videoCtx_ = nullptr;
        AVCodecContext*  audioCtx_ = nullptr;
        AVFrame*         frame_    = nullptr;
        AVPacket*        pkt_      = nullptr;
        SwrContext*      swrCtx_   = nullptr;

        int    videoStream_ = -1;
        int    audioStream_ = -1;
        int    width_       = 0;
        int    height_      = 0;
        float  fps_         = 0.0f;
        double durationSec_ = 0.0;
        int    sampleRate_  = 44100;
        int    channels_    = 2;

        double videoTimeBase_ = 0.0;
        double audioTimeBase_ = 0.0;

        std::vector<float> pendingAudio_;
    };
}
