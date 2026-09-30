// SPDX-License-Identifier: MS-PL
// cna-killer KF-16: EasyGL's frame lease covered only BeginDraw..EndDraw and released the context
// at EndDraw, so a loading thread could take it between frames. The next Update then issued its
// GL calls on a thread with no current context: a SetRenderTarget, a Clear, an ApplyChanges were
// silently dropped, unless something earlier in the same Update happened to create a resource and
// rebind the context on the way.
//
// After one drawn frame, Update binds a render target created in LoadContent, clears it and
// unbinds it -- nothing else in that Update touches the device -- and the next Draw reads it back.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class UpdateTimeGlWorkTest final : public Game
{
public:
    UpdateTimeGlWorkTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
    }

    [[nodiscard]] int Result() const noexcept { return result_; }

protected:
    void LoadContent() override
    {
        target_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), 16, 16);
    }

    void Update(GameTime& gameTime) override
    {
        Game::Update(gameTime);
        if (draws_ >= 1 && !cleared_)
        {
            auto& device = getGraphicsDeviceProperty();
            device.SetRenderTarget(target_.get());
            device.Clear(Color::Green);
            device.SetRenderTarget(nullptr);
            cleared_ = true;
        }
        if (draws_ > 600)
        {
            std::printf("[FAIL] no frame was drawn after the Update-time clear\n");
            result_ = 1;
            Exit();
        }
    }

    void Draw(const GameTime& gameTime) override
    {
        getGraphicsDeviceProperty().Clear(Color::Black);
        Game::Draw(gameTime);
        ++draws_;
        if (!cleared_)
            return;
        std::vector<Color> pixels(256);
        target_->GetData(pixels.data(), 256);
        const bool pass = pixels[0] == Color::Green && pixels[255] == Color::Green;
        std::printf("[%s] a render target cleared from Update() holds the colour in the next Draw: "
                    "(%d,%d,%d,%d)\n",
                    pass ? "PASS" : "FAIL", pixels[0].getRProperty(), pixels[0].getGProperty(),
                    pixels[0].getBProperty(), pixels[0].getAProperty());
        result_ = pass ? 0 : 1;
        Exit();
    }

    void UnloadContent() override
    {
        target_.reset();
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<RenderTarget2D> target_;
    int draws_ = 0;
    bool cleared_ = false;
    int result_ = 1;
};

int main()
{
    UpdateTimeGlWorkTest game;
    game.Run();
    return game.Result();
}
