// SPDX-License-Identifier: MS-PL
// DX-215: public-path render-target SurfaceFormat behavior.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Graphics::PackedVector::HalfVector4;

namespace
{
#if defined(CNA_RENDERER_EASYGL)
    constexpr const char* kRendererName = "EasyGL";
#elif defined(CNA_RENDERER_SDLGPU)
    constexpr const char* kRendererName = "SDLGPU";
#else
    constexpr const char* kRendererName = "unknown";
#endif

#if defined(CNA_RENDERER_EASYGL)
    const char* kVertexShader = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPosition, 0.0, 1.0);
}
)";

    const char* kSinglePixelShader = R"(#version 300 es
precision highp float;
out vec4 FragColor;
void main() { FragColor = vec4(4.0, 0.0, 0.0, 1.0); }
)";

    const char* kHalfPixelShader = R"(#version 300 es
precision highp float;
out vec4 FragColor;
void main() { FragColor = vec4(2.0, -1.0, 0.5, 4.0); }
)";

    const char* kColorPixelShader = R"(#version 300 es
precision highp float;
out vec4 FragColor;
void main() { FragColor = vec4(0.25, 0.5, 0.75, 1.0); }
)";
#elif defined(CNA_RENDERER_SDLGPU)
    // RRC-046: SDLGPU compiles Vulkan GLSL to SPIR-V. The sprite custom-effect contract is the one
    // sdlgpu_mrt_test.cpp uses: vertex inputs 0-2 and the per-draw block at set 1, binding 0.
    const char* kVertexShader = R"(#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(set = 1, binding = 0) uniform PC {
    vec4 vpSize_pad;
    mat4 matrix;
    vec4 color;
    vec4 slot0_pad;
} pc;
void main() {
    vec2 ndc = (inPos / pc.vpSize_pad.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

    const char* kSinglePixelShader = R"(#version 450
layout(location = 0) out vec4 FragColor;
void main() { FragColor = vec4(4.0, 0.0, 0.0, 1.0); }
)";

    const char* kHalfPixelShader = R"(#version 450
layout(location = 0) out vec4 FragColor;
void main() { FragColor = vec4(2.0, -1.0, 0.5, 4.0); }
)";

    const char* kColorPixelShader = R"(#version 450
layout(location = 0) out vec4 FragColor;
void main() { FragColor = vec4(0.25, 0.5, 0.75, 1.0); }
)";
#else
    // The HLSL sources the retired DIRECTX11 renderer compiled were removed with it (RRC-046).
#error "rendertarget_surface_format_contract_test.cpp has ShaderEffect sources for EasyGL and SDLGPU"
#endif

    bool Near(float actual, float expected, float tolerance = 0.01f)
    {
        return std::abs(actual - expected) <= tolerance;
    }
}

class RenderTargetSurfaceFormatContract final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsManager_;
    std::unique_ptr<Texture2D> dummyTexture_;
    int passed_ = 0;
    int failed_ = 0;
    bool done_ = false;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        ok ? ++passed_ : ++failed_;
    }

    void DrawConstant(RenderTarget2D& target, ShaderEffect& effect)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(&target);

        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        SpriteBatch sprites(device);
        sprites.Begin(SpriteSortMode::Immediate, BlendState::Opaque, &point, &noDepth, &noCull,
                      &effect, Matrix::getIdentityProperty());
        sprites.Draw(*dummyTexture_, Rectangle(0, 0, target.getWidthProperty(), target.getHeightProperty()),
                     Rectangle(0, 0, 1, 1), Color::White, 0.0f, Vector2(0.0f, 0.0f),
                     SpriteEffects::None, 0.0f);
        sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void RunSingle(ShaderEffect& effect)
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, 4, 4, false, SurfaceFormat::Single, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        DrawConstant(target, effect);
        float value = -999.0f;
        const Rectangle centre(2, 2, 1, 1);
        target.GetData(0, &centre, &value, 0, 1);
        Check(target.getFormatProperty() == SurfaceFormat::Single,
              "Single target reports SurfaceFormat::Single");
        Check(Near(value, 4.0f), "Single target preserves a drawn value of 4.0");
    }

    void RunHalf(ShaderEffect& effect)
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, 4, 4, true, SurfaceFormat::HalfVector4,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        DrawConstant(target, effect);
        HalfVector4 packed;
        const Rectangle centre(2, 2, 1, 1);
        target.GetData(0, &centre, &packed, 0, 1);
        const Vector4 value = packed.ToVector4();
        Check(target.getFormatProperty() == SurfaceFormat::HalfVector4,
              "HalfVector4 target reports SurfaceFormat::HalfVector4");
        Check(Near(value.X, 2.0f) && Near(value.Y, -1.0f) && Near(value.Z, 0.5f) &&
                  Near(value.W, 4.0f),
              "HalfVector4 target preserves signed and HDR components");

        HalfVector4 mipPacked;
        target.GetData(2, nullptr, &mipPacked, 0, 1);
        const Vector4 mipValue = mipPacked.ToVector4();
        Check(Near(mipValue.X, 2.0f) && Near(mipValue.Y, -1.0f) &&
                  Near(mipValue.Z, 0.5f) && Near(mipValue.W, 4.0f),
              "HalfVector4 mip generation preserves format-native HDR values");
    }

    void RunColor(ShaderEffect& effect)
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, 4, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        DrawConstant(target, effect);
        Color value(0, 0, 0, 0);
        const Rectangle centre(2, 2, 1, 1);
        target.GetData(0, &centre, &value, 0, 1);
        Check(value == Color(64, 128, 191, 255),
              "Color target keeps the established RGBA8 result byte-for-byte");
    }

    /// A render-target format is a PREFERENCE in XNA: the constructor asks
    /// GraphicsAdapter.QueryRenderTargetFormat and allocates what it selects, so a compressed
    /// preference that no device can render into becomes Color rather than an exception
    /// (SOFTWARE-216, recovered from the shipped assemblies). This check first asserted a refusal
    /// (DX-215) and was not reconciled when the two lines of work met; it failed on every renderer
    /// with the framework's measured rule in place.
    void RunUnsupported()
    {
        try
        {
            RenderTarget2D substituted(getGraphicsDeviceProperty(), 4, 4, false,
                                       SurfaceFormat::Dxt1, DepthFormat::None);
            Check(substituted.getFormatProperty() == SurfaceFormat::Color,
                  "a compressed render-target preference is substituted with Color");
        }
        catch (const std::exception&)
        {
            Check(false, "a compressed render-target preference is substituted with Color");
        }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        dummyTexture_ = std::make_unique<Texture2D>(
            getGraphicsDeviceProperty(), 1, 1, false, SurfaceFormat::Color);
        const Color white = Color::White;
        dummyTexture_->SetData(&white, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        std::printf("Render-target SurfaceFormat contract on %s\n", kRendererName);

        ShaderEffect single(getGraphicsDeviceProperty(), kVertexShader, kSinglePixelShader);
        ShaderEffect half(getGraphicsDeviceProperty(), kVertexShader, kHalfPixelShader);
        ShaderEffect color(getGraphicsDeviceProperty(), kVertexShader, kColorPixelShader);
        Check(single.IsEffectValid(), "Single-output ShaderEffect compiles");
        Check(half.IsEffectValid(), "Half-output ShaderEffect compiles");
        Check(color.IsEffectValid(), "Color-output ShaderEffect compiles");

        try
        {
            if (single.IsEffectValid()) RunSingle(single);
            if (half.IsEffectValid()) RunHalf(half);
            if (color.IsEffectValid()) RunColor(color);
            RunUnsupported();
        }
        catch (const std::exception& e)
        {
            getGraphicsDeviceProperty().SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Check(false, std::string("unexpected exception: ") + e.what());
        }

        std::printf("SUMMARY: %d/%d checks passed\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    RenderTargetSurfaceFormatContract()
    {
        graphicsManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsManager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphicsManager_->setPreferredBackBufferWidthProperty(32);
        graphicsManager_->setPreferredBackBufferHeightProperty(32);
    }

    [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    RenderTargetSurfaceFormatContract game;
    game.Run();
    return game.Result();
}
