// SPDX-License-Identifier: MS-PL
//
// XNAPP-262: the build-time XMA codec seam.
//
// XMA is the one format in this plan CNA cannot produce and cannot be made to produce here. The
// audit is recorded in docs/xma-encoder-backend.md; what this file is, is the answer to the second
// half of the question -- everything around the encoder, implemented and tested with no encoder at
// all, and one interface where a user attaches theirs. It is deliberately the same shape as
// EffectCompilerService.cpp, which solves the identical problem for `fxc`: a Microsoft tool that
// cannot be vendored, discovered by option then environment then CMake, run through a launcher,
// and folded into the build fingerprint by its own reported identity.
#include "CNA/Content/Pipeline/XmaEncoderService.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/Environment.hpp"

#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <limits>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Internal/HostProcess.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        /** @brief The `fmt ` format tag every XMA2 stream carries. */
        constexpr std::uint16_t kWaveFormatXma2 = 0x0166u;

        [[nodiscard]] std::string EnvironmentValue(const char* name)
        {
            // System::Environment reads the wide environment on Windows and answers UTF-8, the
            // encoding PathFromUtf8 expects; std::getenv answers ANSI code page bytes there.
            return System::Environment::GetEnvironmentVariable(name).value_or(std::string{});
        }

        /** @brief The encoder path CMake baked in, or an empty string. */
        [[nodiscard]] std::string ConfiguredExecutable()
        {
#if defined(CNA_XMA_ENCODER_EXECUTABLE)
            return CNA_XMA_ENCODER_EXECUTABLE;
#else
            return {};
#endif
        }

        /** @brief The launcher CMake baked in, or an empty string. */
        [[nodiscard]] std::string ConfiguredLauncher()
        {
#if defined(CNA_XMA_ENCODER_LAUNCHER)
            return CNA_XMA_ENCODER_LAUNCHER;
#else
            return {};
#endif
        }

        /** @brief Splits a space-separated argument list, which is what an environment can carry. */
        [[nodiscard]] std::vector<std::string> SplitArguments(const std::string& text)
        {
            std::vector<std::string> arguments;
            std::istringstream stream(text);
            for (std::string token; stream >> token;) { arguments.push_back(token); }
            return arguments;
        }

        /** @brief A scratch directory that removes itself, for the encoder's two files. */
        class ScratchDirectory
        {
        public:
            ScratchDirectory()
            {
                std::error_code error;
                // The counter makes concurrent workers independent without a clock or a random
                // source, so the pipeline stays deterministic.
                static std::atomic<unsigned long long> counter{0u};
                path_ = std::filesystem::temp_directory_path(error) /
                        ("cna-xma-" + std::to_string(counter.fetch_add(1u)) + "-" +
                         std::to_string(reinterpret_cast<std::uintptr_t>(this)));
                std::filesystem::create_directories(path_, error);
            }

            ~ScratchDirectory()
            {
                std::error_code error;
                std::filesystem::remove_all(path_, error);
            }

            ScratchDirectory(const ScratchDirectory&) = delete;
            ScratchDirectory& operator=(const ScratchDirectory&) = delete;

            [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

        private:
            std::filesystem::path path_;
        };

        void PutU16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        }

        void PutU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
            }
        }

        void PutTag(std::vector<std::uint8_t>& bytes, const char* tag)
        {
            for (int index = 0; index < 4; ++index)
            {
                bytes.push_back(static_cast<std::uint8_t>(tag[index]));
            }
        }

        /** @brief Writes the canonical RIFF WAVE the contract hands the encoder. */
        [[nodiscard]] bool WriteWave(const std::filesystem::path& path, const XmaEncodeRequest& request)
        {
            const auto channels = static_cast<std::uint16_t>(request.channelCount);
            const auto bits = static_cast<std::uint16_t>(request.bitsPerSample);
            const auto rate = static_cast<std::uint32_t>(request.sampleRate);
            const auto blockAlign = static_cast<std::uint16_t>(channels * (bits / 8u));
            const auto dataSize = static_cast<std::uint32_t>(request.pcm.size());

            std::vector<std::uint8_t> bytes;
            PutTag(bytes, "RIFF");
            PutU32(bytes, 36u + dataSize);
            PutTag(bytes, "WAVE");
            PutTag(bytes, "fmt ");
            PutU32(bytes, 16u);
            PutU16(bytes, 1u);  // WAVE_FORMAT_PCM
            PutU16(bytes, channels);
            PutU32(bytes, rate);
            PutU32(bytes, rate * blockAlign);
            PutU16(bytes, blockAlign);
            PutU16(bytes, bits);
            PutTag(bytes, "data");
            PutU32(bytes, dataSize);
            bytes.insert(bytes.end(), request.pcm.begin(), request.pcm.end());

            std::ofstream stream(path, std::ios::binary);
            if (!stream) { return false; }
            stream.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(stream);
        }

        [[nodiscard]] std::vector<std::uint8_t> ReadAll(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        }

        [[nodiscard]] std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes,
                                            const std::size_t at)
        {
            std::uint32_t value = 0u;
            for (int index = 3; index >= 0; --index)
            {
                value = (value << 8) | bytes[at + static_cast<std::size_t>(index)];
            }
            return value;
        }

        /**
         * @brief Reads the `fmt ` and `data` chunks out of the RIFF the encoder wrote.
         *
         * Bounds-checked throughout and deliberately unforgiving: this is a file a program outside
         * CNA produced, and a chunk table that does not add up is a refusal rather than something
         * to read past.
         *
         * @param bytes The whole file.
         * @param format Receives the `fmt ` chunk's contents.
         * @param data Receives the `data` chunk's contents.
         * @param failure Receives why the read failed.
         * @return Whether both chunks were found and the format tag is XMA2.
         */
        [[nodiscard]] bool ReadXmaRiff(const std::vector<std::uint8_t>& bytes,
                                       std::vector<std::uint8_t>& format,
                                       std::vector<std::uint8_t>& data, std::string& failure)
        {
            if (bytes.size() < 12u || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
                std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
            {
                failure = "the encoder's output is not a RIFF WAVE file";
                return false;
            }
            std::size_t at = 12u;
            bool sawFormat = false;
            bool sawData = false;
            while (at + 8u <= bytes.size())
            {
                const char* tag = reinterpret_cast<const char*>(bytes.data() + at);
                const std::uint32_t size = ReadU32(bytes, at + 4u);
                const std::size_t body = at + 8u;
                if (size > bytes.size() || body + size > bytes.size())
                {
                    failure = "the encoder's output has a chunk that runs past the end of the file";
                    return false;
                }
                if (std::memcmp(tag, "fmt ", 4) == 0)
                {
                    format.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(body + size));
                    sawFormat = true;
                }
                else if (std::memcmp(tag, "data", 4) == 0)
                {
                    data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body),
                                bytes.begin() + static_cast<std::ptrdiff_t>(body + size));
                    sawData = true;
                }
                at = body + size + (size & 1u);
            }
            if (!sawFormat || !sawData)
            {
                failure = "the encoder's output has no fmt or no data chunk";
                return false;
            }
            if (format.size() < 2u ||
                static_cast<std::uint16_t>(format[0] | (format[1] << 8)) != kWaveFormatXma2)
            {
                failure = "the encoder's output does not declare format tag 0x0166 (XMA2)";
                return false;
            }
            return true;
        }

        [[nodiscard]] std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes, const std::size_t at)
        {
            return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1u] << 8));
        }

        /// One XMA packet: the unit an XMA2 data chunk and its blocks are made of.
        constexpr std::uint32_t kXmaPacketBytes = 2048u;

        /**
         * @brief Checks the XMA2WAVEFORMATEX an encoder declared and reads its loop region (L16).
         *
         * The rules are those every XMA2 stream obeys and the measured XNA Xbox fixture shows
         * (docs/xma-encoder-backend.md): the 34-byte extension is present, the data is whole
         * 2048-byte packets that fit the declared blocks, the play region lies inside the encoded
         * samples, and so does the loop -- which, in the fixture, runs past PlayLength (384 +
         * 22016 > 22050) but not past SamplesEncoded. Bitstream contents are not decoded.
         *
         * @param format The `fmt ` chunk, little-endian as the adapter contract requires.
         * @param data The `data` chunk.
         * @param channels The channel count the request asked for.
         * @param loopStart Receives the encoder's LoopBegin.
         * @param loopLength Receives the encoder's LoopLength.
         * @param failure Receives why the block was refused.
         * @return Whether the block is a usable XMA2 description of @p data.
         */
        [[nodiscard]] bool ReadXma2Extension(const std::vector<std::uint8_t>& format,
                                             const std::vector<std::uint8_t>& data,
                                             const int channels, int& loopStart,
                                             int& loopLength, std::string& failure)
        {
            constexpr std::size_t kHeader = 18u;
            constexpr std::size_t kExtension = 34u;
            if (format.size() < kHeader + kExtension || ReadU16(format, 16u) < kExtension)
            {
                failure = "the encoder's XMA2 format block has no 34-byte XMA2WAVEFORMATEX extension";
                return false;
            }
            const std::uint16_t declaredChannels = ReadU16(format, 2u);
            const std::uint32_t sampleRate = ReadU32(format, 4u);
            const std::uint16_t streams = ReadU16(format, kHeader + 0u);
            const std::uint32_t samplesEncoded = ReadU32(format, kHeader + 6u);
            const std::uint32_t bytesPerBlock = ReadU32(format, kHeader + 10u);
            const std::uint32_t playBegin = ReadU32(format, kHeader + 14u);
            const std::uint32_t playLength = ReadU32(format, kHeader + 18u);
            const std::uint32_t loopBegin = ReadU32(format, kHeader + 22u);
            const std::uint32_t loopSpan = ReadU32(format, kHeader + 26u);
            const std::uint16_t blockCount = ReadU16(format, kHeader + 32u);
            const auto fail = [&failure](std::string why) {
                failure = "the encoder's XMA2 format block is inconsistent: " + std::move(why);
                return false;
            };
            if (declaredChannels != static_cast<std::uint16_t>(channels))
                return fail("it declares " + std::to_string(declaredChannels) + " channel(s) for a " +
                            std::to_string(channels) + "-channel source");
            if (sampleRate == 0u) return fail("its sample rate is zero");
            if (streams == 0u || streams > declaredChannels)
                return fail("NumStreams is " + std::to_string(streams));
            if (bytesPerBlock == 0u || bytesPerBlock % kXmaPacketBytes != 0u)
                return fail("BytesPerBlock " + std::to_string(bytesPerBlock) +
                            " is not a whole number of 2048-byte packets");
            if (blockCount == 0u) return fail("BlockCount is zero");
            if (data.empty() || data.size() % kXmaPacketBytes != 0u)
                return fail("its data chunk holds " + std::to_string(data.size()) +
                            " bytes, not whole 2048-byte packets");
            if (static_cast<std::uint64_t>(data.size()) >
                static_cast<std::uint64_t>(bytesPerBlock) * blockCount)
                return fail("its data chunk is larger than BlockCount x BytesPerBlock");
            if (playLength == 0u ||
                static_cast<std::uint64_t>(playBegin) + playLength > samplesEncoded)
                return fail("the play region " + std::to_string(playBegin) + "+" +
                            std::to_string(playLength) + " is outside SamplesEncoded " +
                            std::to_string(samplesEncoded));
            if (static_cast<std::uint64_t>(loopBegin) + loopSpan > samplesEncoded)
                return fail("the loop " + std::to_string(loopBegin) + "+" + std::to_string(loopSpan) +
                            " is outside SamplesEncoded " + std::to_string(samplesEncoded));
            if (loopBegin > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
                loopSpan > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
                return fail("the loop does not fit a SoundEffect's Int32 fields");
            loopStart = static_cast<int>(loopBegin);
            loopLength = static_cast<int>(loopSpan);
            return true;
        }

        /** @brief The backend a build has when no encoder is attached. */
        class UnavailableXmaEncoder final : public XmaEncoderService
        {
        public:
            explicit UnavailableXmaEncoder(std::string reason) : reason_(std::move(reason)) {}

            [[nodiscard]] XmaEncoderIdentity Identity() const override { return {}; }
            [[nodiscard]] bool Available() const override { return false; }
            [[nodiscard]] std::string UnavailableReason() const override { return reason_; }

            [[nodiscard]] XmaEncodeResult Encode(const XmaEncodeRequest&) const override
            {
                XmaEncodeResult result;
                result.diagnostics.push_back(reason_);
                return result;
            }

        private:
            std::string reason_;
        };

        /** @brief The external-process backend. */
        class ExternalXmaEncoder final : public XmaEncoderService
        {
        public:
            ExternalXmaEncoder(std::filesystem::path executable, std::filesystem::path launcher,
                               std::vector<std::string> arguments, XmaEncoderIdentity identity)
                : executable_(std::move(executable)), launcher_(std::move(launcher)),
                  arguments_(std::move(arguments)), identity_(std::move(identity))
            {
            }

            [[nodiscard]] XmaEncoderIdentity Identity() const override { return identity_; }
            [[nodiscard]] bool Available() const override { return true; }
            [[nodiscard]] std::string UnavailableReason() const override { return {}; }

            [[nodiscard]] XmaEncodeResult Encode(const XmaEncodeRequest& request) const override
            {
                XmaEncodeResult result;
                const ScratchDirectory scratch;
                const std::filesystem::path input = scratch.Path() / "source.wav";
                const std::filesystem::path output = scratch.Path() / "encoded.xma";
                if (!WriteWave(input, request))
                {
                    result.diagnostics.push_back("could not write the encoder's input file in " +
                                                 CNA::Internal::PathToUtf8(scratch.Path()));
                    return result;
                }

                std::vector<std::string> arguments;
                if (!launcher_.empty()) { arguments.push_back(CNA::Internal::PathToUtf8(executable_)); }
                for (const std::string& argument : arguments_)
                {
                    arguments.push_back(Substitute(argument, input, output, request));
                }

                const CNA::Internal::HostProcessResult ran = CNA::Internal::RunHostProcess(
                    launcher_.empty() ? executable_ : launcher_, arguments);
                if (!ran.started)
                {
                    result.diagnostics.push_back("the XMA encoder could not be started: " +
                                                 ran.failure);
                    return result;
                }
                for (const std::string& stream : {ran.standardOutput, ran.standardError})
                {
                    if (!stream.empty()) { result.diagnostics.push_back(stream); }
                }
                if (ran.exitCode != 0)
                {
                    result.diagnostics.push_back("the XMA encoder exited with status " +
                                                 std::to_string(ran.exitCode));
                    return result;
                }

                std::string failure;
                if (!ReadXmaRiff(ReadAll(output), result.formatBlock, result.data, failure))
                {
                    result.diagnostics.push_back(failure);
                    return result;
                }
                // L16: the loop region is the encoder's, read out of its own XMA2 extension. XMA
                // quantizes it -- the genuine Xbox fixture asked for 0/22050 and carries 384/22016
                // -- so the request's values would describe a loop the stream does not have.
                if (!ReadXma2Extension(result.formatBlock, result.data, request.channelCount,
                                       result.loopStart, result.loopLength, failure))
                {
                    result.diagnostics.push_back(failure);
                    return result;
                }
                result.succeeded = true;
                return result;
            }

        private:
            [[nodiscard]] static std::string Substitute(std::string text,
                                                        const std::filesystem::path& input,
                                                        const std::filesystem::path& output,
                                                        const XmaEncodeRequest& request)
            {
                const std::pair<const char*, std::string> replacements[] = {
                    {"{input}", CNA::Internal::PathToUtf8(input)},
                    {"{output}", CNA::Internal::PathToUtf8(output)},
                    {"{quality}", XmaEncodeQualityName(request.quality)},
                    {"{loopStart}", std::to_string(request.loopStart)},
                    {"{loopLength}", std::to_string(request.loopLength)},
                };
                for (const auto& [token, value] : replacements)
                {
                    for (std::size_t at = text.find(token); at != std::string::npos;
                         at = text.find(token, at + value.size()))
                    {
                        text.replace(at, std::strlen(token), value);
                    }
                }
                return text;
            }

            std::filesystem::path executable_;
            std::filesystem::path launcher_;
            std::vector<std::string> arguments_;
            XmaEncoderIdentity identity_;
        };
    }

    const char* XmaEncodeQualityName(const XmaEncodeQuality quality) noexcept
    {
        switch (quality)
        {
            case XmaEncodeQuality::Low: return "low";
            case XmaEncodeQuality::Medium: return "medium";
            case XmaEncodeQuality::Best: break;
        }
        return "best";
    }

    std::string XmaEncoderIdentity::ToString() const
    {
        if (backend.empty()) { return {}; }
        return version.empty() ? backend : backend + " " + version;
    }

    std::shared_ptr<const XmaEncoderService> MakeUnavailableXmaEncoder()
    {
        return std::make_shared<const UnavailableXmaEncoder>(
            std::string(XmaEncoderUnavailableSentence) +
            ": XMA is Microsoft's own Xbox 360 codec. There is no public specification sufficient "
            "to implement a conforming encoder, no licensable encoder, and no encoder in FFmpeg or "
            "in any other dependency this project may take -- FFmpeg decodes xma1 and xma2 and "
            "encodes neither. Attach one with --xma-encoder <path> (or CNA_XMA_ENCODER), which "
            "takes a program that reads a RIFF WAVE and writes a .xma; see "
            "docs/xma-encoder-backend.md.");
    }

    std::shared_ptr<const XmaEncoderService> MakeExternalXmaEncoder(
        const ExternalXmaEncoderOptions& options)
    {
        std::filesystem::path executable = options.executable;
        if (executable.empty())
        {
            const std::string fromEnvironment = EnvironmentValue("CNA_XMA_ENCODER");
            executable = fromEnvironment.empty() ? ConfiguredExecutable() : fromEnvironment;
        }
        if (executable.empty()) { return MakeUnavailableXmaEncoder(); }

        std::error_code error;
        if (!std::filesystem::exists(executable, error) || error)
        {
            return std::make_shared<const UnavailableXmaEncoder>(
                "the XMA encoder '" + CNA::Internal::PathToUtf8(executable) + "' does not exist. Name one that does "
                "with --xma-encoder <path> or CNA_XMA_ENCODER; see docs/xma-encoder-backend.md.");
        }

        std::filesystem::path launcher = options.launcher;
        if (launcher.empty())
        {
            const std::string fromEnvironment = EnvironmentValue("CNA_XMA_ENCODER_LAUNCHER");
            launcher = fromEnvironment.empty() ? ConfiguredLauncher() : fromEnvironment;
        }

        std::vector<std::string> arguments = options.arguments;
        if (arguments.empty()) { arguments = SplitArguments(EnvironmentValue("CNA_XMA_ENCODER_ARGS")); }
        if (arguments.empty()) { arguments = {"{input}", "{output}"}; }

        XmaEncoderIdentity identity;
        identity.backend = CNA::Internal::PathToUtf8(executable.filename());
        // No version probe: unlike `fxc -help`, there is no command every encoder for this codec
        // answers, and inventing one would make a build depend on a convention nothing follows.
        // L16: what decides the output instead is the program's own bytes and how it is run --
        // the launcher and the argument template -- so those are the fingerprint. A filename and
        // a timestamp collided for two different programs and missed a copied-in replacement.
        std::string fingerprint;
        try
        {
            fingerprint = "sha256:" + ContentFileSha256(executable);
        }
        catch (const std::exception&)
        {
            const auto written = std::filesystem::last_write_time(executable, error);
            fingerprint = error ? std::string("unreadable")
                                : "mtime:" + std::to_string(static_cast<long long>(
                                                 written.time_since_epoch().count()));
        }
        if (!launcher.empty())
            fingerprint += " launcher:" + CNA::Internal::PathToUtf8(launcher);
        std::string templateText;
        for (const std::string& argument : arguments)
            templateText += (templateText.empty() ? "" : " ") + argument;
        fingerprint += " args:" + templateText;
        identity.version = std::move(fingerprint);
        return std::make_shared<const ExternalXmaEncoder>(std::move(executable), std::move(launcher),
                                                          std::move(arguments), std::move(identity));
    }

    namespace
    {
        /**
         * @brief Lets CNA.SoundEffectProcessor (content module) use the build's XMA encoder for an
         *        Xbox 360 target, as XNA's own processor writes XMA there (L16).
         */
        class XmaXboxSoundEffectEncoder final : public XboxSoundEffectEncoder
        {
        public:
            explicit XmaXboxSoundEffectEncoder(std::shared_ptr<const XmaEncoderService> encoder)
                : encoder_(std::move(encoder))
            {
            }

            [[nodiscard]] std::string Identity() const override
            {
                return encoder_->Identity().ToString();
            }

            [[nodiscard]] EncodedSoundEffectFormat Encode(const ProcessedSoundEffect& sound) const override
            {
                XmaEncodeRequest request;
                request.pcm = sound.samples;
                request.channelCount = static_cast<int>(sound.channels);
                request.sampleRate = static_cast<int>(sound.sampleRate);
                request.bitsPerSample = static_cast<int>(ProcessedPcmBitsPerSample(sound.encoding));
                request.loopStart = static_cast<int>(sound.loopStart);
                request.loopLength = static_cast<int>(sound.loopLength);
                // The canonical processor takes no parameters; XNA's default quality is Best.
                request.quality = XmaEncodeQuality::Best;
                const XmaEncodeResult result = encoder_->Encode(request);
                if (!result.succeeded || result.formatBlock.size() < 18u)
                {
                    std::string message = "the Xbox 360 SoundEffect could not be encoded to XMA by " +
                                          encoder_->Identity().ToString();
                    for (const std::string& line : result.diagnostics) message += "\n" + line;
                    throw Microsoft::Xna::Framework::Content::ContentLoadException(message);
                }
                const std::vector<std::uint8_t>& block = result.formatBlock;
                EncodedSoundEffectFormat encoded;
                encoded.formatTag = ReadU16(block, 0u);
                encoded.channels = ReadU16(block, 2u);
                encoded.sampleRate = ReadU32(block, 4u);
                encoded.averageBytesPerSecond = ReadU32(block, 8u);
                encoded.blockAlign = ReadU16(block, 12u);
                encoded.bitsPerSample = ReadU16(block, 14u);
                const std::size_t extension =
                    std::min<std::size_t>(ReadU16(block, 16u), block.size() - 18u);
                encoded.extension.assign(block.begin() + 18,
                                         block.begin() + 18 + static_cast<std::ptrdiff_t>(extension));
                encoded.data = result.data;
                encoded.loopStart = result.loopStart;
                encoded.loopLength = result.loopLength;
                return encoded;
            }

        private:
            std::shared_ptr<const XmaEncoderService> encoder_;
        };

        std::mutex& BuildEncoderMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::shared_ptr<const XmaEncoderService>& BuildEncoderSlot()
        {
            static std::shared_ptr<const XmaEncoderService> encoder;
            return encoder;
        }
    }

    void SetBuildXmaEncoder(std::shared_ptr<const XmaEncoderService> encoder)
    {
        const std::lock_guard<std::mutex> lock(BuildEncoderMutex());
        // L16: the built-in Xbox 360 WAV route follows the attached encoder.
        SetXboxSoundEffectEncoder(encoder != nullptr && encoder->Available()
                                      ? std::make_shared<const XmaXboxSoundEffectEncoder>(encoder)
                                      : nullptr);
        BuildEncoderSlot() = std::move(encoder);
    }

    std::shared_ptr<const XmaEncoderService> BuildXmaEncoder()
    {
        const std::lock_guard<std::mutex> lock(BuildEncoderMutex());
        std::shared_ptr<const XmaEncoderService>& slot = BuildEncoderSlot();
        if (slot == nullptr) { slot = MakeUnavailableXmaEncoder(); }
        return slot;
    }
}
