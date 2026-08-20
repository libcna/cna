// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-55: real-device Texture2D mip-level SetData/GetData round-trip test.
//
// Unlike the EasyGL precedent (easygl_texture2d_mip_test.cpp, Task 171), which explicitly notes its
// GetData() reads a pure CPU shadow buffer rather than the GPU, DiligentSampledTexture::GetData()
// (DiligentRenderer.hpp) is documented as reading "a sub-rectangle of a mip level back to
// the CPU through a staging texture" -- a genuine GPU round-trip. This test therefore exercises the
// real thing D3D11's own DX-126 exists to catch: other renderers (Vulkan, Bgfx) were found to
// silently no-op a non-zero mip level's SetData/GetData entirely (Task 867).
//
// A 4x4 mipMap texture has 3 mip levels: 4x4 (mip 0), 2x2 (mip 1), 1x1 (mip 2). Each level is
// uploaded a distinct solid colour and read back through Texture2D::GetData(level, ...), which
// forwards to a real staging-texture GPU readback, not a CPU cache. Colour values must not bleed
// across levels -- the "level 0 unaffected by level 1's own upload" check (mirroring DX-126's own
// extra rigor) proves UpdatePixelsLevel(1, ...) targeted the correct subresource, not level 0.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlack(0, 0, 0, 255);

    bool ColorEqual(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty();
    }
}

class DiligentMipTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    int passCount_ = 0;
    int checkCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label, const Color& got, const Color& want)
    {
        ++checkCount_;
        std::printf("[%s] %s: want=(%d,%d,%d) got=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", label,
                    want.getRProperty(), want.getGProperty(), want.getBProperty(),
                    got.getRProperty(), got.getGProperty(), got.getBProperty());
        if (ok)
            ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        // 4x4 texture with full mip chain: levels 0 (4x4), 1 (2x2), 2 (1x1).
        Texture2D tex(device, 4, 4, /*mipMap=*/true, SurfaceFormat::Color);

        std::vector<Color> red16(16, kRed);
        std::vector<Color> blue4(4, kBlue);
        std::vector<Color> green1(1, kGreen);

        tex.SetData(0, nullptr, red16.data(), 0, 16);
        tex.SetData(1, nullptr, blue4.data(), 0, 4);
        tex.SetData(2, nullptr, green1.data(), 0, 1);

        // mip 0: read back 16 pixels through a real staging-texture GPU readback -- all must be Red.
        {
            std::vector<Color> rb(16, kBlack);
            tex.GetData(0, nullptr, rb.data(), 0, 16);
            for (int i = 0; i < 16; ++i)
            {
                char label[64];
                std::snprintf(label, sizeof(label), "mip0 pixel(%d,%d) real GPU round-trip", i % 4,
                             i / 4);
                Check(ColorEqual(rb[i], kRed), label, rb[i], kRed);
            }
        }

        // mip 1: read back 4 pixels -- all must be Blue.
        {
            std::vector<Color> rb(4, kBlack);
            tex.GetData(1, nullptr, rb.data(), 0, 4);
            for (int i = 0; i < 4; ++i)
            {
                char label[64];
                std::snprintf(label, sizeof(label), "mip1 pixel(%d,%d) real GPU round-trip", i % 2,
                             i / 2);
                Check(ColorEqual(rb[i], kBlue), label, rb[i], kBlue);
            }
        }

        // mip 2: read back 1 pixel -- must be Green.
        {
            std::vector<Color> rb(1, kBlack);
            tex.GetData(2, nullptr, rb.data(), 0, 1);
            Check(ColorEqual(rb[0], kGreen), "mip2 pixel(0,0) real GPU round-trip", rb[0], kGreen);
        }

        // Re-read mip 0 AFTER mips 1 and 2 were uploaded -- proves those uploads targeted the
        // correct subresources, not level 0 (mirrors D3D11's own DX-126 extra rigor).
        {
            std::vector<Color> rb(16, kBlack);
            tex.GetData(0, nullptr, rb.data(), 0, 16);
            bool stillRed = true;
            for (int i = 0; i < 16; ++i)
                if (!ColorEqual(rb[i], kRed))
                    stillRed = false;
            Check(stillRed, "mip0 content unaffected by mip1/mip2 uploads", rb[0], kRed);
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = passCount_ == checkCount_ ? 0 : 1;
        Exit();
    }

public:
    DiligentMipTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(1);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(1);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentMipTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
