// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-22: SkinnedEffect for the OpenGL4 graphics renderer -- adds a dedicated
// skinned3d GLSL 410 core program (new stride 52/56, VertexPositionNormalTextureSkinned +
// optional trailing Color), selected by BindProgramForStride for those two strides. Also adds
// OpenGL4VertexBufferRenderer::ApplyLayout's stride-52/56 vertex attribute cases (BlendWeight as a
// real float4 attribute, BlendIndices as a true GLSL integer attribute via the newly-loaded
// gl4_glVertexAttribIPointer -- glVertexAttribPointer's implicit int-to-float conversion would be
// wrong for bone indices used to subscript the uBones[] array in the shader).
//
// Ported near-verbatim from EasyGLRenderer::EnsureSkinnedProgram's GLSL ES 300 source,
// which itself already matches real XNA SkinnedEffect.fx's Skin() function: skinMat is the sum of
// only the first WeightsPerVertex (1, 2, or 4) uBones[index]*weight pairs -- never all 4
// unconditionally (a real bug, Task 895, already found and fixed while porting this effect to the
// other renderers; this OpenGL4 port uses the corrected formula from the start).
//
// Check A -- identity bones (weightsPerVertex=1, single bone = Identity): the quad renders at its
//   own unshifted NDC position -- proves skinning with an identity bone is a true no-op, not
//   accidentally displacing geometry.
// Check B -- a single bone that is a pure Translate(+0.5, 0, 0) (weightsPerVertex=1): the quad
//   visibly shifts right by the bone's own translation -- proves per-vertex position skinning is
//   real, not just "didn't throw".
// Check C -- a genuine two-bone weighted blend (weightsPerVertex=2): bone0=Translate(-0.5,0,0),
//   bone1=Translate(+1.5,0,0), weights 0.5/0.5 -> blended shift = 0.5*(-0.5)+0.5*(1.5) = +0.5,
//   numerically identical to Check B's own shift but reached via a genuinely different mechanism
//   (2 nonzero weighted bones, not 1) -- decisive proof the shader blends BOTH weighted bones
//   rather than only applying one of them (a bug that picked bone 0 alone would shift by -0.5;
//   bone 1 alone by +1.5; neither matches the correct blended +0.5).
// Check D -- VertexColorEnabled (stride-56 layout's trailing Color attribute): a pure-black
//   per-vertex color zeroes the lit/textured result when VertexColorEnabled=true, and is ignored
//   (same result as VertexColorEnabled=false) when the flag is off -- proves the stride-56 aColor
//   attribute is genuinely read and genuinely gated by the uniform, not silently ignored.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // GPU-compact skinned vertex: matches OpenGL4VertexBufferRenderer::ApplyLayout's stride-52
    // case (position(12) + normal(12) + uv(8) + weights(16) + indices(4) = 52 bytes).
    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

    // Stride-56 skinned+Color vertex (Color appended after BlendIndices).
    struct SkinnedColorGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(SkinnedColorGpuVertex) == 56, "skinned+color vertex must be 56 bytes");
}

class OpenGL4SkinnedEffectTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle reg(x, y, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    // Draws a full-screen-height quad spanning NDC x [xMin, xMax] with the given bones/weights,
    // then samples 3 fixed screen-space points (left/centre/right) to detect where the quad
    // actually ended up after skinning.
    // Draws a quad (NDC x: -1..0 before skinning) with the given bones/weights, then samples 3
    // caller-chosen screen-space x columns (as eighths of the viewport width, matching the NDC
    // convention W/8=~-0.75 .. 7W/8=~+0.75) to detect where the quad actually ended up.
    void DrawAndSample(GraphicsDevice& dev, Texture2D& tex, const std::vector<Matrix>& bones,
                        int weightsPerVertex, int leftEighths, int centreEighths, int rightEighths,
                        Color& outLeft, Color& outCentre, Color& outRight)
    {
        const int W = dev.getViewportProperty().getWidthProperty();
        const int H = dev.getViewportProperty().getHeightProperty();

        dev.Clear(Color(0, 255, 0, 255));

        SkinnedEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(weightsPerVertex);
        fx.EnableDefaultLighting();
        fx.Apply();

        const SkinnedGpuVertex verts[6] = {
            {-1,  1, 0,  0, 0, 1,  0, 0,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
            {-1, -1, 0,  0, 0, 1,  0, 1,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
            { 0, -1, 0,  0, 0, 1,  1, 1,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
            {-1,  1, 0,  0, 0, 1,  0, 0,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
            { 0, -1, 0,  0, 0, 1,  1, 1,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
            { 0,  1, 0,  0, 0, 1,  1, 0,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
        };

        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        outLeft = ReadPixel(dev, W * leftEighths / 8, H / 2);
        outCentre = ReadPixel(dev, W * centreEighths / 8, H / 2);
        outRight = ReadPixel(dev, W * rightEighths / 8, H / 2);
    }

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const std::vector<std::uint8_t> redPx = {255, 0, 0, 255};
        Texture2D tex = Texture2D::CreateFromPixels(dev, 1, 1, redPx);

        const auto isTextured = [](const Color& c) { return c.getRProperty() > c.getGProperty() && c.getRProperty() > 50; };
        const auto isBackground = [](const Color& c) { return c.getGProperty() > c.getRProperty(); };

        // --- Check A: identity bone -- the quad (NDC x: -1..0) stays unshifted ---
        {
            Color inside(0,0,0,0), farOutside(0,0,0,0), nearOutside(0,0,0,0);
            std::vector<Matrix> bones = {Matrix::getIdentityProperty()};
            // eighths=2 -> NDC -0.5 (inside -1..0); eighths=6/7 -> NDC +0.5/+0.75 (outside).
            // Deliberately avoids eighths=4 (NDC 0.0), the quad's own right-edge boundary pixel,
            // where the exact inside/outside rasterization result is not decisively predictable.
            DrawAndSample(dev, tex, bones, 1, 2, 6, 7, inside, farOutside, nearOutside);
            Check(isTextured(inside) && isBackground(farOutside) && isBackground(nearOutside),
                  "Check A: identity bone leaves the quad's own NDC position unshifted (no accidental displacement)");
        }

        // --- Check B: single bone Translate(+0.5,0,0) shifts the quad to NDC x: -0.5..0.5 ---
        {
            Color left(0,0,0,0), centre(0,0,0,0), right(0,0,0,0);
            std::vector<Matrix> bones = {Matrix::CreateTranslation(0.5f, 0.0f, 0.0f)};
            DrawAndSample(dev, tex, bones, 1, 1, 4, 7, left, centre, right);
            Check(isBackground(left) && isTextured(centre) && isBackground(right),
                  "Check B: a single Translate(+0.5,0,0) bone shifts the quad so only the centre sample is inside it");
        }

        // --- Check C: two-bone weighted blend reaches the SAME +0.5 shift via a genuinely
        // different mechanism (0.5*(-0.5) + 0.5*(1.5) = +0.5, distinct from either bone alone) ---
        {
            Color left(0,0,0,0), centre(0,0,0,0), right(0,0,0,0);
            std::vector<Matrix> bones = {
                Matrix::CreateTranslation(-0.5f, 0.0f, 0.0f),
                Matrix::CreateTranslation(1.5f, 0.0f, 0.0f),
            };
            DrawAndSample(dev, tex, bones, 2, 1, 4, 7, left, centre, right);
            Check(isBackground(left) && isTextured(centre) && isBackground(right),
                  "Check C: a two-bone 0.5/0.5 weighted blend (-0.5 and +1.5 translate) reaches the same +0.5 net shift, proving both weighted bones genuinely contribute");
        }

        // --- Check D: VertexColorEnabled gates the stride-56 aColor attribute ---
        {
            dev.Clear(Color(0, 255, 0, 255));

            SkinnedEffect fx(dev);
            fx.setTextureProperty(&tex);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            std::vector<Matrix> bones = {Matrix::getIdentityProperty()};
            fx.SetBoneTransforms(bones);
            fx.setWeightsPerVertexProperty(1);
            fx.EnableDefaultLighting();

            const auto makeQuad = [](float xMin, float xMax) {
                const SkinnedColorGpuVertex tl{xMin,  1, 0,  0, 0, 1,  0, 0,  1, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 255};
                const SkinnedColorGpuVertex bl{xMin, -1, 0,  0, 0, 1,  0, 1,  1, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 255};
                const SkinnedColorGpuVertex br{xMax, -1, 0,  0, 0, 1,  1, 1,  1, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 255};
                const SkinnedColorGpuVertex tr{xMax,  1, 0,  0, 0, 1,  1, 0,  1, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 255};
                return std::vector<SkinnedColorGpuVertex>{tl, bl, br, tl, br, tr};
            };
            std::vector<SkinnedColorGpuVertex> verts;
            const auto quadA = makeQuad(-1.0f, -0.5f); // left: VertexColorEnabled=false
            const auto quadB = makeQuad(0.5f, 1.0f);   // right: VertexColorEnabled=true
            verts.insert(verts.end(), quadA.begin(), quadA.end());
            verts.insert(verts.end(), quadB.begin(), quadB.end());

            VertexBuffer vb(dev, static_cast<int>(verts.size()));
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedColorGpuVertex)));
            dev.SetVertexBuffer(&vb);

            fx.VertexColorEnabled = false;
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);

            const int W = dev.getViewportProperty().getWidthProperty();
            const int H = dev.getViewportProperty().getHeightProperty();
            ExpectPixel("Check D1: VertexColorEnabled=false ignores the black per-vertex color (still lit/textured)",
                        Rectangle(W / 8, H / 2, 1, 1), Color(174, 0, 0, 255), /*tolerance=*/40);
            ExpectPixel("Check D2: VertexColorEnabled=true with a black per-vertex color zeroes the result",
                        Rectangle(7 * W / 8, H / 2, 1, 1), Color(0, 0, 0, 255), /*tolerance=*/10);
        }
    }

public:
    OpenGL4SkinnedEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4SkinnedEffectTest>();
}
