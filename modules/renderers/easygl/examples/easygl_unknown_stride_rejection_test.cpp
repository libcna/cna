// SPDX-License-Identifier: MS-PL
// plan_gltf.md GLTF-157: an unknown raw vertex stride must fail loudly on EasyGL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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

        constexpr int kUnknownStride = 28;
        const std::array<std::uint8_t, kUnknownStride> bytes{};
        VertexBuffer buffer(getGraphicsDeviceProperty(), 1);
        std::string diagnostic = "no exception";
        try
        {
            buffer.SetDataRaw(bytes.data(), 1, kUnknownStride);
        }
        catch (const System::NotSupportedException& e)
        {
            diagnostic = e.what();
            passed_ = diagnostic.find("28") != std::string::npos &&
                      diagnostic.find("VertexDeclaration") != std::string::npos;
        }

        std::printf("[%s] unknown stride rejection: %s\n",
                    passed_ ? "PASS" : "FAIL", diagnostic.c_str());
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
