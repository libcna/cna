// SPDX-License-Identifier: MS-PL
// A native presentation Reset (a resize) keeps every static VertexBuffer/IndexBuffer's contents
// and reports the dynamic buffers it does empty as content-lost.
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::DirectX9::DirectX9Renderer;

class BufferResetSurvivalTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> manager_;

    static std::array<VertexPositionColor, 4> Quad(float x0, float x1, float y0, float y1,
                                                   const Color& c)
    {
        return {{{Vector3(x0, y1, 0.5f), c}, {Vector3(x1, y1, 0.5f), c},
                 {Vector3(x1, y0, 0.5f), c}, {Vector3(x0, y0, 0.5f), c}}};
    }

    /// GetBackBufferData reads physical pixels; after the resize the logical 64x64 surface is
    /// letterboxed, so logical coordinates are mapped through the presented rectangle.
    void ExpectLogical(const char* label, int lx, int ly, const Color& expected)
    {
        auto& renderer = static_cast<DirectX9Renderer&>(getGraphicsDeviceProperty().GetRenderer());
        int vx, vy, vw, vh;
        renderer.GetDefaultViewportRect(vx, vy, vw, vh);
        std::uint8_t pixel[4]{};
        renderer.ReadBackbuffer(vx + lx * vw / 64, vy + ly * vh / 64, 1, 1, pixel);
        std::printf("%s: pixel=(%d,%d,%d)\n", label, pixel[0], pixel[1], pixel[2]);
        ExpectTrue(label, pixel[0] == expected.getRProperty() &&
                          pixel[1] == expected.getGProperty() &&
                          pixel[2] == expected.getBProperty());
    }

    std::unique_ptr<BasicEffect> fx_;
    std::unique_ptr<VertexBuffer> staticVb_, indexedVb_;
    std::unique_ptr<IndexBuffer> ib_;
    std::unique_ptr<DynamicVertexBuffer> dynamicVb_;
    int contentLostRaised_ = 0;

    // Left half: a static buffer drawn without indices. Right half: static vertex and index
    // buffers. Bottom-left quarter: a dynamic buffer.
    static constexpr float kZ = 0.5f;
    std::array<VertexPositionColor, 6> leftList_{}, cornerList_{};

    /// Everything is created before the first Present, which is where the renderer applies the
    /// window's real size with a native Reset -- the resources must live through it.
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        fx_ = std::make_unique<BasicEffect>(dev);
        fx_->VertexColorEnabled = true;
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());

        const auto left = Quad(-1.0f, 0.0f, -1.0f, 1.0f, Color::Lime);
        const auto right = Quad(0.0f, 1.0f, -1.0f, 1.0f, Color::Cyan);
        const auto corner = Quad(-1.0f, 0.0f, -1.0f, 0.0f, Color::Magenta);
        const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
        leftList_ = {left[0], left[1], left[2], left[0], left[2], left[3]};
        cornerList_ = {corner[0], corner[1], corner[2], corner[0], corner[2], corner[3]};

        staticVb_ = std::make_unique<VertexBuffer>(
            dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        staticVb_->SetData(leftList_.data(), 0, 6);
        indexedVb_ = std::make_unique<VertexBuffer>(
            dev, VertexPositionColor::getVertexDeclarationStatic(), 4, BufferUsage::WriteOnly);
        indexedVb_->SetData(right.data(), 0, 4);
        ib_ = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 6,
                                            BufferUsage::WriteOnly);
        ib_->SetData(indices.data(), 0, 6);
        dynamicVb_ = std::make_unique<DynamicVertexBuffer>(
            dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::WriteOnly);
        dynamicVb_->SetData(cornerList_.data(), 0, 6);
        dynamicVb_->ContentLost += [this](System::Object*, const System::EventArgs&) {
            ++contentLostRaised_;
        };
    }

    void DrawStatic(GraphicsDevice& dev)
    {
        dev.Clear(Color::Black);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        fx_->Apply();
        dev.SetVertexBuffer(staticVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(indexedVb_.get());
        dev.setIndicesProperty(ib_.get());
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        dev.setIndicesProperty(nullptr);
        dev.SetVertexBuffer(nullptr);
    }

    void CheckAfterReset(GraphicsDevice& dev, const char* when)
    {
        std::printf("-- %s\n", when);
        ExpectTrue("dynamic buffer reports the content the Reset emptied",
                   dynamicVb_->getIsContentLostProperty());
        DrawStatic(dev);
        ExpectLogical("static vertex buffer still holds its quad", 16, 16, Color::Lime);
        ExpectLogical("static index buffer still holds its indices", 48, 32, Color::Cyan);

        // XNA's contract for a lost dynamic buffer: write it again and it is usable.
        dynamicVb_->SetData(cornerList_.data(), 0, 6, SetDataOptions::Discard);
        ExpectTrue("SetData clears IsContentLost", !dynamicVb_->getIsContentLostProperty());
        fx_->Apply();
        dev.SetVertexBuffer(dynamicVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        ExpectLogical("re-filled dynamic buffer draws", 16, 48, Color::Magenta);
        ExpectLogical("the dynamic draw did not disturb the static one", 16, 16, Color::Lime);
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX9Renderer&>(dev.GetRenderer());

        // The first Present applies the window's size. Whether that took a Reset depends on the
        // window system, so the content-lost count is only pinned for the explicit resize below.
        dev.Clear(Color::Black);
        dev.Present();
        DrawStatic(dev);
        ExpectLogical("static vertex buffer draws after the first Present", 16, 16, Color::Lime);
        ExpectLogical("static index buffer draws after the first Present", 48, 32, Color::Cyan);

        contentLostRaised_ = 0;
        dynamicVb_->SetData(cornerList_.data(), 0, 6, SetDataOptions::Discard);
        getWindowProperty().EndScreenDeviceChange("", 160, 96);
        if (!ExpectTrue("resize Reset succeeded", renderer.TryBeginDrawEXT())) return;
        ExpectTrue("ContentLost raised once for the dynamic buffer", contentLostRaised_ == 1);
        CheckAfterReset(dev, "after the resize Reset");
    }

public:
    BufferResetSurvivalTest() : manager_(std::make_unique<GraphicsDeviceManager>(this))
    {
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }
};

int main() { return CNA::Examples::RunPixelTest<BufferResetSurvivalTest>(); }
