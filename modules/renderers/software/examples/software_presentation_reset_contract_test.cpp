// SPDX-License-Identifier: MS-PL
// SOFTWARE-127/181: renderer-neutral proof for meaningful presentation/reset behavior.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SoftwarePresentationResetContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int frame_ = 0;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        ++total_;
        if (condition)
            ++passed_;
    }

    void DrawFullScreen(GraphicsDevice& device, const Color& color, float depth)
    {
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        const VertexPositionColor vertices[6] = {
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3(-1.0f, -1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3( 1.0f,  1.0f, depth), color},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    Color ReadCenter(GraphicsDevice& device)
    {
        const auto& viewport = device.getViewportProperty();
        Color pixel;
        const Rectangle region(
            viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void CheckDepthAttachment(GraphicsDevice& device)
    {
        graphics_->setPreferredDepthStencilFormatProperty(DepthFormat::None);
        graphics_->ApplyChanges();
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        bool rejectedMissingDepth = false;
        try
        {
            device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                         Color::Black, 1.0f, 0);
        }
        catch (const System::InvalidOperationException&)
        {
            rejectedMissingDepth = true;
        }
        Check(rejectedMissingDepth,
              "DepthFormat::None rejects an explicit clear of its missing depth attachment");
        device.Clear(Color::Black);
        DrawFullScreen(device, Color::Red, 0.2f);
        DrawFullScreen(device, Color::Green, 0.8f);
        Check(ReadCenter(device).getGProperty() == 128,
              "DepthFormat::None removes depth rejection from the selected backbuffer");

        graphics_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24);
        graphics_->ApplyChanges();
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        DrawFullScreen(device, Color::Red, 0.2f);
        DrawFullScreen(device, Color::Green, 0.8f);
        Check(ReadCenter(device).getRProperty() == 255,
              "DepthFormat::Depth24 restores depth rejection on the selected backbuffer");
    }

    void CheckStencilAttachment(GraphicsDevice& device)
    {
        DepthStencilState requireOne;
        requireOne.setDepthBufferEnableProperty(false);
        requireOne.setDepthBufferWriteEnableProperty(false);
        requireOne.setStencilEnableProperty(true);
        requireOne.setStencilFunctionProperty(CompareFunction::Equal);
        requireOne.setReferenceStencilProperty(1);
        requireOne.setStencilPassProperty(StencilOperation::Keep);

        graphics_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24);
        graphics_->ApplyChanges();
        device.setDepthStencilStateProperty(requireOne);
        bool rejectedMissingStencil = false;
        try
        {
            device.Clear(ClearOptions::Target | ClearOptions::Stencil,
                         Color::Black, 1.0f, 0);
        }
        catch (const System::InvalidOperationException&)
        {
            rejectedMissingStencil = true;
        }
        Check(rejectedMissingStencil,
              "Depth24 rejects an explicit clear of its missing stencil attachment");
        device.Clear(Color::Black);
        DrawFullScreen(device, Color::Green, 0.5f);
        Check(ReadCenter(device).getGProperty() == 128,
              "Depth24 has no stencil attachment, so stencil testing is inactive");

        graphics_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        graphics_->ApplyChanges();
        device.setDepthStencilStateProperty(requireOne);
        device.Clear(ClearOptions::Target | ClearOptions::Stencil, Color::Black, 1.0f, 0);
        DrawFullScreen(device, Color::Green, 0.5f);
        Check(ReadCenter(device).getGProperty() == 0,
              "Depth24Stencil8 restores stencil rejection on the selected backbuffer");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        if (frame_ == 0)
        {
            Viewport custom(5, 7, 20, 18);
            device.setViewportProperty(custom);
            device.setScissorRectangleProperty(Rectangle(6, 8, 12, 10));
            ++frame_;
            return;
        }

        const auto beforeResize = device.getViewportProperty();
        Check(beforeResize.getXProperty() == 5 && beforeResize.getYProperty() == 7 &&
                  beforeResize.getWidthProperty() == 20 && beforeResize.getHeightProperty() == 18,
              "Present preserves a custom viewport when the backbuffer size is unchanged");
        const Rectangle beforeScissor = device.getScissorRectangleProperty();
        Check(beforeScissor == Rectangle(6, 8, 12, 10),
              "Present preserves a custom scissor rectangle when size is unchanged");

        graphics_->setPreferredBackBufferWidthProperty(80);
        graphics_->setPreferredBackBufferHeightProperty(60);
        graphics_->ApplyChanges();
        const auto afterResize = device.getViewportProperty();
        Check(afterResize.getXProperty() == 0 && afterResize.getYProperty() == 0 &&
                  afterResize.getWidthProperty() == 80 && afterResize.getHeightProperty() == 60,
              "Backbuffer resize resets Viewport to the complete new target");
        Check(device.getScissorRectangleProperty() == Rectangle(0, 0, 80, 60),
              "Backbuffer resize resets ScissorRectangle to the complete new target");

        RasterizerState rasterizer;
        rasterizer.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rasterizer);
        device.setBlendStateProperty(BlendState::Opaque);
        CheckDepthAttachment(device);
        CheckStencilAttachment(device);

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    SoftwarePresentationResetContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(48);
        graphics_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24);
    }

    int Result() const { return result_; }
};

int main()
{
    SoftwarePresentationResetContractTest game;
    game.Run();
    return game.Result();
}
