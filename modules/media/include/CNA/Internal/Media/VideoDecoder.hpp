// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Forward-declare FFmpeg types so this header stays clean of extern "C"
struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct AVCodecParameters;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace CNA::Internal::Media
{
    /**
     * @brief Reports whether this build contains the optional FFmpeg video backend.
     *
     * @return true when video decoding is available; otherwise false.
     */
    [[nodiscard]] bool IsVideoDecoderAvailable() noexcept;

    /**
     * @brief Verifies that the optional video decoder is available.
     *
     * @throws System::NotSupportedException If CNA was built without a video backend.
     */
    void RequireVideoDecoderAvailable();

    /**
     * @brief Internal video/audio decoder used by VideoPlayer.
     *
     * The enabled implementation owns FFmpeg state for one open video file; the disabled
     * implementation preserves the same link-complete type and rejects decoding deterministically.
     * This type is not thread-safe; all methods must be called from the same thread.
     */
    class VideoDecoder
    {
    public:
        /** @brief Constructs a closed decoder. */
        VideoDecoder();

        /** @brief Closes the decoder and releases all decoder resources. */
        ~VideoDecoder();

        /** @brief VideoDecoder instances cannot be copied. */
        VideoDecoder(const VideoDecoder&)            = delete;

        /** @brief VideoDecoder instances cannot be copy-assigned. */
        VideoDecoder& operator=(const VideoDecoder&) = delete;

        /**
         * @brief Opens a file and reads its stream metadata.
         *
         * @param path Path to the video file.
         * @return true on success; false if the file cannot be opened or has no usable video.
         * @throws System::NotSupportedException If CNA was built without a video backend.
         */
        bool Open(const std::string& path);

        /** @brief Closes all streams and releases decoder resources. */
        void Close();

        /**
         * @brief Returns whether a file is open.
         *
         * @return true when a file is open.
         */
        [[nodiscard]] bool IsOpen()          const { return fmtCtx_ != nullptr; }

        /**
         * @brief Returns the decoded frame width.
         *
         * @return Width in pixels.
         */
        [[nodiscard]] int  GetWidth()        const { return width_; }

        /**
         * @brief Returns the decoded frame height.
         *
         * @return Height in pixels.
         */
        [[nodiscard]] int  GetHeight()       const { return height_; }

        /**
         * @brief Returns the decoded frame rate.
         *
         * @return Frames per second.
         */
        [[nodiscard]] float GetFPS()         const { return fps_; }

        /**
         * @brief Returns the container duration.
         *
         * @return Duration in seconds.
         */
        [[nodiscard]] double GetDuration()   const { return durationSec_; }
        // Requires a working resampler too, not just an opened audio codec -- ProcessAudioPacket()
        // silently discards every decoded audio frame without a working swrCtx_ (it early-returns),
        // so a caller checking only audioCtx_ would believe a video has playable audio that in fact
        // never produces a single sample (found by external code review, plans/plan_media.md MEDIA-160).
        // Open()/OpenAudioStreamByIndex() both build+verify the resampler transactionally before
        // ever committing to using a track at all (MEDIA-162), so audioCtx_ and swrCtx_ can no
        // longer diverge through either of those paths -- the one remaining way they can is a
        // resampler-recreation failure inside SeekToStart() (MEDIA-167), which frees the old
        // resampler and rebuilds a fresh one on every seek; if that rebuild fails, audioCtx_ stays
        // valid (the codec itself is untouched) while swrCtx_ is left null, correctly reported here
        // as "no audio" from that point on rather than silently believed still available (found by
        // external code review, plans/plan_media.md MEDIA-169 -- this comment previously described an
        // already-superseded SetupResampler()-during-initial-setup scenario Phase 13's MEDIA-162 fix
        // had already closed off, instead of the real, current path).
        /**
         * @brief Returns whether decoded audio is usable.
         *
         * @return true when audio is usable.
         */
        [[nodiscard]] bool HasAudio()        const { return audioCtx_ != nullptr && swrCtx_ != nullptr; }

        /**
         * @brief Returns the decoded audio sample rate.
         *
         * @return Samples per second.
         */
        [[nodiscard]] int GetSampleRate()    const { return sampleRate_; }

        /**
         * @brief Returns the decoded audio channel count.
         *
         * @return Number of channels.
         */
        [[nodiscard]] int GetChannels()      const { return channels_; }

        /**
         * @brief Seeks to the beginning of the stream.
         *
         * @throws System::NotSupportedException If CNA was built without a video backend.
         */
        void SeekToStart();

        /**
         * @brief Switches the active zero-based audio stream.
         *
         * @param trackIndex Zero-based index among audio streams.
         * @return true if the active stream changed; false for the current or an invalid index.
         * @throws System::NotSupportedException If CNA was built without a video backend.
         */
        bool SetAudioStream(int trackIndex);

        /**
         * @brief Switches the active zero-based video stream.
         *
         * @param trackIndex Zero-based index among video streams.
         * @return true if the active stream changed; false for the current or an invalid index.
         * @throws System::NotSupportedException If CNA was built without a video backend.
         */
        bool SetVideoStream(int trackIndex);

        /**
         * @brief Decodes the next video frame into an RGBA output buffer.
         *
         * @param rgbaOut Output buffer, resized to width times height times four bytes.
         * @param ptsOut Receives the presentation timestamp in seconds.
         * @return false on end of stream or a decode error.
         * @throws System::NotSupportedException If CNA was built without a video backend.
         */
        bool NextFrame(std::vector<uint8_t>& rgbaOut, double& ptsOut);

        /**
         * @brief Drains decoded audio into an interleaved float32 buffer.
         *
         * @param samplesOut Buffer to append decoded samples to.
         * @throws System::NotSupportedException If CNA was built without a video backend.
         */
        void DrainAudio(std::vector<float>& samplesOut);

    private:
        bool SendPackets();          // read & send packets to codec
        bool ReceiveVideoFrame();    // receive one decoded video frame
        void ConvertFrameToRGBA(std::vector<uint8_t>& out);
        void ProcessAudioPacket(AVPacket* pkt);
        bool OpenAudioStreamByIndex(int streamIdx);
        bool OpenVideoStreamByIndex(int streamIdx);

        // Allocates + configures a codec context from stream parameters; returns nullptr (freeing
        // any partial allocation) on either allocation or avcodec_parameters_to_context failure,
        // instead of risking a null dereference downstream (plans/plan_media.md MEDIA-38).
        static AVCodecContext* AllocAndConfigureCodecContext(
            const AVCodec* codec, const AVCodecParameters* params);

        // Builds a new resampler for the given codec context, resampling to packed float32.
        // Returns nullptr (freeing any partial allocation) if either allocation or swr_init fails,
        // instead of leaving a half-configured context a later swr_convert call would crash on
        // (plans/plan_media.md MEDIA-38). Deliberately does NOT assign to swrCtx_ itself -- callers
        // build the new resampler and confirm it works BEFORE committing it (and destroying
        // whatever it replaces), so a resampler-setup failure can be reported as a genuine open
        // failure rather than silently leaving audio half-broken after the old, working state has
        // already been discarded (plans/plan_media.md MEDIA-162, found by external code review).
        static SwrContext* CreateResampler(AVCodecContext* ctx);

        // BT.601 8-bit planar YUV -> RGBA (no libswscale needed). hChromaShift/vChromaShift
        // encode the chroma subsampling: 0 = full resolution on that axis, 1 = halved (4:2:0 is
        // shift 1/1, 4:2:2 is shift 1/0, 4:4:4 is shift 0/0) -- plans/plan_media.md MEDIA-35.
        static void yuv_planar8_to_rgba(
            const uint8_t* y, int yStride,
            const uint8_t* u, int uStride,
            const uint8_t* v, int vStride,
            uint8_t* rgba, int w, int h,
            int hChromaShift, int vChromaShift);

        // BT.601 10-/12-bit (16-bit-packed, little-endian) planar YUV -> RGBA. Downshifts each
        // sample to its 8-bit equivalent (bitDepth-8 bits) before reusing the same integer YUV
        // math as the 8-bit path -- plans/plan_media.md MEDIA-36.
        static void yuv_planar16_to_rgba(
            const uint8_t* y, int yStride,
            const uint8_t* u, int uStride,
            const uint8_t* v, int vStride,
            uint8_t* rgba, int w, int h,
            int hChromaShift, int vChromaShift, int bitDepth);

        // Generic planar/packed YUV → RGBA fallback via libavutil
        void generic_to_rgba(AVFrame* src, std::vector<uint8_t>& out);

        AVFormatContext* fmtCtx_   = nullptr;
        AVCodecContext*  videoCtx_ = nullptr;
        AVCodecContext*  audioCtx_ = nullptr;
        AVFrame*         frame_    = nullptr;
        AVPacket*        pkt_      = nullptr;
        SwrContext*      swrCtx_   = nullptr;

        // Must survive across NextFrame() calls, not just within one: a video packet retained
        // after send_packet() returns EAGAIN is almost always followed by receive_frame()
        // immediately yielding a buffered frame on the very next outer-loop iteration, which
        // returns to the caller before the pending packet is ever resent. A function-local flag
        // loses that state on the next call, silently dropping the retained frame (found by
        // external code review, plans/plan_media.md MEDIA-146).
        bool havePendingVideoPacket_ = false;

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
