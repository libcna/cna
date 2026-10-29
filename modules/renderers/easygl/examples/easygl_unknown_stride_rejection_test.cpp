// SPDX-License-Identifier: MS-PL
// GLTF-157: an unknown raw vertex stride must fail loudly on EasyGL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class UnknownStrideRejectionTest final : public Game
{
    bool done_ = false;
    bool passed_ = false;

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        // VULKAN-139. GLTF-157's contract is that an unknown raw stride fails
        // LOUDLY -- it does not say when. EasyGL selects its native layout from the stride at
        // UPLOAD, so it can refuse there. A renderer that binds what the caller DECLARED and falls
        // back to the stride table only at DRAW time (the retired VULKAN renderer did, VULKAN-144;
        // VULKAN-152 declared exactly that boundary) stores an unknown stride without complaint,
        // and the draw is what refuses.
        //
        // Asserting the upload-time throw alone would therefore assert EasyGL's ARCHITECTURE, not
        // GLTF-157's contract. This asks for the refusal and lets it arrive at either point, while
        // still requiring that it (a) happen, (b) be a NotSupportedException and (c) name the
        // stride -- a refusal that does not say which stride is not "loudly".
        constexpr int kUnknownStride = 28;
        constexpr int kVertices = 3;
        const std::array<std::uint8_t, kUnknownStride * kVertices> bytes{};
        auto& dev = getGraphicsDeviceProperty();
        VertexBuffer buffer(dev, kVertices);
        std::string diagnostic = "no exception";
        const char* where = "upload";
        try
        {
            buffer.SetDataRaw(bytes.data(), kVertices, kUnknownStride);
        }
        catch (const System::NotSupportedException& e)
        {
            diagnostic = e.what();
        }
        if (diagnostic == "no exception")
        {
            // Nothing refused the upload. The draw must, then -- and a renderer that draws an
            // unknown stride without complaint is the failure this test exists to catch.
            where = "draw";
            try
            {
                // An effect has to be applied first, or DrawPrimitives refuses for that reason
                // instead and this test would pass on the wrong refusal.
                BasicEffect effect(dev);
                effect.Apply();
                dev.SetVertexBuffer(&buffer);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                // A deferred renderer records the draw and decides its vertex layout when the
                // frame is replayed, so the refusal -- if there is one -- arrives at Present.
                dev.Present();
            }
            catch (const System::NotSupportedException& e) { diagnostic = e.what(); }
            catch (const std::exception& e)                { diagnostic = e.what(); }
        }
        passed_ = diagnostic != "no exception" &&
                  diagnostic.find("28") != std::string::npos;

        std::printf("[%s] unknown stride %d is refused at %s: %s\n",
                    passed_ ? "PASS" : "FAIL", kUnknownStride, where, diagnostic.c_str());
        Exit();
    }

public:
    [[nodiscard]] int Result() const noexcept { return passed_ ? 0 : 1; }
};

int main()
{
    UnknownStrideRejectionTest game;
    game.Run();
    return game.Result();
}
