// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-200: the two optional 32-bit-float adapter features, and what this
// renderer does when they are absent.
//
// `WGPUFeatureName_Float32Filterable` and `Float32Blendable` are ordinary optional features.
// `R32Float`/`RG32Float`/`RGBA32Float` are RENDERABLE in core WebGPU without either, so neither
// absence is a reason to refuse the format -- what they add is sampling one with a filtering
// sampler, and blending into one. Both are requested at device creation when the adapter has them.
//
// THE PROBLEM THIS FILE SOLVES. Every adapter available to this project has both, so the code paths
// that depend on their absence would otherwise be branches no test ever executes -- asserted by
// reading them rather than by running them, which is how a refusal that throws the wrong type, or
// never fires at all, survives review. `DebugForceFloat32FeaturesAbsentEXT` forces the renderer to
// report both absent. It does NOT remove the real device features, so what runs here is CNA's own
// decision-making, which is precisely the part that can be wrong.
//
// Checks:
//   A  with the features present (this adapter), a float target blends without complaint
//   B  with them forced absent, a blend on a float target is REFUSED, by name, naming the feature
//   C  ... and the refusal is narrow: the same target still clears and draws opaquely
//   D  ... and it is narrow the other way too: a Color target still blends
//   E  the limitations text names the missing feature rather than staying silent
//   F  the flags restore, so the override leaks into nothing

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

class WebGpuFloat32FeaturesTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int checkCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    /// Renders one blended quad into a target of @p format and returns the failure, if any.
    std::string BlendInto(GraphicsDevice& device, SurfaceFormat format)
    {
        const VertexPositionColor quad[6] = {
            {Vector3(-0.8f,  0.8f, 0.0f), Color(200, 40, 20, 128)},
            {Vector3(-0.8f, -0.8f, 0.0f), Color(200, 40, 20, 128)},
            {Vector3( 0.8f, -0.8f, 0.0f), Color(200, 40, 20, 128)},
            {Vector3(-0.8f,  0.8f, 0.0f), Color(200, 40, 20, 128)},
            {Vector3( 0.8f, -0.8f, 0.0f), Color(200, 40, 20, 128)},
            {Vector3( 0.8f,  0.8f, 0.0f), Color(200, 40, 20, 128)},
        };
        try
        {
            RenderTarget2D target(device, 8, 8, false, format, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&target);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::AlphaBlend);
            device.Clear(Color(0, 0, 0, 255));
            BasicEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setLightingEnabledProperty(false);
            effect.setTextureEnabledProperty(false);
            effect.setVertexColorEnabledProperty(true);
            effect.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            // The refusal is raised while the pipeline is built, which happens at flush -- so the
            // frame has to actually be rendered before this is called a success.
            device.Present();
        }
        catch (const std::exception& e)
        {
            try { device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
            return e.what();
        }
        return {};
    }

    /// Clears and draws opaquely into a target of @p format; returns the failure, if any.
    std::string OpaqueInto(GraphicsDevice& device, SurfaceFormat format)
    {
        try
        {
            RenderTarget2D target(device, 8, 8, false, format, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(&target);
            device.setBlendStateProperty(BlendState::Opaque);
            device.Clear(Color(16, 32, 48, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            device.Present();
        }
        catch (const std::exception& e)
        {
            try { device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
            return e.what();
        }
        return {};
    }

public:
    WebGpuFloat32FeaturesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const { return result_; }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(device.GetRenderer());

        if (!device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        {
            std::printf("[INFO] this adapter renders into no 32-bit float format; WEBGPU-200's "
                        "refusal paths are not reachable and nothing is claimed\n");
            result_ = 0;
            Exit();
            return;
        }
        std::printf("[INFO] adapter features as created: Float32Filterable=%d Float32Blendable=%d\n",
                    renderer.Float32FilterableEXT() ? 1 : 0,
                    renderer.Float32BlendableEXT() ? 1 : 0);

        // A -- the present case. This adapter has the feature, so a blend must simply work.
        const std::string presentFailure = BlendInto(device, SurfaceFormat::Vector4);
        check(presentFailure.empty(),
              "A: with Float32Blendable present, a blended draw into a Vector4 target succeeds" +
                  (presentFailure.empty() ? std::string() : " -- got \"" + presentFailure + '"'));

        // B/C/D -- the absent case, forced, because no adapter here can produce it.
        renderer.DebugForceFloat32FeaturesAbsentEXT(true);

        const std::string absentFailure = BlendInto(device, SurfaceFormat::Vector4);
        std::printf("[INFO] with the features forced absent, the blend reports: %s\n",
                    absentFailure.empty() ? "(accepted)" : absentFailure.c_str());
        check(!absentFailure.empty(),
              "B: with Float32Blendable absent, a blended draw into a Vector4 target is REFUSED "
              "rather than silently rendered as an opaque overwrite");
        check(absentFailure.find("Float32Blendable") != std::string::npos,
              "B: the refusal names the adapter feature that was missing, so a caller can act on it");

        const std::string opaqueFailure = OpaqueInto(device, SurfaceFormat::Vector4);
        check(opaqueFailure.empty(),
              "C: the refusal is narrow -- the same float target still clears and draws opaquely" +
                  (opaqueFailure.empty() ? std::string() : " -- got \"" + opaqueFailure + '"'));

        const std::string colorBlendFailure = BlendInto(device, SurfaceFormat::Color);
        check(colorBlendFailure.empty(),
              "D: narrow the other way too -- a Color target still blends" +
                  (colorBlendFailure.empty() ? std::string()
                                             : " -- got \"" + colorBlendFailure + '"'));

        const std::string limitations{renderer.GetAdditionalLimitationsTextEXT()};
        check(limitations.find("Float32Blendable") != std::string::npos,
              "E: the limitations text names the missing feature rather than staying silent");

        // G/H -- the Float32Filterable half, same forced-absence technique. A game samples a render
        // target through BasicEffect.Texture, which is the route this refusal guards.
        {
            RenderTarget2D floatSource(device, 8, 8, false, SurfaceFormat::Vector4,
                                       DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
            device.SetRenderTarget(&floatSource);
            device.setBlendStateProperty(BlendState::Opaque);
            device.Clear(Color(200, 40, 20, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const auto sampleWith = [&](TextureFilter filter) -> std::string {
                const VertexPositionTexture quad[6] = {
                    {Vector3(-0.8f,  0.8f, 0.0f), Vector2(0.0f, 0.0f)},
                    {Vector3(-0.8f, -0.8f, 0.0f), Vector2(0.0f, 1.0f)},
                    {Vector3( 0.8f, -0.8f, 0.0f), Vector2(1.0f, 1.0f)},
                    {Vector3(-0.8f,  0.8f, 0.0f), Vector2(0.0f, 0.0f)},
                    {Vector3( 0.8f, -0.8f, 0.0f), Vector2(1.0f, 1.0f)},
                    {Vector3( 0.8f,  0.8f, 0.0f), Vector2(1.0f, 0.0f)},
                };
                try
                {
                    SamplerState sampler;
                    sampler.setFilterProperty(filter);
                    device.getSamplerStatesProperty()[0] = sampler;
                    device.setRasterizerStateProperty(RasterizerState::CullNone);
                    device.setBlendStateProperty(BlendState::Opaque);
                    BasicEffect effect(device);
                    effect.setWorldProperty(Matrix::getIdentityProperty());
                    effect.setViewProperty(Matrix::getIdentityProperty());
                    effect.setProjectionProperty(Matrix::getIdentityProperty());
                    effect.setLightingEnabledProperty(false);
                    effect.setTextureEnabledProperty(true);
                    effect.setTextureProperty(&floatSource);
                    effect.setVertexColorEnabledProperty(false);
                    effect.Apply();
                    device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
                    device.Present();
                }
                catch (const std::exception& e) { return e.what(); }
                return {};
            };

            const std::string linearFailure = sampleWith(TextureFilter::Linear);
            std::printf("[INFO] with the features forced absent, a LINEAR sample reports: %s\n",
                        linearFailure.empty() ? "(accepted)" : linearFailure.c_str());
            check(!linearFailure.empty() &&
                      linearFailure.find("Float32Filterable") != std::string::npos,
                  "G: with Float32Filterable absent, a FILTERING sample of a Vector4 target is "
                  "refused by name, naming the feature");
            const std::string pointFailure = sampleWith(TextureFilter::Point);
            check(pointFailure.empty(),
                  "H: ... and Point filtering still samples it, because a non-filtering sampler "
                  "needs no feature" +
                      (pointFailure.empty() ? std::string() : " -- got \"" + pointFailure + '"'));
        }

        // F -- and the override leaks into nothing.
        renderer.DebugForceFloat32FeaturesAbsentEXT(false);
        const std::string restored = BlendInto(device, SurfaceFormat::Vector4);
        check(restored.empty() && renderer.Float32BlendableEXT(),
              "F: clearing the override restores the real device capability");

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }
};

int main()
{
    WebGpuFloat32FeaturesTest test;
    test.Run();
    return test.Result();
}
