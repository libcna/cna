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
        // Requires a working resampler too, not just an opened audio codec -- ProcessAudioPacket()
        // silently discards every decoded audio frame without a working swrCtx_ (it early-returns),
        // so a caller checking only audioCtx_ would believe a video has playable audio that in fact
        // never produces a single sample (found by external code review, plan_media.md MEDIA-160).
        // Open()/OpenAudioStreamByIndex() both build+verify the resampler transactionally before
        // ever committing to using a track at all (MEDIA-162), so audioCtx_ and swrCtx_ can no
        // longer diverge through either of those paths -- the one remaining way they can is a
        // resampler-recreation failure inside SeekToStart() (MEDIA-167), which frees the old
        // resampler and rebuilds a fresh one on every seek; if that rebuild fails, audioCtx_ stays
        // valid (the codec itself is untouched) while swrCtx_ is left null, correctly reported here
        // as "no audio" from that point on rather than silently believed still available (found by
        // external code review, plan_media.md MEDIA-169 -- this comment previously described an
        // already-superseded SetupResampler()-during-initial-setup scenario Phase 13's MEDIA-162 fix
        // had already closed off, instead of the real, current path).
        [[nodiscard]] bool HasAudio()        const { return audioCtx_ != nullptr && swrCtx_ != nullptr; }
        [[nodiscard]] int GetSampleRate()    const { return sampleRate_; }
        [[nodiscard]] int GetChannels()      const { return channels_; }

        /// Seeks to the beginning of the stream.
        void SeekToStart();

        /// Switches the active audio stream to the trackIndex-th audio stream (0-based). Returns
        /// true if a different stream is now active (a genuine switch happened, decode state was
        /// discarded), false if trackIndex was already active or out of range (a true no-op) --
        /// callers that recreate downstream resources (audio streams, textures) after a switch
        /// need to know whether anything actually changed, not just that the call didn't throw
        /// (plan_media.md MEDIA-154, found by external code review).
        bool SetAudioStream(int trackIndex);

        /// Switches the active video stream to the trackIndex-th video stream (0-based). Same
        /// return-value contract as SetAudioStream() above.
        bool SetVideoStream(int trackIndex);

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

        // Allocates + configures a codec context from stream parameters; returns nullptr (freeing
        // any partial allocation) on either allocation or avcodec_parameters_to_context failure,
        // instead of risking a null dereference downstream (plan_media.md MEDIA-38).
        static AVCodecContext* AllocAndConfigureCodecContext(
            const AVCodec* codec, const AVCodecParameters* params);

        // Builds a new resampler for the given codec context, resampling to packed float32.
        // Returns nullptr (freeing any partial allocation) if either allocation or swr_init fails,
        // instead of leaving a half-configured context a later swr_convert call would crash on
        // (plan_media.md MEDIA-38). Deliberately does NOT assign to swrCtx_ itself -- callers
        // build the new resampler and confirm it works BEFORE committing it (and destroying
        // whatever it replaces), so a resampler-setup failure can be reported as a genuine open
        // failure rather than silently leaving audio half-broken after the old, working state has
        // already been discarded (plan_media.md MEDIA-162, found by external code review).
        static SwrContext* CreateResampler(AVCodecContext* ctx);

        // BT.601 8-bit planar YUV -> RGBA (no libswscale needed). hChromaShift/vChromaShift
        // encode the chroma subsampling: 0 = full resolution on that axis, 1 = halved (4:2:0 is
        // shift 1/1, 4:2:2 is shift 1/0, 4:4:4 is shift 0/0) -- plan_media.md MEDIA-35.
        static void yuv_planar8_to_rgba(
            const uint8_t* y, int yStride,
            const uint8_t* u, int uStride,
            const uint8_t* v, int vStride,
            uint8_t* rgba, int w, int h,
            int hChromaShift, int vChromaShift);

        // BT.601 10-/12-bit (16-bit-packed, little-endian) planar YUV -> RGBA. Downshifts each
        // sample to its 8-bit equivalent (bitDepth-8 bits) before reusing the same integer YUV
        // math as the 8-bit path -- plan_media.md MEDIA-36.
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
        // external code review, plan_media.md MEDIA-146).
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
