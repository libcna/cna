// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Content::Pipeline
{
    /**
     * @brief How much of the source an XMA encode keeps
     *        (plans/plan_xnapipeline_parity.md `XNAPP-262`).
     *
     * The three levels XNA's `ConversionQuality` has, spelled here rather than taken from the
     * façade so the seam stays free of the XNA type -- exactly as @ref EffectSourceProfile is its
     * own enum rather than `GraphicsProfile`. A backend that has no notion of quality ignores it.
     */
    enum class XmaEncodeQuality
    {
        /** @brief Smallest output. */
        Low,
        /** @brief The middle setting. */
        Medium,
        /** @brief Closest to the source; XNA's own default. */
        Best,
    };

    /** @brief Returns the stable lowercase spelling of a quality (`"low"`, `"medium"`, `"best"`). */
    [[nodiscard]] const char* XmaEncodeQualityName(XmaEncodeQuality quality) noexcept;

    /** @brief Everything an encode depends on besides the backend itself. */
    struct XmaEncodeRequest
    {
        /** @brief Interleaved little-endian PCM sample data. */
        std::vector<std::uint8_t> pcm;

        /** @brief Channels in @ref pcm. */
        int channelCount = 1;

        /** @brief Samples per second. */
        int sampleRate = 44100;

        /** @brief Bits per sample in @ref pcm; 8 or 16. */
        int bitsPerSample = 16;

        /** @brief First sample of the loop region, or zero when there is none. */
        int loopStart = 0;

        /** @brief Length of the loop region in samples, or zero when there is none. */
        int loopLength = 0;

        /** @brief How much of the source to keep. */
        XmaEncodeQuality quality = XmaEncodeQuality::Best;
    };

    /**
     * @brief What an encode produced.
     *
     * `formatBlock` is the `XMA2WAVEFORMATEX` **as the encoder wrote it**, little-endian, in the
     * layout a `.xma` RIFF file's `fmt ` chunk carries. Converting it into the Xbox 360's own
     * big-endian order is the writer's job and not the backend's, because the same encoded payload
     * is the same payload whichever container carries it (see docs/xma-encoder-backend.md).
     */
    struct XmaEncodeResult
    {
        /** @brief The encoded XMA2 payload, empty when the encode failed. */
        std::vector<std::uint8_t> data;

        /** @brief The `XMA2WAVEFORMATEX` the encoder wrote, little-endian, `fmt ` chunk order. */
        std::vector<std::uint8_t> formatBlock;

        /** @brief Loop start in the units the encoder's own frames impose. */
        int loopStart = 0;

        /** @brief Loop length in those same units. */
        int loopLength = 0;

        /** @brief Everything the backend said, whether it succeeded or not. */
        std::vector<std::string> diagnostics;

        /** @brief Whether the encode succeeded and @ref data is usable. */
        bool succeeded = false;
    };

    /**
     * @brief Stable identity of one XMA encoder backend, for the build fingerprint.
     *
     * Two encoders, or two versions of one, must not be treated as interchangeable by an
     * incremental build: the same PCM legitimately encodes to different bytes. The processor folds
     * this into its own component version, which the manifest already fingerprints, so attaching or
     * changing an encoder rebuilds rather than reusing stale artifacts.
     */
    struct XmaEncoderIdentity
    {
        /** @brief Backend name, e.g. `"external"`. Empty when no encoder is available. */
        std::string backend;

        /** @brief Version the encoder reported about itself, or empty when it did not. */
        std::string version;

        /** @brief A single stable string combining the above, suitable for a fingerprint. */
        [[nodiscard]] std::string ToString() const;

        /** @brief Compares every field. */
        bool operator==(const XmaEncoderIdentity& other) const = default;
    };

    /**
     * @brief The exact sentence a build says when no XMA encoder is attached.
     *
     * A fixed phrase rather than a formatted one, because it is the thing the parity matrix, the
     * plan and the diagnostics all have to agree on: `.wav`'s Xbox 360 row reads it verbatim.
     */
    inline constexpr const char* XmaEncoderUnavailableSentence = "XMA ENCODER EXTERNALLY UNAVAILABLE";

    /**
     * @brief A build-time XMA encoder.
     *
     * Deliberately an interface with a process boundary behind it rather than a linked library,
     * for the same reason @ref EffectCompilerService is: the codec is Microsoft's own, it has no
     * public specification sufficient to implement a conforming encoder, and no encoder for it can
     * be vendored here. Keeping the seam at this interface means everything around it -- the
     * conversion, the loop and duration semantics, the container, the fingerprint, the refusal --
     * is implemented and tested with no encoder at all, and attaching one later changes no public
     * pipeline API.
     */
    class XmaEncoderService
    {
    public:
        /** @brief Enables destruction through the interface. */
        virtual ~XmaEncoderService() = default;

        /** @brief Returns the fingerprint identity of this backend. */
        [[nodiscard]] virtual XmaEncoderIdentity Identity() const = 0;

        /**
         * @brief Whether this backend can encode right now.
         *
         * False means no encoder is attached, or the one named was not found. A build then refuses
         * with @ref UnavailableReason rather than writing something that is not XMA.
         */
        [[nodiscard]] virtual bool Available() const = 0;

        /**
         * @brief A complete explanation of why @ref Available is false, or an empty string.
         *
         * Begins with @ref XmaEncoderUnavailableSentence whenever the reason is that no encoder is
         * attached, so a caller can recognize that case without matching prose.
         */
        [[nodiscard]] virtual std::string UnavailableReason() const = 0;

        /**
         * @brief Encodes one buffer of PCM.
         *
         * @param request What to encode and how.
         * @return The encoded payload, or a failed result carrying the backend's diagnostics.
         */
        [[nodiscard]] virtual XmaEncodeResult Encode(const XmaEncodeRequest& request) const = 0;
    };

    /**
     * @brief Creates the backend a build has when no encoder is attached.
     *
     * Never null, never available, and its @ref XmaEncoderService::UnavailableReason begins with
     * @ref XmaEncoderUnavailableSentence. This is the default everywhere.
     *
     * @return The unavailable backend.
     */
    [[nodiscard]] std::shared_ptr<const XmaEncoderService> MakeUnavailableXmaEncoder();

    /** @brief How to find and drive an external XMA encoder. */
    struct ExternalXmaEncoderOptions
    {
        /**
         * @brief Explicit path to the encoder, or empty to discover one.
         *
         * Discovery order, first hit wins: this field; the `CNA_XMA_ENCODER` environment variable;
         * the path baked in by CMake's `CNA_XMA_ENCODER_EXECUTABLE`. There is deliberately no
         * search of `PATH`: an encoder for this codec is something a user attaches on purpose, and
         * picking one up by name would make a build depend on what happens to be installed.
         */
        std::filesystem::path executable;

        /**
         * @brief A program to run the encoder *through*, or empty to run it directly.
         *
         * The encoders that exist are Windows binaries, and the ordinary way to run one on a Linux
         * build machine is `wine`. Discovery order matches @ref executable: this field, then
         * `CNA_XMA_ENCODER_LAUNCHER`, then CMake's `CNA_XMA_ENCODER_LAUNCHER`.
         */
        std::filesystem::path launcher;

        /**
         * @brief The argument template, or empty for `{input} {output}`.
         *
         * Each entry is one argument, with `{input}`, `{output}`, `{quality}`, `{loopStart}` and
         * `{loopLength}` substituted. A user whose encoder spells its command line differently
         * adapts here rather than in CNA; `CNA_XMA_ENCODER_ARGS` supplies it as a space-separated
         * list when this is empty.
         */
        std::vector<std::string> arguments;

        /** @brief Compares every field. */
        bool operator==(const ExternalXmaEncoderOptions& other) const = default;
    };

    /**
     * @brief Creates the external-process XMA encoder backend.
     *
     * The contract is the one every encoder for this codec already satisfies: it is handed a
     * canonical RIFF WAVE file and must write a RIFF file whose `fmt ` chunk is the
     * `XMA2WAVEFORMATEX` (format tag `0x0166`) and whose `data` chunk is the encoded payload --
     * which is exactly what a `.xma` file is. CNA reads that back and owns everything after it.
     *
     * Resolution happens once, when this is called. The returned service is then immutable and
     * reports @ref XmaEncoderService::Available accordingly, so a build with no encoder refuses at
     * the first Xbox sound effect with one complete explanation rather than once per asset.
     *
     * @param options Discovery overrides; every field may be empty.
     * @return A service that is never null, and is unavailable when no encoder was named.
     */
    [[nodiscard]] std::shared_ptr<const XmaEncoderService> MakeExternalXmaEncoder(
        const ExternalXmaEncoderOptions& options = {});

    /**
     * @brief Attaches the encoder this process's builds use.
     *
     * `AudioContent::ConvertFormat(ConversionFormat::Xma, ...)` is XNA's own API for asking for
     * XMA, and it takes no service: a processor calls it on the content it was handed. So the
     * backend is attached to the process rather than threaded through a signature that XNA fixed.
     * The contract is *set once, before the build, and not again during it* -- a build tool calls
     * this while parsing its command line, which is before any source is discovered. Passing null
     * restores @ref MakeUnavailableXmaEncoder.
     *
     * @param encoder The backend to use, or null for none.
     */
    void SetBuildXmaEncoder(std::shared_ptr<const XmaEncoderService> encoder);

    /**
     * @brief Returns the encoder this process's builds use.
     *
     * Never null: with nothing attached it is @ref MakeUnavailableXmaEncoder, whose
     * @ref XmaEncoderService::UnavailableReason begins with @ref XmaEncoderUnavailableSentence.
     *
     * @return The attached backend.
     */
    [[nodiscard]] std::shared_ptr<const XmaEncoderService> BuildXmaEncoder();
}
