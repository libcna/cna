// SPDX-License-Identifier: MS-PL
// living-room-simulator's "non-finite EasyGL PBR pixels": the PBR programs normalised the
// interpolated normal and the Gram-Schmidt tangent without a length check, so a zero normal (a
// singular World, e.g. a node scaled flat) or a zero tangent (missing, zero, or parallel to the
// normal) made the shading normal NaN. Where max(NaN, 0) is 0 -- llvmpipe -- the surface silently
// lost all direct light; elsewhere the NaN reached a float scene target, which a bloom or a probe
// convolution then spreads across the frame. Each degenerate case draws into a Vector4 target,
// where NaN survives readback, and must be finite and lit like the control, on the rigid and the
// skinned program.
//
// plans/plan_apple_m4.md AM4-125: the guards that catch those zero vectors compared squared lengths
// with an absolute 1e-8, and the rigid program's interpolated normal is not unit length -- it is
// the inverse-transpose of World applied to it, 1/scale long. Under a World scaled by 1e4 or more
// a perfectly good normal was taken for a zero one and replaced by +Z. A second control with a
// tilted normal (where +Z is visibly wrong) is drawn plainly and under a World scaled by 1e5.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    struct PbrVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrVertex) == 48);

    struct SkinnedPbrVertex
    {
        PbrVertex base;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrVertex) == 68);

    /** @brief A full-viewport quad facing +Z whose every vertex carries @p normal and @p tangent. */
    std::vector<PbrVertex> Quad(const Vector3& tangent, const Vector3& normal)
    {
        const auto at = [&](float x, float y, float u, float v) {
            return PbrVertex{x, y, 0.0f, normal.X, normal.Y, normal.Z,
                             tangent.X, tangent.Y, tangent.Z, 1.0f, u, v};
        };
        const PbrVertex tl = at(-1.0f, 1.0f, 0.0f, 0.0f);
        const PbrVertex bl = at(-1.0f, -1.0f, 0.0f, 1.0f);
        const PbrVertex br = at(1.0f, -1.0f, 1.0f, 1.0f);
        const PbrVertex tr = at(1.0f, 1.0f, 1.0f, 0.0f);
        return {tl, bl, br, tl, br, tr};
    }

    template <typename Effect>
    void Configure(Effect& effect, const Matrix& world)
    {
        effect.setWorldProperty(world);
        effect.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 2.0f), Vector3::Zero,
                                                    Vector3::Up));
        effect.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.5f, 4.0f));
        effect.setMetallicFactorProperty(0.5f);
        effect.setRoughnessFactorProperty(0.5f);
        // A float scene target takes linear output; the sRGB encode's max(c, 0) would hide a NaN
        // on drivers where max(NaN, 0) is 0.
        effect.setEncodeOutputToSrgbEXTProperty(false);
        effect.setAmbientLightColorProperty(Vector3(0.2f, 0.2f, 0.2f));
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        effect.DirectionalLight0.setSpecularColorProperty(Vector3::One);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
    }
}

class PbrDegenerateBasisTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);

        struct Case
        {
            const char* name;
            Vector3 tangent;
            Vector3 normal;
            Matrix world;
        };
        const Vector3 facing(0.0f, 0.0f, 1.0f);
        const Vector3 tilted(0.6f, 0.0f, 0.8f);
        // Each group's first case is its control.
        const std::vector<std::vector<Case>> groups = {
            {
                {"control", Vector3(1.0f, 0.0f, 0.0f), facing, Matrix::getIdentityProperty()},
                {"zero tangent", Vector3::Zero, facing, Matrix::getIdentityProperty()},
                {"tangent parallel to the normal", Vector3(0.0f, 0.0f, 1.0f), facing,
                 Matrix::getIdentityProperty()},
                {"singular World (flattened along Z)", Vector3(1.0f, 0.0f, 0.0f), facing,
                 Matrix::CreateScale(1.0f, 1.0f, 0.0f)},
            },
            {
                {"tilted-normal control", Vector3(0.0f, 1.0f, 0.0f), tilted,
                 Matrix::getIdentityProperty()},
                {"tilted normal under a World scaled by 1e5", Vector3(0.0f, 1.0f, 0.0f), tilted,
                 Matrix::CreateScale(1.0e5f)},
            },
        };

        for (int program = 0; program < 2; ++program)
        {
            const char* name = program == 0 ? "PbrEffect" : "SkinnedPbrEffect";
            for (const std::vector<Case>& group : groups)
            {
                std::vector<Vector4> control;
                for (const Case& testCase : group)
                {
                    const std::vector<Vector4> pixels =
                        program == 0
                            ? DrawRigid(device, target, testCase.tangent, testCase.normal, testCase.world)
                            : DrawSkinned(device, target, testCase.tangent, testCase.normal, testCase.world);
                    if (control.empty()) { control = pixels; }
                    Check(pixels, control, std::string(name) + ", " + testCase.name);
                }
            }
        }
        device.SetVertexBuffer(nullptr);
    }

private:
    std::vector<Vector4> DrawRigid(GraphicsDevice& device, RenderTarget2D& target,
                                   const Vector3& tangent, const Vector3& normal, const Matrix& world)
    {
        const std::vector<PbrVertex> vertices = Quad(tangent, normal);
        VertexBuffer buffer(device, static_cast<int>(vertices.size()));
        buffer.SetDataRaw(vertices.data(), static_cast<int>(vertices.size()),
                          static_cast<int>(sizeof(PbrVertex)));
        PbrEffect effect(device);
        Configure(effect, world);
        return Draw(device, target, buffer, effect);
    }

    std::vector<Vector4> DrawSkinned(GraphicsDevice& device, RenderTarget2D& target,
                                     const Vector3& tangent, const Vector3& normal, const Matrix& world)
    {
        std::vector<SkinnedPbrVertex> vertices;
        for (const PbrVertex& vertex : Quad(tangent, normal))
            vertices.push_back({vertex, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0});
        VertexBuffer buffer(device, static_cast<int>(vertices.size()));
        buffer.SetDataRaw(vertices.data(), static_cast<int>(vertices.size()),
                          static_cast<int>(sizeof(SkinnedPbrVertex)));
        SkinnedPbrEffect effect(device);
        Configure(effect, world);
        effect.SetBoneTransforms({Matrix::getIdentityProperty()});
        effect.setWeightsPerVertexProperty(1);
        return Draw(device, target, buffer, effect);
    }

    static std::vector<Vector4> Draw(GraphicsDevice& device, RenderTarget2D& target,
                                     VertexBuffer& buffer, Effect& effect)
    {
        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 0, 0));
        device.SetVertexBuffer(&buffer);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetRenderTarget(nullptr);
        std::vector<Vector4> pixels(static_cast<std::size_t>(kSize * kSize));
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels;
    }

    void Check(const std::vector<Vector4>& pixels, const std::vector<Vector4>& control,
               const std::string& label)
    {
        int nonFinite = 0;
        float worst = 0.0f;
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            const Vector4& p = pixels[i];
            if (!std::isfinite(p.X) || !std::isfinite(p.Y) || !std::isfinite(p.Z) ||
                !std::isfinite(p.W))
            {
                ++nonFinite;
                continue;
            }
            const Vector4& c = control[i];
            worst = std::max({worst, std::fabs(p.X - c.X), std::fabs(p.Y - c.Y),
                              std::fabs(p.Z - c.Z), std::fabs(p.W - c.W)});
        }
        // The fallback tangent is not the control's, so the flat normal-map texel (0.004, 0.004, 1)
        // tilts a hair differently: about 0.02 here. A lost light moves the result by about 0.7.
        const bool pass = nonFinite == 0 && worst <= 0.05f;
        std::printf("[%s] %s: %d non-finite, largest difference from the control %.4f\n",
                    pass ? "PASS" : "FAIL", label.c_str(), nonFinite, worst);
        if (!pass) { MarkFailedEXT(); }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<PbrDegenerateBasisTest>();
}
