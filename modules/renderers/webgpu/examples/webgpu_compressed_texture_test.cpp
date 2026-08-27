// SPDX-License-Identifier: MS-PL
// WEBGPU-144: GPU-native block-compressed (DXT1 / BC1) textures.
//
// Creates a 4x4 SurfaceFormat::Dxt1 Texture2D and SetData()s a single solid-red DXT1 block. The
// renderer uploads the raw 8-byte BC1 block to a WGPUTextureFormat_BC1RGBAUnorm texture (no CPU
// decompression); the GPU decodes it at sample time. Drawing the texture and reading the backbuffer
// proves the native decode; GetData() proves the exact block bytes round-trip.
//
// DXT1 solid-red block: c0 = 0xF800 (red in RGB565), c1 = 0x0000 (black), all 16 2-bit indices = 0,
// so every texel selects c0 -> solid red (255,0,0).
//
// Check A -- the sampled centre reads RED: the GPU natively decoded the BC1 block.
// Check B -- GetData() returns the 8 block bytes byte-for-byte (the renderer is the authoritative
//   compressed store).
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // c0 = 0xF800 (red), c1 = 0x0000 (black), indices = 0. c0 > c1 -> 4-colour mode, index 0 = c0.
    constexpr std::array<std::uint8_t, 8> kSolidRedDxt1{0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // DXT5/BC3 opaque-red block (16 bytes: 8 alpha + 8 colour). alpha0=alpha1=0xFF, alpha idx=0 ->
    // every texel alpha 255; colour is the same solid-red DXT1 block. Exercises the 16-byte path.
    constexpr std::array<std::uint8_t, 16> kSolidRedDxt5{
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // alpha
        0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // colour

    bool colorNear(Color a, Color b, int tol = 16)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class WebGpuCompressedTextureTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<Texture2D> tex_;
    std::unique_ptr<Texture2D> tex5_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        tex_ = std::make_unique<Texture2D>(dev, 4, 4, false, SurfaceFormat::Dxt1);
        tex_->SetData(kSolidRedDxt1.data(), static_cast<int>(kSolidRedDxt1.size()));
        tex5_ = std::make_unique<Texture2D>(dev, 4, 4, false, SurfaceFormat::Dxt5);
        tex5_->SetData(kSolidRedDxt5.data(), static_cast<int>(kSolidRedDxt5.size()));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::Black);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        sb_->Draw(*tex_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 4, 4), Color::White);
        sb_->End();

        // Check A: the GPU natively decoded the BC1 block -> red.
        check(colorNear(readCenter(dev), Color::Red),
              "DXT1/BC1 texture sampled natively renders red (GPU-decoded the block)");

        // Check B: GetData round-trips the exact block bytes.
        std::array<std::uint8_t, 8> readback{};
        tex_->GetData(readback.data(), 0, static_cast<int>(readback.size()));
        check(readback == kSolidRedDxt1,
              "GetData returns the exact 8 DXT1 block bytes (compressed round-trip)");

        // Check C/D: the 16-byte block path (DXT5/BC3) -- native decode + exact round-trip.
        dev.Clear(Color::Black);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        sb_->Draw(*tex5_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 4, 4), Color::White);
        sb_->End();
        check(colorNear(readCenter(dev), Color::Red),
              "DXT5/BC3 texture sampled natively renders red (16-byte block path)");
        std::array<std::uint8_t, 16> readback5{};
        tex5_->GetData(readback5.data(), 0, static_cast<int>(readback5.size()));
        check(readback5 == kSolidRedDxt5,
              "GetData returns the exact 16 DXT5 block bytes (compressed round-trip)");

        std::printf("=== %d/4 PASS ===\n", passCount_);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    WebGpuCompressedTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuCompressedTextureTest game;
    game.Run();
    return game.getResult();
}
