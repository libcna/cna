// SPDX-License-Identifier: MS-PL
//
// XNB-43 (Phase G): deterministic, whole-container fuzz test -- unlike XNB-30A's
// LzxDecoderFuzzTests.cpp (which only exercised the LZX decompressor in isolation), this mutates
// real `.xnb` files' ENTIRE byte streams (10-byte header, optional compressed payload, the
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

#include "CNA/TestSupport/TestScratch.hpp"
#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include <cstring>
#include "CNA/Internal/PathUtf8.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include <string>

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
#include "System/NotSupportedException.hpp"

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

        const std::filesystem::path root = CNA::TestSupport::FixtureRoot()
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
                ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root));
                cm.setGraphicsDevice(gd);
                T asset = cm.Load<T>(assetName);
                (void)asset;
                ++completed;
            }
            catch (const ContentLoadException&)
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
                // accepted here -- std::runtime_error, and every other class, still fail this
                // test; System::NotSupportedException only as the profile refusal below.
                ++cleanlyRejected;
            }
            catch (const System::NotSupportedException& refusal)
            {
                // WINCLOSE-0029: VertexDeclaration::ValidateForProfile rejects a declaration the
                // active GraphicsProfile cannot express -- a mutated element format out of range,
                // a stride over 255, more than 16 elements -- with NotSupportedException, as XNA's
                // own profile validation does. That is content being refused, so it counts; every
                // other NotSupportedException (a renderer capability boundary) still fails.
                // Since TQ-024 a profile refusal carries XNA's own resource text, "XNA Framework
                // <Reach|HiDef> profile does not support ..." (ProfileValidation.cpp).
                const std::string message = refusal.getMessageProperty();
                const bool profileRefusal =
                    (message.rfind("XNA Framework Reach profile ", 0) == 0 ||
                     message.rfind("XNA Framework HiDef profile ", 0) == 0);
                if (!profileRefusal)
                    throw;
                ++cleanlyRejected;
            }
            // TQ-018: std::out_of_range, std::length_error and std::invalid_argument used to count as
            // clean refusals here. None is a refusal XNA's Load<T> makes (ContentReader.ReadAsset wraps
            // IOException only; the readers refuse with ContentLoadException or the ArgumentException
            // family): a std:: exception reaching this frame is an internal error leaking out of the
            // content boundary, and now fails the test like every other unlisted type.
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

        // The sum is the iteration count by construction; the fixed seed must reach both outcomes.
        EXPECT_GT(completed, 0);
        EXPECT_GT(cleanlyRejected, 0);
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

TEST_F(XnbContainerFuzzTest, MutatedLz4TextureFixtureNeverCrashesAndOnlyFailsCleanly)
{
    RunContainerFuzz<Texture2D>(
        "tests/assets/xnb/monogame/windows/lz4/white-1.xnb", "fixture",
        0x4C5A34ULL, // "LZ4" in hex
        1500, gd);
}

TEST_F(XnbContainerFuzzTest, MutatedRealSoundEffectFixtureNeverCrashesAndOnlyFailsCleanly)
{
    RunContainerFuzz<SoundEffect>(
        "tests/assets/xnb/monogame/windows/uncompressed/audio/tone_mono_44khz_16bit.xnb", "fixture",
        0x534E44ULL, // "SND" in hex-ish
        1500, gd);
}

TEST_F(XnbContainerFuzzTest, Safe002ReaderFamiliesAndSharedTablesLoadRefuseAndRecover)
{
    const auto build = [](bool strings, bool reverse, int32_t count, int32_t elementId,
                          int32_t sharedCount, int32_t rootId, int32_t version) {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write7BitEncodedInt(2);
        const std::string element = strings ? "String" : "Int32";
        const std::string list = "Microsoft.Xna.Framework.Content.ListReader`1[[System." + element + "]]";
        const std::string value = "Microsoft.Xna.Framework.Content." + element + "Reader";
        writer.Write(reverse ? value : list);
        writer.Write(version);
        writer.Write(reverse ? list : value);
        writer.Write(int32_t{0});
        writer.Write7BitEncodedInt(sharedCount);
        writer.Write7BitEncodedInt(rootId);
        writer.Write(count);
        for (int32_t i = 0; i < count && i < 2; ++i)
        {
            if (strings) { writer.Write7BitEncodedInt(elementId); writer.Write(std::string("seed")); }
            else { writer.Write(int32_t{42 + i}); }
        }
        for (int32_t i = 0; i < sharedCount && i < 2; ++i)
        {
            writer.Write7BitEncodedInt(reverse ? 1 : 2);
            if (strings) { writer.Write(std::string("shared")); }
            else { writer.Write(int32_t{91}); }
        }
        writer.Flush();
        const auto body = stream.ToArray();
        std::vector<uint8_t> file{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
        const auto size = static_cast<int32_t>(file.size() + body.size());
        std::memcpy(file.data() + 6, &size, sizeof(size));
        file.insert(file.end(), body.begin(), body.end());
        return file;
    };
    const auto root = CNA::TestSupport::FixtureRoot() /
        ("cna_safe002_xnb_families_" + std::to_string(reinterpret_cast<std::uintptr_t>(&gd)));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() {
        std::error_code error; std::filesystem::remove_all(root, error); } } cleanup{root};
    ContentManager manager(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root));
    for (const bool strings : {false, true})
    {
        for (const bool reverse : {false, true})
        {
            SCOPED_TRACE(::testing::Message() << "strings=" << strings << " reverse=" << reverse);
            const int32_t listId = reverse ? 2 : 1;
            const int32_t valueId = reverse ? 1 : 2;
            const auto load = [&] {
                if (strings)
                {
                    EXPECT_EQ((std::vector<std::string>{"seed", "seed"}),
                              manager.Load<std::vector<std::string>>("fixture"));
                }
                else
                {
                    EXPECT_EQ((std::vector<int32_t>{42, 43}), manager.Load<std::vector<int32_t>>("fixture"));
                }
            };
            // TQ-033, XNA: ListReader`1.Read IL_0026-002c loops `while (count-- > 0)`, so a
            // negative count is an empty list rather than a refusal, and the shared resources after
            // it still read.
            WriteBytes(root / "fixture.xnb", build(strings, reverse, -1, valueId, 2, listId, 0));
            if (strings) { EXPECT_TRUE(manager.Load<std::vector<std::string>>("fixture").empty()); }
            else { EXPECT_TRUE(manager.Load<std::vector<int32_t>>("fixture").empty()); }
            manager.Unload();
            struct Mutation { int32_t count, element, shared, root, version; };
            for (const Mutation& mutation : {
                     Mutation{INT32_MAX, valueId, 2, listId, 0},
                     {2, valueId, INT32_MAX, listId, 0}, {2, valueId, 2, 3, 0},
                     {2, valueId, 2, listId, 1}})
            {
                SCOPED_TRACE(::testing::Message() << mutation.count << "/" << mutation.shared
                                                << "/" << mutation.root << "/" << mutation.version);
                WriteBytes(root / "fixture.xnb", build(strings, reverse, mutation.count,
                    mutation.element, mutation.shared, mutation.root, mutation.version));
                if (strings) { EXPECT_THROW((void)manager.Load<std::vector<std::string>>("fixture"), ContentLoadException); }
                else { EXPECT_THROW((void)manager.Load<std::vector<int32_t>>("fixture"), ContentLoadException); }
                WriteBytes(root / "fixture.xnb", build(strings, reverse, 2, valueId, 2, listId, 0));
                load();
                manager.Unload();
            }
            if (strings)
            {
                WriteBytes(root / "fixture.xnb", build(true, reverse, 2, 3, 2, listId, 0));
                EXPECT_THROW((void)manager.Load<std::vector<std::string>>("fixture"), ContentLoadException);
                WriteBytes(root / "fixture.xnb", build(true, reverse, 2, valueId, 2, listId, 0));
                load();
                manager.Unload();
            }
        }
    }
    RecordProperty("family_table_combinations", 4);
    RecordProperty("refusal_recovery_pairs", 22);
}
