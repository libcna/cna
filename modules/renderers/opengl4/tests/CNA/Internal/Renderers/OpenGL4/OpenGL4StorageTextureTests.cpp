// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0030: StorageTexture2D on OpenGL4.
//
// The renderer-neutral conformance case writes one Color image from compute and reads it back.
// These cases cover what it does not, on this renderer: every storage format's exact bytes through
// upload and readback (including a sub-rectangle of a smaller mip), a float image written by
// compute and read back bit-exactly, and a storage texture SAMPLED by a ShaderEffect -- the
// graphics-side binding (ShaderEffect::SetStorageTextureEXT) no shared case draws through.

#if defined(CNA_RENDERER_OPENGL4) && defined(CNA_CNAEXT)

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/StorageTexture2D.hpp"
#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
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

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using CNA::Graphics::ComputeShader;
    using CNA::Graphics::StorageTexture2D;
    using CNA::Graphics::StorageTexture2DDescriptor;
    using CNA::Graphics::StorageTexture2DUsage;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;

    constexpr StorageTexture2DUsage kFullUsage =
        StorageTexture2DUsage::StorageRead | StorageTexture2DUsage::StorageWrite |
        StorageTexture2DUsage::Sampled | StorageTexture2DUsage::TransferSource |
        StorageTexture2DUsage::TransferDestination;

    std::unique_ptr<GraphicsDevice> HiDefDevice()
    {
        return std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
                                                GraphicsProfile::HiDef, PresentationParameters());
    }

    bool Advertises(GraphicsDevice& device, const SurfaceFormat format)
    {
        const auto required = static_cast<CNA::RendererFormatUsage>(
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageRead) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::Sampled) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferSource) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::TransferDestination) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::Mipmapped));
        return device.GetRendererSurfaceFormatSupportEXT(format).Supports(required);
    }

    /// @p texels texels of @p format whose every value survives any exact transfer: finite floats
    /// and halves, and signed-normalised bytes that avoid -128 (which GL may store as -127).
    std::vector<std::uint8_t> Pattern(const SurfaceFormat format, const std::size_t texels,
                                      const int seed)
    {
        const std::size_t size = static_cast<std::size_t>(Texture::GetFormatSizeEXT(format));
        std::vector<std::uint8_t> bytes(texels * size);
        switch (format)
        {
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
                for (std::size_t i = 0; i < bytes.size() / 4; ++i)
                {
                    const float value = static_cast<float>(static_cast<int>(i) + seed) * 0.25f - 3.0f;
                    std::memcpy(bytes.data() + i * 4, &value, 4);
                }
                break;
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                for (std::size_t i = 0; i < bytes.size() / 2; ++i)
                {
                    // 1.0 .. 2.0 with a varying mantissa: every one a finite normal half.
                    const std::uint16_t half =
                        static_cast<std::uint16_t>(0x3C00u + ((i * 37u + seed) % 0x3FFu));
                    std::memcpy(bytes.data() + i * 2, &half, 2);
                }
                break;
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
                for (std::size_t i = 0; i < bytes.size(); ++i)
                    bytes[i] = static_cast<std::uint8_t>(
                        static_cast<std::int8_t>(static_cast<int>((i * 37 + seed) % 255) - 127));
                break;
            default:
                for (std::size_t i = 0; i < bytes.size(); ++i)
                    bytes[i] = static_cast<std::uint8_t>(i * 29 + static_cast<std::size_t>(seed));
                break;
        }
        return bytes;
    }
}

TEST(OpenGL4StorageTexture, EveryStorageFormatRoundTripsItsExactBytes)
{
    auto device = HiDefDevice();
    if (device->GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    ASSERT_TRUE(device->SupportsRendererFeatureEXT(CNA::RendererFeature::ComputeImageBinding));

    // Vulkan's fifteen storage formats. The two CNA-only single-channel ones are refused here by
    // name, because OpenGL4's Texture2D does not store them and StorageTexture2D requires
    // Texture2D storage of its format.
    const std::array<SurfaceFormat, 15> formats{
        SurfaceFormat::Color, SurfaceFormat::NormalizedByte2, SurfaceFormat::NormalizedByte4,
        SurfaceFormat::Rgba1010102, SurfaceFormat::Rg32, SurfaceFormat::Rgba64,
        SurfaceFormat::Single, SurfaceFormat::Vector2, SurfaceFormat::Vector4,
        SurfaceFormat::HalfSingle, SurfaceFormat::HalfVector2, SurfaceFormat::HalfVector4,
        SurfaceFormat::HdrBlendable, SurfaceFormat::ByteEXT, SurfaceFormat::UShortEXT};
    int roundTripped = 0;
    for (const SurfaceFormat format : formats)
    {
        const int ordinal = static_cast<int>(format);
        const StorageTexture2DDescriptor descriptor(5, 3, 2, format, kFullUsage);
        if (!Advertises(*device, format))
        {
            EXPECT_TRUE(format == SurfaceFormat::ByteEXT || format == SurfaceFormat::UShortEXT)
                << "storage format " << ordinal << " is not advertised";
            EXPECT_THROW(StorageTexture2D(*device, descriptor), System::NotSupportedException);
            continue;
        }

        StorageTexture2D texture(*device, descriptor);
        const std::size_t size = static_cast<std::size_t>(Texture::GetFormatSizeEXT(format));

        // Level 0, whole: 5x3.
        const std::vector<std::uint8_t> level0 = Pattern(format, 15, 1);
        texture.setData(0, nullptr, level0.data(), level0.size());
        std::vector<std::uint8_t> back0(level0.size(), 0xCD);
        texture.getData(0, nullptr, back0.data(), back0.size());
        EXPECT_EQ(level0, back0) << "level 0 of format " << ordinal;

        // Level 1 is 2x1: its right texel alone, then the whole level.
        const std::vector<std::uint8_t> level1 = Pattern(format, 2, 7);
        texture.setData(1, nullptr, level1.data(), level1.size());
        const std::vector<std::uint8_t> right = Pattern(format, 1, 40);
        const Rectangle rightTexel(1, 0, 1, 1);
        texture.setData(1, &rightTexel, right.data(), right.size());
        std::vector<std::uint8_t> back1(level1.size(), 0xCD);
        texture.getData(1, nullptr, back1.data(), back1.size());
        std::vector<std::uint8_t> expected1 = level1;
        std::memcpy(expected1.data() + size, right.data(), size);
        EXPECT_EQ(expected1, back1) << "level 1 of format " << ordinal;

        std::vector<std::uint8_t> backRight(size, 0xCD);
        texture.getData(1, &rightTexel, backRight.data(), backRight.size());
        EXPECT_EQ(right, backRight) << "level 1 sub-rectangle of format " << ordinal;
        ++roundTripped;
    }
    EXPECT_EQ(13, roundTripped);
}

TEST(OpenGL4StorageTexture, AComputeWrittenFloatImageReadsBackExactly)
{
    auto device = HiDefDevice();
    if (device->GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";

    constexpr int kWidth = 8;
    constexpr int kHeight = 4;
    StorageTexture2D image(*device, StorageTexture2DDescriptor(kWidth, kHeight, 1,
                                                               SurfaceFormat::Vector4, kFullUsage));
    ComputeShader writer(*device, R"GLSL(#version 430 core
layout(local_size_x = 8, local_size_y = 4) in;
layout(rgba32f, binding = 2) writeonly uniform image2D uImage;
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    imageStore(uImage, p, vec4(float(p.x) + 0.5, float(p.y) * 3.0, float(p.x * p.y), -1.25));
}
)GLSL");
    writer.bindStorageTexture(2, image, CNA::GraphicsImageAccess::WriteOnly);
    writer.dispatch(1, 1);

    std::vector<float> texels(static_cast<std::size_t>(kWidth * kHeight * 4), 0.0f);
    image.getData(0, nullptr, texels.data(), texels.size() * sizeof(float));
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
        {
            const float* texel = texels.data() + (static_cast<std::size_t>(y) * kWidth + x) * 4;
            EXPECT_EQ(static_cast<float>(x) + 0.5f, texel[0]) << x << "," << y;
            EXPECT_EQ(static_cast<float>(y) * 3.0f, texel[1]) << x << "," << y;
            EXPECT_EQ(static_cast<float>(x * y), texel[2]) << x << "," << y;
            EXPECT_EQ(-1.25f, texel[3]) << x << "," << y;
        }
}

TEST(OpenGL4StorageTexture, ASampledStorageTextureDrawsItsTexels)
{
    auto device = HiDefDevice();
    if (device->GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    device->setBlendStateProperty(BlendState::Opaque);
    device->setDepthStencilStateProperty(DepthStencilState::None);
    device->setRasterizerStateProperty(RasterizerState::CullNone);

    // 2x2 texels, uploaded row by row: the first row is the top of the image.
    StorageTexture2D storage(*device, StorageTexture2DDescriptor(2, 2, 1, SurfaceFormat::Color,
                                                                 kFullUsage));
    const std::array<std::uint8_t, 16> texels{
        255, 0, 0, 255,    0, 255, 0, 255,
        0, 0, 255, 255,    255, 255, 255, 255};
    storage.setData(0, nullptr, texels.data(), texels.size());

    ShaderEffect effect(*device, R"GLSL(#version 410 core
layout(location = 0) in vec3 aPosition;
out vec2 vUv;
void main()
{
    gl_Position = vec4(aPosition, 1.0);
    vUv = vec2(aPosition.x * 0.5 + 0.5, 0.5 - aPosition.y * 0.5);
}
)GLSL",
                        R"GLSL(#version 410 core
in vec2 vUv;
uniform sampler2D uStorage;
out vec4 FragColor;
void main() { FragColor = texture(uStorage, vUv); }
)GLSL");
    ASSERT_TRUE(effect.IsEffectValid());

    const std::array<std::array<float, 3>, 6> corners{{
        {-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
    VertexBuffer quad(*device,
                      VertexDeclaration(12, {VertexElement(0, VertexElementFormat::Vector3,
                                                           VertexElementUsage::Position, 0)}),
                      6, BufferUsage::None);
    quad.SetDataRaw(corners.data(), 6, 12);
    RenderTarget2D target(*device, 2, 2);

    device->SetRenderTarget(&target);
    device->Clear(Color::Black);
    device->getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    effect.SetStorageTextureEXT(0, storage);
    effect.Apply();
    device->SetVertexBuffer(&quad);
    device->DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    device->SetVertexBuffer(nullptr);
    device->SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    std::array<Color, 4> pixels{};
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    EXPECT_EQ(Color(255, 0, 0, 255).getPackedValueProperty(), pixels[0].getPackedValueProperty());
    EXPECT_EQ(Color(0, 255, 0, 255).getPackedValueProperty(), pixels[1].getPackedValueProperty());
    EXPECT_EQ(Color(0, 0, 255, 255).getPackedValueProperty(), pixels[2].getPackedValueProperty());
    EXPECT_EQ(Color(255, 255, 255, 255).getPackedValueProperty(), pixels[3].getPackedValueProperty());
}

#endif // CNA_RENDERER_OPENGL4 && CNA_CNAEXT
