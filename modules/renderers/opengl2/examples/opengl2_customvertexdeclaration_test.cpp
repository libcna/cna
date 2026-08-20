// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: proof that a genuinely custom VertexDeclaration (attribute order that matches
// NONE of this renderer's fixed byte-strides) is honored via VertexBuffer::SetDataRaw() ->
// IVertexBufferRenderer::SetVertexDeclaration() -> BindVertexAttributesForDeclaration(), instead
// of silently falling back to (and misreading data through) the fixed-offset stride dispatch.
//
// The declaration below deliberately REORDERS the standard VertexPositionColorTexture fields --
// Color first, then Position, then TextureCoordinate -- while keeping the total size at exactly
// 24 bytes, the SAME size as the built-in VertexPositionColorTexture stride. This is a
// deliberate differentiator: if BindVertexAttributesForDeclaration() were accidentally bypassed
// in favour of the old fixed-offset stride-24 dispatch (which assumes Position@0/Color@12/
// TexCoord@16), it would misread the Color bytes as Position floats, producing degenerate/absent
// geometry -- so a correctly-rendered quad here is a real, meaningful proof the declaration path
// is genuinely driving the bind, not a coincidence of matching sizes.
//
// A custom ShaderEffect (World/View/Projection uniforms, the real XNA-HLSL-style names direct 3D
// custom-effect draws use -- see opengl2_shadereffect_test.cpp) consumes it via this renderer's
// own established attribute-naming convention (aColor/aPosition/aTexCoord), proving the
// customEffectRenderer + custom-declaration combination this renderer also wires works together.
//
// Check A -- the reordered-declaration quad renders its own solid texture colour at the centre
//   pixel (not black/garbage) -- proves the declaration, not stale/misread offsets, drove the bind.
// Check B -- 30 frames of the whole scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 30;
    constexpr int kSize = 64;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

    const char* kVert =
        "attribute vec3 aPosition;attribute vec4 aColor;attribute vec2 aTexCoord;"
        "uniform mat4 World;uniform mat4 View;uniform mat4 Projection;"
        "varying vec4 vColor;varying vec2 vTex;"
        "void main(){gl_Position=Projection*View*World*vec4(aPosition,1.0);vColor=aColor;vTex=aTexCoord;}";
    const char* kFrag =
        "varying vec4 vColor;varying vec2 vTex;uniform sampler2D uTex;"
        "void main(){gl_FragColor=texture2D(uTex,vTex)*vColor;}";

    // Reordered vertex: Color(0,4 bytes) + Position(4,12 bytes) + TexCoord(16,8 bytes) = 24 bytes
    // -- same TOTAL size as VertexPositionColorTexture, but Position is NOT at offset 0.
    struct ReorderedVertex
    {
        std::uint8_t r, g, b, a;
        float x, y, z;
        float u, v;
    };
    static_assert(sizeof(ReorderedVertex) == 24, "reordered vertex must be 24 bytes");
}

class OpenGL2CustomVertexDeclarationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(const std::string& label, bool ok)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    Color ReadPixel(int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&rect, &px, 0, 1);
        return px;
    }

    void RunScene(bool runChecks)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);

        const Color texColor(40, 180, 90, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&texColor, 1);

        ShaderEffect fx(dev, kVert, kFrag);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.SetTexture(0, tex);
        fx.SetUniformInt("uTex", 0);
        fx.Apply();

        const std::uint8_t white[4] = {255, 255, 255, 255};
        const ReorderedVertex verts[6] = {
            {white[0], white[1], white[2], white[3], -1, 1, -0.8f, 0, 0},
            {white[0], white[1], white[2], white[3], 1, -1, -0.8f, 1, 1},
            {white[0], white[1], white[2], white[3], -1, -1, -0.8f, 0, 1},
            {white[0], white[1], white[2], white[3], -1, 1, -0.8f, 0, 0},
            {white[0], white[1], white[2], white[3], 1, 1, -0.8f, 1, 0},
            {white[0], white[1], white[2], white[3], 1, -1, -0.8f, 1, 1},
        };

        const VertexDeclaration decl{
            VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            VertexElement(4, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        };
        VertexBuffer vb(dev, decl, 6, BufferUsage::None);
        vb.SetDataRaw(verts, 6, sizeof(ReorderedVertex));

        dev.Clear(Color::Black);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("custom (reordered) VertexDeclaration renders correctly: got=" + ColorStr(got),
                  Matches(got, texColor, 10));
        }
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        RunScene(/*runChecks=*/frame_ == 1);

        if (frame_ == kTotalFrames)
        {
            Check(std::to_string(kTotalFrames) + " frames render with no exception", true);
            std::printf("=== %d/%d PASS ===\n", passCount_, 2);
            result_ = (passCount_ == 2) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2CustomVertexDeclarationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2CustomVertexDeclarationTest game;
    game.Run();
    return game.getResult();
}
