// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-046: exact cube-target storage and face-aware MRT validation.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"
#include "System/NotSupportedException.hpp"
#include "common/PixelTestGame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWindowSize = 32;
    constexpr int kTargetSize = 8;

    struct FormatCase
    {
        SurfaceFormat format;
        int internalFormat;
        int bytesPerTexel;
    };

    constexpr std::array kFormats{
        FormatCase{SurfaceFormat::Color, 0x8058, 4},
        FormatCase{SurfaceFormat::Rgba1010102, 0x8059, 4},
        FormatCase{SurfaceFormat::Rg32, 0x822C, 4},
        FormatCase{SurfaceFormat::Rgba64, 0x805B, 8},
        FormatCase{SurfaceFormat::Single, 0x822E, 4},
        FormatCase{SurfaceFormat::Vector2, 0x8230, 8},
        FormatCase{SurfaceFormat::Vector4, 0x8814, 16},
        FormatCase{SurfaceFormat::HalfSingle, 0x822D, 2},
        FormatCase{SurfaceFormat::HalfVector2, 0x822F, 4},
        FormatCase{SurfaceFormat::HalfVector4, 0x881A, 8},
        FormatCase{SurfaceFormat::HdrBlendable, 0x881A, 8}};

    template<typename T>
    [[nodiscard]] T ReadScalar(const std::vector<std::uint8_t>& bytes, const std::size_t offset = 0)
    {
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }

    [[nodiscard]] bool Near(
        const float actual, const float expected, const float tolerance = 0.012f)
    {
        return std::fabs(actual - expected) <= tolerance;
    }

    [[nodiscard]] bool Matches(
        const Color& actual, const Color& expected, const int tolerance = 8)
    {
        return std::abs(actual.getRProperty() - expected.getRProperty()) <= tolerance &&
            std::abs(actual.getGProperty() - expected.getGProperty()) <= tolerance &&
            std::abs(actual.getBProperty() - expected.getBProperty()) <= tolerance &&
            std::abs(actual.getAProperty() - expected.getAProperty()) <= tolerance;
    }

    [[nodiscard]] bool MatchesClearValue(
        const SurfaceFormat format, const std::vector<std::uint8_t>& bytes)
    {
        constexpr float red = 64.0f / 255.0f;
        constexpr float green = 128.0f / 255.0f;
        constexpr float blue = 191.0f / 255.0f;
        if (format == SurfaceFormat::Color)
            return bytes == std::vector<std::uint8_t>{64, 128, 191, 255};
        if (format == SurfaceFormat::Rgba1010102)
        {
            const std::uint32_t packed = ReadScalar<std::uint32_t>(bytes);
            return std::abs(static_cast<int>(packed & 0x3FFu) - 257) <= 1 &&
                std::abs(static_cast<int>((packed >> 10u) & 0x3FFu) - 514) <= 1 &&
                std::abs(static_cast<int>((packed >> 20u) & 0x3FFu) - 766) <= 1 &&
                ((packed >> 30u) & 0x3u) == 3u;
        }
        if (format == SurfaceFormat::Rg32)
            return std::abs(static_cast<int>(ReadScalar<std::uint16_t>(bytes)) - 16448) <= 1 &&
                std::abs(static_cast<int>(ReadScalar<std::uint16_t>(bytes, 2)) - 32896) <= 1;
        if (format == SurfaceFormat::Rgba64)
            return std::abs(static_cast<int>(ReadScalar<std::uint16_t>(bytes)) - 16448) <= 1 &&
                std::abs(static_cast<int>(ReadScalar<std::uint16_t>(bytes, 2)) - 32896) <= 1 &&
                std::abs(static_cast<int>(ReadScalar<std::uint16_t>(bytes, 4)) - 49087) <= 1 &&
                ReadScalar<std::uint16_t>(bytes, 6) == 65535u;
        if (format == SurfaceFormat::Single)
            return Near(ReadScalar<float>(bytes), red);
        if (format == SurfaceFormat::Vector2)
            return Near(ReadScalar<float>(bytes), red) &&
                Near(ReadScalar<float>(bytes, 4), green);
        if (format == SurfaceFormat::Vector4)
            return Near(ReadScalar<float>(bytes), red) &&
                Near(ReadScalar<float>(bytes, 4), green) &&
                Near(ReadScalar<float>(bytes, 8), blue) &&
                Near(ReadScalar<float>(bytes, 12), 1.0f);

        using Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper;
        const auto half = [&](const std::size_t offset)
        {
            return HalfTypeHelper::Convert(ReadScalar<std::uint16_t>(bytes, offset));
        };
        if (format == SurfaceFormat::HalfSingle)
            return Near(half(0), red);
        if (format == SurfaceFormat::HalfVector2)
            return Near(half(0), red) && Near(half(2), green);
        return Near(half(0), red) && Near(half(2), green) &&
            Near(half(4), blue) && Near(half(6), 1.0f);
    }
}

class RlglRenderTargetCubeTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglRenderTargetCubeTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kWindowSize);
        graphics_->setPreferredBackBufferHeightProperty(kWindowSize);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        namespace Bridge = CNA::Internal::Renderers::Rlgl::Bridge;
        namespace Rlgl = CNA::Internal::Renderers::Rlgl;
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();

        constexpr std::array unsupported{
            SurfaceFormat::Bgr565, SurfaceFormat::Bgra5551,
            SurfaceFormat::Bgra4444, SurfaceFormat::Dxt1,
            SurfaceFormat::Dxt3, SurfaceFormat::Dxt5,
            SurfaceFormat::NormalizedByte2, SurfaceFormat::NormalizedByte4,
            SurfaceFormat::Alpha8};
        bool classificationExact = true;
        for (const FormatCase& entry : kFormats)
        {
            classificationExact = classificationExact &&
                renderer.ClassifyRenderTargetCubeFormatEXT(
                    static_cast<int>(entry.format)) ==
                    CNA::Internal::Renderers::RendererFormatVerdict::Supported;
        }
        for (const SurfaceFormat format : unsupported)
        {
            classificationExact = classificationExact &&
                renderer.ClassifyRenderTargetCubeFormatEXT(
                    static_cast<int>(format)) ==
                    CNA::Internal::Renderers::RendererFormatVerdict::Unsupported;
        }
        Check(classificationExact,
              "cube FBO probes advertise exactly the eleven classic renderable formats");

        bool exactStorageAndPixels = true;
        bool allFacesAllocated = true;
        bool independentMsaaFaces = true;
        bool generatedMipExact = true;
        for (std::size_t index = 0; index < kFormats.size(); ++index)
        {
            const FormatCase& entry = kFormats[index];
            RenderTargetCube target(
                device, kTargetSize, true, entry.format,
                DepthFormat::Depth24Stencil8, 4,
                RenderTargetUsage::PreserveContents);
            const auto storage = Rlgl::GetRenderTargetCubeResourceSnapshotForTesting(
                *target.GetRenderTargetCubeRenderer());
            const auto texture = Bridge::GetTextureCubeSnapshotForTesting(
                storage.colorTexture, storage.levelCount);
            exactStorageAndPixels = exactStorageAndPixels &&
                texture.internalFormat == entry.internalFormat &&
                storage.surfaceFormat == static_cast<int>(entry.format) &&
                storage.resolveFramebuffer != 0 && storage.depthStencilRenderbuffer != 0 &&
                storage.multiSampleCount >= 2;
            allFacesAllocated = allFacesAllocated &&
                std::all_of(texture.levelZeroWidths.begin(), texture.levelZeroWidths.end(),
                            [](const int width) { return width == kTargetSize; }) &&
                std::all_of(texture.finalLevelWidths.begin(), texture.finalLevelWidths.end(),
                            [](const int width) { return width == 1; });
            const std::set<unsigned int> msaaNames(
                storage.multisampleColorRenderbuffers.begin(),
                storage.multisampleColorRenderbuffers.end());
            independentMsaaFaces = independentMsaaFaces &&
                msaaNames.size() == 6 && *msaaNames.begin() != 0;

            const auto face = static_cast<CubeMapFace>(index % 6u);
            device.SetRenderTarget(&target, face);
            device.Clear(Color(64, 128, 191, 255));
            device.SetRenderTargets({});

            std::vector<std::uint8_t> raw(
                static_cast<std::size_t>(entry.bytesPerTexel));
            Bridge::ReadRenderTargetCube(
                storage.colorTexture, static_cast<int>(face), 0, kTargetSize,
                kTargetSize / 2, kTargetSize / 2, 1, 1,
                raw.data(), static_cast<int>(entry.format));
            exactStorageAndPixels = exactStorageAndPixels &&
                MatchesClearValue(entry.format, raw);

            if (entry.format == SurfaceFormat::HalfVector4)
            {
                std::vector<std::uint8_t> mipRaw(8u);
                Bridge::ReadRenderTargetCube(
                    storage.colorTexture, static_cast<int>(face),
                    storage.levelCount - 1, 1, 0, 0, 1, 1,
                    mipRaw.data(), static_cast<int>(entry.format));
                generatedMipExact = MatchesClearValue(entry.format, mipRaw);
            }
        }
        Check(exactStorageAndPixels,
              "all eleven cube formats retain exact native identities and rendered transfer bytes");
        Check(allFacesAllocated,
              "every exact cube format allocates six complete mip chains");
        Check(independentMsaaFaces,
              "every multisampled cube owns six distinct face color renderbuffers");
        Check(generatedMipExact,
              "resolved HalfVector4 output reaches the generated terminal cube mip");

        bool unsupportedRejected = false;
        try
        {
            RenderTargetCube target(
                device, kTargetSize, false, SurfaceFormat::Dxt1,
                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            (void)target;
        }
        catch (const System::NotSupportedException&)
        {
            unsupportedRejected = true;
        }
        Check(unsupportedRejected,
              "non-renderable cube formats are refused instead of substituted");

        bool depthFormatsExact = true;
        for (const DepthFormat depth : {
                 DepthFormat::Depth16, DepthFormat::Depth24,
                 DepthFormat::Depth24Stencil8})
        {
            RenderTargetCube target(
                device, kTargetSize, false, SurfaceFormat::Color,
                depth, 0, RenderTargetUsage::PreserveContents);
            const auto storage = Rlgl::GetRenderTargetCubeResourceSnapshotForTesting(
                *target.GetRenderTargetCubeRenderer());
            depthFormatsExact = depthFormatsExact &&
                storage.depthFormat == static_cast<int>(depth) &&
                storage.depthStencilRenderbuffer != 0 &&
                target.getDepthStencilFormatProperty() == depth;
        }
        RenderTargetCube stencilTarget(
            device, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&stencilTarget, CubeMapFace::NegativeY);
        device.Clear(
            ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
            Color::Black, 1.0f, 37);
        const bool stencilExact =
            Bridge::ReadStencilForTesting(3, 3, kTargetSize) == 37;
        device.SetRenderTargets({});
        Check(depthFormatsExact && stencilExact,
              "Depth16/Depth24/Depth24Stencil8 cube attachments are real and stencil clears persist");

        RenderTargetCube depthTarget(
            device, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::Depth24, 0, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&depthTarget, CubeMapFace::PositiveY);
        device.Clear(Color::Black, 1.0f);
        device.SetDepthTestEnabled(true);
        device.SetDepthWriteEnabled(true);
        BasicEffect depthEffect(device);
        depthEffect.VertexColorEnabled = true;
        depthEffect.setWorldProperty(Matrix::getIdentityProperty());
        depthEffect.setViewProperty(Matrix::getIdentityProperty());
        depthEffect.setProjectionProperty(Matrix::getIdentityProperty());
        depthEffect.Apply();
        const std::array<VertexPositionColor, 12> depthQuads{
            VertexPositionColor(Vector3(-1, -1, 0.5f), Color::Red),
            VertexPositionColor(Vector3(-1, 1, 0.5f), Color::Red),
            VertexPositionColor(Vector3(1, -1, 0.5f), Color::Red),
            VertexPositionColor(Vector3(-1, 1, 0.5f), Color::Red),
            VertexPositionColor(Vector3(1, 1, 0.5f), Color::Red),
            VertexPositionColor(Vector3(1, -1, 0.5f), Color::Red),
            VertexPositionColor(Vector3(-1, -1, -0.5f), Color::Green),
            VertexPositionColor(Vector3(-1, 1, -0.5f), Color::Green),
            VertexPositionColor(Vector3(1, -1, -0.5f), Color::Green),
            VertexPositionColor(Vector3(-1, 1, -0.5f), Color::Green),
            VertexPositionColor(Vector3(1, 1, -0.5f), Color::Green),
            VertexPositionColor(Vector3(1, -1, -0.5f), Color::Green)};
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, depthQuads.data(), 0, 4);
        device.SetDepthTestEnabled(false);
        device.SetDepthWriteEnabled(false);
        device.SetRenderTargets({});
        Color depthPixel;
        const Rectangle centre(3, 3, 1, 1);
        depthTarget.GetData(
            CubeMapFace::PositiveY, 0, &centre, &depthPixel, 0, 1);
        Check(Matches(depthPixel, Color::Green),
              "cube-face depth testing selects the nearer primitive");

        RenderTargetCube mixedCube(
            device, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 4, RenderTargetUsage::PreserveContents);
        RenderTarget2D mixed2D(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 4, RenderTargetUsage::PreserveContents);
        device.SetRenderTargets({
            RenderTargetBinding(
                static_cast<Texture*>(&mixedCube), CubeMapFace::PositiveZ),
            RenderTargetBinding(&mixed2D)});
        device.Clear(Color::Blue);
        BasicEffect mrtEffect(device);
        mrtEffect.VertexColorEnabled = true;
        mrtEffect.setWorldProperty(Matrix::getIdentityProperty());
        mrtEffect.setViewProperty(Matrix::getIdentityProperty());
        mrtEffect.setProjectionProperty(Matrix::getIdentityProperty());
        mrtEffect.Apply();
        const std::array<VertexPositionColor, 3> triangle{
            VertexPositionColor(Vector3(-1, -1, 0), Color::Red),
            VertexPositionColor(Vector3(0, 1, 0), Color::Red),
            VertexPositionColor(Vector3(1, -1, 0), Color::Red)};
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, triangle.data(), 0, 1);
        device.SetRenderTargets({});
        Color cubeMrtPixel;
        Color peerPixel;
        mixedCube.GetData(
            CubeMapFace::PositiveZ, 0, &centre, &cubeMrtPixel, 0, 1);
        mixed2D.GetData(0, &centre, &peerPixel, 0, 1);
        Check(Matches(cubeMrtPixel, Color::Red) && Matches(peerPixel, Color::Blue),
              "cube-face plus RenderTarget2D MRT preserves ordered independent attachments");

        RenderTargetCube twoFaceCube(
            device, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 4, RenderTargetUsage::PreserveContents);
        device.SetRenderTargets({
            RenderTargetBinding(
                static_cast<Texture*>(&twoFaceCube), CubeMapFace::PositiveX),
            RenderTargetBinding(
                static_cast<Texture*>(&twoFaceCube), CubeMapFace::NegativeX)});
        device.Clear(Color::Blue);
        mrtEffect.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, triangle.data(), 0, 1);
        device.SetRenderTargets({});
        Color positiveX;
        Color negativeX;
        twoFaceCube.GetData(
            CubeMapFace::PositiveX, 0, &centre, &positiveX, 0, 1);
        twoFaceCube.GetData(
            CubeMapFace::NegativeX, 0, &centre, &negativeX, 0, 1);
        Check(Matches(positiveX, Color::Red) && Matches(negativeX, Color::Blue),
              "two faces of one cube can occupy distinct MRT slots without aliasing");
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    return CNA::Examples::RunPixelTest<RlglRenderTargetCubeTest>();
}
