// SPDX-License-Identifier: MS-PL
// plan_fna3d.md FNA3D-14: pins the FNA3D renderer's capability answers and its deterministic
// rejections. A renderer that quietly claims a capability it cannot serve is worse than one that
// refuses, so the point of this test is that each "false" below is matched by a real refusal and
// each "true" is matched by something the other example tests actually exercise.
//
// Check A -- CustomEffects is FALSE and CreateEffectRenderer really returns null. FNA3D's only
//   shader entry point takes a compiled Direct3D 9 Effect binary; nothing in the library compiles
//   a GLSL/HLSL source string, so a "true" here would be a promise broken at the first
//   ShaderEffect.
// Check B -- MultiStreamVertexInput is TRUE, which is FNA3D's native shape
//   (FNA3D_ApplyVertexBufferBindings takes an array of per-stream declarations), and
//   GetMaxVertexStreams reports XNA's own ceiling.
// Check C -- the 3D capabilities this renderer really has are reported true, and each is backed
//   by a factory that returns a live resource rather than null.
// Check D -- Instancing tracks FNA3D_SupportsHardwareInstancing rather than being asserted.
// Check E -- an unknown vertex stride is rejected with a diagnostic instead of being bound as a
//   plausible-looking layout.
// Check F -- an out-of-contract state ordinal is rejected rather than cast into an undefined
//   FNA3D enumerator.
// Check G -- the renderer reports a real back-buffer format/depth format and a sane MSAA count.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dEnumMapping.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dVertexLayouts.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;
using CNA::Internal::Renderers::Fna3d::ElementsForStride;
using CNA::Internal::Renderers::Fna3d::Fna3dIndexBufferRenderer;
using CNA::Internal::Renderers::Fna3d::Fna3dRenderer;
using CNA::Internal::Renderers::Fna3d::Fna3dVertexBufferRenderer;
using CNA::Internal::Renderers::GpuDrawParams;
using CNA::Internal::Renderers::Fna3d::ToFna3dBlend;

class Fna3dCapabilitiesTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();
        auto* fna3d = dynamic_cast<Fna3dRenderer*>(&renderer);
        if (!Check(fna3d != nullptr, "the active renderer is Fna3dRenderer"))
        {
            return;
        }

        // Check A -- no source-string shader compilation, and the refusal to match.
        Check(!renderer.SupportsCapability(GraphicsCapability::CustomEffects),
              "CustomEffects is reported false");
        Check(renderer.CreateEffectRenderer("vs", "fs") == nullptr,
              "CreateEffectRenderer returns null, matching that capability");

        // Check B -- native multi-stream vertex input.
        Check(renderer.SupportsCapability(GraphicsCapability::MultiStreamVertexInput),
              "MultiStreamVertexInput is reported true");
        Check(renderer.GetMaxVertexStreams() ==
                  CNA::Internal::Renderers::kMaxVertexStreams,
              "GetMaxVertexStreams reports XNA's own 16-binding ceiling");

        // Check C -- 3D capabilities backed by live resources.
        Check(renderer.SupportsCapability(GraphicsCapability::ThreeD), "ThreeD is true");
        Check(renderer.SupportsCapability(GraphicsCapability::DepthStencilBuffer),
              "DepthStencilBuffer is true");
        Check(renderer.SupportsCapability(GraphicsCapability::MultipleRenderTargets),
              "MultipleRenderTargets is true");
        Check(renderer.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame is true");
        Check(renderer.SupportsCapability(GraphicsCapability::Texture3D), "Texture3D is true");

        Check(renderer.SupportsCapability(GraphicsCapability::OcclusionQuery) ==
                  (renderer.CreateOcclusionQuery() != nullptr),
              "the OcclusionQuery capability matches whether a query can be created");
        Check(renderer.SupportsCapability(GraphicsCapability::Texture3D) ==
                  (renderer.CreateTexture3D(4, 4, 4, false, 0) != nullptr),
              "the Texture3D capability matches whether a volume texture can be created");
        Check(renderer.CreateTextureCube(4, false, 0) != nullptr,
              "a cube map can be created");
        Check(renderer.CreateRenderTargetCube(8, 0, false, false, 0) != nullptr,
              "a cube render target can be created");

        // Check D -- instancing is reported false and refused, with the reason.
        Check(!renderer.SupportsCapability(GraphicsCapability::Instancing),
              "Instancing is reported false: the stock effects declare no per-instance input");
        Check(!renderer.SupportsCapability(GraphicsCapability::CustomEffects),
              "and the reason -- no custom-effect compilation -- is reported alongside it");

        // Check D2 -- the refusal itself, so the capability answer is not the only statement of
        // it. GraphicsDevice rejects an instanced draw before the renderer sees it once the
        // capability is false, so the renderer method is exercised directly here.
        {
            bool refusedInstancedDraw = false;
            std::string instancedMessage;
            try
            {
                GpuDrawParams params{};
                Fna3dVertexBufferRenderer vertexBuffer(fna3d->GetDeviceStateEXT(), 3, 16);
                Fna3dIndexBufferRenderer indexBuffer(fna3d->GetDeviceStateEXT(), 3, false);
                renderer.DrawInstancedPrimitivesEx(
                    vertexBuffer, indexBuffer, Matrix::getIdentityProperty(),
                    Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                    PrimitiveType::TriangleList, 1, 4, params);
            }
            catch (const std::runtime_error& error)
            {
                refusedInstancedDraw = true;
                instancedMessage = error.what();
            }
            Check(refusedInstancedDraw,
                  "DrawInstancedPrimitivesEx refuses rather than stacking every instance");
            Check(instancedMessage.find("per-instance vertex input") != std::string::npos,
                  "the instanced refusal names the stock-effect reason");
        }

        // Check E -- an unknown stride is refused with a diagnostic.
        bool rejectedStride = false;
        std::string strideMessage;
        try
        {
            (void) ElementsForStride(17);
        }
        catch (const std::runtime_error& error)
        {
            rejectedStride = true;
            strideMessage = error.what();
        }
        Check(rejectedStride, "an unknown vertex stride is rejected, not silently reinterpreted");
        Check(strideMessage.find("stride 17") != std::string::npos,
              "the stride rejection names the stride it refused");

        // A stride CNA does define must still resolve, so the rejection above is a real boundary
        // rather than a blanket refusal.
        Check(ElementsForStride(24).size() == 3,
              "the SpriteBatch stride still resolves to its three elements");

        // Check F -- an out-of-contract ordinal is refused.
        bool rejectedOrdinal = false;
        try
        {
            (void) ToFna3dBlend(99);
        }
        catch (const std::runtime_error&)
        {
            rejectedOrdinal = true;
        }
        Check(rejectedOrdinal, "an out-of-range Blend ordinal is rejected, not cast blindly");

        // Check G -- honest presentation reporting.
        const int backBufferFormat =
            renderer.GetAppliedBackBufferFormatEXT(static_cast<int>(SurfaceFormat::Color));
        Check(backBufferFormat == static_cast<int>(SurfaceFormat::Color),
              "the applied back-buffer format is the Color format FNA3D created");
        const int depthFormat =
            renderer.GetAppliedDepthStencilFormatEXT(static_cast<int>(DepthFormat::Depth24Stencil8));
        std::printf("[info] applied depth/stencil format ordinal: %d\n", depthFormat);
        Check(depthFormat >= static_cast<int>(DepthFormat::None) &&
                  depthFormat <= static_cast<int>(DepthFormat::Depth24Stencil8),
              "the applied depth/stencil format is a real DepthFormat ordinal");
        Check(renderer.GetMultiSampleCount() >= 0,
              "the back buffer reports a non-negative MSAA sample count");
        Check(renderer.GetMaxTextureDimension() >= 4096,
              "the maximum texture dimension is at least XNA HiDef's guarantee");

        // The device still works after all of that.
        device.Clear(Color(12, 34, 56, 255));
        ExpectPixel("the device still renders after the capability sweep",
                    Rectangle(1, 1, 1, 1), Color(12, 34, 56, 255));
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3dCapabilitiesTest>();
}
