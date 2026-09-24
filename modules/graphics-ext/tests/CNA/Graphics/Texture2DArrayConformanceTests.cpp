// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4_modern_graphics.md GL4-0037: a renderer-neutral live conformance suite for
// CNA::Graphics::Texture2DArray.
//
// Texture2DArrayTests proves the public class against a mock renderer: validation, tracking, the
// retention of the internal record. What no shared case did was drive a REAL renderer's array:
// only Vulkan's own examples uploaded, read back and sampled one. These cases ask every renderer
// that publishes the feature -- a non-zero MaxTextureArrayLayers -- for the same observable
// results, and a renderer that does not publish it skips with that reason:
//
//   * every advertised format keeps each layer, each mip and each sub-rectangle byte-exact, and a
//     write to one layer leaves the others untouched (a format the renderer's array factory
//     declines with NotSupportedException is reported, never Color);
//   * the published layer limit is real: that many layers are created, one more is refused;
//   * each layer reaches a ShaderEffect through texture-array unit 0, through one portable package
//     (GLSL ES, desktop GLSL, and SPIR-V/WGSL at set 1 binding 16);
//   * the binding keeps the renderer's array alive after the public object is disposed, until the
//     unit is cleared, and an array that outlives its device is released without harm.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/Texture2DArray.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "System/NotSupportedException.hpp"
#include "EngineTestSupport.hpp"
#include "TextureArrayShaderPackage.generated.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using CNA::Graphics::ShaderBindingRequirementEXT;
using CNA::Graphics::ShaderBindingTypeEXT;
using CNA::Graphics::ShaderCodeEXT;
using CNA::Graphics::ShaderPackageEXT;
using CNA::Graphics::Texture2DArray;
using CNA::Graphics::Texture2DArrayDescriptor;
using CNA::Graphics::Texture2DArrayUsage;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace {

    namespace Package = CNA::Tests::TextureArray;

    constexpr Texture2DArrayUsage kTransferUsage =
        Texture2DArrayUsage::Sampled | Texture2DArrayUsage::TransferSource |
        Texture2DArrayUsage::TransferDestination;

    /// The published layer limit, or 0 where the renderer publishes no texture arrays.
    std::uint64_t ArrayLayerLimit(const GraphicsDevice& device)
    {
        const CNA::RendererLimitValue limit =
            device.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureArrayLayers);
        return limit.known ? limit.value : 0;
    }

    bool AdvertisesTransfers(const GraphicsDevice& device, const SurfaceFormat format)
    {
        const auto required = static_cast<CNA::RendererFormatUsage>(
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::Sampled) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::Mipmapped));
        return device.GetRendererSurfaceFormatSupportEXT(format).Supports(required);
    }

    /// Bytes of a width x height rectangle of @p format: whole 4x4 blocks for compressed formats.
    std::size_t RectangleBytes(const SurfaceFormat format, const int width, const int height)
    {
        const bool compressed = Texture::GetBlockSizeSquaredEXT(format) != 1;
        const std::size_t columns = compressed ? static_cast<std::size_t>((width + 3) / 4)
                                               : static_cast<std::size_t>(width);
        const std::size_t rows = compressed ? static_cast<std::size_t>((height + 3) / 4)
                                            : static_cast<std::size_t>(height);
        return columns * rows * static_cast<std::size_t>(Texture::GetFormatSizeEXT(format));
    }

    /// @p bytes bytes of @p format whose values survive any exact transfer: finite floats and
    /// halves, signed-normalised bytes that avoid -128 (which an API may store as -127), and
    /// anything at all for the other formats, compressed blocks included.
    std::vector<std::uint8_t> Pattern(const SurfaceFormat format, const std::size_t bytes,
                                      const int seed)
    {
        std::vector<std::uint8_t> out(bytes);
        switch (format)
        {
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
                for (std::size_t i = 0; i < bytes / 4; ++i)
                {
                    const float value =
                        static_cast<float>(static_cast<int>(i) + seed) * 0.25f - 3.0f;
                    std::memcpy(out.data() + i * 4, &value, 4);
                }
                break;
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                for (std::size_t i = 0; i < bytes / 2; ++i)
                {
                    const std::uint16_t half = static_cast<std::uint16_t>(
                        0x3C00u + ((i * 37u + static_cast<std::size_t>(seed)) % 0x3FFu));
                    std::memcpy(out.data() + i * 2, &half, 2);
                }
                break;
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
                for (std::size_t i = 0; i < bytes; ++i)
                    out[i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(
                        static_cast<int>((i * 37 + static_cast<std::size_t>(seed)) % 255) - 127));
                break;
            default:
                for (std::size_t i = 0; i < bytes; ++i)
                    out[i] = static_cast<std::uint8_t>(i * 29 + static_cast<std::size_t>(seed));
                break;
        }
        return out;
    }

    ShaderPackageEXT ArrayPackage()
    {
        const auto words = [](const std::uint32_t* begin, std::size_t byteSize) {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(begin);
            return std::vector<std::uint8_t>(bytes, bytes + byteSize);
        };
        using CNA::ShaderLanguageEXT;
        using CNA::ShaderStageEXT;
        return ShaderPackageEXT(
            {
                ShaderCodeEXT(ShaderLanguageEXT::GlslEs, ShaderStageEXT::Vertex, "main",
                              "array.es.vert.glsl", std::string(Package::kEsVertexSource)),
                ShaderCodeEXT(ShaderLanguageEXT::GlslEs, ShaderStageEXT::Fragment, "main",
                              "array.es.frag.glsl", std::string(Package::kEsFragmentSource)),
                ShaderCodeEXT(ShaderLanguageEXT::GlslDesktop, ShaderStageEXT::Vertex, "main",
                              "array.desktop.vert.glsl",
                              std::string(Package::kDesktopVertexSource)),
                ShaderCodeEXT(ShaderLanguageEXT::GlslDesktop, ShaderStageEXT::Fragment, "main",
                              "array.desktop.frag.glsl",
                              std::string(Package::kDesktopFragmentSource)),
                ShaderCodeEXT(ShaderLanguageEXT::SpirV, ShaderStageEXT::Vertex, "main",
                              "array.vulkan.vert.glsl",
                              words(Package::kVulkanVertexSpirV,
                                    Package::kVulkanVertexSpirVByteSize)),
                ShaderCodeEXT(ShaderLanguageEXT::SpirV, ShaderStageEXT::Fragment, "main",
                              "array.vulkan.frag.glsl",
                              words(Package::kVulkanFragmentSpirV,
                                    Package::kVulkanFragmentSpirVByteSize)),
                ShaderCodeEXT(ShaderLanguageEXT::Wgsl, ShaderStageEXT::Vertex, "main",
                              "array.vulkan.vert.glsl -> wgsl",
                              std::string(Package::kVulkanVertexWgsl)),
                ShaderCodeEXT(ShaderLanguageEXT::Wgsl, ShaderStageEXT::Fragment, "main",
                              "array.vulkan.frag.glsl -> wgsl",
                              std::string(Package::kVulkanFragmentWgsl)),
            },
            {ShaderStageEXT::Vertex, ShaderStageEXT::Fragment},
            {ShaderBindingRequirementEXT("uArray", 0, ShaderBindingTypeEXT::SampledTexture2DArray,
                                         ShaderStageEXT::Fragment)});
    }

    /// Solid texels of @p colour, @p count of them, in Color's byte order.
    std::vector<std::uint8_t> Solid(const Color& colour, const std::size_t count)
    {
        std::vector<std::uint8_t> bytes(count * 4);
        for (std::size_t i = 0; i < count; ++i)
        {
            bytes[i * 4 + 0] = colour.getRProperty();
            bytes[i * 4 + 1] = colour.getGProperty();
            bytes[i * 4 + 2] = colour.getBProperty();
            bytes[i * 4 + 3] = colour.getAProperty();
        }
        return bytes;
    }

    /// Draws one full-target quad sampling @p layer of the array bound to @p effect's unit 0 and
    /// returns the target's packed pixels.
    std::vector<std::uint32_t> DrawLayer(GraphicsDevice& device, ShaderEffect& effect,
                                         RenderTarget2D& target, const int layer)
    {
        const float z = static_cast<float>(layer);
        const std::array<std::array<float, 3>, 6> corners{{
            {-1, -1, z}, {1, -1, z}, {1, 1, z}, {-1, -1, z}, {1, 1, z}, {-1, 1, z}}};
        VertexBuffer quad(device,
                          VertexDeclaration(12, {VertexElement(0, VertexElementFormat::Vector3,
                                                               VertexElementUsage::Position, 0)}),
                          6, BufferUsage::None);
        quad.SetDataRaw(corners.data(), 6, 12);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        // Point sampling: the arrays here do not declare Filterable usage, and a renderer may refuse
        // linear or mip filtering of one that does not (Vulkan does).
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        effect.Apply();
        device.SetVertexBuffer(&quad);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> pixels(static_cast<std::size_t>(target.getWidthProperty()) *
                                  static_cast<std::size_t>(target.getHeightProperty()));
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        std::vector<std::uint32_t> packed;
        packed.reserve(pixels.size());
        for (const Color& pixel : pixels) packed.push_back(pixel.getPackedValueProperty());
        return packed;
    }

    const std::array<Color, 3> kLayerColours{Color::Red, Color::Lime, Color::Blue};

} // namespace

TEST(Texture2DArrayConformance, EveryAdvertisedFormatKeepsItsLayersMipsAndRectanglesExact)
{
    CnaTest::EngineLayer::HiDefDevice gd;
    if (ArrayLayerLimit(gd) < 3)
        GTEST_SKIP() << "this renderer publishes no texture arrays (MaxTextureArrayLayers "
                     << ArrayLayerLimit(gd) << ")";

    constexpr int kWidth = 8;
    constexpr int kHeight = 4;
    constexpr int kLayers = 3;
    int formatsChecked = 0;
    std::string checked;
    std::string declined;
    for (int ordinal = 0; ordinal <= static_cast<int>(SurfaceFormat::UShortEXT); ++ordinal)
    {
        const auto format = static_cast<SurfaceFormat>(ordinal);
        if (!AdvertisesTransfers(gd, format)) continue;
        SCOPED_TRACE("SurfaceFormat " + std::to_string(ordinal));
        // Format usages are published per format, not per resource kind, so a renderer whose
        // answer is the union of its Texture2D and StorageTexture2D storage can advertise a format
        // its array factory then declines. The public class turns that into NotSupportedException,
        // its documented refusal; any other failure -- and a declined Color -- is a defect.
        std::unique_ptr<Texture2DArray> created;
        try
        {
            created = std::make_unique<Texture2DArray>(
                gd, Texture2DArrayDescriptor(kWidth, kHeight, kLayers, 2, format,
                                             kTransferUsage));
        }
        catch (const System::NotSupportedException& error)
        {
            EXPECT_NE(format, SurfaceFormat::Color) << error.what();
            declined += (declined.empty() ? "" : " ") + std::to_string(ordinal);
            continue;
        }
        Texture2DArray& array = *created;

        // Every layer of both levels, each with its own bytes.
        const std::size_t level0Bytes = RectangleBytes(format, kWidth, kHeight);
        const std::size_t level1Bytes = RectangleBytes(format, kWidth / 2, kHeight / 2);
        std::array<std::vector<std::uint8_t>, kLayers> level0;
        std::array<std::vector<std::uint8_t>, kLayers> level1;
        for (int layer = 0; layer < kLayers; ++layer)
        {
            level0[static_cast<std::size_t>(layer)] = Pattern(format, level0Bytes, 11 * layer + 1);
            level1[static_cast<std::size_t>(layer)] = Pattern(format, level1Bytes, 13 * layer + 5);
            array.setData(layer, 0, nullptr, level0[static_cast<std::size_t>(layer)].data(),
                          level0Bytes);
            array.setData(layer, 1, nullptr, level1[static_cast<std::size_t>(layer)].data(),
                          level1Bytes);
        }

        // A sub-rectangle of layer 1, level 0: the right half, which is whole 4x4 blocks too.
        const Rectangle rightHalf(kWidth / 2, 0, kWidth / 2, kHeight);
        const std::size_t halfBytes = RectangleBytes(format, kWidth / 2, kHeight);
        const std::vector<std::uint8_t> half = Pattern(format, halfBytes, 97);
        array.setData(1, 0, &rightHalf, half.data(), halfBytes);

        // What layer 1 must now hold: its left half as written, its right half replaced.
        std::vector<std::uint8_t> expectedLayer1 = level0[1];
        const bool compressed = Texture::GetBlockSizeSquaredEXT(format) != 1;
        const std::size_t rows = compressed ? static_cast<std::size_t>(kHeight / 4)
                                            : static_cast<std::size_t>(kHeight);
        const std::size_t rowBytes = level0Bytes / rows;
        const std::size_t halfRowBytes = halfBytes / rows;
        for (std::size_t row = 0; row < rows; ++row)
            std::memcpy(expectedLayer1.data() + row * rowBytes + (rowBytes - halfRowBytes),
                        half.data() + row * halfRowBytes, halfRowBytes);

        for (int layer = 0; layer < kLayers; ++layer)
        {
            std::vector<std::uint8_t> back(level0Bytes, 0xCD);
            array.getData(layer, 0, nullptr, back.data(), back.size());
            EXPECT_EQ(layer == 1 ? expectedLayer1 : level0[static_cast<std::size_t>(layer)], back)
                << "level 0 of layer " << layer;
            std::vector<std::uint8_t> backMip(level1Bytes, 0xCD);
            array.getData(layer, 1, nullptr, backMip.data(), backMip.size());
            EXPECT_EQ(level1[static_cast<std::size_t>(layer)], backMip) << "level 1 of layer "
                                                                        << layer;
        }
        std::vector<std::uint8_t> backHalf(halfBytes, 0xCD);
        array.getData(1, 0, &rightHalf, backHalf.data(), backHalf.size());
        EXPECT_EQ(half, backHalf) << "the sub-rectangle read back on its own";

        ++formatsChecked;
        checked += (checked.empty() ? "" : " ") + std::to_string(ordinal);
    }
    std::printf("[TEXTURE2DARRAY] %d format(s) round-tripped exactly: %s; declined at creation: "
                "%s\n",
                formatsChecked, checked.c_str(), declined.empty() ? "none" : declined.c_str());
    EXPECT_TRUE(AdvertisesTransfers(gd, SurfaceFormat::Color))
        << "a renderer that publishes texture arrays must at least transfer Color";
    EXPECT_GT(formatsChecked, 0);
}

TEST(Texture2DArrayConformance, ThePublishedLayerLimitIsReal)
{
    CnaTest::EngineLayer::HiDefDevice gd;
    const std::uint64_t limit = ArrayLayerLimit(gd);
    if (limit == 0) GTEST_SKIP() << "this renderer publishes no texture arrays";
    ASSERT_LE(limit, UINT64_C(65536)) << "an implausible layer limit";

    // A 1x1 array of exactly the published layer count is created and holds its last layer.
    const int layers = static_cast<int>(limit);
    Texture2DArray full(gd, Texture2DArrayDescriptor(1, 1, layers, 1, SurfaceFormat::Color,
                                                     kTransferUsage));
    const std::vector<std::uint8_t> texel = Solid(Color::Lime, 1);
    full.setData(layers - 1, 0, nullptr, texel.data(), texel.size());
    std::vector<std::uint8_t> back(4, 0);
    full.getData(layers - 1, 0, nullptr, back.data(), back.size());
    EXPECT_EQ(texel, back);

    // One more is refused before the renderer is asked.
    EXPECT_THROW(Texture2DArray(gd, Texture2DArrayDescriptor(1, 1, layers + 1, 1,
                                                             SurfaceFormat::Color,
                                                             kTransferUsage)),
                 System::NotSupportedException);
}

TEST(Texture2DArrayConformance, EachLayerReachesAShaderEffectThroughUnitZero)
{
    CnaTest::EngineLayer::HiDefDevice gd;
    if (ArrayLayerLimit(gd) < 3) GTEST_SKIP() << "this renderer publishes no texture arrays";
    const ShaderPackageEXT package = ArrayPackage();
    const auto selection = package.selectFor(gd);
    if (!selection.isUsable()) GTEST_SKIP() << selection.getDiagnostic();
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    Texture2DArray array(gd, Texture2DArrayDescriptor(2, 2, 3, 1, SurfaceFormat::Color,
                                                      Texture2DArrayUsage::Sampled |
                                                          Texture2DArrayUsage::TransferDestination));
    for (int layer = 0; layer < 3; ++layer)
    {
        const std::vector<std::uint8_t> texels =
            Solid(kLayerColours[static_cast<std::size_t>(layer)], 4);
        array.setData(layer, 0, nullptr, texels.data(), texels.size());
    }
    ShaderEffect effect(gd, package);
    ASSERT_TRUE(effect.IsEffectValid());
    effect.SetTextureArrayEXT(0, array);
    RenderTarget2D target(gd, 4, 4);

    for (int layer = 0; layer < 3; ++layer)
    {
        const std::uint32_t expected =
            kLayerColours[static_cast<std::size_t>(layer)].getPackedValueProperty();
        for (const std::uint32_t pixel : DrawLayer(gd, effect, target, layer))
            EXPECT_EQ(expected, pixel) << "sampling layer " << layer;
    }
}

TEST(Texture2DArrayConformance, ABindingOutlivesTheDisposedPublicArrayUntilCleared)
{
    CnaTest::EngineLayer::HiDefDevice gd;
    if (ArrayLayerLimit(gd) < 2) GTEST_SKIP() << "this renderer publishes no texture arrays";
    const ShaderPackageEXT package = ArrayPackage();
    const auto selection = package.selectFor(gd);
    if (!selection.isUsable()) GTEST_SKIP() << selection.getDiagnostic();
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    auto array = std::make_unique<Texture2DArray>(
        gd, Texture2DArrayDescriptor(2, 2, 2, 1, SurfaceFormat::Color,
                                     Texture2DArrayUsage::Sampled |
                                         Texture2DArrayUsage::TransferDestination));
    for (int layer = 0; layer < 2; ++layer)
    {
        const std::vector<std::uint8_t> texels =
            Solid(kLayerColours[static_cast<std::size_t>(layer)], 4);
        array->setData(layer, 0, nullptr, texels.data(), texels.size());
    }
    ShaderEffect effect(gd, package);
    ASSERT_TRUE(effect.IsEffectValid());
    effect.SetTextureArrayEXT(0, *array);
    array->Dispose();
    array.reset();   // the public object is gone; the effect keeps the renderer's array

    RenderTarget2D target(gd, 4, 4);
    for (const std::uint32_t pixel : DrawLayer(gd, effect, target, 1))
        EXPECT_EQ(kLayerColours[1].getPackedValueProperty(), pixel)
            << "the binding did not keep the disposed array's storage";
    EXPECT_NO_THROW(effect.ClearTextureArrayEXT(0));
}

TEST(Texture2DArrayConformance, AnArrayOutlivingItsDeviceIsReleasedSafely)
{
    auto gd = std::make_unique<CnaTest::EngineLayer::HiDefDevice>();
    if (ArrayLayerLimit(*gd) < 2) GTEST_SKIP() << "this renderer publishes no texture arrays";
    auto array = std::make_unique<Texture2DArray>(
        *gd, Texture2DArrayDescriptor(4, 4, 2, 2, SurfaceFormat::Color, kTransferUsage));
    const std::vector<std::uint8_t> texels = Solid(Color::Red, 16);
    array->setData(1, 0, nullptr, texels.data(), texels.size());
    gd.reset();                          // the device -- and its context or queue -- first
    EXPECT_NO_THROW(array.reset());      // then the array: nothing may reach a dead device
}

#endif // CNA_CNAEXT
