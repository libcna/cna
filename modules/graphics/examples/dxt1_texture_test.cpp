// SPDX-License-Identifier: MS-PL
// Task 125: EasyGL/Vulkan integration test — DXT1 texture loaded via FromStream,
// rendered, and pixel readback asserts correct colour.
//
// Constructs a minimal in-memory DDS file containing one 4×4 DXT1 block with all
// pixels solid red (R=255, G=0, B=0, A=255) and passes it through
// Texture2D::FromStream.  The decoded texture is then rendered as a full-screen
// sprite via SpriteBatch and read back.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IO/Stream.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// ---------------------------------------------------------------------------
// Minimal read-only in-memory Stream backed by a byte vector.
// ---------------------------------------------------------------------------
class InMemoryStream : public System::IO::Stream
{
    const std::vector<uint8_t>& data_;
    int pos_ = 0;
public:
    explicit InMemoryStream(const std::vector<uint8_t>& data) : data_(data) {}

    System::IO::intcs Read(System::IO::bytecs buffer[],
                           System::IO::intcs offset,
                           System::IO::intcs count) override
    {
        const int remaining = static_cast<int>(data_.size()) - pos_;
        const int n = std::min(count, remaining);
        if (n > 0)
        {
            std::memcpy(buffer + offset, data_.data() + pos_, static_cast<std::size_t>(n));
            pos_ += n;
        }
        return n;
    }

    void Close() override {}

    [[nodiscard]] System::IO::intcs getLengthProperty() const override
    {
        return static_cast<System::IO::intcs>(data_.size());
    }
};

// ---------------------------------------------------------------------------
// Minimal DDS file for a 4×4 DXT1 texture (one block = 8 bytes).
//
// DXT1 solid-red block: c0=0xF800 (red in RGB565), c1=0x0000 (black),
// lookup=0x00000000 → all 16 pixels select c0 → (255,0,0,255).
// ---------------------------------------------------------------------------
static std::vector<uint8_t> BuildSolidRedDxt1Dds()
{
    std::vector<uint8_t> buf(128 + 8, 0);

    // DDS magic "DDS "
    buf[0] = 'D'; buf[1] = 'D'; buf[2] = 'S'; buf[3] = ' ';

    // DDS_HEADER starts at byte 4:
    //   [4]  dwSize   = 124
    //   [12] dwHeight = 4
    //   [16] dwWidth  = 4
    //   [76] ddspf.dwSize  = 32
    //   [80] ddspf.dwFlags = DDPF_FOURCC = 4
    //   [84] ddspf.dwFourCC = 'DXT1'
    buf[4]  = 124;           // dwSize
    buf[12] = 4;             // dwHeight
    buf[16] = 4;             // dwWidth
    buf[76] = 32;            // ddspf.dwSize
    buf[80] = 4;             // ddspf.dwFlags = DDPF_FOURCC
    buf[84] = 'D'; buf[85] = 'X'; buf[86] = 'T'; buf[87] = '1'; // FourCC

    // 8-byte DXT1 block starting at offset 128
    buf[128] = 0x00; buf[129] = 0xF8;   // c0 = 0xF800 = red in RGB565
    buf[130] = 0x00; buf[131] = 0x00;   // c1 = 0x0000 = black
    // lookup = 0 → all 16 pixels use c0 (red)
    buf[132] = 0x00; buf[133] = 0x00; buf[134] = 0x00; buf[135] = 0x00;

    return buf;
}

class Dxt1TextureTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D                    tex_;
    bool                         done_   = false;
    int                          result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);

        // Load the DXT1 DDS via Texture2D::FromStream.
        auto dds = BuildSolidRedDxt1Dds();
        InMemoryStream stream(dds);
        tex_ = Texture2D::FromStream(device, stream);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 0, 255));
        device.SetDepthTestEnabled(false);

        sb_->Begin();
        sb_->Draw(tex_,
                  Rectangle(0, 0, W, H),
                  Rectangle(0, 0, 4, 4),
                  Color::White);
        sb_->End();

        const Rectangle region(W / 2, H / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);

        const bool pass = (pixel.getRProperty() >= 200 &&
                           pixel.getGProperty() <= 50  &&
                           pixel.getBProperty() <= 50);

        if (pass)
        {
            std::printf("[PASS] DXT1Texture: pixel=(%d,%d,%d,%d)\n",
                        pixel.getRProperty(), pixel.getGProperty(),
                        pixel.getBProperty(), pixel.getAProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] DXT1Texture: pixel=(%d,%d,%d,%d), expected (~255,0,0,*)\n",
                        pixel.getRProperty(), pixel.getGProperty(),
                        pixel.getBProperty(), pixel.getAProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    Dxt1TextureTest game;
    game.Run();
    return game.getResult();
}
