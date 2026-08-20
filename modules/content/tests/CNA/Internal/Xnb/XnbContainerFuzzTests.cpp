// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-43 (Phase G): deterministic, whole-container fuzz test -- unlike XNB-30A's
// LzxDecoderFuzzTests.cpp (which only exercised the LZX decompressor in isolation), this mutates
// a real, externally-produced, uncompressed .xnb file's ENTIRE byte stream (10-byte header, the
// type-reader table, the shared-resource region, and the root object body) and loads it through
// the exact same path a real game uses -- ContentManager::Load<T>() -- exercising header parsing,
// type-reader-table parsing, root-object dispatch, shared-resource fixups, and every reader
// BlenderDefaultCube.xnb's real content touches (Model, math, VertexBuffer/IndexBuffer,
// BasicEffect) together, plus a Texture2D and a SoundEffect fixture as their own root dispatch
// targets. Asserts every mutated input either loads successfully or fails with one of a small set
// of expected, clean exception types -- never crashes, hangs, or corrupts memory.
// The mutation stream is deterministic (fixed seed, no wall clock/std::random_device) so it is
// reproducible. Also run under an ASan+UBSan build (-DCNA_SANITIZE=address,undefined) as the real
// net for memory-safety/UB issues a plain exception-type check cannot detect on its own.

#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "CNA/GraphicsCapability.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/ArgumentException.hpp"
#include "System/IO/EndOfStreamException.hpp"

using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    struct Rng
    {
        std::uint64_t state;
        explicit Rng(std::uint64_t seed) : state(seed) {}
        std::uint32_t next()
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<std::uint32_t>(state >> 33);
        }
        std::uint32_t below(std::uint32_t n) { return next() % n; }
    };

    std::vector<uint8_t> ReadWholeFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream ss;
        ss << file.rdbuf();
        const std::string s = ss.str();
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // Byte flips, truncation, byte insertion, and random-value overwrites -- covering both
    // "corrupt an existing field" and "shift every later field's alignment" mutation classes.
    void Mutate(Rng& rng, std::vector<uint8_t>& bytes)
    {
        if (bytes.empty()) return;

        const uint32_t mutationCount = 1 + rng.below(6);
        for (uint32_t m = 0; m < mutationCount; ++m)
        {
            if (bytes.empty()) return;
            switch (rng.below(4))
            {
                case 0: // flip a random bit in a random byte
                    bytes[rng.below(static_cast<uint32_t>(bytes.size()))] ^= static_cast<uint8_t>(1u << rng.below(8));
                    break;
                case 1: // truncate to a random shorter length
                    if (bytes.size() > 1)
                    {
                        bytes.resize(1 + rng.below(static_cast<uint32_t>(bytes.size()) - 1));
                    }
                    break;
                case 2: // overwrite a random byte with a random value
                    bytes[rng.below(static_cast<uint32_t>(bytes.size()))] = static_cast<uint8_t>(rng.below(256));
                    break;
                case 3: // insert a random byte at a random position (shifts everything after it)
                    bytes.insert(bytes.begin() + rng.below(static_cast<uint32_t>(bytes.size()) + 1),
                                 static_cast<uint8_t>(rng.below(256)));
                    break;
                default: break;
            }
        }
    }

    // Shared body for every fixture below: mutate a real seed file's entire byte stream,
    // load it via ContentManager::Load<T>(), and confirm the outcome is always either a real
    // success or one of a small set of expected, clean exception types -- never a crash/hang.
    template <typename T>
    void RunContainerFuzz(
        const std::string& seedPath, const std::string& assetName, std::uint64_t seed,
        int iterations, GraphicsDevice& gd)
    {
        const std::vector<uint8_t> seedBytes = ReadWholeFile(seedPath);
        ASSERT_FALSE(seedBytes.empty()) << "Real fixture not found relative to CWD: " << seedPath;

        const std::filesystem::path root = std::filesystem::temp_directory_path()
            / ("cna_xnb_container_fuzz_" + std::to_string(seed));
        std::filesystem::create_directories(root);

        Rng rng(seed);
        int completed = 0;
        int cleanlyRejected = 0;

        for (int i = 0; i < iterations; ++i)
        {
            std::vector<uint8_t> mutated = seedBytes;
            Mutate(rng, mutated);
            WriteBytes(root / (assetName + ".xnb"), mutated);

            try
            {
                ContentManager cm(nullptr, root.string());
                cm.setGraphicsDevice(gd);
                T asset = cm.Load<T>(assetName);
                (void)asset;
                ++completed;
            }
            catch (const ContentLoadException&)
            {
                ++cleanlyRejected;
            }
            catch (const System::IO::EndOfStreamException&)
            {
                ++cleanlyRejected;
            }
            catch (const System::ArgumentException&)
            {
                // The XNA public API's own argument validation rejecting mutated content before it
                // can reach the GPU. The Model fixture reaches it through
                // VertexBuffer::SetData (VertexBuffer.cpp:183): a mutation of the vertex
                // declaration's element offsets/formats, or of the stride, leaves a declared
                // element outside the bytes actually uploaded, and SetData refuses the upload
                // rather than reading past the end of the caller's data. The stride guard
                // (VertexBuffer.cpp:153) raises the derived ArgumentOutOfRangeException on the same
                // path, which this base catch also covers. Only the ArgumentException family is
                // accepted here -- System::NotSupportedException (a renderer capability boundary),
                // std::runtime_error, and every other class still fail this test.
                ++cleanlyRejected;
            }
            catch (const std::bad_any_cast&)
            {
                ++cleanlyRejected; // a mutated type-reader-table/dispatch index resolved to the wrong type
            }
            catch (const std::out_of_range&)
            {
                ++cleanlyRejected; // e.g. a bad bone-reference/shared-resource index caught by .at()
            }
            catch (const std::length_error&)
            {
                ++cleanlyRejected; // a container construction request that std:: itself refused
            }
            catch (const std::invalid_argument&)
            {
                ++cleanlyRejected; // e.g. XnbTypeName's generic-argument parser rejecting malformed brackets
            }
            catch (const std::bad_alloc&)
            {
                ADD_FAILURE() << "iteration #" << i
                               << ": std::bad_alloc surfaced instead of a clean ContentLoadException -- "
                                  "an allocation-bomb guard gap, not an acceptable outcome";
            }
            // Any other exception type, or a crash/hang, fails this test (uncaught exception
            // aborts it; a crash is caught by the process exit code / a sanitizer run, not by
            // this catch list).
        }

        std::error_code ec;
        std::filesystem::remove_all(root, ec);

        EXPECT_EQ(completed + cleanlyRejected, iterations);
    }

    class XnbContainerFuzzTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd;
    };
}

TEST_F(XnbContainerFuzzTest, MutatedRealModelFixtureNeverCrashesAndOnlyFailsCleanly)
{
    // Model loading unconditionally rejects with std::runtime_error (not in RunContainerFuzz's
    // "clean rejection" catch list, which enumerates content/parsing exceptions, not capability
    // rejections) on a renderer with no 3D pipeline -- every mutation would hit that same
    // categorical rejection before the container parser it fuzzes is even reached, so there is
    // nothing left for this fuzz pass to exercise. Texture2D/SoundEffect below are unaffected and
    // keep running everywhere.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    RunContainerFuzz<Model>(
        "tests/assets/xnb/monogame/windows/uncompressed/BlenderDefaultCube.xnb", "fixture",
        0x584E42ULL, // "XNB" in hex-ish, arbitrary fixed seed
        1500, gd);
}

TEST_F(XnbContainerFuzzTest, MutatedRealTexture2DFixtureNeverCrashesAndOnlyFailsCleanly)
{
    RunContainerFuzz<Texture2D>(
        "tests/assets/xnb/monogame/windows/uncompressed/white-1.xnb", "fixture",
        0x54455832ULL, // "TEX2" in hex-ish
        1500, gd);
}

TEST_F(XnbContainerFuzzTest, MutatedRealSoundEffectFixtureNeverCrashesAndOnlyFailsCleanly)
{
    RunContainerFuzz<SoundEffect>(
        "tests/assets/xnb/monogame/windows/uncompressed/audio/tone_mono_44khz_16bit.xnb", "fixture",
        0x534E44ULL, // "SND" in hex-ish
        1500, gd);
}
