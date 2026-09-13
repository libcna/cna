// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-25 (Phase D3): unit tests for Texture3DReader and TextureCubeReader.
// TextureCubeReader is verified end-to-end against a real, externally-produced fixture
// (MonoGame's SampleCube64DXT1Mips.xnb -- 6 faces, full DXT1 mip chain including the sub-4x4
// rounding edge cases). No Texture3DReader fixture was found anywhere in the available library
// (volume textures are rare in real XNA content), so it is tested with a hand-constructed stream
// verified field-by-field against FNA's own Texture3DReader.cs byte order instead.

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the compile-time guard it replaced.
using namespace CNA::Testing::Renderers;
#include <sstream>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Xnb/Texture3DContentTypeReader.hpp"
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

// -----------------------------------------------------------------------
// REMED-GFX-130: does THIS build's renderer actually read a cube face back? See the identical
// constant (and its full rationale) in tests/Microsoft/Xna/Framework/Graphics/TextureCubeTests.cpp.
// -----------------------------------------------------------------------
// REMED-GFX-135 additionally makes the UPLOAD side of this fixture load-bearing: a renderer that
// cannot store a cube face now throws System::NotSupportedException out of TextureCubeReader's own
// SetData call, so the whole ContentManager::Load fails instead of quietly returning an empty cube.
// Software gained real per-mip cube storage in that finding, so its mip readback is now exact.
// plans/plan_sokol.md SOKOL-27: SokolTextureCubeRenderer stores every declared mip level's six faces in a
// real CPU shadow, so its readback is exact at every level too.
// PortableGL keeps the same nullptr CreateTextureCube default -- no cube resource exists there
// either (docs/portablegl-renderer.md).
// plans/plan_runtimerenderer.md RTR-P9-11: evaluated at runtime, so these describe the ACTIVE renderer
// rather than the build default. The three-way split is preserved exactly.
//
// PIXIJS (plans/plan_pixijs.md PIXIJS-71) is in the "no cube resource exists" set: no cube override
// written, so it keeps the shared nullptr CreateTextureCube default, v1 scope being 2D-only.
[[nodiscard]] inline bool CubeStorageSupported()
{
    return !CNA_RENDERER_IS(SdlRenderer, Canvas, HtmlDom, FreeDirect, Headless, Gdi, OpenVg,
                            PortableGL, TinyGL, PixiJs, NanoVg);
}

[[nodiscard]] inline bool CubeLevel0ReadbackSupported() { return CubeStorageSupported(); }

/// OpenGL ES 1.1 stores the whole declared chain and reads the base level back through a scratch
/// framebuffer, but GL_OES_framebuffer_object requires an attached texture's level to be 0, so no
/// mip level above 0 can be read there however much storage exists. That is exactly why these are
/// three separate questions rather than one.
[[nodiscard]] inline bool CubeMipReadbackSupported()
{
    return !CNA_RENDERER_IS(OpenGLES1) && CubeLevel0ReadbackSupported();
}

/// Whether this renderer can fetch a volume texture's voxels back to the CPU.
///
/// The same split cube faces already have here. IGL owns real volume pixels but IGL v1.1.1 cannot
/// attach a 3D texture to a framebuffer, which is its only readback route -- verified by attempting
/// it (`GL_INVALID_OPERATION ... invalid textarget GL_TEXTURE_3D`), see plans/plan_igl.md IGL-17. GetData
/// refuses rather than fabricating voxels, and the shared layer raises NotSupportedException.
[[nodiscard]] inline bool VolumeReadbackSupported()
{
    return !CNA_RENDERER_IS(Igl);
}

namespace
{
    std::string ReadWholeFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    std::shared_ptr<Texture3D> ReadHandConstructedVolume(
        GraphicsDevice& device, SurfaceFormat format, int32_t width, int32_t height,
        int32_t depth, const std::vector<std::vector<std::uint8_t>>& levels)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<int32_t>(format));
        writer.Write(width);
        writer.Write(height);
        writer.Write(depth);
        writer.Write(static_cast<int32_t>(levels.size()));
        for (const auto& bytes : levels)
        {
            writer.Write(static_cast<int32_t>(bytes.size()));
            for (const std::uint8_t value : bytes) writer.Write(value);
        }
        writer.Flush();
        const auto serialized = stream.ToArray();

        ContentManager content;
        content.setGraphicsDevice(device);
        System::IO::MemoryStream body(serialized.data(), static_cast<int32_t>(serialized.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.Texture3DReader");
        if (!typeReader) return nullptr;
        return std::any_cast<std::shared_ptr<Texture3D>>(
            typeReader->ReadUntyped(reader, std::any{}));
    }

    TextureCube ReadHandConstructedCube(
        GraphicsDevice& device, SurfaceFormat format,
        const std::array<std::vector<std::uint8_t>, 6>& faces, int size = 1)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<int32_t>(format));
        writer.Write(static_cast<int32_t>(size));
        writer.Write(static_cast<int32_t>(1));
        for (const auto& bytes : faces)
        {
            writer.Write(static_cast<int32_t>(bytes.size()));
            for (const std::uint8_t value : bytes) writer.Write(value);
        }
        writer.Flush();
        const auto serialized = stream.ToArray();

        ContentManager content;
        content.setGraphicsDevice(device);
        System::IO::MemoryStream body(serialized.data(), static_cast<int32_t>(serialized.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.TextureCubeReader");
        if (!typeReader)
            throw std::runtime_error("TextureCubeReader is not registered");
        return std::any_cast<TextureCube>(typeReader->ReadUntyped(reader, std::any{}));
    }

    class Texture3DTextureCubeContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterTexture3DXnbReader();
            CNA::Internal::Xnb::RegisterTextureCubeXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters()};
    };
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, BothReadersAreRegisteredUnderRealFnaCanonicalNames)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.Texture3DReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.TextureCubeReader"));
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, TextureCubeReaderLoadsRealMonoGameFixtureEndToEnd)
{
    ContentManager cm(nullptr, "tests/assets/xnb/monogame/windows/uncompressed");
    cm.setGraphicsDevice(gd);

    // REMED-GFX-135: on a renderer with no cube storage, TextureCubeReader's own SetData call now
    // throws instead of silently discarding all 42 face/level uploads, so the load fails as a whole
    // rather than handing back a TextureCube that reports LevelCount 7 and holds nothing. That is
    // the content-pipeline consequence of the finding, asserted here rather than worked around.
    if (!CubeStorageSupported())
    {
        EXPECT_THROW((void)cm.Load<TextureCube>("SampleCube64DXT1Mips"),
                     System::NotSupportedException);
        return;
    }

    TextureCube cube = cm.Load<TextureCube>("SampleCube64DXT1Mips");

    EXPECT_EQ(cube.getSizeProperty(), 64);
    const bool preservesDxt1 = gd.GetRenderer().LoadsCompressedContentNativelyEXT()
        && gd.GetRenderer().IsCompressedCubeTransferFormatEXT(
            static_cast<int>(SurfaceFormat::Dxt1));
    EXPECT_EQ(cube.getFormatProperty(),
              preservesDxt1 ? SurfaceFormat::Dxt1 : SurfaceFormat::Color);
    EXPECT_EQ(cube.getLevelCountProperty(), 7);

    // Every face/level combination decoded without throwing and produced real pixel data
    // (a corrupted DXT1 decode would very likely produce an exception or all-zero output, not a
    // plausible non-degenerate image) -- spot-check level 0 (64x64) and the smallest levels (the
    // sub-4x4 DXT1 block-rounding edge cases, 2x2 and 1x1, each still exactly one 8-byte block).
    //
    // REMED-GFX-130: the readback half of this test is only meaningful where the renderer really
    // reads a cube face back. It used to be issued unconditionally, and the smallest-level check
    // was a bare EXPECT_NO_THROW that asserted nothing at all -- a renderer answering with the
    // shared layer's fabricated transparent-black face passed it. Both halves now assert the real
    // outcome for this renderer, and the sentinel proves the rejection path writes nothing.
    const Color sentinel(0xA5, 0xA5, 0xA5, 0xA5);
    std::vector<Color> level0(64 * 64, sentinel);
    Color onePixel = sentinel;
    if (CubeLevel0ReadbackSupported())
    {
        ASSERT_NO_THROW(cube.GetData(CubeMapFace::PositiveX, level0.data(),
                                     static_cast<int>(level0.size())));
        bool sawNonUniform = false;
        for (std::size_t i = 1; i < level0.size(); ++i)
        {
            if (level0[i].getPackedValueProperty() != level0[0].getPackedValueProperty())
            {
                sawNonUniform = true;
                break;
            }
        }
        EXPECT_TRUE(sawNonUniform) << "level 0 should not decode to a uniformly flat image";

        // The smallest mip level (1x1) is the sub-4x4 DXT1 block-rounding edge case. Every renderer
        // that stores cube faces at all now stores the whole declared chain (REMED-GFX-135 gave
        // Software the per-mip storage it was the last to be missing).
        if (CubeMipReadbackSupported())
        {
            ASSERT_NO_THROW(cube.GetData(CubeMapFace::NegativeZ, 6, nullptr, &onePixel, 0, 1));
            EXPECT_NE(onePixel.getPackedValueProperty(), sentinel.getPackedValueProperty())
                << "the 1x1 mip level should decode to a real texel";
        }
        else
        {
            EXPECT_THROW(cube.GetData(CubeMapFace::NegativeZ, 6, nullptr, &onePixel, 0, 1),
                         System::NotSupportedException);
            EXPECT_EQ(onePixel.getPackedValueProperty(), sentinel.getPackedValueProperty());
        }
    }
    else
    {
        EXPECT_THROW(cube.GetData(CubeMapFace::PositiveX, level0.data(),
                                  static_cast<int>(level0.size())),
                     System::NotSupportedException);
        EXPECT_EQ(level0[0].getPackedValueProperty(), sentinel.getPackedValueProperty());
    }
}

// SOFTWARE-275: FNA's TextureCubeReader constructs the serialized format and forwards every face
// byte array unchanged. CNA accepted only Color/DXT and converted every non-native-compressed
// payload through RGBA8, so no packed, integer, half or floating-point XNA cube could be loaded.
TEST_F(Texture3DTextureCubeContentTypeReaderTest,
       TextureCubeReaderPreservesEveryClassicUncompressedFormatAndExactBytes)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    constexpr std::array<SurfaceFormat, 15> formats{{
        SurfaceFormat::Color,
        SurfaceFormat::Bgr565,
        SurfaceFormat::Bgra5551,
        SurfaceFormat::Bgra4444,
        SurfaceFormat::Rgba1010102,
        SurfaceFormat::Rg32,
        SurfaceFormat::Rgba64,
        SurfaceFormat::Alpha8,
        SurfaceFormat::Single,
        SurfaceFormat::Vector2,
        SurfaceFormat::Vector4,
        SurfaceFormat::HalfSingle,
        SurfaceFormat::HalfVector2,
        SurfaceFormat::HalfVector4,
        SurfaceFormat::HdrBlendable,
    }};

    for (const SurfaceFormat format : formats)
    {
        SCOPED_TRACE(static_cast<int>(format));
        const std::size_t byteCount = static_cast<std::size_t>(
            Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(format));
        std::array<std::vector<std::uint8_t>, 6> expected;
        for (std::size_t face = 0; face < expected.size(); ++face)
        {
            expected[face].resize(byteCount);
            for (std::size_t index = 0; index < byteCount; ++index)
            {
                expected[face][index] = static_cast<std::uint8_t>(
                    0x21u + face * 23u + index * 11u + static_cast<std::size_t>(format));
            }
        }

        TextureCube cube = ReadHandConstructedCube(gd, format, expected);
        EXPECT_EQ(cube.getFormatProperty(), format);
        EXPECT_EQ(cube.getLevelCountProperty(), 1);
        for (std::size_t face = 0; face < expected.size(); ++face)
        {
            std::vector<std::uint8_t> actual(byteCount, 0xCDu);
            cube.GetData(static_cast<CubeMapFace>(face), actual.data(),
                         static_cast<int>(actual.size()));
            EXPECT_EQ(actual, expected[face]) << "face " << face;
        }
    }
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest,
       TextureCubeReaderPreservesEveryClassicCompressedFormatAndExactBlocks)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    constexpr std::array<SurfaceFormat, 3> formats{{
        SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5,
    }};
    for (const SurfaceFormat format : formats)
    {
        SCOPED_TRACE(static_cast<int>(format));
        const std::size_t byteCount = static_cast<std::size_t>(
            Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(format));
        std::array<std::vector<std::uint8_t>, 6> expected;
        for (std::size_t face = 0; face < expected.size(); ++face)
        {
            expected[face].resize(byteCount);
            for (std::size_t index = 0; index < byteCount; ++index)
            {
                expected[face][index] = static_cast<std::uint8_t>(
                    0x13u + face * 29u + index * 7u);
            }
        }

        TextureCube cube = ReadHandConstructedCube(gd, format, expected, 4);
        EXPECT_EQ(cube.getFormatProperty(), format);
        for (std::size_t face = 0; face < expected.size(); ++face)
        {
            std::vector<std::uint8_t> actual(byteCount, 0xCDu);
            cube.GetData(static_cast<CubeMapFace>(face), actual.data(),
                         static_cast<int>(actual.size()));
            EXPECT_EQ(actual, expected[face]) << "face " << face;
        }
    }
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest,
       TextureCubeReaderRejectsSignedNormalizedFormats)
{
    for (const SurfaceFormat format :
         {SurfaceFormat::NormalizedByte2, SurfaceFormat::NormalizedByte4})
    {
        SCOPED_TRACE(static_cast<int>(format));
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<int32_t>(format));
        writer.Write(static_cast<int32_t>(1));
        writer.Write(static_cast<int32_t>(1));
        writer.Flush();
        const auto serialized = stream.ToArray();

        ContentManager content;
        content.setGraphicsDevice(gd);
        System::IO::MemoryStream body(
            serialized.data(), static_cast<int32_t>(serialized.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.TextureCubeReader");
        ASSERT_NE(typeReader, nullptr);
        EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
    }
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, ContentManagerStronglyCachesTextureCubeUntilUnload)
{
    if (!CubeStorageSupported())
    {
        GTEST_SKIP() << "The active renderer has no cube texture storage.";
    }

    ContentManager cm(nullptr, "tests/assets/xnb/monogame/windows/uncompressed");
    cm.setGraphicsDevice(gd);

    std::weak_ptr<CNA::Internal::Renderers::ITextureCubeRenderer> cachedRenderer;
    {
        TextureCube first = cm.Load<TextureCube>("SampleCube64DXT1Mips");
        cachedRenderer = first.GetRenderer().weak_from_this();
    }

    ASSERT_FALSE(cachedRenderer.expired())
        << "ContentManager must own the loaded cube after caller wrappers are destroyed";
    const auto originalRenderer = cachedRenderer.lock();

    TextureCube cached = cm.Load<TextureCube>("SampleCube64DXT1Mips");
    EXPECT_EQ(originalRenderer.get(), &cached.GetRenderer());

    cm.Unload();
    TextureCube reloaded = cm.Load<TextureCube>("SampleCube64DXT1Mips");
    EXPECT_NE(originalRenderer.get(), &reloaded.GetRenderer());
}

// REMED-CONTENT-004: Texture3D is a documented, renderer-dependent capability -- Headless has no
// real GPU resource of any kind, and Software's Texture3D support is an explicit v1 scope boundary
// (plans/plan_software.md Boundaries). On a renderer that doesn't support it, reading now throws a clean
// System::NotSupportedException (from Texture3D's own constructor) instead of previously silently
// succeeding with all-zero pixel data.
TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderParsesHandConstructedBytesMatchingFnaByteOrder)
{
    // No real Texture3D .xnb fixture was found anywhere in the available library -- hand
    // constructed, verified field-by-field against FNA's Texture3DReader.cs's exact read order
    // (SurfaceFormat, width, height, depth, levelCount, then per-level [dataSize, data]).
    constexpr int32_t width = 2;
    constexpr int32_t height = 2;
    constexpr int32_t depth = 1;
    constexpr int32_t levelCount = 1;
    const std::vector<Color> pixels{
        Color(255, 0, 0, 255), Color(0, 255, 0, 255),
        Color(0, 0, 255, 255), Color(255, 255, 0, 255)};

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(width);
    writer.Write(height);
    writer.Write(depth);
    writer.Write(levelCount);
    writer.Write(static_cast<int32_t>(pixels.size() * 4)); // dataSize
    for (const Color& c : pixels)
    {
        writer.Write(c.getRProperty());
        writer.Write(c.getGProperty());
        writer.Write(c.getBProperty());
        writer.Write(c.getAProperty());
    }
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture3DReader");
    ASSERT_NE(typeReader, nullptr);

    if (!gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
    {
        EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), System::NotSupportedException);
        return;
    }

    std::any resultAny = typeReader->ReadUntyped(reader, std::any{});
    auto texture = std::any_cast<std::shared_ptr<Texture3D>>(resultAny);
    ASSERT_NE(texture, nullptr);

    EXPECT_EQ(texture->getWidthProperty(), width);
    EXPECT_EQ(texture->getHeightProperty(), height);
    EXPECT_EQ(texture->getDepthProperty(), depth);
    EXPECT_EQ(texture->getFormatProperty(), SurfaceFormat::Color);

    std::vector<Color> readBack(4, Color(0, 0, 0, 0));
    if (!VolumeReadbackSupported())
    {
        // The reader's own parsing is what this test is about, and it has been fully checked above;
        // only the round trip needs a renderer that can fetch voxels back.
        EXPECT_THROW((void)texture->GetData(readBack.data(), 4), System::NotSupportedException);
        return;
    }
    texture->GetData(readBack.data(), 4);
    for (std::size_t i = 0; i < pixels.size(); ++i)
    {
        EXPECT_EQ(readBack[i].getPackedValueProperty(), pixels[i].getPackedValueProperty()) << "pixel " << i;
    }
}

// SOFTWARE-274: FNA does not reinterpret a Texture3DReader payload as Color. It constructs the
// declared format and forwards the exact byte array for each mip level to Texture3D.SetData<byte>.
// CNA previously accepted only Color/DXT here and always produced a Color texture, making fourteen
// legal XNA HiDef volume formats impossible to load even though both parity renderers store them.
TEST_F(Texture3DTextureCubeContentTypeReaderTest,
       Texture3DReaderPreservesEveryClassicVolumeFormatAndExactBytes)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    struct FormatCase
    {
        SurfaceFormat format;
        std::size_t bytesPerVoxel;
    };
    constexpr std::array<FormatCase, 15> cases{{
        {SurfaceFormat::Color, 4u},
        {SurfaceFormat::Bgr565, 2u},
        {SurfaceFormat::Bgra5551, 2u},
        {SurfaceFormat::Bgra4444, 2u},
        {SurfaceFormat::Rgba1010102, 4u},
        {SurfaceFormat::Rg32, 4u},
        {SurfaceFormat::Rgba64, 8u},
        {SurfaceFormat::Alpha8, 1u},
        {SurfaceFormat::Single, 4u},
        {SurfaceFormat::Vector2, 8u},
        {SurfaceFormat::Vector4, 16u},
        {SurfaceFormat::HalfSingle, 2u},
        {SurfaceFormat::HalfVector2, 4u},
        {SurfaceFormat::HalfVector4, 8u},
        {SurfaceFormat::HdrBlendable, 8u},
    }};

    for (const FormatCase& formatCase : cases)
    {
        SCOPED_TRACE(static_cast<int>(formatCase.format));
        std::vector<std::uint8_t> expected(formatCase.bytesPerVoxel * 2u);
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            expected[index] = static_cast<std::uint8_t>(
                0x21u + index * 17u + static_cast<std::size_t>(formatCase.format));
        }

        const auto texture = ReadHandConstructedVolume(
            gd, formatCase.format, 2, 1, 1, {expected});
        ASSERT_NE(texture, nullptr);
        EXPECT_EQ(texture->getFormatProperty(), formatCase.format);
        EXPECT_EQ(texture->getLevelCountProperty(), 1);

        std::vector<std::uint8_t> actual(expected.size(), 0xCDu);
        texture->GetData(actual.data(), static_cast<int>(actual.size()));
        EXPECT_EQ(actual, expected);
    }
}

// SOFTWARE-274: the canonical XNB validator retained the old width/height-only volume mip rule
// after SOFTWARE-273 corrected Texture3D itself. A 1x1x8 authored volume therefore rejected its
// legal second through fourth levels before the resource constructor could see them.
TEST_F(Texture3DTextureCubeContentTypeReaderTest,
       Texture3DReaderAcceptsDepthDominantMipChainAndPreservesEveryLevel)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    const std::vector<std::vector<std::uint8_t>> expected{
        {0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u},
        {0x20u, 0x21u, 0x22u, 0x23u},
        {0x30u, 0x31u},
        {0x40u},
    };
    const auto texture = ReadHandConstructedVolume(
        gd, SurfaceFormat::Alpha8, 1, 1, 8, expected);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getFormatProperty(), SurfaceFormat::Alpha8);
    EXPECT_EQ(texture->getLevelCountProperty(), 4);

    int depth = 8;
    for (int level = 0; level < 4; ++level)
    {
        std::vector<std::uint8_t> actual(expected[static_cast<std::size_t>(level)].size(), 0xCDu);
        texture->GetData(level, 0, 0, 1, 1, 0, depth,
                         actual.data(), 0, static_cast<int>(actual.size()));
        EXPECT_EQ(actual, expected[static_cast<std::size_t>(level)]);
        depth = std::max(depth >> 1, 1);
    }
}

// REMED-CONTENT-003: TextureCubeReader was the one sibling among the three XNB texture readers
// missing this check -- a crafted declared byteCount undersized relative to faceSize*faceSize*4
// previously reached the unchecked pixel-unpacking loop, an out-of-bounds heap read. Ported
// verbatim from Texture2DReader's own identical, already-tested check.
TEST_F(Texture3DTextureCubeContentTypeReaderTest, TextureCubeReaderRejectsByteCountMismatchedWithSize)
{
    constexpr int32_t size = 2;
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(size);
    writer.Write(static_cast<int32_t>(1)); // levels
    writer.Write(static_cast<int32_t>(4)); // byteCount for face 0 level 0 -- should be 2*2*4=16, not 4
    writer.Write(static_cast<uint8_t>(0));
    writer.Write(static_cast<uint8_t>(0));
    writer.Write(static_cast<uint8_t>(0));
    writer.Write(static_cast<uint8_t>(0));
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.TextureCubeReader");
    ASSERT_NE(typeReader, nullptr);
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

// REMED-CONTENT-009: Texture3DReader's own decoded-byte-size check has one more factor than
// Texture2DReader's (width*height*depth*4 vs width*height*4), found to share the identical
// signed-int64-overflow UB shape during that task's root-cause sweep. This is the exact adversarial
// shape confirmed by UBSan pre-fix.
TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderAbsurdlyLargeDimensionsThrowContentLoadExceptionNotBadAlloc)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(static_cast<int32_t>(0x7FFFFFFF)); // width -- adversarially huge
    writer.Write(static_cast<int32_t>(0x7FFFFFFF)); // height -- adversarially huge
    writer.Write(static_cast<int32_t>(0x7FFFFFFF)); // depth -- adversarially huge
    writer.Write(static_cast<int32_t>(1));           // levelCount
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture3DReader");
    ASSERT_NE(typeReader, nullptr);
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderZeroWidthThrowsContentLoadException)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(static_cast<int32_t>(0)); // width -- zero, not just negative
    writer.Write(static_cast<int32_t>(4));
    writer.Write(static_cast<int32_t>(4));
    writer.Write(static_cast<int32_t>(1));
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture3DReader");
    ASSERT_NE(typeReader, nullptr);
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderZeroDepthThrowsContentLoadException)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(static_cast<int32_t>(4));
    writer.Write(static_cast<int32_t>(4));
    writer.Write(static_cast<int32_t>(0)); // depth -- zero, not just negative
    writer.Write(static_cast<int32_t>(1));
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture3DReader");
    ASSERT_NE(typeReader, nullptr);
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

// REMED-CONTENT-009: TextureCubeReader's own decoded-byte-size check (size*size*4) shares the
// identical signed-int64-overflow UB shape as Texture2DReader's -- confirmed during that task's
// root-cause sweep.
TEST_F(Texture3DTextureCubeContentTypeReaderTest, TextureCubeReaderAbsurdlyLargeSizeThrowsContentLoadExceptionNotBadAlloc)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(static_cast<int32_t>(0x7FFFFFFF)); // size -- adversarially huge
    writer.Write(static_cast<int32_t>(1));           // levels
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.TextureCubeReader");
    ASSERT_NE(typeReader, nullptr);
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, TextureCubeReaderZeroSizeThrowsContentLoadException)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
    writer.Write(static_cast<int32_t>(0)); // size -- zero, not just negative
    writer.Write(static_cast<int32_t>(1)); // levels
    writer.Flush();
    const auto buf = ms.ToArray();
    const std::string fields(reinterpret_cast<const char*>(buf.data()), buf.size());

    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream body(
        reinterpret_cast<const uint8_t*>(fields.data()), static_cast<int32_t>(fields.size()));
    ContentReader reader(&cm, &body, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.TextureCubeReader");
    ASSERT_NE(typeReader, nullptr);
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

TEST_F(Texture3DTextureCubeContentTypeReaderTest, Texture3DReaderRejectsCompressedSurfaceFormats)
{
    // XNA 4.0's exact HiDef volume-format table contains all fifteen uncompressed formats above,
    // but no block-compressed format. CNA used to accept DXT1/3/5 and reject Bgr565 instead.
    for (const SurfaceFormat format :
         {SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5})
    {
        SCOPED_TRACE(static_cast<int>(format));
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<int32_t>(format));
        writer.Write(static_cast<int32_t>(4));
        writer.Write(static_cast<int32_t>(4));
        writer.Write(static_cast<int32_t>(1));
        writer.Write(static_cast<int32_t>(1));
        writer.Flush();
        const auto serialized = stream.ToArray();

        ContentManager content;
        content.setGraphicsDevice(gd);
        System::IO::MemoryStream body(
            serialized.data(), static_cast<int32_t>(serialized.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.Texture3DReader");
        ASSERT_NE(typeReader, nullptr);
        EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
    }
}
