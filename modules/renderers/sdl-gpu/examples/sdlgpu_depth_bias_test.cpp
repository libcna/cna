// SPDX-License-Identifier: MS-PL
// REMED-GFX-051: SDL_GPU RasterizerState.DepthBias/SlopeScaleDepthBias regression.
//
// SDL 3.5 exposes constant factor, clamp, slope factor, and an explicit enable bit only in
// SDL_GPUGraphicsPipelineCreateInfo::rasterizer_state. Before this remediation CNA captured both
// public bias floats in ApplyRasterizerState, but RenderStateSnapshot, PipelineCacheKey, and
// FillRasterizerState omitted them. Every queued draw therefore replayed through the same
// zero-bias native pipeline.
//
// This is deliberately a public rendering regression first: moderately sized factors and exact
// or near-coplanar, distinct-colour geometry make the depth winner deterministic. It does not use
// the old Vulkan -1e6 flat-bias llvmpipe diagnostic as its oracle. Cache-cardinality checks then
// prove that each genuinely static SDL_GPU state gets exactly one reusable pipeline, while
// signed zero, target identity/dimensions, viewport state, and line-only state do not
// fragment the cache.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "common/PixelTestGame.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kTargetWidth = 96;
    constexpr int kTargetHeight = 72;

    // On the preferred D24 target, 4096 r-units is about 0.000244 of normalized depth: comfortably
    // observable for exact-coplanar tests without being an extreme clamp-to-an-endpoint value.
    // A slope factor of 2 acts on a real ~0.007/pixel screen-depth slope in this small target.
    constexpr float kConstantBias = 4096.0f;
    constexpr float kSlopeBias = 2.0f;

    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);

    bool IsRed(const Color& c)
    {
        return c.getRProperty() >= 200
            && c.getGProperty() <= 50
            && c.getBProperty() <= 50;
    }

    bool IsGreen(const Color& c)
    {
        return c.getGProperty() >= 200
            && c.getRProperty() <= 50
            && c.getBProperty() <= 50;
    }

    bool IsBlue(const Color& c)
    {
        return c.getBProperty() >= 200
            && c.getRProperty() <= 50
            && c.getGProperty() <= 50;
    }

    RasterizerState BiasState(float constantBias, float slopeBias)
    {
        RasterizerState state;
        state.setCullModeProperty(CullMode::None);
        state.setDepthBiasProperty(constantBias);
        state.setSlopeScaleDepthBiasProperty(slopeBias);
        return state;
    }

    DepthStencilState StrictLess()
    {
        DepthStencilState state;
        state.setDepthBufferFunctionProperty(CompareFunction::Less);
        return state;
    }

    const char* kCustomVertexSource = R"GLSL(
#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 fragUV;
layout(set = 1, binding = 0) uniform PC {
    vec4 vpSize_pad;
    mat4 matrix;
    vec4 color;
    vec4 slot0_pad;
} pc;
void main() {
    vec2 ndc = (inPos / pc.vpSize_pad.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    fragUV = inUV;
}
)GLSL";

    const char* kCustomFragmentSource = R"GLSL(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D uTexture;
layout(set = 3, binding = 0) uniform PC {
    vec4 vpSize_pad;
    mat4 matrix;
    vec4 color;
    vec4 slot0_pad;
} pc;
void main() {
    outColor = texture(uTexture, fragUV);
}
)GLSL";
}

class SdlGpuDepthBiasTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<Texture2D> greenTexture_;
    std::unique_ptr<ShaderEffect> customEffect_;
    int frame_ = 0;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;
    std::size_t beforeBackbufferPipelines_ = 0;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (ok)
            ++passed_;
    }

    static std::unique_ptr<BasicEffect> ApplyColorEffect(GraphicsDevice& device)
    {
        auto effect = std::make_unique<BasicEffect>(device);
        effect->setWorldProperty(Matrix::getIdentityProperty());
        effect->setViewProperty(Matrix::getIdentityProperty());
        effect->setProjectionProperty(Matrix::getIdentityProperty());
        effect->VertexColorEnabled = true;
        effect->Apply();
        return effect;
    }

    static std::array<VertexPositionColor, 3> Triangle(
        bool sloped, const Color& color, float flatDepth = 0.5f)
    {
        // SDL_GPU's cross-renderer clip-space depth is 0..1. Keep flat tests at 0.5 so both
        // positive and negative bias have room to move without near/far clamping.
        const float topDepth = sloped ? 0.25f : flatDepth;
        const float bottomDepth = sloped ? 0.75f : flatDepth;
        return {{
            {Vector3(0.0f, 0.82f, topDepth), color},
            {Vector3(0.82f, -0.72f, bottomDepth), color},
            {Vector3(-0.82f, -0.72f, bottomDepth), color},
        }};
    }

    static void DrawTriangle(GraphicsDevice& device, bool sloped, const Color& color,
                             bool indexed = false, float flatDepth = 0.5f)
    {
        const auto vertices = Triangle(sloped, color, flatDepth);
        const auto effect = ApplyColorEffect(device);
        if (!indexed)
        {
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      vertices.data(), 0, 1);
            return;
        }

        VertexBuffer vertexBuffer(
            device, VertexPositionColor::getVertexDeclarationStatic(),
            3, BufferUsage::None);
        vertexBuffer.SetData(vertices.data(), 0, 3);
        const std::uint16_t indices[3] = {0, 1, 2};
        IndexBuffer indexBuffer(
            device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
        indexBuffer.SetData(indices, 3);

        device.SetVertexBuffer(&vertexBuffer);
        device.SetIndexBuffer(&indexBuffer);
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffer(nullptr);
    }

    static void DrawLine(GraphicsDevice& device, const Color& color)
    {
        const VertexPositionColor vertices[2] = {
            {Vector3(-0.82f, 0.0f, 0.5f), color},
            {Vector3(0.82f, 0.0f, 0.5f), color},
        };
        const auto effect = ApplyColorEffect(device);
        device.DrawUserPrimitives(PrimitiveType::LineList, vertices, 0, 1);
    }

    static void BeginTarget(GraphicsDevice& device, RenderTarget2D& target,
                            const DepthStencilState& depthState,
                            float minDepth = 0.0f, float maxDepth = 1.0f)
    {
        device.SetRenderTarget(&target);
        Viewport viewport(
            0, 0,
            target.getWidthProperty(), target.getHeightProperty());
        viewport.setMinDepthProperty(minDepth);
        viewport.setMaxDepthProperty(maxDepth);
        device.setViewportProperty(viewport);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(depthState);
        device.Clear(Color::Black, 1.0f);
    }

    static Color ReadCenter(RenderTarget2D& target)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle center(
            target.getWidthProperty() / 2,
            target.getHeightProperty() / 2,
            1, 1);
        target.GetData(0, &center, &pixel, 0, 1);
        return pixel;
    }

    static std::vector<Color> ReadAll(RenderTarget2D& target)
    {
        std::vector<Color> pixels(
            static_cast<std::size_t>(target.getWidthProperty())
                * static_cast<std::size_t>(target.getHeightProperty()),
            Color(0, 0, 0, 0));
        const Rectangle whole(
            0, 0,
            target.getWidthProperty(), target.getHeightProperty());
        target.GetData(
            0, &whole, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    template <typename Predicate>
    static int Count(const std::vector<Color>& pixels, Predicate predicate)
    {
        return static_cast<int>(std::count_if(
            pixels.begin(), pixels.end(), predicate));
    }

    Color RenderTrianglePair(
        GraphicsDevice& device, RenderTarget2D& target, bool sloped,
        const RasterizerState& firstState, const Color& firstColor,
        const RasterizerState& secondState, const Color& secondColor,
        bool indexedSecond = false,
        float minDepth = 0.0f, float maxDepth = 1.0f)
    {
        BeginTarget(
            device, target, DepthStencilState::Default, minDepth, maxDepth);
        device.setRasterizerStateProperty(firstState);
        DrawTriangle(device, sloped, firstColor);
        device.setRasterizerStateProperty(secondState);
        DrawTriangle(device, sloped, secondColor, indexedSecond);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadCenter(target);
    }

    Color RenderThree(
        GraphicsDevice& device, RenderTarget2D& target, bool sloped,
        const RasterizerState& firstState,
        const RasterizerState& secondState,
        const RasterizerState& thirdState)
    {
        BeginTarget(device, target, DepthStencilState::Default);
        device.setRasterizerStateProperty(firstState);
        DrawTriangle(device, sloped, kRed);
        device.setRasterizerStateProperty(secondState);
        DrawTriangle(device, sloped, kGreen);
        device.setRasterizerStateProperty(thirdState);
        DrawTriangle(device, sloped, kBlue);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadCenter(target);
    }

    Color RenderSprite(
        GraphicsDevice& device, RenderTarget2D& target,
        const RasterizerState& spriteRasterizer,
        Microsoft::Xna::Framework::Graphics::Effect* customEffect = nullptr)
    {
        BeginTarget(device, target, DepthStencilState::Default);
        device.setRasterizerStateProperty(BiasState(0.0f, 0.0f));
        // Stock/custom sprite shaders emit clip-space z=0. Use the same reference depth here;
        // only positive bias is asserted for SpriteBatch, so near-plane clamping is irrelevant.
        DrawTriangle(device, false, kRed, /*indexed=*/false, /*flatDepth=*/0.0f);

        SpriteBatch sprites(device);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState depth = DepthStencilState::Default;
        RasterizerState rasterizer = spriteRasterizer;
        sprites.Begin(
            SpriteSortMode::Immediate, BlendState::Opaque, &point,
            &depth, &rasterizer, customEffect);
        sprites.Draw(
            *greenTexture_,
            Rectangle(0, 0, target.getWidthProperty(), target.getHeightProperty()),
            Rectangle(0, 0, 1, 1),
            Color::White);
        sprites.End();

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadCenter(target);
    }

    void RunRenderTargetChecks(GraphicsDevice& device)
    {
        auto& renderer =
            static_cast<SdlGpuRenderer&>(device.GetRenderer());

        const bool d24 = SDL_GPUTextureSupportsFormat(
            renderer.Device(), SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
        const bool d32 = SDL_GPUTextureSupportsFormat(
            renderer.Device(), SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
        std::printf(
            "[INFO] SDL=%d.%d.%d driver=%s D24S8=%d D32FS8=%d\n",
            SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION,
            SDL_GetGPUDeviceDriver(renderer.Device()), d24, d32);
        Check(
            SDL_MAJOR_VERSION == 3 && SDL_MINOR_VERSION == 5
                && SDL_MICRO_VERSION == 0,
            "installed SDL headers report exactly 3.5.0");
        Check(d24 || d32, "runtime exposes a depth-backed SDL_GPU target format");
        Check(
            renderer.IsDebugModeEnabledEXT(),
            "SDL_GPU debug mode is enabled for validation");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 0,
            "colored pipeline cache starts empty");
        Check(
            renderer.GetSpritePipelineCacheSizeEXT() == 0,
            "stock SpriteBatch pipeline cache starts empty");

        RenderTarget2D target(
            device, kTargetWidth, kTargetHeight, false,
            SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
            RenderTargetUsage::DiscardContents);

        const RasterizerState zero = BiasState(0.0f, 0.0f);
        const RasterizerState positiveConstant =
            BiasState(kConstantBias, 0.0f);
        const RasterizerState negativeConstant =
            BiasState(-kConstantBias, 0.0f);
        const RasterizerState positiveSlope =
            BiasState(0.0f, kSlopeBias);
        const RasterizerState negativeSlope =
            BiasState(0.0f, -kSlopeBias);
        const RasterizerState combined =
            BiasState(kConstantBias, kSlopeBias);
        const RasterizerState signedZero = BiasState(-0.0f, -0.0f);

        Check(
            IsGreen(RenderTrianglePair(
                device, target, false,
                zero, kRed, zero, kGreen)),
            "zero bias: later coplanar triangle wins LessEqual");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 1,
            "zero bias creates one disabled-bias triangle pipeline");

        Check(
            IsRed(RenderTrianglePair(
                device, target, false,
                zero, kRed, positiveConstant, kGreen)),
            "positive constant bias pushes the later flat triangle away");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 2,
            "positive constant bias creates exactly one pipeline variant");

        Check(
            IsGreen(RenderTrianglePair(
                device, target, false,
                negativeConstant, kGreen, zero, kRed)),
            "negative constant bias pulls the first flat triangle forward");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 3,
            "negative constant bias creates exactly one pipeline variant");

        Check(
            IsRed(RenderTrianglePair(
                device, target, true,
                zero, kRed, positiveSlope, kGreen)),
            "positive slope bias pushes the later sloped triangle away");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 4,
            "positive slope bias creates exactly one pipeline variant");

        Check(
            IsGreen(RenderTrianglePair(
                device, target, true,
                negativeSlope, kGreen, zero, kRed)),
            "negative slope bias pulls the first sloped triangle forward");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 5,
            "negative slope bias creates exactly one pipeline variant");

        Check(
            IsRed(RenderTrianglePair(
                device, target, true,
                zero, kRed, combined, kGreen)),
            "combined positive constant+slope bias pushes sloped geometry away");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 6,
            "combined constant+slope values create one distinct variant");

        Check(
            IsGreen(RenderTrianglePair(
                device, target, false,
                zero, kRed, positiveSlope, kGreen)),
            "slope-only bias contributes zero on flat triangle geometry");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 6,
            "flat use reuses the existing slope-only pipeline");

        Check(
            IsGreen(RenderTrianglePair(
                device, target, false,
                zero, kRed, signedZero, kGreen)),
            "negative signed zero remains the exact zero-bias behavior");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 6,
            "signed zero normalizes to the disabled-bias cache entry");

        Check(
            IsBlue(RenderThree(
                device, target, false,
                zero, positiveConstant, zero)),
            "A->B->A restores zero -> positive constant -> zero");
        Check(
            IsBlue(RenderThree(
                device, target, false,
                negativeConstant, positiveConstant, negativeConstant)),
            "A->B->A restores negative constant -> positive constant -> negative");
        Check(
            IsBlue(RenderThree(
                device, target, true,
                negativeConstant, positiveSlope, negativeConstant)),
            "A->B->A restores constant-only -> slope-only -> constant-only");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 6,
            "all A->B->A transitions reuse the six legitimate triangle variants");

        BeginTarget(device, target, DepthStencilState::Default);
        device.setRasterizerStateProperty(zero);
        DrawTriangle(device, false, kRed);
        auto source = std::make_unique<RasterizerState>(negativeConstant);
        device.setRasterizerStateProperty(*source);
        DrawTriangle(device, false, kGreen);
        source->setDepthBiasProperty(kConstantBias);
        device.setRasterizerStateProperty(*source);
        source.reset();
        DrawTriangle(device, false, kBlue);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(
            IsGreen(ReadCenter(target)),
            "queued draw keeps its complete bias snapshot after source mutation/destruction");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 6,
            "deferred source mutation reuses captured negative/positive variants");

        Check(
            IsRed(RenderTrianglePair(
                device, target, false,
                zero, kRed, positiveConstant, kGreen,
                /*indexedSecond=*/true)),
            "indexed draw path applies positive constant bias");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 6,
            "indexed and non-indexed draws share compatible pipelines");

        RenderTarget2D compatibleTarget(
            device, kTargetWidth / 2, kTargetHeight / 2, false,
            SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
            RenderTargetUsage::PreserveContents);
        Check(
            IsGreen(RenderTrianglePair(
                device, compatibleTarget, false,
                zero, kRed, zero, kGreen)),
            "second compatible RenderTarget2D object renders correctly");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 6,
            "target object identity and dimensions do not fragment the cache");

        Check(
            IsRed(RenderTrianglePair(
                device, target, false,
                zero, kRed, positiveConstant, kGreen,
                /*indexedSecond=*/false, 0.2f, 0.8f)),
            "positive bias remains correct with Viewport.MinDepth/MaxDepth=0.2/0.8");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 6,
            "viewport depth range is dynamic and excluded from pipeline identity");

        BeginTarget(device, target, StrictLess());
        device.setRasterizerStateProperty(zero);
        DrawLine(device, kRed);
        device.setRasterizerStateProperty(negativeConstant);
        DrawLine(device, kGreen);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const std::vector<Color> linePixels = ReadAll(target);
        Check(
            Count(linePixels, IsRed) > 8 && Count(linePixels, IsGreen) == 0,
            "native line-list primitives are not polygon-depth-biased");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 7,
            "line-list bias normalizes out to one topology pipeline");

        BeginTarget(device, target, DepthStencilState::DepthRead);
        device.setRasterizerStateProperty(zero);
        const auto nearTriangle = Triangle(false, kGreen);
        auto shiftedNear = nearTriangle;
        for (auto& vertex : shiftedNear)
            vertex.Position.Z = -0.3f;
        const auto nearEffect = ApplyColorEffect(device);
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, shiftedNear.data(), 0, 1);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        auto farTriangle = Triangle(false, kRed);
        for (auto& vertex : farTriangle)
            vertex.Position.Z = 0.3f;
        const auto farEffect = ApplyColorEffect(device);
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, farTriangle.data(), 0, 1);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(
            IsRed(ReadCenter(target)),
            "DepthRead tests but does not write; later Default draw remains valid");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 8,
            "DepthRead adds only its legitimate depth-write-disabled variant");

        RenderTarget2D depthless(
            device, kTargetWidth, kTargetHeight, false,
            SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        BeginTarget(device, depthless, DepthStencilState::None);
        device.setRasterizerStateProperty(zero);
        DrawTriangle(device, false, kRed);
        device.setRasterizerStateProperty(positiveConstant);
        DrawTriangle(device, false, kGreen);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(
            IsGreen(ReadCenter(depthless)),
            "depthless pass remains valid and bias cannot change color order");
        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 10,
            "depthless zero/enabled-bias compatibility adds exactly two variants");

        const Color spriteZero =
            RenderSprite(device, target, zero);
        Check(
            IsGreen(spriteZero),
            "stock SpriteBatch zero bias preserves equal-depth LessEqual draw");
        Check(
            renderer.GetSpritePipelineCacheSizeEXT() == 1,
            "stock SpriteBatch zero bias creates one disabled-bias pipeline");

        const Color spritePositive =
            RenderSprite(device, target, positiveConstant);
        Check(
            IsRed(spritePositive),
            "stock SpriteBatch positive constant bias pushes its triangles away");
        Check(
            renderer.GetSpritePipelineCacheSizeEXT() == 2,
            "stock SpriteBatch positive bias creates one enabled-bias variant");

        Check(
            IsGreen(RenderSprite(device, target, zero)),
            "stock SpriteBatch A->B->A restores zero bias");
        Check(
            renderer.GetSpritePipelineCacheSizeEXT() == 2,
            "stock SpriteBatch A->B->A reuses both variants");

        Check(
            IsGreen(RenderSprite(device, target, positiveSlope)),
            "stock SpriteBatch flat triangles get zero slope contribution");
        Check(
            renderer.GetSpritePipelineCacheSizeEXT() == 3,
            "stock SpriteBatch slope-only state adds one legitimate variant");

        Check(
            customEffect_->IsEffectValid(),
            "custom SDL_GPU ShaderEffect compiled for pipeline-family coverage");
        auto* customRenderer = dynamic_cast<SdlGpuEffectRenderer*>(
            customEffect_->GetEffectRendererPtr());
        Check(
            customRenderer != nullptr,
            "custom effect exposes the SDL_GPU pipeline family");
        if (customRenderer != nullptr)
        {
            Check(
                IsGreen(RenderSprite(
                    device, target, zero, customEffect_.get())),
                "custom-effect SpriteBatch zero-bias pipeline renders correctly");
            Check(
                customRenderer->GetPipelineCacheSizeEXT() == 1,
                "custom-effect zero bias creates one disabled-bias pipeline");
            Check(
                IsGreen(RenderSprite(
                    device, target, positiveConstant, customEffect_.get())),
                "custom-effect enabled-bias pipeline remains validation-clean");
            Check(
                customRenderer->GetPipelineCacheSizeEXT() == 2,
                "custom-effect positive bias creates one cache variant");
            Check(
                IsGreen(RenderSprite(
                    device, target, zero, customEffect_.get())),
                "custom-effect A->B->A restores zero bias");
            Check(
                customRenderer->GetPipelineCacheSizeEXT() == 2,
                "custom-effect A->B->A reuses both variants");
        }

        Check(
            renderer.GetColoredPipelineCacheSizeEXT() == 10,
            "RT pipeline cardinality is exactly 10: 6 triangle bias, line, DepthRead, and 2 depthless variants");

        // Queue one ordinary biased backbuffer draw. Game::Run performs the normal presentation
        // after this Draw override returns; the next frame checks whether the backbuffer format
        // required one additional compatible pipeline. No test-only Present/submission is added.
        beforeBackbufferPipelines_ =
            renderer.GetColoredPipelineCacheSizeEXT();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(positiveConstant);
        device.Clear(Color::Black, 1.0f);
        DrawTriangle(device, false, kRed);
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        greenTexture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(
                device, 1, 1,
                std::vector<std::uint8_t>{0, 255, 0, 255}));
        customEffect_ = std::make_unique<ShaderEffect>(
            device, kCustomVertexSource, kCustomFragmentSource);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& device = getGraphicsDeviceProperty();
        auto& renderer =
            static_cast<SdlGpuRenderer&>(device.GetRenderer());

        if (frame_ == 1)
        {
            RunRenderTargetChecks(device);
            return;
        }

        const std::size_t afterBackbuffer =
            renderer.GetColoredPipelineCacheSizeEXT();
        Check(
            afterBackbuffer >= beforeBackbufferPipelines_
                && afterBackbuffer <= beforeBackbufferPipelines_ + 1,
            "backbuffer native execution adds at most one format-compatible bias pipeline");
        std::printf(
            "[INFO] colored cache: RT=%zu, after backbuffer=%zu\n",
            beforeBackbufferPipelines_, afterBackbuffer);
        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = (passed_ == total_) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuDepthBiasTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kTargetWidth);
        graphics_->setPreferredBackBufferHeightProperty(kTargetHeight);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int GetResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuDepthBiasTest game;
    game.Run();
    return game.GetResult();
}
