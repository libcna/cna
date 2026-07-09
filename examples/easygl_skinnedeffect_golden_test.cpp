// SPDX-License-Identifier: MS-PL
// Task 469: golden-image consumer reusing Phase 46's already-verified SkinnedEffect capstone
// scene (Task 409, examples/easygl_skinnedeffect_combined_test.cpp) via
// PixelTestGame::CompareGoldenImage() (Task 463) instead of Task 409's own predicate check.
//
// Recreates only Task 409's "quad A" (Task 406's identity-bone case): w0=1, i0=0 -> Bones[0] =
// Identity -> stays at authored NDC x: -1.0..-0.5. Task 409's own check for this quad is a
// predicate (R > G && R > 50, "red-dominant"), not a literal colour, since EnableDefaultLighting()
// applies real Phong lighting math that isn't practical to derive analytically by hand -- unlike
// Tasks 464-468's literal expected values. The ExpectPixel cross-check below therefore uses the
// project's own live-observed value (captured once via CNA_UPDATE_GOLDEN=1 and independently
// confirmed red-dominant, matching Task 409's own predicate) rather than an analytically-derived
// number.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // GPU-compact skinned vertex: matches the stride-52 layout (Task 123's own convention),
    // identical to Task 409's own SkinnedGpuVertex.
    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

    void AppendQuad(std::vector<SkinnedGpuVertex>& out,
                     float w0, float w1, float w2, float w3,
                     std::uint8_t i0, std::uint8_t i1, std::uint8_t i2, std::uint8_t i3)
    {
        const float xMin = -1.0f, xMax = -0.5f;
        const SkinnedGpuVertex tl{ xMin,  1, 0,  0,0,1,  0,0,  w0,w1,w2,w3,  i0,i1,i2,i3 };
        const SkinnedGpuVertex bl{ xMin, -1, 0,  0,0,1,  0,1,  w0,w1,w2,w3,  i0,i1,i2,i3 };
        const SkinnedGpuVertex br{ xMax, -1, 0,  0,0,1,  1,1,  w0,w1,w2,w3,  i0,i1,i2,i3 };
        const SkinnedGpuVertex tr{ xMax,  1, 0,  0,0,1,  1,0,  w0,w1,w2,w3,  i0,i1,i2,i3 };
        out.push_back(tl); out.push_back(bl); out.push_back(br);
        out.push_back(tl); out.push_back(br); out.push_back(tr);
    }
}

class SkinnedEffectGoldenTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        const std::vector<std::uint8_t> px = { 255, 0, 0, 255 };
        Texture2D tex = Texture2D::CreateFromPixels(device, 1, 1, px);

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding -- same as Task 409's own comment.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        SkinnedEffect fx(device);
        fx.setTextureProperty(&tex);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.EnableDefaultLighting();
        fx.Apply();

        std::vector<SkinnedGpuVertex> verts;
        AppendQuad(verts, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0); // quad A: identity bone

        VertexBuffer vb(device, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedGpuVertex)));
        device.SetVertexBuffer(&vb);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, static_cast<int>(verts.size() / 3));

        // Same pixel column Task 409 uses for quad A (W/8, NDC x ~ -0.75).
        const int samplePx = W / 8;
        const int sampleY = H / 2;
        // Cross-check against Task 409's own live-observed, lighting-derived value (see file
        // header) -- independent of the golden PNG's own contents (same rationale as Tasks
        // 464-468).
        ExpectPixel("quadA-identity-vs-task409-observed", Rectangle(samplePx, sampleY, 1, 1),
                    Color(174, 0, 0, 255), /*tolerance=*/40);
        CompareGoldenImage("skinnedeffect-quadA-identity",
                            Rectangle(samplePx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_skinnedeffect_golden_test.png",
                            /*tolerance=*/40);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<SkinnedEffectGoldenTest>();
}
