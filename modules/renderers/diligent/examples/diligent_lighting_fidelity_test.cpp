// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-59: real-device, hand-derived proof of the four lighting-fidelity
// defects fixed in this task's own BasicEffect/SkinnedEffect/EnvironmentMapEffect lit shader path
// (kLitPixelHlsl and its per-vertex-lit sibling, shared with SkinnedEffect and, for the vertex
// shader's normal transform, EnvironmentMapEffect too).
//
// Every check reuses this project's own already-validated non-degenerate camera convention
// (eye=(0,0,3) via Matrix.CreateLookAt, 45-degree perspective, an oversized flat quad at z=0 -- see
// diligent_pbr_test.cpp's own MakeQuad()/RenderPbr(), whose hand-derived pixel predictions already
// prove this exact setup is numerically well-conditioned), deliberately avoiding the
// eye-coplanar-with-surface degenerate configuration DILIGENT-30's own VertexLit investigation
// root-caused as numerically fragile.
//
// Check A -- Emissive isolation. DiffuseColor=black, Ambient=0, no active lights, EmissiveColor set
//   to a known non-black colour. The old bug folded ambient+emissive into one value multiplied by
//   DiffuseColor (here: black) -- the pixel would read black. The fix adds EmissiveColor AFTER that
//   multiply, so the pixel must read EmissiveColor unchanged.
// Check B -- Specular scaled by final alpha. A light/eye configuration with nDotH=1 (maximum
//   possible specular) and a deliberately dim diffuse term, compared at DiffuseColor.a=1 vs a=0.5.
//   BasicEffect's own CPU layer premultiplies DiffuseColor.rgb by alpha (SetMaterialColor's
//   documented, correct XNA behaviour) -- at a=0 that would ALSO zero the diffuse term, so it
//   cannot isolate the specular*alpha fix on its own. At a=0.5 both terms are still observable:
//   the old bug added specular*SpecularColor unconditionally (saturating regardless of alpha), so
//   the fix (specular scaled by alpha too) must read a distinctly darker, unsaturated mid-grey
//   instead of the saturated white a=1 produces.
// Check C -- Multi-light additive. Two lights of different pure colours aimed the same way at the
//   same surface must SUM (magenta), not have one silently overwrite the other.
// Check D -- Non-uniform-world normal (World's inverse-transpose, not World itself). A diagonal
//   object-space normal (1,1,0)/sqrt(2) under World=Scale(4,1,1) with a light aimed along +Y:
//   correctly transformed (inverse-transpose), the surface stays strongly lit (nDotL~=0.97); with
//   the old bug (World applied directly), the same configuration reads weakly lit (nDotL~=0.24) --
//   a large, unambiguous, hand-derived difference.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // Position + Normal + UV, stride 32: DrawInternal() dispatches purely on vertex buffer stride
    // (16/20/24/32/48/52/68), and stride 32 with lighting enabled is what selects the lit
    // ShaderVariant::LitTextured3D/LitTexturedVertexLit3D this task's own fix touches -- a
    // Position+Normal-only (stride 24) buffer would silently dispatch to the UNLIT
    // ColoredTextured3D variant instead (stride 24's own meaning), never exercising the lit shaders
    // at all. UV is unused by BasicEffect's lit-without-texture path but must still be present for
    // the stride to be correct.
    struct GpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
    };
    static_assert(sizeof(GpuVertex) == 32, "GpuVertex must be exactly 32 bytes (Position+Normal+UV)");

    // An oversized quad (matching diligent_pbr_test.cpp's own MakeQuad() convention) so the centre
    // pixel is covered regardless of exact FOV/aspect rounding, with a caller-supplied normal.
    std::vector<GpuVertex> MakeQuad(float nx, float ny, float nz)
    {
        GpuVertex tl{}, bl{}, br{}, tr{};
        auto fill = [&](GpuVertex& v, float x, float y) {
            v.px = x; v.py = y; v.pz = 0.0f;
            v.nx = nx; v.ny = ny; v.nz = nz;
            v.u = 0.0f; v.v = 0.0f;
        };
        fill(tl, -4.0f, 4.0f);
        fill(bl, -4.0f, -4.0f);
        fill(br, 4.0f, -4.0f);
        fill(tr, 4.0f, 4.0f);
        return {tl, bl, br, tl, br, tr};
    }

    Color ReadCenter(GraphicsDevice& device)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool ColorNear(Color a, Color b, int tolerance)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }
}

class DiligentLightingFidelityTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        ++totalCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok)
            ++passCount_;
    }

    static void SetCommonCamera(BasicEffect& effect)
    {
        effect.setViewProperty(
            Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        effect.setProjectionProperty(
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // ---- Check A: emissive isolation ----
        {
            auto verts = MakeQuad(0.0f, 0.0f, 1.0f);
            VertexBuffer vb(device, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                          static_cast<int>(sizeof(GpuVertex)));

            BasicEffect effect(device);
            SetCommonCamera(effect);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setLightingEnabledProperty(true);
            effect.setPreferPerPixelLightingProperty(true);
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setDiffuseColorProperty(Vector3::Zero);
            effect.DirectionalLight0.setEnabledProperty(false);
            effect.DirectionalLight1.setEnabledProperty(false);
            effect.DirectionalLight2.setEnabledProperty(false);
            effect.setEmissiveColorProperty(Vector3(0.8f, 0.2f, 0.4f));

            device.Clear(Color::Black);
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const Color got = ReadCenter(device);
            const Color expected(204, 51, 102, 255);
            std::printf("    (A: got=(%d,%d,%d) expected ~(%d,%d,%d))\n", got.getRProperty(),
                        got.getGProperty(), got.getBProperty(), expected.getRProperty(),
                        expected.getGProperty(), expected.getBProperty());
            Check(ColorNear(got, expected, 20),
                 "emissive reaches the pixel unmultiplied by DiffuseColor(=black)");
        }

        // ---- Check B: specular scaled by final alpha ----
        {
            auto verts = MakeQuad(0.0f, 0.0f, 1.0f);
            VertexBuffer vb(device, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                          static_cast<int>(sizeof(GpuVertex)));

            const auto render = [&](float alpha) {
                BasicEffect effect(device);
                SetCommonCamera(effect);
                effect.setWorldProperty(Matrix::getIdentityProperty());
                effect.setLightingEnabledProperty(true);
                effect.setPreferPerPixelLightingProperty(true);
                effect.setAmbientLightColorProperty(Vector3::Zero);
                effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                effect.setAlphaProperty(alpha);
                effect.setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                effect.setSpecularPowerProperty(1.0f);
                // Light travels straight at the quad along -Z, matching the eye direction back to
                // the camera at (0,0,3): lightDir == eyeDir == the quad's own normal, so
                // half-vector == normal exactly (nDotH=1, the maximum possible specular term) --
                // deliberately non-degenerate (see this file's own header note).
                effect.DirectionalLight0.setEnabledProperty(true);
                effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
                effect.DirectionalLight0.setDiffuseColorProperty(Vector3(0.1f, 0.1f, 0.1f));
                effect.DirectionalLight0.setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                effect.DirectionalLight1.setEnabledProperty(false);
                effect.DirectionalLight2.setEnabledProperty(false);

                device.Clear(Color::Black);
                effect.Apply();
                device.SetVertexBuffer(&vb);
                device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                return ReadCenter(device);
            };

            const Color dim = render(0.5f);
            const Color bright = render(1.0f);
            std::printf("    (B: alpha=0.5 got=(%d,%d,%d), alpha=1 got=(%d,%d,%d))\n",
                        dim.getRProperty(), dim.getGProperty(), dim.getBProperty(),
                        bright.getRProperty(), bright.getGProperty(), bright.getBProperty());
            // alpha=0.5: baseColor.rgb=(0.5,0.5,0.5) (CPU-premultiplied), diffuse term=0.1 ->
            // diffuse contribution=(0.05,0.05,0.05); specular = 1*1*alpha(0.5) = 0.5. Total =
            // 0.55 per channel =~ 140/255, well short of saturation.
            Check(ColorNear(dim, Color(140, 140, 140, 255), 20),
                 "alpha=0.5: specular is scaled down with alpha, pixel stays an unsaturated "
                 "mid-grey");
            // alpha=1: full specular (nDotH=1, SpecularColor=white) saturates the channel.
            Check(ColorNear(bright, Color(255, 255, 255, 255), 15),
                 "alpha=1: full specular term is added, saturating the pixel");
            Check(!ColorNear(dim, bright, 40),
                 "specular contribution genuinely differs between alpha=0.5 and alpha=1");
        }

        // ---- Check C: multi-light additive ----
        {
            auto verts = MakeQuad(0.0f, 0.0f, 1.0f);
            VertexBuffer vb(device, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                          static_cast<int>(sizeof(GpuVertex)));

            BasicEffect effect(device);
            SetCommonCamera(effect);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setLightingEnabledProperty(true);
            effect.setPreferPerPixelLightingProperty(true);
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.setSpecularColorProperty(Vector3::Zero);
            effect.DirectionalLight0.setEnabledProperty(true);
            effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            effect.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            effect.DirectionalLight1.setEnabledProperty(true);
            effect.DirectionalLight1.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            effect.DirectionalLight1.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f));
            effect.DirectionalLight2.setEnabledProperty(false);

            device.Clear(Color::Black);
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const Color got = ReadCenter(device);
            std::printf("    (C: got=(%d,%d,%d) expected ~magenta)\n", got.getRProperty(),
                        got.getGProperty(), got.getBProperty());
            Check(ColorNear(got, Color(255, 0, 255, 255), 20),
                 "two differently-coloured lights sum (magenta), neither overwrites the other");
        }

        // ---- Check D: non-uniform-world normal (inverse-transpose, not World directly) ----
        {
            const float diag = 0.70710678f; // (1,1,0)/sqrt(2)
            auto verts = MakeQuad(diag, diag, 0.0f);
            VertexBuffer vb(device, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                          static_cast<int>(sizeof(GpuVertex)));

            BasicEffect effect(device);
            SetCommonCamera(effect);
            effect.setWorldProperty(Matrix::CreateScale(4.0f, 1.0f, 1.0f));
            effect.setLightingEnabledProperty(true);
            effect.setPreferPerPixelLightingProperty(true);
            effect.setAmbientLightColorProperty(Vector3::Zero);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.setSpecularColorProperty(Vector3::Zero);
            // Aimed along +Y: correctly inverse-transpose-transformed, the diagonal normal stays
            // mostly Y-aligned (nDotL~=0.97); transformed by World directly (the bug), it becomes
            // mostly X-aligned instead (nDotL~=0.24) -- see this file's own header derivation.
            effect.DirectionalLight0.setEnabledProperty(true);
            effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
            effect.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.DirectionalLight1.setEnabledProperty(false);
            effect.DirectionalLight2.setEnabledProperty(false);

            device.Clear(Color::Black);
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const Color got = ReadCenter(device);
            std::printf("    (D: got=(%d,%d,%d) expected ~(247,247,247), wrong-transform would be "
                        "~(62,62,62))\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            Check(ColorNear(got, Color(247, 247, 247, 255), 20),
                 "non-uniform World scale: normal transformed by the inverse-transpose stays "
                 "strongly lit, not weakly lit as a direct-World transform would read");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = passCount_ == totalCount_ ? 0 : 1;
        Exit();
    }

public:
    DiligentLightingFidelityTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentLightingFidelityTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
