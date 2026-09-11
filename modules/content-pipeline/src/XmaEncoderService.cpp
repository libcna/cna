// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-262: the build-time XMA codec seam.
//
// XMA is the one format in this plan CNA cannot produce and cannot be made to produce here. The
// audit is recorded in docs/xma-encoder-backend.md; what this file is, is the answer to the second
// half of the question -- everything around the encoder, implemented and tested with no encoder at
// all, and one interface where a user attaches theirs. It is deliberately the same shape as
// EffectCompilerService.cpp, which solves the identical problem for `fxc`: a Microsoft tool that
// cannot be vendored, discovered by option then environment then CMake, run through a launcher,
// and folded into the build fingerprint by its own reported identity.
#include "CNA/Content/Pipeline/XmaEncoderService.hpp"

#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
            const char* value = std::getenv(name);
            return value == nullptr ? std::string{} : std::string(value);
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
                                                 scratch.Path().string());
                    return result;
                }

                std::vector<std::string> arguments;
                if (!launcher_.empty()) { arguments.push_back(executable_.string()); }
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
                // Loop points are the request's, in the encoder's own frames: the backend contract
                // does not carry them back, and an encoder that resamples the loop region has to
                // say so in its own container. Recorded so the caller can see they are pass-through
                // rather than measured (docs/xma-encoder-backend.md).
                result.loopStart = request.loopStart;
                result.loopLength = request.loopLength;
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
                    {"{input}", input.string()},
                    {"{output}", output.string()},
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
                "the XMA encoder '" + executable.string() + "' does not exist. Name one that does "
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
        identity.backend = executable.filename().string();
        // No version probe: unlike `fxc -help`, there is no command every encoder for this codec
        // answers, and inventing one would make a build depend on a convention nothing follows.
        // The file's own last-write time is what distinguishes two builds of one encoder.
        const auto written = std::filesystem::last_write_time(executable, error);
        if (!error)
        {
            identity.version = std::to_string(
                static_cast<long long>(written.time_since_epoch().count()));
        }
        return std::make_shared<const ExternalXmaEncoder>(std::move(executable), std::move(launcher),
                                                          std::move(arguments), std::move(identity));
    }

    namespace
    {
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
