// SPDX-License-Identifier: MS-PL
// DX-244: public DirectX device-loss and long-lived resource recovery contract.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#if defined(CNA_RENDERER_DIRECTX11)
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
using ActiveRenderer = CNA::Internal::Renderers::DirectX11::DirectX11Renderer;
#elif defined(CNA_RENDERER_DIRECTX12)
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
using ActiveRenderer = CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
#else
#error This contract is for the DirectX renderer family.
#endif

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTargetSize = 16;
    constexpr int kRecoveryCycles = 16;
    const Color kClear(0, 64, 255, 255);
    const Color kDraw(255, 0, 0, 255);

    bool SameColor(const Color& actual, const Color& expected)
    {
        return actual.getRProperty() == expected.getRProperty() &&
               actual.getGProperty() == expected.getGProperty() &&
               actual.getBProperty() == expected.getBProperty() &&
               actual.getAProperty() == expected.getAProperty();
    }
}

class D3DContextRecoveryContract final : public CNA::Examples::PixelTestGame
{
    std::vector<std::string> deviceEvents_;
    int resourceCreated_ = 0;
    int resourceDestroyed_ = 0;
    int contentLost_ = 0;

    bool RenderAndRead(GraphicsDevice& device, RenderTarget2D& target,
                       Texture2D& texture, VertexBuffer& vertices,
                       IndexBuffer& indices, BasicEffect& effect,
                       int cycle)
    {
        device.SetRenderTarget(&target);
        device.Clear(kClear);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetVertexBuffer(&vertices);
        device.setIndicesProperty(&indices);

        effect.setTextureProperty(&texture);
        effect.Apply();
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::array<Color, kTargetSize * kTargetSize> pixels{};
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        const bool clearPassed = SameColor(pixels[0], kClear);
        const bool drawPassed = SameColor(
            pixels[static_cast<std::size_t>(kTargetSize / 2) * kTargetSize + kTargetSize / 2],
            kDraw);
        Check(clearPassed, ("cycle " + std::to_string(cycle) +
                            " clear survives device recovery").c_str());
        Check(drawPassed, ("cycle " + std::to_string(cycle) +
                           " indexed textured draw/readback survives device recovery").c_str());
        return clearPassed && drawPassed;
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto* renderer = dynamic_cast<ActiveRenderer*>(&device.GetRenderer());
        if (!ExpectTrue("active renderer is the expected DirectX implementation", renderer != nullptr))
            return;

        device.SetContextRecoveryEnabled(true);
        device.DeviceLost += [this](System::Object*, const System::EventArgs&) {
            deviceEvents_.emplace_back("Lost");
        };
        device.DeviceResetting += [this](System::Object*, const System::EventArgs&) {
            deviceEvents_.emplace_back("Resetting");
        };
        device.DeviceReset += [this](System::Object*, const System::EventArgs&) {
            deviceEvents_.emplace_back("Reset");
        };
        device.ResourceCreated +=
            [this](System::Object*, const ResourceCreatedEventArgs&) { ++resourceCreated_; };
        device.ResourceDestroyed +=
            [this](System::Object*, const ResourceDestroyedEventArgs&) { ++resourceDestroyed_; };

        Texture2D texture(device, 1, 1);
        texture.SetData(&kDraw, 1);

        const std::array<VertexPositionColorTexture, 4> quad{{
            {Vector3(-0.5f,  0.5f, 0.5f), Color::White, Vector2(0.0f, 0.0f)},
            {Vector3(-0.5f, -0.5f, 0.5f), Color::White, Vector2(0.0f, 1.0f)},
            {Vector3( 0.5f, -0.5f, 0.5f), Color::White, Vector2(1.0f, 1.0f)},
            {Vector3( 0.5f,  0.5f, 0.5f), Color::White, Vector2(1.0f, 0.0f)},
        }};
        const std::array<std::uint16_t, 6> indexData{{0, 1, 2, 0, 2, 3}};

        VertexBuffer vertices(device,
                              VertexPositionColorTexture::getVertexDeclarationStatic(),
                              static_cast<int>(quad.size()), BufferUsage::None);
        vertices.SetData(quad.data(), static_cast<int>(quad.size()));
        IndexBuffer indices(device, IndexElementSize::SixteenBits,
                            static_cast<int>(indexData.size()), BufferUsage::None);
        indices.SetData(indexData.data(), static_cast<int>(indexData.size()));
        RenderTarget2D target(device, kTargetSize, kTargetSize);
        target.ContentLost +=
            [this](System::Object*, const System::EventArgs&) { ++contentLost_; };

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&texture);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);

        const std::size_t trackedResources = device.GetTrackedResourceCount();
        const std::size_t recoverableResources = renderer->GetRecoverableResourceCountEXT();
        const int createdAfterSetup = resourceCreated_;
        const int destroyedAfterSetup = resourceDestroyed_;

        Check(renderer->CanBeginDrawEXT(), "renderer accepts draws before simulated loss");
        Check(recoverableResources >= 4,
              "texture, vertex buffer, index buffer and target are registered for recovery");
        RenderAndRead(device, target, texture, vertices, indices, effect, 0);

        for (int cycle = 1; cycle <= kRecoveryCycles; ++cycle)
        {
            deviceEvents_.clear();
            renderer->DebugSimulateContextLoss();

            const bool lostState = !renderer->CanBeginDrawEXT() &&
                device.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Lost;
            const bool lostEvent = deviceEvents_.size() == 1 && deviceEvents_[0] == "Lost";
            Check(lostState, ("cycle " + std::to_string(cycle) +
                              " rejects draws while device is lost").c_str());
            Check(lostEvent, ("cycle " + std::to_string(cycle) +
                              " raises only DeviceLost before restore").c_str());

            renderer->DebugRestoreContext();
            const bool eventOrder = deviceEvents_.size() == 3 &&
                deviceEvents_[0] == "Lost" && deviceEvents_[1] == "Resetting" &&
                deviceEvents_[2] == "Reset";
            const bool normalState = renderer->CanBeginDrawEXT() &&
                device.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Normal;
            const bool registriesStable =
                device.GetTrackedResourceCount() == trackedResources &&
                renderer->GetRecoverableResourceCountEXT() == recoverableResources;
            const bool resourceEventsStable = resourceCreated_ == createdAfterSetup &&
                                              resourceDestroyed_ == destroyedAfterSetup;
            const bool targetReportedLoss = contentLost_ == cycle &&
                                            target.getIsContentLostProperty();

            Check(eventOrder, ("cycle " + std::to_string(cycle) +
                               " raises Lost, Resetting, Reset exactly once in order").c_str());
            Check(normalState, ("cycle " + std::to_string(cycle) +
                                " returns device to Normal").c_str());
            Check(registriesStable, ("cycle " + std::to_string(cycle) +
                                     " does not leak registry entries").c_str());
            Check(resourceEventsStable, ("cycle " + std::to_string(cycle) +
                                          " does not fake resource create/destroy events").c_str());
            Check(targetReportedLoss, ("cycle " + std::to_string(cycle) +
                                       " reports RenderTarget2D content loss").c_str());
            RenderAndRead(device, target, texture, vertices, indices, effect, cycle);
            Check(!target.getIsContentLostProperty(),
                  ("cycle " + std::to_string(cycle) +
                   " clears target loss state when rebound").c_str());
        }

        device.DeviceLost.Clear();
        device.DeviceResetting.Clear();
        device.DeviceReset.Clear();
        device.ResourceCreated.Clear();
        device.ResourceDestroyed.Clear();
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<D3DContextRecoveryContract>();
}
