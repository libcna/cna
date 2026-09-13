// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2260: deterministic GL 4.1/extension classifier coverage plus a live
// context oracle for independently discovered modern features and truthful shader reporting.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>

using CNA::Internal::Renderers::OpenGL4::OpenGL4Renderer;
using CNA::Internal::Renderers::OpenGL4::GL4::ClassifyModernCapabilities;
using CNA::Internal::Renderers::OpenGL4::GL4::ModernCapabilityInputs;
using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GameTime;
using Microsoft::Xna::Framework::GraphicsDeviceManager;

namespace
{
    [[nodiscard]] ModernCapabilityInputs AllEntryPoints(const int major, const int minor)
    {
        ModernCapabilityInputs inputs;
        inputs.contextMajor = major;
        inputs.contextMinor = minor;
        inputs.computeEntryPoints = true;
        inputs.shaderStorageBufferEntryPoints = true;
        inputs.imageLoadStoreEntryPoints = true;
        inputs.textureArrayEntryPoints = true;
        inputs.indirectDrawingEntryPoints = true;
        inputs.timerQueryEntryPoints = true;
        inputs.baseInstanceEntryPoints = true;
        inputs.internalFormatQueryEntryPoints = true;
        return inputs;
    }
}

class OpenGL4ModernFeatureDiscoveryTest final : public Game
{
public:
    OpenGL4ModernFeatureDiscoveryTest()
        : graphics_(std::make_unique<GraphicsDeviceManager>(this))
    {
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(64);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int Result() const { return result_; }

protected:
    void Draw(const GameTime&) override
    {
        const ModernCapabilityInputs gl41Inputs = AllEntryPoints(4, 1);
        const auto gl41 = ClassifyModernCapabilities(gl41Inputs);
        Check(gl41.textureArraysNative, "GL 4.1 independently provides texture arrays");
        Check(gl41.indirectDrawingNative, "GL 4.1 independently provides indirect drawing");
        Check(gl41.gpuTimersNative, "GL 4.1 independently provides timer queries");
        Check(!gl41.computeShadersNative, "GL 4.1 alone does not report compute");
        Check(!gl41.shaderStorageBuffersNative, "GL 4.1 alone does not report SSBOs");
        Check(!gl41.imageLoadStoreNative, "GL 4.1 alone does not report image load/store");
        Check(!gl41.baseInstanceDrawingNative, "GL 4.1 alone does not report base instance");
        Check(!gl41.internalFormatQueriesNative,
              "GL 4.1 alone does not report full internal-format queries");

        ModernCapabilityInputs extensionInputs = gl41Inputs;
        extensionInputs.computeShaderExtension = true;
        extensionInputs.shaderStorageBufferExtension = true;
        extensionInputs.imageLoadStoreExtension = true;
        extensionInputs.baseInstanceExtension = true;
        extensionInputs.internalFormatQuery2Extension = true;
        const auto extensionCaps = ClassifyModernCapabilities(extensionInputs);
        Check(extensionCaps.computeShadersNative && extensionCaps.shaderStorageBuffersNative &&
                  extensionCaps.imageLoadStoreNative,
              "sufficient GL 4.1 extensions expose the three compute resource facts");
        Check(extensionCaps.baseInstanceDrawingNative,
              "base instance has its own extension route");
        Check(extensionCaps.internalFormatQueriesNative,
              "format queries have their own extension route");

        ModernCapabilityInputs gl43Inputs = AllEntryPoints(4, 3);
        const auto gl43 = ClassifyModernCapabilities(gl43Inputs);
        Check(gl43.computeShadersNative && gl43.shaderStorageBuffersNative &&
                  gl43.imageLoadStoreNative && gl43.textureArraysNative &&
                  gl43.indirectDrawingNative && gl43.gpuTimersNative &&
                  gl43.baseInstanceDrawingNative && gl43.internalFormatQueriesNative,
              "GL 4.3 core classifies every discovered group independently");

        gl43Inputs.indirectDrawingEntryPoints = false;
        const auto missingIndirect = ClassifyModernCapabilities(gl43Inputs);
        Check(!missingIndirect.indirectDrawingNative && missingIndirect.computeShadersNative &&
                  missingIndirect.gpuTimersNative && missingIndirect.internalFormatQueriesNative,
              "one missing entry-point group disables only its own feature");

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<OpenGL4Renderer&>(device.GetRenderer());
        const auto& live = renderer.GetModernCapabilitiesEXT();
        std::printf("OpenGL %d.%d modern facts: compute=%d ssbo=%d image=%d array=%d "
                    "indirect=%d timer=%d baseInstance=%d formatQuery=%d rgba16f=%d/%d "
                    "rgba32f=%d/%d\n",
                    live.contextMajor, live.contextMinor,
                    live.computeShadersNative, live.shaderStorageBuffersNative,
                    live.imageLoadStoreNative, live.textureArraysNative,
                    live.indirectDrawingNative, live.gpuTimersNative,
                    live.baseInstanceDrawingNative, live.internalFormatQueriesNative,
                    live.rgba16FloatRenderableKnown, live.rgba16FloatRenderable,
                    live.rgba32FloatRenderableKnown, live.rgba32FloatRenderable);

        Check(live.contextMajor > 4 ||
                  (live.contextMajor == 4 && live.contextMinor >= 1),
              "ordinary renderer creation retains the OpenGL 4.1 floor");
        Check(live.textureArraysNative, "live context independently exposes texture arrays");
        Check(live.indirectDrawingNative, "live context independently exposes indirect drawing");
        Check(live.gpuTimersNative, "live context independently exposes timer queries");
        if (live.contextMajor > 4 || (live.contextMajor == 4 && live.contextMinor >= 3))
        {
            Check(live.computeShadersNative && live.shaderStorageBuffersNative &&
                      live.imageLoadStoreNative,
                  "live GL 4.3+ context exposes compute, SSBO and image entry points");
            Check(live.internalFormatQueriesNative && live.rgba16FloatRenderableKnown &&
                      live.rgba32FloatRenderableKnown,
                  "live GL 4.3+ context returns independent float-format query results");
        }

        Check(device.ExecutesShaderEffectSourceEXT(),
              "OpenGL4 truthfully reports custom shader-source execution");
        Check(device.GetShaderDialectEXT() ==
                  CNA::Internal::Renderers::ShaderDialectEXT::GlslDesktop,
              "OpenGL4 reports the desktop GLSL dialect");
        Check(device.SupportsRendererFeatureEXT(
                  CNA::RendererFeature::ShaderEffectSourceExecution) &&
                  device.SupportsRendererFeatureEXT(
                      CNA::RendererFeature::ShaderDialectGlslDesktop),
              "the detailed public profile carries both truthful shader facts");
        Check(device.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                               CNA::ShaderStageEXT::Vertex) &&
                  device.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                                   CNA::ShaderStageEXT::Fragment),
              "OpenGL4 accepts desktop GLSL vertex and fragment payloads");
        Check(!device.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                                CNA::ShaderStageEXT::Compute) &&
                  !device.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::GlslEs,
                                                    CNA::ShaderStageEXT::Vertex),
              "OpenGL4 does not overclaim unimplemented compute or another GLSL dialect");

        Exit();
    }

private:
    void Check(const bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition)
            result_ = 1;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int result_ = 0;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL4ModernFeatureDiscoveryTest game;
    game.Run();
    return game.Result();
}
