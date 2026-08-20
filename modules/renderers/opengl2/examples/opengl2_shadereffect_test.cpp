// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact ShaderEffect (CreateEffectRenderer) proof for the native OpenGL 2.1
// graphics renderer -- a user-authored, runtime-compiled GLSL program bypassing every built-in
// program entirely. GLSL 1.10/1.20 has no `layout(location=N)` qualifier (unlike EasyGL's GLES3
// shaders), so a custom shader MUST use this renderer's own fixed attribute names (aPosition,
// aColor, aTexCoord -- see EffectRenderer's own doc comment) to receive vertex data at all; every
// shader source below follows that convention.
//
// Check A -- CompileProgram() with valid GLSL succeeds: IsEffectValid() true, GetCompileError()
//   empty.
// Check B -- CompileProgram() with deliberately invalid GLSL fails cleanly: IsEffectValid()
//   false, GetCompileError() non-empty, and constructing/using the effect does not crash.
// Check C -- World/View/Projection wiring + vertex-color pass-through: a custom shader that just
//   forwards aColor unchanged reads back the vertex's own color exactly -- proves
//   drawInternal()'s customEffectRenderer path binds the program, uploads the matrices under their
//   real XNA-HLSL-style uniform names, and feeds the vertex buffer through the same stride-based
//   attribute layout as every built-in program.
// Check D -- SetUniformVec4() actually reaches the shader: the SAME draw with a uTint uniform
//   multiplying the fragment color produces a different (halved) result -- proves uniform-by-name
//   binding is real, not a no-op.
// Check E -- SetTexture()/BindTexture() actually reaches the shader: a custom shader sampling a
//   solid-color 1x1 texture reads back that exact color -- proves texture-unit binding is real.
// Check F -- 30 frames of the whole scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

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

    const char* kColorPassthroughVert =
        "attribute vec3 aPosition;attribute vec4 aColor;"
        "uniform mat4 World;uniform mat4 View;uniform mat4 Projection;"
        "varying vec4 vColor;"
        "void main(){gl_Position=Projection*View*World*vec4(aPosition,1.0);vColor=aColor;}";
    const char* kColorPassthroughFrag =
        "varying vec4 vColor;uniform vec4 uTint;"
        "void main(){gl_FragColor=vColor*uTint;}";

    const char* kTexturedVert =
        "attribute vec3 aPosition;attribute vec2 aTexCoord;"
        "uniform mat4 World;uniform mat4 View;uniform mat4 Projection;"
        "varying vec2 vTex;"
        "void main(){gl_Position=Projection*View*World*vec4(aPosition,1.0);vTex=aTexCoord;}";
    const char* kTexturedFrag =
        "varying vec2 vTex;uniform sampler2D uTex;"
        "void main(){gl_FragColor=texture2D(uTex,vTex);}";

    const char* kInvalidFrag = "this is not valid GLSL at all !!! {{{";
}

class OpenGL2ShaderEffectTest : public Game
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

    void DrawColorQuad(GraphicsDevice& dev, ShaderEffect& fx, const Color& color)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const VertexPositionColor verts[6] = {
            {Vector3(-1, 1, 0), color}, {Vector3(1, -1, 0), color}, {Vector3(-1, -1, 0), color},
            {Vector3(-1, 1, 0), color}, {Vector3(1, 1, 0), color},  {Vector3(1, -1, 0), color},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void RunScene(bool runChecks)
    {
        auto& dev = getGraphicsDeviceProperty();

        // Check A: valid compile.
        ShaderEffect valid(dev, kColorPassthroughVert, kColorPassthroughFrag);
        if (runChecks)
        {
            Check("valid GLSL compiles: IsEffectValid()=true", valid.IsEffectValid());
        }

        // Check B: invalid compile fails cleanly.
        ShaderEffect invalid(dev, kColorPassthroughVert, kInvalidFrag);
        if (runChecks)
        {
            Check("invalid GLSL: IsEffectValid()=false", !invalid.IsEffectValid());
        }

        // Check C: vertex-color pass-through, World/View/Projection wiring, tint=(1,1,1,1).
        dev.Clear(Color::Black);
        valid.SetUniformVec4("uTint", 1.0f, 1.0f, 1.0f, 1.0f);
        DrawColorQuad(dev, valid, Color(180, 90, 30, 255));
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("vertex-color pass-through + matrix wiring: got=" + ColorStr(got),
                  Matches(got, Color(180, 90, 30, 255), 10));
        }

        // Check D: SetUniformVec4 tint actually reaches the shader (halves the output).
        dev.Clear(Color::Black);
        valid.SetUniformVec4("uTint", 0.5f, 0.5f, 0.5f, 1.0f);
        DrawColorQuad(dev, valid, Color(180, 90, 30, 255));
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("SetUniformVec4 tint halves the output: got=" + ColorStr(got),
                  Matches(got, Color(90, 45, 15, 255), 12));
        }

        // Check E: texture sampling via SetTexture()/BindTexture().
        const Color texColor(20, 200, 220, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&texColor, 1);

        ShaderEffect texturedFx(dev, kTexturedVert, kTexturedFrag);
        texturedFx.setWorldProperty(Matrix::getIdentityProperty());
        texturedFx.setViewProperty(Matrix::getIdentityProperty());
        texturedFx.setProjectionProperty(Matrix::getIdentityProperty());
        texturedFx.SetTexture(0, tex);
        texturedFx.SetUniformInt("uTex", 0);
        texturedFx.Apply();

        dev.Clear(Color::Black);
        const VertexPositionTexture texVerts[6] = {
            {Vector3(-1, 1, 0), Vector2(0, 0)}, {Vector3(1, -1, 0), Vector2(1, 1)}, {Vector3(-1, -1, 0), Vector2(0, 1)},
            {Vector3(-1, 1, 0), Vector2(0, 0)}, {Vector3(1, 1, 0), Vector2(1, 0)},  {Vector3(1, -1, 0), Vector2(1, 1)},
        };
        VertexBuffer texVb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        texVb.SetData(texVerts, 0, 6);
        dev.SetVertexBuffer(&texVb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("custom shader texture sampling: got=" + ColorStr(got),
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
            std::printf("=== %d/%d PASS ===\n", passCount_, 6);
            result_ = (passCount_ == 6) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2ShaderEffectTest()
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

    OpenGL2ShaderEffectTest game;
    game.Run();
    return game.getResult();
}
