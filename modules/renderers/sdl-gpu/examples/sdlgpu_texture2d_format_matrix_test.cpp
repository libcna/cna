// SPDX-License-Identifier: MS-PL
// SDLGPU-69: truthful classic Texture2D format classification, DXT block preservation across
// NPOT/partial/mip writes, and D3D9 missing-channel semantics for NormalizedByte2 sampling.

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using CNA::Internal::Renderers::RendererFormatVerdict;
using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer;
using CNA::Internal::Renderers::SdlGpu::SdlGpuTextureRenderer;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    void AppendDxt1SolidBlock(std::vector<std::uint8_t>& bytes, std::uint16_t rgb565)
    {
        bytes.push_back(static_cast<std::uint8_t>(rgb565 & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>(rgb565 >> 8));
        bytes.push_back(0);
        bytes.push_back(0);
        bytes.insert(bytes.end(), 4, 0);
    }
}

class SdlGpuTexture2DFormatMatrixTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passed_ = 0;
    int failed_ = 0;
    bool done_ = false;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        condition ? ++passed_ : ++failed_;
    }

    void CheckClassifiers(SdlGpuRenderer& renderer)
    {
        const std::array supported{
            SurfaceFormat::Color,
            SurfaceFormat::Bgr565,
            SurfaceFormat::Bgra5551,
            SurfaceFormat::Bgra4444,
            SurfaceFormat::Dxt1,
            SurfaceFormat::Dxt3,
            SurfaceFormat::Dxt5,
            SurfaceFormat::NormalizedByte2,
            SurfaceFormat::NormalizedByte4,
        };
        bool supportedExact = true;
        for (const SurfaceFormat format : supported)
        {
            supportedExact &= renderer.ClassifySurfaceFormatEXT(static_cast<int>(format)) ==
                RendererFormatVerdict::Supported;
        }
        Check(supportedExact,
              "the exact nine-format EasyGL Texture2D set is explicitly Supported");

        const std::array unsupported{
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
        };
        bool unsupportedExact = true;
        for (const SurfaceFormat format : unsupported)
        {
            unsupportedExact &= renderer.ClassifySurfaceFormatEXT(static_cast<int>(format)) ==
                RendererFormatVerdict::Unsupported;
        }
        Check(unsupportedExact,
              "every remaining classic format is explicitly Unsupported, never silently RGBA8");

        const bool compressionExact =
            renderer.IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt1)) &&
            renderer.IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt3)) &&
            renderer.IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt5)) &&
            !renderer.IsCompressedTransferFormatEXT(
                static_cast<int>(SurfaceFormat::NormalizedByte4));
        Check(compressionExact,
              "only DXT1/3/5 use exact block transfers");

        bool colorTransferExact =
            renderer.ClassifyColorTransferFormatEXT(static_cast<int>(SurfaceFormat::Color)) ==
                RendererFormatVerdict::Supported;
        for (const SurfaceFormat format : supported)
        {
            if (format != SurfaceFormat::Color)
                colorTransferExact &= renderer.ClassifyColorTransferFormatEXT(
                    static_cast<int>(format)) == RendererFormatVerdict::Unsupported;
        }
        for (const SurfaceFormat format : unsupported)
        {
            colorTransferExact &= renderer.ClassifyColorTransferFormatEXT(
                static_cast<int>(format)) == RendererFormatVerdict::Unsupported;
        }
        Check(colorTransferExact,
              "only Color accepts Color-shaped transfer elements across all classic formats");
    }

    void CheckNormalizedByte2Sampling(GraphicsDevice& device)
    {
        Texture2D texture(device, 1, 1, false, SurfaceFormat::NormalizedByte2);
        const PackedVector::NormalizedByte2 texel(0.0f, 0.0f);
        texture.SetData(&texel, 1);

        RenderTarget2D target(device, 1, 1, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        const SamplerState pointClamp = SamplerState::PointClamp;
        device.SetRenderTarget(&target);
        device.Clear(Color::Red);
        {
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
            batch.Draw(texture, Rectangle(0, 0, 1, 1), Color::White);
            batch.End();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color pixel;
        target.GetData(&pixel, 1);
        Check(pixel.getRProperty() <= 8 && pixel.getGProperty() <= 8 &&
                  pixel.getBProperty() >= 247 && pixel.getAProperty() >= 247,
              "NormalizedByte2 sampling expands missing B/A to one (blue), not zero (black)");
    }

    void CheckCompressedNpotPartialAndMips(GraphicsDevice& device)
    {
        Texture2D texture(device, 7, 5, true, SurfaceFormat::Dxt1);

        std::vector<std::uint8_t> level0;
        AppendDxt1SolidBlock(level0, 0xF800u); // red, top-left
        AppendDxt1SolidBlock(level0, 0x07E0u); // green, top-right partial block
        AppendDxt1SolidBlock(level0, 0x001Fu); // blue, bottom-left partial block
        AppendDxt1SolidBlock(level0, 0xFFFFu); // white, bottom-right partial block
        texture.SetData(level0.data(), static_cast<int>(level0.size()));

        std::vector<std::uint8_t> roundTrip(level0.size());
        texture.GetData(roundTrip.data(), static_cast<int>(roundTrip.size()));
        Check(roundTrip == level0,
              "NPOT DXT1 level 0 preserves all four padded blocks byte-for-byte");

        std::vector<std::uint8_t> replacement;
        AppendDxt1SolidBlock(replacement, 0x001Fu);
        const Rectangle topRight(4, 0, 3, 4);
        texture.SetData(0, &topRight, replacement.data(), 0,
                        static_cast<int>(replacement.size()));
        std::copy(replacement.begin(), replacement.end(), level0.begin() + 8);
        std::fill(roundTrip.begin(), roundTrip.end(), 0);
        texture.GetData(roundTrip.data(), static_cast<int>(roundTrip.size()));
        Check(roundTrip == level0,
              "block-aligned partial DXT1 update changes one NPOT edge block and preserves three");

        std::vector<std::uint8_t> mip1;
        AppendDxt1SolidBlock(mip1, 0xF800u); // level 1 is 3x2: one padded block
        texture.SetData(1, nullptr, mip1.data(), 0, static_cast<int>(mip1.size()));
        std::vector<std::uint8_t> mip1Read(mip1.size());
        texture.GetData(1, nullptr, mip1Read.data(), 0, static_cast<int>(mip1Read.size()));
        Check(mip1Read == mip1,
              "authored sub-4x4 DXT mip preserves its single padded block byte-for-byte");

        const auto& native = dynamic_cast<const SdlGpuTextureRenderer&>(texture.GetRenderer());
        Texture2D dxt3(device, 4, 4, false, SurfaceFormat::Dxt3);
        Texture2D dxt5(device, 4, 4, false, SurfaceFormat::Dxt5);
        const bool allNative = native.UsesNativeCompressionEXT() &&
            dynamic_cast<const SdlGpuTextureRenderer&>(dxt3.GetRenderer())
                .UsesNativeCompressionEXT() &&
            dynamic_cast<const SdlGpuTextureRenderer&>(dxt5.GetRenderer())
                .UsesNativeCompressionEXT();
        const auto& renderer = dynamic_cast<const SdlGpuRenderer&>(device.GetRenderer());
        Check(renderer.LoadsCompressedContentNativelyEXT() == allNative,
              "compressed-content loader policy matches actual BC1/2/3 device storage");
        std::printf("[INFO] DXT storage path: %s\n",
                    native.UsesNativeCompressionEXT() ? "native BC1" : "renderer RGBA8 decode");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = dynamic_cast<SdlGpuRenderer&>(device.GetRenderer());
        CheckClassifiers(renderer);
        CheckNormalizedByte2Sampling(device);
        CheckCompressedNpotPartialAndMips(device);

        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    SdlGpuTexture2DFormatMatrixTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuTexture2DFormatMatrixTest game;
    game.Run();
    return game.Result();
}
