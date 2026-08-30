// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-34: the .cnj "sourceFile" capability matrix. Texture2D's support (sourceFile +
// colorKey) is already covered by CnjSourceFileTests.cpp/CnjCacheIsolationTests.cpp -- this file
// covers the other branches: SoundEffect and TextureCube delegate via sourceFile (no
// metadata fields yet); SpriteFont, Effect, Model, and AnimationClip (CNB-40) explicitly reject a
// "sourceFile" field with a clear ContentLoadException, since their .cnj documents are
// self-contained descriptors.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the compile-time guard it replaced.
using namespace CNA::Testing::Renderers;

#include "System/NotSupportedException.hpp"
#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/Environment.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

namespace
{
    // A tests-only scratch content root, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction. Mirrors ContentManagerSkinnedModelTests.cpp.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_cnj_capability_matrix_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void W16(std::vector<uint8_t>& v, uint16_t x)
    {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
    }

    void W32(std::vector<uint8_t>& v, uint32_t x)
    {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
        v.push_back(static_cast<uint8_t>(x >> 16));
        v.push_back(static_cast<uint8_t>(x >> 24));
    }

    void Tag(std::vector<uint8_t>& v, const char* t)
    {
        v.insert(v.end(), t, t + 4);
    }

    // Minimal valid 16-bit mono PCM WAV: 0.1s of silence @ 44100Hz. Mirrors
    // SoundEffectTests.cpp's BuildMinimalWavBytes().
    std::vector<uint8_t> BuildMinimalWavBytes()
    {
        constexpr uint16_t channels      = 1;
        constexpr uint32_t sampleRate    = 44100;
        constexpr uint8_t  bitsPerSample = 16;
        constexpr uint32_t frameCount    = 4410;
        const uint32_t audioLen = frameCount * channels * (bitsPerSample / 8);

        const uint16_t blockAlign     = static_cast<uint16_t>(channels * (bitsPerSample / 8));
        const uint32_t avgBytesPerSec = sampleRate * blockAlign;
        const uint32_t riffPayload    = 4 + (8 + 16) + (8 + audioLen);

        std::vector<uint8_t> wav;
        Tag(wav, "RIFF"); W32(wav, riffPayload);
        Tag(wav, "WAVE");
        Tag(wav, "fmt "); W32(wav, 16);
        W16(wav, 1); W16(wav, channels);
        W32(wav, sampleRate); W32(wav, avgBytesPerSec);
        W16(wav, blockAlign); W16(wav, bitsPerSample);
        Tag(wav, "data"); W32(wav, audioLen);
        wav.resize(wav.size() + audioLen, 0);
        return wav;
    }

    // Minimal valid single-face-worth-of-data-omitted DDS is not decodable as a cube map without
    // all 6 faces present, so this builds a full minimal 4x4 DXT1 cube map (1 block/face), the
    // smallest real, valid TextureCube.DDSFromStreamEXT can decode. Adapted/condensed from
    // TextureCubeTests.cpp's BuildSolidColorCubeDds().
    std::vector<uint8_t> BuildMinimalCubeDds()
    {
        constexpr uint32_t kDdsdHeightWidth  = 0x2 | 0x4;
        constexpr uint32_t kDdpfFourCC       = 0x4;
        constexpr uint32_t kDdscapsTexture   = 0x1000;
        constexpr uint32_t kDdscaps2Cubemap  = 0x200;
        constexpr uint32_t kDdscaps2AllFaces = 0xFC00;

        std::vector<uint8_t> d;
        Tag(d, "DDS ");
        W32(d, 124);
        W32(d, kDdsdHeightWidth);
        W32(d, 4); // height
        W32(d, 4); // width
        W32(d, 0);
        W32(d, 0);
        W32(d, 1); // mipMapCount
        for (int i = 0; i < 11; ++i) W32(d, 0);

        W32(d, 32);
        W32(d, kDdpfFourCC);
        Tag(d, "DXT1");
        W32(d, 0); W32(d, 0); W32(d, 0); W32(d, 0); W32(d, 0);

        W32(d, kDdscapsTexture);
        W32(d, kDdscaps2Cubemap | kDdscaps2AllFaces);
        W32(d, 0); W32(d, 0); W32(d, 0);

        // 6 faces x one solid (black) DXT1 block each.
        for (int face = 0; face < 6; ++face)
        {
            for (int i = 0; i < 8; ++i) d.push_back(0);
        }
        return d;
    }
}

class CnjCapabilityMatrixTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

// plans/plan_platform.md PLAT-SDL2-8: needs the decoder/mixer engine, which is the SDL3_mixer
// implementation and is absent from the archive for every other CNA_AUDIO_PLATFORM value.
// Without it a SoundEffect reports a zero duration and VideoPlayer opens no audio stream,
// so this case is unobservable there rather than merely untested.
#ifdef SOUND_ENABLED
TEST_F(CnjCapabilityMatrixTest, SoundEffectDelegatesViaSourceFile)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    ScratchContentRoot root;
    WriteBytes(root.path() / "beep.wav", BuildMinimalWavBytes());
    WriteFile(root.path() / "beep.cnj",
              R"({"cnjVersion": 1, "type": "SoundEffect", "sourceFile": "beep.wav"})");

    ContentManager cm(nullptr, root.path().string());

    SoundEffect loaded = cm.Load<SoundEffect>("beep");
    EXPECT_GT(loaded.getDurationProperty().getTotalMillisecondsProperty(), 0.0);
}
#endif  // SOUND_ENABLED


// REMED-GFX-135: does THIS build's renderer actually store a cube face? Native 2D, ASCII, Canvas
// and DIRECTX3 create no cube resource at all and Headless stores no pixel data by design, so
// TextureCube::SetData -- and therefore every content path that uploads a cube -- now refuses
// deterministically instead of accepting the data and discarding it. Same constant and same
// reviewed renderer set as tests/Microsoft/Xna/Framework/Graphics/TextureCubeTests.cpp.
// plans/plan_sokol.md SOKOL-27: SokolTextureCubeRenderer stores real cube pixels (see
// tests/Microsoft/Xna/Framework/Graphics/TextureCubeTests.cpp for the full contract).
// PortableGL keeps the same nullptr CreateTextureCube default -- no cube resource exists there
// either (docs/portablegl-renderer.md).
// plans/plan_runtimerenderer.md RTR-P9-11: evaluated at runtime, so this describes the ACTIVE
// renderer rather than the build default. PIXIJS (plans/plan_pixijs.md PIXIJS-71) is in the "no cube
// resource exists" set: no cube override written, v1 scope being 2D-only.
[[nodiscard]] inline bool CubeStorageSupported()
{
    return !CNA_RENDERER_IS(SdlRenderer, Canvas, HtmlDom, FreeDirect, Headless, Gdi,
                            PortableGL, TinyGL, PixiJs);
}

TEST_F(CnjCapabilityMatrixTest, TextureCubeDelegatesViaSourceFile)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "cube.dds", BuildMinimalCubeDds());
    WriteFile(root.path() / "cube.cnj",
              R"({"cnjVersion": 1, "type": "TextureCube", "sourceFile": "cube.dds"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    if (!CubeStorageSupported())
    {
        EXPECT_THROW((void)cm.Load<TextureCube>("cube"), System::NotSupportedException);
        return;
    }
    TextureCube loaded = cm.Load<TextureCube>("cube");
    EXPECT_EQ(loaded.getSizeProperty(), 4);
}

TEST_F(CnjCapabilityMatrixTest, SpriteFontRejectsSourceFile)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj",
              R"({"cnjVersion": 1, "type": "SpriteFont", "sourceFile": "atlas.png"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<SpriteFont>("wrong"), ContentLoadException);
}

TEST_F(CnjCapabilityMatrixTest, EffectRejectsSourceFile)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj",
              R"({"cnjVersion": 1, "type": "Effect", "sourceFile": "shader.glsl"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<std::shared_ptr<Effect>>("wrong"), ContentLoadException);
}

TEST_F(CnjCapabilityMatrixTest, ModelRejectsSourceFile)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj",
              R"({"cnjVersion": 1, "type": "Model", "sourceFile": "mesh.obj"})");

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(cm.Load<Model>("wrong"), ContentLoadException);
}

TEST_F(CnjCapabilityMatrixTest, AnimationClipRejectsSourceFile)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj",
              R"({"cnjVersion": 1, "type": "AnimationClip", "sourceFile": "clip.bin"})");

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<AnimationClipEXT>("wrong"), ContentLoadException);
}
