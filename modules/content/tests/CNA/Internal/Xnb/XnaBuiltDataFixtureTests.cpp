// SPDX-License-Identifier: MS-PL
//
// TQ-033: the texture, Curve, collection and primitive readers against .xnb files the genuine
// XNA 4.0 pipeline built (tests/reference/xna40/differential; sources in tests/assets/xna40,
// recipes in tools/xna-pipeline-oracle/differential/corpus.json). Every expected value is a field
// of those files, decoded by hand per the reader IL:
//   TextureCubeReader.Read IL_0000-006d: format, size, levels, then face-major (face 0..5 outer,
//     level inner) Int32 size + bytes, SetData(face, level, null, ...);
//   Texture3DReader.Read IL_0000-008e: format, width, height, depth, levels, then per level the
//     whole box SetData(level, 0, 0, w, h, 0, d), each extent halved after it;
//   Texture2DReader.Read IL_0000-0067: format, width, height, levels (mipmap = levels > 1);
//   CurveReader.Read (Microsoft.Xna.Framework.dll) IL_000d-0068: PreLoop, PostLoop, count, then per key position,
//     value, tangentIn, tangentOut, continuity;
//   ListReader`1.Read / DictionaryReader`2.Read: Int32 count, then ReadObject<T>(elementReader) --
//     value-type elements raw, reference-type elements behind their own reader index.

#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveLoopType;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    Bytes ReadDifferentialFixture(const std::string& name)
    {
        const std::string path = "tests/reference/xna40/differential/" + name + ".xnb";
        std::ifstream stream(path, std::ios::binary);
        EXPECT_TRUE(stream.good()) << path << " (the tests run from the source root)";
        return Bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    }

    class InMemoryContentManager final : public ContentManager
    {
    public:
        InMemoryContentManager() : ContentManager(nullptr, "Content") { RouteLoadsThroughOpenStreamEXT(true); }

        std::map<std::string, Bytes> files;

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName) override
        {
            const auto found = files.find(assetName);
            if (found == files.end()) { throw ContentLoadException("no asset '" + assetName + "'"); }
            return std::make_unique<System::IO::MemoryStream>(
                found->second.data(), static_cast<std::int32_t>(found->second.size()), false);
        }
    };

    class XnaBuiltDataFixtureTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            manager.setGraphicsDevice(device);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        template <typename T>
        T LoadFixture(const std::string& name)
        {
            manager.files[name] = ReadDifferentialFixture(name);
            return manager.Load<T>(name);
        }

        // The texture fixtures are HiDef builds (header flags 0x01).
        GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters::CreateWindowedEXT(800, 480)};
        InMemoryContentManager manager;
    };
}

// probe_cube.dds: a 4x4 Color cube, one level, each face a different ramp. The first texel of
// face f is (0x25 (f+1), 0x07 (f+1), f+1, 255) in the file's face order.
TEST_F(XnaBuiltDataFixtureTest, CubeFacesArriveInTheFilesFaceOrder)
{
    if (CNA_RENDERER_IS(CNA::Testing::Renderers::Stub)) GTEST_SKIP() << "the STUB renderer stores no cube faces";
    const TextureCube cube = LoadFixture<TextureCube>("texture_dds_cube");
    EXPECT_EQ(cube.getSizeProperty(), 4);
    EXPECT_EQ(cube.getLevelCountProperty(), 1);
    EXPECT_EQ(cube.getFormatProperty(), SurfaceFormat::Color);
    const Color firstTexel[6] = {
        Color(0x25, 0x07, 0x01, 0xff), Color(0x4a, 0x0e, 0x02, 0xff), Color(0x6f, 0x15, 0x03, 0xff),
        Color(0x94, 0x1c, 0x04, 0xff), Color(0xb9, 0x23, 0x05, 0xff), Color(0xde, 0x2a, 0x06, 0xff)};
    for (int face = 0; face < 6; ++face)
    {
        Color texels[16];
        cube.GetData(static_cast<CubeMapFace>(face), texels, 16);
        EXPECT_EQ(texels[0], firstTexel[face]) << "face " << face;
    }
    // Last texel of face 5: (0x02, 0xa2, 0x7e) -- the red channel wrapped past 255.
    Color lastFace[16];
    cube.GetData(CubeMapFace::NegativeZ, lastFace, 16);
    EXPECT_EQ(lastFace[15], Color(0x02, 0xa2, 0x7e, 0xff));
}

// probe_volume.dds: 4x4x4 Color, one level; slice z starts with the same ramp as cube face z.
TEST_F(XnaBuiltDataFixtureTest, VolumeSlicesArriveInDepthOrder)
{
    // HEADLESS and STUB keep no real volume storage and refuse a Texture3D by capability.
    if (!device.SupportsCapability(CNA::GraphicsCapability::Texture3D))
        GTEST_SKIP() << "renderer has no real volume (3D) texture storage";
    const std::shared_ptr<Texture3D> volume = LoadFixture<std::shared_ptr<Texture3D>>("texture_dds_volume");
    ASSERT_NE(volume, nullptr);
    EXPECT_EQ(volume->getWidthProperty(), 4);
    EXPECT_EQ(volume->getHeightProperty(), 4);
    EXPECT_EQ(volume->getDepthProperty(), 4);
    EXPECT_EQ(volume->getLevelCountProperty(), 1);
    EXPECT_EQ(volume->getFormatProperty(), SurfaceFormat::Color);
    Color voxels[64];
    volume->GetData(voxels, 64);
    EXPECT_EQ(voxels[0], Color(0x25, 0x07, 0x01, 0xff));
    EXPECT_EQ(voxels[16], Color(0x4a, 0x0e, 0x02, 0xff));
    EXPECT_EQ(voxels[32], Color(0x6f, 0x15, 0x03, 0xff));
    EXPECT_EQ(voxels[48], Color(0x94, 0x1c, 0x04, 0xff));
    EXPECT_EQ(voxels[63], Color(0xb8, 0x94, 0x7c, 0xff));
}

// mip_impulse16: 16x16 Color with the pipeline's full five-level chain (16, 8, 4, 2, 1). Each
// level's first texel, from the file: 0, 1, 13, 5, 4 (grey, opaque).
TEST_F(XnaBuiltDataFixtureTest, MipChainLevelsArriveInLevelOrder)
{
    const Texture2D texture = LoadFixture<Texture2D>("mip_impulse16");
    EXPECT_EQ(texture.getWidthProperty(), 16);
    EXPECT_EQ(texture.getHeightProperty(), 16);
    EXPECT_EQ(texture.getLevelCountProperty(), 5);
    EXPECT_EQ(texture.getFormatProperty(), SurfaceFormat::Color);
    const int side[5] = {16, 8, 4, 2, 1};
    const std::uint8_t grey[5] = {0, 1, 13, 5, 4};
    for (int level = 0; level < 5; ++level)
    {
        std::vector<Color> texels(static_cast<std::size_t>(side[level] * side[level]));
        texture.GetData(level, nullptr, texels.data(), 0, static_cast<int>(texels.size()));
        const int g = grey[level];
        EXPECT_EQ(texels[0], Color(g, g, g, 0xff)) << "level " << level;
    }
}

// xml_curve.xml: PreLoop Constant, PostLoop Oscillate, keys (0,0) (1,10) (2,5), tangents 0, Smooth.
TEST_F(XnaBuiltDataFixtureTest, CurveKeepsItsLoopTypesAndKeys)
{
    const Curve curve = LoadFixture<Curve>("xml_framework_curve");
    EXPECT_EQ(curve.getPreLoopProperty(), CurveLoopType::Constant);
    EXPECT_EQ(curve.getPostLoopProperty(), CurveLoopType::Oscillate);
    const auto& keys = curve.getKeysProperty();
    ASSERT_EQ(keys.getCountProperty(), 3);
    const float positions[3] = {0, 1, 2};
    const float values[3] = {0, 10, 5};
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(keys[i].getPositionProperty(), positions[i]) << i;
        EXPECT_EQ(keys[i].getValueProperty(), values[i]) << i;
        EXPECT_EQ(keys[i].getTangentInProperty(), 0.0f) << i;
        EXPECT_EQ(keys[i].getTangentOutProperty(), 0.0f) << i;
        EXPECT_EQ(keys[i].getContinuityProperty(), CurveContinuity::Smooth) << i;
    }
}

TEST_F(XnaBuiltDataFixtureTest, StringToInt32DictionaryHoldsEveryPair)
{
    const auto dictionary =
        LoadFixture<std::unordered_map<std::string, std::int32_t>>("xml_generic_dictionary");
    EXPECT_EQ(dictionary, (std::unordered_map<std::string, std::int32_t>{
                              {"alpha", 1}, {"beta", 2}, {"gamma", 3}}));
}

TEST_F(XnaBuiltDataFixtureTest, RectangleListKeepsElementOrderAndFieldOrder)
{
    const auto rectangles = LoadFixture<std::vector<Rectangle>>("xml_generic_rectangle_list");
    EXPECT_EQ(rectangles, (std::vector<Rectangle>{
                              Rectangle(0, 0, 16, 16), Rectangle(16, 0, 8, 8), Rectangle(24, 8, 32, 4)}));
}

TEST_F(XnaBuiltDataFixtureTest, StringListAndPrimitiveRootsLoad)
{
    EXPECT_EQ(LoadFixture<std::vector<std::string>>("xml_intermediate_passthrough"),
              (std::vector<std::string>{"first", "second", "third"}));
    EXPECT_EQ(LoadFixture<std::int32_t>("xml_primitive_int"), 1234567);
    EXPECT_EQ(LoadFixture<Vector3>("xml_framework_vector3"), Vector3(1.5f, -2.25f, 3.0f));
}
