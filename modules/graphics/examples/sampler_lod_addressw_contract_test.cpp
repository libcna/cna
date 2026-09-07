// SPDX-License-Identifier: MS-PL
//
// plans/plan_dx.md DX-257: differential public-path proof for the three SamplerState fields that
// construction-only tests cannot validate. Every mip and volume slice has a distinct flat colour,
// so an incorrect native field mapping identifies itself in the readback.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
#if defined(CNA_RENDERER_DIRECTX11)
    constexpr const char* kRendererName = "D3D11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr const char* kRendererName = "D3D12";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr const char* kRendererName = "EasyGL";
#else
    constexpr const char* kRendererName = "unknown";
#endif

    const Color kLevel0(255, 0, 0, 255);
    const Color kLevel1(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kYellow(255, 255, 0, 255);

#if defined(CNA_RENDERER_EASYGL)
    const char* kVolumeVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
uniform mat4 projection;
out vec4 vColor;
void main() {
    gl_Position = projection * vec4(aPosition, 0.0, 1.0);
    vColor = aColor;
}
)";

    const char* kVolumePixelShader = R"(#version 330 core
uniform sampler3D VolumeSampler;
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = texture(VolumeSampler, vec3(0.5, 1.25, 1.25)) * vColor;
}
)";
#else
    const char* kVolumeVertexShader = R"(
struct VSIn { float2 pos : POSITION0; float2 uv : TEXCOORD0; float4 color : COLOR0; };
struct VSOut { float4 pos : SV_Position; float4 color : TEXCOORD0; };
cbuffer CB : register(b0) { float4 vpSize; float4 pad1[4]; float4 uColor; float4 uFloat0; };
VSOut main(VSIn input) {
    VSOut output;
    float2 ndc = (input.pos / vpSize.xy) * 2.0 - 1.0;
    output.pos = float4(ndc.x, -ndc.y, 0.0, 1.0);
    output.color = input.color;
    return output;
}
)";

    const char* kVolumePixelShader = R"(
Texture3D VolumeSampler : register(t0);
SamplerState VolumeSamplerState : register(s0);
struct PSIn { float4 pos : SV_Position; float4 color : TEXCOORD0; };
float4 main(PSIn input) : SV_Target {
    return VolumeSampler.Sample(VolumeSamplerState, float3(0.5, 1.25, 1.25)) * input.color;
}
)";
#endif

    bool Near(const Color& actual, const Color& expected)
    {
        const auto close = [](int a, int b) { return std::abs(a - b) <= 4; };
        return close(actual.getRProperty(), expected.getRProperty()) &&
               close(actual.getGProperty(), expected.getGProperty()) &&
               close(actual.getBProperty(), expected.getBProperty()) &&
               close(actual.getAProperty(), expected.getAProperty());
    }

    std::string Describe(const Color& color)
    {
        return "(" + std::to_string(color.getRProperty()) + "," +
               std::to_string(color.getGProperty()) + "," +
               std::to_string(color.getBProperty()) + "," +
               std::to_string(color.getAProperty()) + ")";
    }
}

class SamplerLodAddressWContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsManager_;
    std::unique_ptr<Texture2D> mipTexture_;
    std::unique_ptr<Texture2D> dummyTexture_;
    std::unique_ptr<Texture3D> volumeTexture_;
    std::unique_ptr<Texture3D> decoyVolume_;
    std::unique_ptr<ShaderEffect> volumeEffect_;
    bool done_ = false;
    int checks_ = 0;
    int passes_ = 0;

    static void FillMip(Texture2D& texture, int level, int size, const Color& color)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), color);
        texture.SetData(level, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
    }

    void CheckColor(const char* label, const Color& actual, const Color& expected)
    {
        const bool ok = Near(actual, expected);
        std::printf("[%s] %s: got=%s expected=%s\n", ok ? "PASS" : "FAIL", label,
                    Describe(actual).c_str(), Describe(expected).c_str());
        ++checks_;
        if (ok) ++passes_;
    }

    Color DrawTexture2D(const SamplerState& sampler)
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&target);
        device.setViewportProperty(Viewport(0, 0, 8, 8));
        device.Clear(Color(17, 19, 23, 255));

        SpriteBatch sprites(device);
        sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr,
                      nullptr, Matrix::getIdentityProperty());
        sprites.Draw(*mipTexture_, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8), Color::White,
                     0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
        sprites.End();

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color centre(0, 0, 0, 0);
        const Rectangle pixel(4, 4, 1, 1);
        target.GetData(0, &pixel, &centre, 0, 1);
        return centre;
    }

    Color DrawTexture3D(TextureAddressMode addressV, TextureAddressMode addressW)
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&target);
        device.setViewportProperty(Viewport(0, 0, 8, 8));
        device.Clear(Color(17, 19, 23, 255));

        SamplerState sampler;
        sampler.setFilterProperty(TextureFilter::Point);
        sampler.setAddressUProperty(TextureAddressMode::Clamp);
        sampler.setAddressVProperty(addressV);
        sampler.setAddressWProperty(addressW);
        volumeEffect_->SetTexture(0, *volumeTexture_);

        SpriteBatch sprites(device);
        sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr,
                      volumeEffect_.get(), Matrix::getIdentityProperty());
        sprites.Draw(*dummyTexture_, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White,
                     0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
        sprites.End();

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color centre(0, 0, 0, 0);
        const Rectangle pixel(4, 4, 1, 1);
        target.GetData(0, &pixel, &centre, 0, 1);
        return centre;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        mipTexture_ = std::make_unique<Texture2D>(device, 8, 8, true, SurfaceFormat::Color);
        int size = 8;
        const std::array<Color, 4> mipColors{{kLevel0, kLevel1, kBlue, kYellow}};
        for (int level = 0; level < mipTexture_->getLevelCountProperty(); ++level)
        {
            FillMip(*mipTexture_, level, size, mipColors[static_cast<std::size_t>(std::min(level, 3))]);
            size = std::max(1, size / 2);
        }

        dummyTexture_ = std::make_unique<Texture2D>(device, 1, 1, false, SurfaceFormat::Color);
        const std::array<Color, 1> dummy{{Color(255, 255, 255, 255)}};
        dummyTexture_->SetData(dummy.data(), static_cast<int>(dummy.size()));

        // Layout is [z][y][x]. Each V/W combination names itself: z0/y0 red, z0/y1 blue,
        // z1/y0 green, z1/y1 yellow.
        volumeTexture_ = std::make_unique<Texture3D>(device, 1, 2, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> volume{{kLevel0, kBlue, kLevel1, kYellow}};
        volumeTexture_->SetData(volume.data(), static_cast<int>(volume.size()));

        // Upload a decoy last so a stale upload-time Texture3D binding cannot satisfy the oracle.
        decoyVolume_ = std::make_unique<Texture3D>(device, 1, 2, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> black{{Color::Black, Color::Black, Color::Black, Color::Black}};
        decoyVolume_->SetData(black.data(), static_cast<int>(black.size()));

        volumeEffect_ = std::make_unique<ShaderEffect>(device, kVolumeVertexShader, kVolumePixelShader);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        std::printf("Sampler LOD/AddressW contract on %s\n", kRendererName);

        SamplerState baseline = SamplerState::PointClamp;
        baseline.setMaxMipLevelProperty(0);
        baseline.setMipMapLevelOfDetailBiasProperty(0.0f);
        CheckColor("baseline selects mip level 0", DrawTexture2D(baseline), kLevel0);

        SamplerState maxMip = baseline;
        maxMip.setMaxMipLevelProperty(1);
        CheckColor("MaxMipLevel=1 selects mip level 1", DrawTexture2D(maxMip), kLevel1);
        CheckColor("MaxMipLevel state does not leak", DrawTexture2D(baseline), kLevel0);

        SamplerState biased = baseline;
        biased.setMipMapLevelOfDetailBiasProperty(1.0f);
        CheckColor("LOD bias +1 selects mip level 1", DrawTexture2D(biased), kLevel1);
        CheckColor("LOD bias state does not leak", DrawTexture2D(baseline), kLevel0);

        if (!volumeEffect_->IsEffectValid())
        {
            std::printf("[FAIL] Texture3D ShaderEffect compile: %s\n",
                        volumeEffect_->GetCompileErrorEXT().c_str());
            ++checks_;
        }
        else
        {
            // Sample v=1.25,w=1.25. These three independent states select three different voxels.
            CheckColor("AddressV=Clamp AddressW=Wrap selects z0/y1",
                       DrawTexture3D(TextureAddressMode::Clamp, TextureAddressMode::Wrap), kBlue);
            CheckColor("AddressV=Clamp AddressW=Clamp selects z1/y1",
                       DrawTexture3D(TextureAddressMode::Clamp, TextureAddressMode::Clamp), kYellow);
            CheckColor("AddressV=Wrap AddressW=Clamp selects z1/y0",
                       DrawTexture3D(TextureAddressMode::Wrap, TextureAddressMode::Clamp), kLevel1);
            CheckColor("AddressW state does not leak",
                       DrawTexture3D(TextureAddressMode::Clamp, TextureAddressMode::Wrap), kBlue);
        }

        std::printf("SUMMARY: %d/%d checks passed\n", passes_, checks_);
        Exit();
    }

public:
    SamplerLodAddressWContractTest()
    {
        graphicsManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsManager_->setPreferredBackBufferWidthProperty(64);
        graphicsManager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const { return passes_ == checks_ ? 0 : 1; }
};

int main()
{
    SamplerLodAddressWContractTest game;
    game.Run();
    return game.Result();
}
