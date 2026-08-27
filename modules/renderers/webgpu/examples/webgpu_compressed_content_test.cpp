// SPDX-License-Identifier: MS-PL
// WEBGPU-144 Phase 2 (XNB-24): the content loaders reach the GPU-native block-compressed path.
// Phase 1 proved a Texture2D built directly with a Dxt* format + SetData(blocks) uploads to a
// WGPUTextureFormat_BC*. This test proves the two CONTENT loaders now do the same instead of
// force-decoding DXT to Color: Texture2D::FromStream (DDS) and the .xnb Texture2DReader.
//
// WebGPU opts in via LoadsCompressedContentNativelyEXT(); the actual per-format capability (and the
// bcSupported_ device gate) is IsCompressedTransferFormatEXT. The test asserts the CONTRACT rather
// than a fixed format: a loaded DXT texture keeps its compressed format exactly when the renderer
// reports it can transfer that format as blocks, and decodes to Color otherwise -- so it is correct
// on an adapter with or without BC support, while still proving the gate is wired.
//
// Check A -- FromStream(DDS DXT1, 8x8, 4 mips): Format() == Dxt1 (native) or Color (fallback),
//   matching the capability; the 4-level chain is preserved either way.
// Check B -- that same texture renders its level-0 red through SpriteBatch (proves the native BC
//   upload actually samples correctly end-to-end via FromStream, not just that a format was set).
// Check C -- .xnb Texture2DReader(DXT5, 8x8, 4 mips): Format() == Dxt5 (native) or Color, matching
//   the capability; the 4-level chain is preserved.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IO/Stream.hpp"

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Content;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;

    class VectorStream final : public System::IO::Stream
    {
    public:
        explicit VectorStream(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
        System::IO::intcs Read(System::IO::bytecs buffer[], System::IO::intcs offset,
                               System::IO::intcs count) override
        {
            const int remaining = static_cast<int>(bytes_.size()) - position_;
            const int read = std::min(count, remaining);
            if (read > 0)
            {
                std::memcpy(buffer + offset, bytes_.data() + position_,
                            static_cast<std::size_t>(read));
                position_ += read;
            }
            return read;
        }
        void Close() override {}
        [[nodiscard]] System::IO::intcs getLengthProperty() const override
        {
            return static_cast<System::IO::intcs>(bytes_.size());
        }
    private:
        const std::vector<std::uint8_t>& bytes_;
        int position_ = 0;
    };

    void WriteU32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v)
    {
        b[o + 0] = static_cast<std::uint8_t>(v);
        b[o + 1] = static_cast<std::uint8_t>(v >> 8);
        b[o + 2] = static_cast<std::uint8_t>(v >> 16);
        b[o + 3] = static_cast<std::uint8_t>(v >> 24);
    }
    void AppendU32(std::vector<std::uint8_t>& b, std::uint32_t v)
    {
        const std::size_t o = b.size();
        b.resize(o + 4u);
        WriteU32(b, o, v);
    }

    // A solid-colour DXT block; c0 = rgb565, c1/index = 0 so every texel selects c0. DXT5's alpha
    // endpoints are 0, so its decoded alpha is 0 -- fine here, the format assertion is the point.
    std::vector<std::uint8_t> SolidDxtBlock(std::uint32_t fourCC, std::uint16_t rgb565)
    {
        const bool dxt1 = fourCC == 0x31545844u;
        std::vector<std::uint8_t> block(dxt1 ? 8u : 16u, 0u);
        const std::size_t colorOffset = dxt1 ? 0u : 8u;
        block[colorOffset + 0] = static_cast<std::uint8_t>(rgb565);
        block[colorOffset + 1] = static_cast<std::uint8_t>(rgb565 >> 8);
        return block;
    }

    constexpr std::uint16_t kRed565 = 0xF800u;

    // 8x8, `mipCount` levels, every level a solid red block.
    std::vector<std::uint8_t> BuildDds(std::uint32_t fourCC, int mipCount)
    {
        std::vector<std::uint8_t> b(128u, 0u);
        b[0] = 'D'; b[1] = 'D'; b[2] = 'S'; b[3] = ' ';
        WriteU32(b, 4u, 124u);
        WriteU32(b, 12u, 8u);   // height
        WriteU32(b, 16u, 8u);   // width
        WriteU32(b, 28u, static_cast<std::uint32_t>(mipCount));
        WriteU32(b, 76u, 32u);
        WriteU32(b, 80u, 4u);   // DDPF_FOURCC
        WriteU32(b, 84u, fourCC);
        int w = 8, h = 8;
        for (int level = 0; level < mipCount; ++level)
        {
            const auto block = SolidDxtBlock(fourCC, kRed565);
            const int blocks = ((w + 3) / 4) * ((h + 3) / 4);
            for (int i = 0; i < blocks; ++i)
                b.insert(b.end(), block.begin(), block.end());
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
        return b;
    }

    std::vector<std::uint8_t> BuildXnbDxt5Body()
    {
        std::vector<std::uint8_t> b;
        AppendU32(b, static_cast<std::uint32_t>(SurfaceFormat::Dxt5));
        AppendU32(b, 8u);   // width
        AppendU32(b, 8u);   // height
        AppendU32(b, 4u);   // levelCount
        int w = 8, h = 8;
        for (int level = 0; level < 4; ++level)
        {
            const auto block = SolidDxtBlock(0x35545844u, kRed565);
            const int blocks = ((w + 3) / 4) * ((h + 3) / 4);
            AppendU32(b, static_cast<std::uint32_t>(blocks * static_cast<int>(block.size())));
            for (int i = 0; i < blocks; ++i)
                b.insert(b.end(), block.begin(), block.end());
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
        return b;
    }
}

class WebGpuCompressedContentTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D ddsTex_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    Texture2D LoadXnb(GraphicsDevice& device, const std::vector<std::uint8_t>& body)
    {
        ContentTypeReaderManager::ClearTypeCreators();
        CNA::Internal::Xnb::RegisterTexture2DXnbReader();
        ContentManager manager;
        manager.setGraphicsDevice(device);
        VectorStream stream(body);
        ContentReader reader(&manager, &stream, "webgpu-compressed-content", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.Texture2DReader");
        Texture2D t = std::any_cast<Texture2D>(typeReader->ReadUntyped(reader, std::any{}));
        ContentTypeReaderManager::ClearTypeCreators();
        return t;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        sb_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        auto dds = BuildDds(0x31545844u, 4);
        VectorStream stream(dds);
        ddsTex_ = Texture2D::FromStream(getGraphicsDeviceProperty(), stream);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = dev.GetRenderer();

        const bool nativeDxt1 = renderer.LoadsCompressedContentNativelyEXT()
            && renderer.IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt1));
        const bool nativeDxt5 = renderer.LoadsCompressedContentNativelyEXT()
            && renderer.IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt5));

        // Check A: FromStream DDS DXT1 keeps its compressed format iff the renderer can transfer it.
        const SurfaceFormat expectedDds = nativeDxt1 ? SurfaceFormat::Dxt1 : SurfaceFormat::Color;
        check(ddsTex_.getFormatProperty() == expectedDds && ddsTex_.getLevelCountProperty() == 4,
              nativeDxt1
                  ? "FromStream DDS DXT1 keeps native Dxt1 storage with its 4-level chain"
                  : "FromStream DDS DXT1 decodes to Color (renderer reports no BC transfer)");

        // Check B: the loaded texture renders red -- native BC upload samples correctly via FromStream.
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        sb_->Begin();
        sb_->Draw(ddsTex_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 8, 8), Color::White);
        sb_->End();
        Color pixel(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getRProperty() >= 200 && pixel.getGProperty() <= 55 && pixel.getBProperty() <= 55,
              "the DXT1 texture loaded via FromStream renders red");

        // Check C: .xnb Texture2DReader DXT5 keeps its compressed format iff transferable.
        auto xnb = BuildXnbDxt5Body();
        Texture2D xnbTex = LoadXnb(dev, xnb);
        const SurfaceFormat expectedXnb = nativeDxt5 ? SurfaceFormat::Dxt5 : SurfaceFormat::Color;
        check(xnbTex.getFormatProperty() == expectedXnb && xnbTex.getLevelCountProperty() == 4,
              nativeDxt5
                  ? "XNB Texture2DReader DXT5 keeps native Dxt5 storage with its 4-level chain"
                  : "XNB Texture2DReader DXT5 decodes to Color (renderer reports no BC transfer)");

        std::printf("=== %d/3 PASS (nativeDxt1=%d nativeDxt5=%d) ===\n",
                    passCount_, nativeDxt1 ? 1 : 0, nativeDxt5 ? 1 : 0);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    WebGpuCompressedContentTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuCompressedContentTest game;
    game.Run();
    return game.getResult();
}
