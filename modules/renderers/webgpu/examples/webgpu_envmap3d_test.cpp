// SPDX-License-Identifier: MS-PL
// WEBGPU-25/36/74: verify WebGPURenderer's env_map3d.wgsl / GetOrCreatePipelineEnvMap3D() /
// DrawPrimitivesEx()+DrawIndexedPrimitivesEx() dispatch for EnvironmentMapEffect (stride-32
// VertexPositionNormalTexture, the same vertex layout as lit_textured3d.wgsl) -- this renderer's
// first cube-map shader, ported from VulkanRenderer's env_map3d.{vert,frag}.glsl (itself
// cross-checked against EasyGLRenderer::EnsureEnvMapped3DProgram()'s identical GLSL formula
// before porting). Also exercises WebGPUTextureCubeRenderer (CreateTextureCube()/SetData()), this
// renderer's first cube-map texture support (a minimal, read-only-after-upload implementation --
// see that class's own doc comment for the documented TextureCube/RenderTargetCube parity gaps
// this does NOT close).
//
// All checks use World=View=Projection=Identity, a quad at z=0.5 with Normal=(0,0,-1) (facing the
// camera at the origin, matching webgpu_littextured3d_test.cpp's own convention) and no directional
// lights enabled (DirectionalLight0/1/2 all default-disabled, AmbientLightColor/EmissiveColor
// default to black) -- this deliberately makes the shader's own "lit" contribution
// (litRGB = (emissive + lightSum) * diffuseColor) exactly zero, isolating the environment-map
// contribution so the checks below only ever depend on real cube-map sampling / Fresnel / amount
// math, not on any lighting interaction.
//
// Hand-derived geometry (verified independently, not just asserted): with View=Identity, the
// camera-space eye position (EnvironmentMapEffect::FillGpuDrawParams() computes it as
// Matrix::Invert(View).Translation) is (0,0,0). World=Identity leaves the quad's own vertex
// position/normal untouched, so at the quad's centre (worldPos=(0,0,0.5), N=(0,0,-1)):
//   E = normalize(eyePos - worldPos) = normalize((0,0,-0.5)) = (0,0,-1)
//   reflDir = reflect(-E, N) = reflect((0,0,1), (0,0,-1)) = (0,0,1) - 2*(-1)*(0,0,-1) = (0,0,-1)
// (0,0,-1) is exactly the CubeMapFace::NegativeZ sampling direction (face index 5, this project's
// own "0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z" convention -- see IRenderTargetCubeRenderer's doc comment).
// The cube map below paints all 6 faces distinct solid colours, with NegativeZ = cyan (0,255,255)
// -- Check A's expected pixel is exactly that colour, proving the reflection vector direction is
// really computed and really samples the correct face, not just "some texture, some colour".
// WEBGPU-176 CORRECTED THE DERIVATION BELOW, and with it Check B's expected colour. XNA evaluates
// ComputeFresnelFactor in the VERTEX shader (EnvironmentMapEffect.fx's ComputeEnvMapVSOutput) and
// Gouraud-interpolates the scalar; it does NOT recompute it per fragment from an interpolated
// normal. This file previously derived the Fresnel term "at the quad's centre" -- which is the
// per-fragment model, and was true only because this renderer was the one CNA backend computing it
// per fragment. The quad's VERTICES are not head-on at all: with corners at (+-1,+-1,0.5) and the
// eye at the origin,
//   E(corner)      = normalize(-(+-1,+-1,0.5)) , |(+-1,+-1,0.5)| = 1.5
//   dot(E,N)       = (-0.5/1.5) * -1 = 1/3   -- the SAME at all four corners
//   fresnel        = pow(1 - |1/3|, 1) * EnvironmentMapAmount(1) = 2/3
// and since all four vertices carry 2/3, the interpolated weight is a constant 2/3 across the
// quad. So Check B's centre is mix(black, cyan, 2/3) = (0,170,170), not black. That value is still
// a real differential proof against Check A's full cyan (0,255,255) -- Fresnel demonstrably gates
// the blend -- and it is now the gate XNA actually applies rather than the one a per-fragment
// shader applies. dot(E,N) does equal 1 at the quad's CENTRE, which is what the old derivation
// used; that number simply never reaches the shader under XNA's rule.
//
// Check A -- FresnelFactor=0 (disables Fresnel weighting -> flat EnvironmentMapAmount blend),
//   EnvironmentMapAmount=1: renders cyan (the NegativeZ face colour) -- proves the cube map is
//   genuinely sampled along the correct reflection direction and the flat blend path works.
// Check B -- FresnelFactor=1 (Fresnel enabled, EnvironmentMapEffect's own real default),
//   EnvironmentMapAmount=1, same exact scene as Check A: renders two thirds of cyan (0,170,170),
//   not the full cyan -- proves the Fresnel edge-weighting term genuinely gates the blend, at the
//   exact per-vertex weight derived above, not just "present but inert". This is the same
//   reflection direction and the same bound cube map as Check A -- only FresnelFactor differs, so
//   this is a real differential proof, not two independently-plausible-looking results.
// Check C -- FresnelFactor=0 (flat blend, same as Check A) but EnvironmentMapAmount=0: renders
//   black -- proves EnvironmentMapAmount genuinely scales/gates the blend independently of Fresnel
//   (distinct code path from Check B's suppression).
// Check D -- same scene as Check A (flat blend, full amount) but drawn via
//   DrawIndexedPrimitivesEx() (GraphicsDevice::DrawIndexedPrimitives(), a real IndexBuffer) instead
//   of the non-indexed DrawPrimitivesEx() path used by A/B/C: renders cyan -- proves the indexed
//   dispatch branch (QueueEnvMapDraw() called with a non-null IIndexBufferRenderer) also reaches
//   env_map3d.wgsl correctly, not just the non-indexed one.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool colorNear(Color a, Color b, int tol = 24)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    // Non-indexed triangle list (6 vertices, matching webgpu_littextured3d_test.cpp's own
    // MakeFacingQuad() convention) -- used by Checks A/B/C.
    VertexBuffer MakeFacingQuad(GraphicsDevice& dev)
    {
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        const Vector3 n(0.0f, 0.0f, -1.0f);
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), n, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.5f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), n, Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.5f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), n, Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.5f), n, Vector2(1.0f, 0.0f) },
        };
        vb.SetData(verts, 0, 6);
        return vb;
    }

    // 4 unique vertices + a 6-index triangle list -- used by Check D's indexed draw path.
    VertexBuffer MakeFacingQuadUnique(GraphicsDevice& dev)
    {
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 4, BufferUsage::None);
        const Vector3 n(0.0f, 0.0f, -1.0f);
        const VertexPositionNormalTexture verts[4] = {
            { Vector3(-1.0f,  1.0f, 0.5f), n, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.5f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), n, Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.5f), n, Vector2(1.0f, 0.0f) },
        };
        vb.SetData(verts, 0, 4);
        return vb;
    }

    std::unique_ptr<IndexBuffer> MakeQuadIndices(GraphicsDevice& dev)
    {
        auto ib = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib->SetData(indices, 0, 6);
        return ib;
    }
}

class WebGpuEnvMap3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<TextureCube> cubeMap_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    // Distinct solid colour per face -- NegativeZ (index 5) is the one this test's hand-derived
    // geometry expects to be sampled; the other 5 are all different from it and from each other so
    // an accidentally-wrong face index would show up as a visibly wrong colour, not a lucky match.
    void BuildCubeMap(GraphicsDevice& dev)
    {
        cubeMap_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        const Color red(255, 0, 0, 255);
        const Color green(0, 255, 0, 255);
        const Color blue(0, 0, 255, 255);
        const Color yellow(255, 255, 0, 255);
        const Color magenta(255, 0, 255, 255);
        const Color cyan(0, 255, 255, 255);
        cubeMap_->SetData(CubeMapFace::PositiveX, &red, 1);
        cubeMap_->SetData(CubeMapFace::NegativeX, &green, 1);
        cubeMap_->SetData(CubeMapFace::PositiveY, &blue, 1);
        cubeMap_->SetData(CubeMapFace::NegativeY, &yellow, 1);
        cubeMap_->SetData(CubeMapFace::PositiveZ, &magenta, 1);
        cubeMap_->SetData(CubeMapFace::NegativeZ, &cyan, 1);
    }

protected:
    void LoadContent() override
    {
        BuildCubeMap(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        // PointClamp, not the default Linear filter: the readback pixel centre (kSize/2, kSize/2)
        // is one half-texel off the mathematical NDC origin (standard pixel-centre-at-(px+0.5)
        // rasterization), so the hand-derived reflDir=(0,0,-1) is reproduced only to within a
        // fraction of a percent in practice -- close enough to stay deep inside the NegativeZ
        // face's own region (any face selection logic based on dominant axis magnitude still picks
        // NegativeZ unambiguously) but, with only a single texel covering an entire 1x1 face and
        // hardware seamless cube-edge filtering under Linear, still enough to pull in a small,
        // real neighbouring-face contribution under Linear sampling. PointClamp sidesteps this
        // entirely and reads the exact face texel, matching every other WebGPU test's own
        // established "assert exact colours via PointClamp/PointWrap, not the default Linear
        // filter" convention (see e.g. examples/easygl_sampler_state_effect_test.cpp).
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        const Color cyan(0, 255, 255, 255);

        // Check A: Fresnel disabled -> flat EnvironmentMapAmount=1 blend, samples NegativeZ (cyan).
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            EnvironmentMapEffect fx(dev);
            fx.setEnvironmentMapProperty(cubeMap_.get());
            fx.setEnvironmentMapAmountProperty(1.0f);
            fx.setFresnelFactorProperty(0.0f);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), cyan),
                  "Fresnel disabled + EnvironmentMapAmount=1 samples the correct cube face (cyan)");
        }

        // Check B: same exact scene, Fresnel enabled (EnvironmentMapEffect's own real default).
        // WEBGPU-176: the weight is XNA's per-VERTEX one, a constant 2/3 across this quad (see the
        // derivation at the top of the file), so the centre is two thirds of the way from the
        // unlit black base to the cyan face -- gated, but not to zero.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            EnvironmentMapEffect fx(dev);
            fx.setEnvironmentMapProperty(cubeMap_.get());
            fx.setEnvironmentMapAmountProperty(1.0f);
            fx.setFresnelFactorProperty(1.0f);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            const Color measured = readCenter(dev);
            const Color twoThirdsCyan(0, 170, 170, 255);
            std::printf("[info] Fresnel-weighted centre = (%d,%d,%d), expected (%d,%d,%d)\n",
                        measured.getRProperty(), measured.getGProperty(), measured.getBProperty(),
                        twoThirdsCyan.getRProperty(), twoThirdsCyan.getGProperty(),
                        twoThirdsCyan.getBProperty());
            check(colorNear(measured, twoThirdsCyan, 6),
                  "Fresnel gates the env-map blend at XNA's per-VERTEX weight of 2/3 (0,170,170), "
                  "measurably below Check A's full cyan");
        }

        // Check C: Fresnel disabled again (flat blend path) but EnvironmentMapAmount=0 -- proves
        // the amount itself genuinely gates the blend, independent of Check B's Fresnel gating.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            EnvironmentMapEffect fx(dev);
            fx.setEnvironmentMapProperty(cubeMap_.get());
            fx.setEnvironmentMapAmountProperty(0.0f);
            fx.setFresnelFactorProperty(0.0f);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Black),
                  "EnvironmentMapAmount=0 suppresses the blend even with Fresnel disabled (black)");
        }

        // Check D: identical scene to Check A, but through DrawIndexedPrimitivesEx() (a real
        // IndexBuffer, 4 unique vertices + 6 indices) instead of the non-indexed dispatch path.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuadUnique(dev);
            std::unique_ptr<IndexBuffer> ib = MakeQuadIndices(dev);
            EnvironmentMapEffect fx(dev);
            fx.setEnvironmentMapProperty(cubeMap_.get());
            fx.setEnvironmentMapAmountProperty(1.0f);
            fx.setFresnelFactorProperty(0.0f);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.setIndicesProperty(ib.get());
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.setIndicesProperty(nullptr);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), cyan),
                  "DrawIndexedPrimitivesEx() dispatch also reaches env_map3d.wgsl correctly (cyan)");
        }

        std::printf("=== %d/4 PASS ===\n", passCount_);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    WebGpuEnvMap3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuEnvMap3DTest game;
    game.Run();
    return game.getResult();
}
