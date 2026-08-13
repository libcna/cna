// SPDX-License-Identifier: MS-PL
//
// plan_xnb.md XNB-46: before RegisterAllBuiltInXnbReaders() existed, no single call site
// registered every built-in .xnb ContentTypeReader -- a real game had to discover and call all
// thirteen individual Register*XnbReader()/RegisterXXXReaders() functions itself. This test proves
// the umbrella function is both complete (every canonical name Phase A-F/D3 implements ends up
// registered) and genuinely functional (a fresh ContentManager, with nothing registered by hand,
// can load a real fixture of every major asset category CNA supports).

#include <gtest/gtest.h>

#include "System/NotSupportedException.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"

using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureCube;
using Microsoft::Xna::Framework::Media::Song;

namespace
{
    class XnbBuiltInReaderRegistrationTest : public ::testing::Test
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

    constexpr const char* kUncompressedDir = "tests/assets/xnb/monogame/windows/uncompressed";
}

TEST_F(XnbBuiltInReaderRegistrationTest, RegistersEveryPrimitiveReader)
{
    static constexpr const char* kNames[] = {
        "Microsoft.Xna.Framework.Content.ByteReader",
        "Microsoft.Xna.Framework.Content.SByteReader",
        "Microsoft.Xna.Framework.Content.Int16Reader",
        "Microsoft.Xna.Framework.Content.UInt16Reader",
        "Microsoft.Xna.Framework.Content.Int32Reader",
        "Microsoft.Xna.Framework.Content.UInt32Reader",
        "Microsoft.Xna.Framework.Content.Int64Reader",
        "Microsoft.Xna.Framework.Content.UInt64Reader",
        "Microsoft.Xna.Framework.Content.SingleReader",
        "Microsoft.Xna.Framework.Content.DoubleReader",
        "Microsoft.Xna.Framework.Content.BooleanReader",
        "Microsoft.Xna.Framework.Content.CharReader",
        "Microsoft.Xna.Framework.Content.StringReader",
    };
    for (const char* name : kNames)
    {
        EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(name)) << name;
    }
}

TEST_F(XnbBuiltInReaderRegistrationTest, RegistersEveryMathReader)
{
    static constexpr const char* kNames[] = {
        "Microsoft.Xna.Framework.Content.Vector2Reader",
        "Microsoft.Xna.Framework.Content.Vector3Reader",
        "Microsoft.Xna.Framework.Content.Vector4Reader",
        "Microsoft.Xna.Framework.Content.MatrixReader",
        "Microsoft.Xna.Framework.Content.QuaternionReader",
        "Microsoft.Xna.Framework.Content.ColorReader",
        "Microsoft.Xna.Framework.Content.PlaneReader",
        "Microsoft.Xna.Framework.Content.PointReader",
        "Microsoft.Xna.Framework.Content.RectangleReader",
        "Microsoft.Xna.Framework.Content.BoundingBoxReader",
        "Microsoft.Xna.Framework.Content.BoundingSphereReader",
        "Microsoft.Xna.Framework.Content.BoundingFrustumReader",
        "Microsoft.Xna.Framework.Content.RayReader",
    };
    for (const char* name : kNames)
    {
        EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(name)) << name;
    }
}

TEST_F(XnbBuiltInReaderRegistrationTest, RegistersEveryOtherBuiltInReader)
{
    static constexpr const char* kNames[] = {
        "Microsoft.Xna.Framework.Content.DecimalReader",
        "Microsoft.Xna.Framework.Content.DateTimeReader",
        "Microsoft.Xna.Framework.Content.TimeSpanReader",
        "Microsoft.Xna.Framework.Content.CurveReader",
        "Microsoft.Xna.Framework.Content.Texture2DReader",
        "Microsoft.Xna.Framework.Content.Texture3DReader",
        "Microsoft.Xna.Framework.Content.TextureCubeReader",
        "Microsoft.Xna.Framework.Content.SpriteFontReader",
        "Microsoft.Xna.Framework.Content.SoundEffectReader",
        "Microsoft.Xna.Framework.Content.SongReader",
        "Microsoft.Xna.Framework.Content.AlphaTestEffectReader",
        "Microsoft.Xna.Framework.Content.BasicEffectReader",
        "Microsoft.Xna.Framework.Content.DualTextureEffectReader",
        "Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader",
        "Microsoft.Xna.Framework.Content.SkinnedEffectReader",
        "Microsoft.Xna.Framework.Content.VertexDeclarationReader",
        "Microsoft.Xna.Framework.Content.VertexBufferReader",
        "Microsoft.Xna.Framework.Content.IndexBufferReader",
        "Microsoft.Xna.Framework.Content.ModelReader",
        "Microsoft.Xna.Framework.Content.EffectReader", // known-unsupported placeholder (XNB-32A)
    };
    for (const char* name : kNames)
    {
        EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(name)) << name;
    }
}

TEST_F(XnbBuiltInReaderRegistrationTest, IsIdempotentWhenCalledMultipleTimes)
{
    EXPECT_NO_THROW(CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders());
    EXPECT_NO_THROW(CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders());
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.Texture2DReader"));
}

TEST_F(XnbBuiltInReaderRegistrationTest, FreshContentManagerLoadsATexture2DFixtureWithNoOtherSetup)
{
    ContentManager cm(nullptr, kUncompressedDir);
    cm.setGraphicsDevice(gd);
    Texture2D texture = cm.Load<Texture2D>("white-1");
    EXPECT_GT(texture.getWidthProperty(), 0);
    EXPECT_GT(texture.getHeightProperty(), 0);
}


// REMED-GFX-135: does THIS build's renderer actually store a cube face? Native 2D, ASCII, Canvas
// and DIRECTX3 create no cube resource at all and Headless stores no pixel data by design, so
// TextureCube::SetData -- and therefore every content path that uploads a cube -- now refuses
// deterministically instead of accepting the data and discarding it. Same constant and same
// reviewed renderer set as tests/Microsoft/Xna/Framework/Graphics/TextureCubeTests.cpp.
// plan_sokol.md SOKOL-27: SokolTextureCubeRenderer stores real cube pixels (see
// tests/Microsoft/Xna/Framework/Graphics/TextureCubeTests.cpp for the full contract).
// PortableGL keeps the same nullptr CreateTextureCube default -- no cube resource exists there
// either (docs/portablegl-renderer.md).
#if defined(CNA_RENDERER_SDL_RENDERER) || \
    defined(CNA_RENDERER_CANVAS) || defined(CNA_RENDERER_HTML_DOM) || \
    defined(CNA_RENDERER_FREEDIRECT) || defined(CNA_RENDERER_HEADLESS) || \
    defined(CNA_RENDERER_GDI) || defined(CNA_RENDERER_OPENVG) || \
    defined(CNA_RENDERER_PORTABLEGL)
constexpr bool kCubeStorageSupported = false;
#else
constexpr bool kCubeStorageSupported = true;
#endif

TEST_F(XnbBuiltInReaderRegistrationTest, FreshContentManagerLoadsATextureCubeFixtureWithNoOtherSetup)
{
    ContentManager cm(nullptr, kUncompressedDir);
    cm.setGraphicsDevice(gd);
    if (!kCubeStorageSupported)
    {
        EXPECT_THROW((void)cm.Load<TextureCube>("SampleCube64DXT1Mips"),
                     System::NotSupportedException);
        return;
    }
    TextureCube cube = cm.Load<TextureCube>("SampleCube64DXT1Mips");
    EXPECT_EQ(cube.getSizeProperty(), 64);
}

TEST_F(XnbBuiltInReaderRegistrationTest, FreshContentManagerLoadsAModelFixtureWithNoOtherSetup)
{
    // Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline rejects it
    // before this test's ModelReader round-trip is reached. Guarded per-test (not in the shared
    // fixture's SetUp()) so the SpriteFont/SoundEffect/Song/Texture2D fixtures in this same file,
    // none of which need a 3D pipeline, keep running everywhere.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    ContentManager cm(nullptr, kUncompressedDir);
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("BlenderDefaultCube");
    EXPECT_NE(model.getRootProperty(), nullptr);
    EXPECT_GT(model.getMeshesProperty().getCountProperty(), 0);
}

TEST_F(XnbBuiltInReaderRegistrationTest, FreshContentManagerLoadsASpriteFontFixtureWithNoOtherSetup)
{
    ContentManager cm(nullptr, kUncompressedDir);
    cm.setGraphicsDevice(gd);
    SpriteFont font = cm.Load<SpriteFont>("Default");
    EXPECT_GT(font.getCharactersProperty().size(), 0u);
}

TEST_F(XnbBuiltInReaderRegistrationTest, FreshContentManagerLoadsASoundEffectFixtureWithNoOtherSetup)
{
    ContentManager cm(nullptr, std::string(kUncompressedDir) + "/audio");
    cm.setGraphicsDevice(gd);
    SoundEffect effect = cm.Load<SoundEffect>("tone_mono_44khz_16bit");
    EXPECT_GT(effect.getDurationProperty().getTotalMillisecondsProperty(), 0.0);
}

TEST_F(XnbBuiltInReaderRegistrationTest, FreshContentManagerLoadsASongFixtureWithNoOtherSetup)
{
    ContentManager cm(nullptr, std::string(kUncompressedDir) + "/song");
    cm.setGraphicsDevice(gd);
    Song song = cm.Load<Song>("one_two_three");
    EXPECT_EQ(song.getNameProperty(), "one_two_three");
}
