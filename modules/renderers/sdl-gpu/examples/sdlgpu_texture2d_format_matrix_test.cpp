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
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include "System/ArgumentException.hpp"

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
using CNA::Internal::Renderers::SdlGpu::SdlGpuTextureCubeRenderer;
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

        // plans/plan_apple_m4.md AM4-184: the remaining classic formats are stored natively (widened
        // to four channels with D3D9's fill where they have fewer) when the device can sample that
        // storage, and refused otherwise -- an explicit verdict either way, never RGBA8.
        const std::array remaining{
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
        bool remainingExplicit = true;
        for (const SurfaceFormat format : remaining)
        {
            const RendererFormatVerdict verdict =
                renderer.ClassifySurfaceFormatEXT(static_cast<int>(format));
            remainingExplicit &= verdict == RendererFormatVerdict::Supported ||
                                 verdict == RendererFormatVerdict::Unsupported;
        }
        Check(remainingExplicit,
              "every remaining classic format has an explicit verdict, never a deferred RGBA8");

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
        for (const SurfaceFormat format : remaining)
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

    /// AM4-184: a format the renderer calls Supported constructs, and one it refuses throws.
    void CheckRemainingFormatsAgreeWithTheirVerdicts(GraphicsDevice& device, SdlGpuRenderer& renderer)
    {
        bool agree = true;
        for (const SurfaceFormat format :
             {SurfaceFormat::Rgba1010102, SurfaceFormat::Rg32, SurfaceFormat::Rgba64,
              SurfaceFormat::Alpha8, SurfaceFormat::Single, SurfaceFormat::Vector2,
              SurfaceFormat::Vector4, SurfaceFormat::HalfSingle, SurfaceFormat::HalfVector2,
              SurfaceFormat::HalfVector4, SurfaceFormat::HdrBlendable})
        {
            const bool supported = renderer.ClassifySurfaceFormatEXT(static_cast<int>(format)) ==
                RendererFormatVerdict::Supported;
            bool constructed = false;
            try
            {
                Texture2D texture(device, 2, 2, false, format);
                constructed = true;
            }
            catch (const std::exception&)
            {
            }
            if (constructed != supported)
            {
                std::printf("[INFO] SurfaceFormat %d: verdict %s, construction %s\n",
                            static_cast<int>(format), supported ? "Supported" : "Unsupported",
                            constructed ? "succeeded" : "failed");
                agree = false;
            }
        }
        Check(agree, "each remaining format constructs exactly when it is classified Supported");
    }

    /// Draws a 1x1 texture with Opaque blending into a cleared Color target and reads it back.
    template <typename Texel>
    Color SampleThroughSprite(GraphicsDevice& device, SurfaceFormat format, const Texel& texel)
    {
        Texture2D texture(device, 1, 1, false, format);
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
        return pixel;
    }

    /// AM4-184: the stored fill, observed through SpriteBatch's identity channel expansion for
    /// these two formats, so only the storage itself can supply it.
    void CheckWidenedStorageSampling(GraphicsDevice& device, SdlGpuRenderer& renderer)
    {
        if (renderer.ClassifySurfaceFormatEXT(static_cast<int>(SurfaceFormat::Rg32)) ==
            RendererFormatVerdict::Supported)
        {
            const std::uint32_t rg = 0u;  // R = G = 0
            const Color pixel = SampleThroughSprite(device, SurfaceFormat::Rg32, rg);
            Check(pixel.getRProperty() <= 2 && pixel.getGProperty() <= 2 &&
                      pixel.getBProperty() >= 253 && pixel.getAProperty() >= 253,
                  "Rg32 sampling fills missing B/A with one (blue), not zero (black)");
        }
        const std::uint8_t alpha = 0x80u;
        const Color pixel = SampleThroughSprite(device, SurfaceFormat::Alpha8, alpha);
        Check(pixel.getRProperty() == 0 && pixel.getGProperty() == 0 &&
                  pixel.getBProperty() == 0 && pixel.getAProperty() >= 0x7Fu &&
                  pixel.getAProperty() <= 0x81u,
              "Alpha8 samples as (0, 0, 0, A), as Direct3D 9's A8 does");
    }

    void CheckCompressedNpotPartialAndMips(GraphicsDevice& device)
    {
        // XNA 4.0 requires a classic DXT Texture2D's dimensions to be multiples of four under
        // both profiles (SOFTWARE-214, from the recovered XNA validation). This fixture used to
        // build a 7x5 chain, which the shared layer now refuses before any renderer storage
        // exists; the NPOT, edge-block and sub-4x4 subjects it covered are reached legally
        // below through a 12x12 chain whose lower levels are not block multiples.
        bool unalignedRefused = false;
        try
        {
            Texture2D invalid(device, 7, 5, true, SurfaceFormat::Dxt1);
        }
        catch (const System::ArgumentException&)
        {
            unalignedRefused = true;
        }
        Check(unalignedRefused, "XNA refuses a 7x5 DXT1 Texture2D before allocating storage");

        // 12x12 -> 6x6 -> 3x3 -> 1x1: level 0 is NPOT, level 1 is a 2x2 block grid whose right
        // column and bottom row are padded (the bottom-right block in both directions, as the
        // 7x5 chain's were), and levels 2 and 3 are each a single padded block.
        Texture2D texture(device, 12, 12, true, SurfaceFormat::Dxt1);

        std::vector<std::uint8_t> level0;
        AppendDxt1SolidBlock(level0, 0xF800u); // red
        AppendDxt1SolidBlock(level0, 0x07E0u); // green
        AppendDxt1SolidBlock(level0, 0x001Fu); // blue, top-right
        AppendDxt1SolidBlock(level0, 0xFFFFu); // white
        AppendDxt1SolidBlock(level0, 0xFFE0u); // yellow
        AppendDxt1SolidBlock(level0, 0xF81Fu); // magenta
        AppendDxt1SolidBlock(level0, 0x8410u); // grey
        AppendDxt1SolidBlock(level0, 0x0000u); // black
        AppendDxt1SolidBlock(level0, 0x7BEFu); // dark grey, bottom-right
        texture.SetData(level0.data(), static_cast<int>(level0.size()));

        std::vector<std::uint8_t> roundTrip(level0.size());
        texture.GetData(roundTrip.data(), static_cast<int>(roundTrip.size()));
        Check(roundTrip == level0,
              "NPOT DXT1 level 0 preserves all nine blocks byte-for-byte");

        std::vector<std::uint8_t> replacement;
        AppendDxt1SolidBlock(replacement, 0x07FFu);
        const Rectangle topRight(8, 0, 4, 4);
        texture.SetData(0, &topRight, replacement.data(), 0,
                        static_cast<int>(replacement.size()));
        std::copy(replacement.begin(), replacement.end(), level0.begin() + 16);
        std::fill(roundTrip.begin(), roundTrip.end(), 0);
        texture.GetData(roundTrip.data(), static_cast<int>(roundTrip.size()));
        Check(roundTrip == level0,
              "block-aligned partial DXT1 update changes one NPOT block and preserves eight");

        std::vector<std::uint8_t> mip1;
        AppendDxt1SolidBlock(mip1, 0xF800u); // level 1 is 6x6: one full block,
        AppendDxt1SolidBlock(mip1, 0x07E0u); // a block padded to the right,
        AppendDxt1SolidBlock(mip1, 0xFFE0u); // a block padded below,
        AppendDxt1SolidBlock(mip1, 0xF81Fu); // and a 2x2-texel corner block padded both ways
        texture.SetData(1, nullptr, mip1.data(), 0, static_cast<int>(mip1.size()));
        std::vector<std::uint8_t> edgeReplacement;
        AppendDxt1SolidBlock(edgeReplacement, 0x001Fu);
        const Rectangle edgeBlock(4, 0, 2, 4);
        texture.SetData(1, &edgeBlock, edgeReplacement.data(), 0,
                        static_cast<int>(edgeReplacement.size()));
        std::copy(edgeReplacement.begin(), edgeReplacement.end(), mip1.begin() + 8);
        std::vector<std::uint8_t> mip1Read(mip1.size());
        texture.GetData(1, nullptr, mip1Read.data(), 0, static_cast<int>(mip1Read.size()));
        Check(mip1Read == mip1,
              "edge-reaching partial DXT1 update changes the padded edge block and preserves three");

        std::vector<std::uint8_t> cornerReplacement;
        AppendDxt1SolidBlock(cornerReplacement, 0xFFFFu);
        const Rectangle cornerBlock(4, 4, 2, 2);
        texture.SetData(1, &cornerBlock, cornerReplacement.data(), 0,
                        static_cast<int>(cornerReplacement.size()));
        std::copy(cornerReplacement.begin(), cornerReplacement.end(), mip1.begin() + 24);
        std::fill(mip1Read.begin(), mip1Read.end(), 0);
        texture.GetData(1, nullptr, mip1Read.data(), 0, static_cast<int>(mip1Read.size()));
        Check(mip1Read == mip1,
              "a partial DXT1 update of the vertically padded corner block preserves the other three");

        std::vector<std::uint8_t> mip2;
        AppendDxt1SolidBlock(mip2, 0xF800u); // level 2 is 3x3: one padded block
        texture.SetData(2, nullptr, mip2.data(), 0, static_cast<int>(mip2.size()));
        std::vector<std::uint8_t> mip2Read(mip2.size());
        texture.GetData(2, nullptr, mip2Read.data(), 0, static_cast<int>(mip2Read.size()));
        Check(mip2Read == mip2,
              "authored sub-4x4 DXT mip preserves its single padded block byte-for-byte");

        const auto& native = dynamic_cast<const SdlGpuTextureRenderer&>(texture.GetRenderer());
        Texture2D dxt3(device, 4, 4, false, SurfaceFormat::Dxt3);
        Texture2D dxt5(device, 4, 4, false, SurfaceFormat::Dxt5);
        TextureCube cubeDxt1(device, 4, false, SurfaceFormat::Dxt1);
        TextureCube cubeDxt3(device, 4, false, SurfaceFormat::Dxt3);
        TextureCube cubeDxt5(device, 4, false, SurfaceFormat::Dxt5);
        const bool allNative = native.UsesNativeCompressionEXT() &&
            dynamic_cast<const SdlGpuTextureRenderer&>(dxt3.GetRenderer())
                .UsesNativeCompressionEXT() &&
            dynamic_cast<const SdlGpuTextureRenderer&>(dxt5.GetRenderer())
                .UsesNativeCompressionEXT() &&
            dynamic_cast<const SdlGpuTextureCubeRenderer&>(cubeDxt1.GetRenderer())
                .UsesNativeCompressionEXT() &&
            dynamic_cast<const SdlGpuTextureCubeRenderer&>(cubeDxt3.GetRenderer())
                .UsesNativeCompressionEXT() &&
            dynamic_cast<const SdlGpuTextureCubeRenderer&>(cubeDxt5.GetRenderer())
                .UsesNativeCompressionEXT();
        const auto& renderer = dynamic_cast<const SdlGpuRenderer&>(device.GetRenderer());
        Check(renderer.LoadsCompressedContentNativelyEXT() == allNative,
              "compressed-content loader policy matches actual 2D/cube BC1/2/3 device storage");
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
        CheckRemainingFormatsAgreeWithTheirVerdicts(device, renderer);
        CheckWidenedStorageSampling(device, renderer);
        CheckCompressedNpotPartialAndMips(device);

        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    SdlGpuTexture2DFormatMatrixTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // plans/plan_pre_sdlgpu_closeout.md PSG-0008: this test exercises a resource the
        // Reach profile refuses outright, so under the default profile it threw before its
        // first check and reported as a renderer failure rather than as a test defect.
        // profile_dead_tests.py named it DEAD-ON-PROFILE; GTI-0003 fixed three SDL_GPU
        // tests this way and missed these.
        gdm_->setGraphicsProfileProperty(
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
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
