// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-11 (D9-111): validates D3D9EffectRenderer -- runtime D3DCompile() of a
// custom vertex+pixel shader pair, per-stage name->register lookup via D9-110's CTAB parser, and
// real uniform-driven pixel output. Mirrors D3D11EffectRenderer's own DX-58 test bar: "compile a
// trivial custom vertex+pixel shader pair at runtime, draw a real triangle with it, read back the
// exact pixel color driven by a uniform."
//
// Check A -- CompileProgram() succeeds for a real custom vertex+pixel shader pair (vs_2_0/ps_2_0
//   under Reach).
// Check B -- Bind() + a real draw + a uniform-driven pixel color: SetUniformVec4("MyColor", ...)
//   on the PIXEL shader's own real register genuinely drives the rendered output -- not a
//   hardcoded/ignored no-op.
// Check C -- changing the SAME uniform to a DIFFERENT color changes the rendered output
//   accordingly -- a real, discriminating "the upload is genuinely per-call" proof, not just
//   "some color happened to render once".
// Check D -- SetUniformMat4("WorldViewProj", ...) on the VERTEX shader's own real register
//   genuinely drives geometry: translating World far outside the NDC cube leaves the target pixel
//   unpainted (same "oversized triangle" discipline DirectX9_Draw's own Check C already established).
// Check E -- GetCompileError() reports a non-empty, real compiler diagnostic for a deliberately
//   broken shader, and CompileProgram() returns false / IsValid() stays false.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9EffectRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexDeclarations.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX9;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    // Stride-16 VertexPositionColor (POSITION0 FLOAT3 @0, COLOR0 UBYTE4N @12) -- an existing,
    // already-established D3D9VertexDeclarations entry. The custom pixel shader below never reads
    // COLOR0 -- its output color comes entirely from the "MyColor" uniform, which is the whole
    // point of Checks B/C.
    struct PositionColorVertex { float x, y, z; uint32_t color; };

    const char* kVertexShaderSrc = R"(
float4x4 WorldViewProj;

struct VSInput { float3 Position : POSITION0; };
struct VSOutput { float4 Position : POSITION0; };

VSOutput main(VSInput input)
{
    VSOutput output;
    output.Position = mul(float4(input.Position, 1.0), WorldViewProj);
    return output;
}
)";

    const char* kPixelShaderSrc = R"(
float4 MyColor;

float4 main() : COLOR0
{
    return MyColor;
}
)";

    const char* kBrokenShaderSrc = R"(
this is not valid HLSL at all !!!
)";

    // Matches this project's own established Matrix->HLSL-register convention
    // (D3D9EffectDraw.cpp's own UploadMatrixConstantVS): transpose, then column-major-pack into a
    // flat float[16] -- SetUniformMat4() takes a raw pointer, so the caller (here, the test itself,
    // playing the role of game code) is responsible for this conversion, exactly as
    // D3D11EffectRenderer's own SetUniformMat4() contract already requires of ITS callers.
    void ToShaderRegistersEXT(const Matrix& m, float out[16])
    {
        const Matrix transposed = Matrix::Transpose(m);
        transposed.ToColumnMajor(out);
    }
}

class D3D9EffectRendererTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX9Renderer&>(dev.GetRenderer());
        IDirect3DDevice9* device = renderer.GetDeviceEXT();

        renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, CNA::Internal::Renderers::BlendWriteState{}); // REMED-GFX-077 default write state
        renderer.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);

        // Full-NDC oversized triangle -- with World=View=Projection=Identity, these Position
        // values ARE clip-space coordinates directly, same trick DirectX9_Draw's own kTri establishes.
        const PositionColorVertex tri[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFFFFFFFFu},
            { 3.0f, -1.0f, 0.0f, 0xFFFFFFFFu},
            {-1.0f,  3.0f, 0.0f, 0xFFFFFFFFu},
        };

        UINT declCount = 0;
        const D3DVERTEXELEMENT9* elems = VertexElementsForStrideD3D9(16, declCount);
        Microsoft::WRL::ComPtr<IDirect3DVertexDeclaration9> decl;
        device->CreateVertexDeclaration(elems, decl.GetAddressOf());

        auto vb = renderer.CreateVertexBuffer(3);
        vb->SetData(tri, 3, sizeof(PositionColorVertex));
        auto* d3d9Vb = static_cast<D3D9VertexBufferRenderer*>(vb.get());

        auto drawFullscreenTriangle = [&](const Matrix& world)
        {
            device->SetVertexDeclaration(decl.Get());
            device->SetStreamSource(0, d3d9Vb->GetBufferEXT(), 0, sizeof(PositionColorVertex));
            (void)world; // WorldViewProj is uploaded via SetUniformMat4 directly, not via this param
            device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
        };

        const Microsoft::Xna::Framework::Rectangle centerRegion(28, 28, 4, 4);
        auto readCenter = [&]() -> Color
        {
            std::vector<Color> px(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, px.data(), 0, static_cast<int>(px.size()));
            return px[0];
        };

        // Check A: CompileProgram() succeeds.
        D3D9EffectRenderer effect(device, /*hiDef=*/false);
        const bool compiled = effect.CompileProgram(kVertexShaderSrc, kPixelShaderSrc);
        check(compiled && effect.IsValid(),
              "D3D9EffectRenderer::CompileProgram(): a real custom vertex+pixel shader pair compiles "
              "successfully via the real D3DCompile() (vs_2_0/ps_2_0 under Reach)");

        if (compiled)
        {
            float identityRegs[16];
            ToShaderRegistersEXT(Matrix::getIdentityProperty(), identityRegs);
            effect.SetUniformMat4("WorldViewProj", identityRegs);

            // Check B: a uniform-driven pixel color.
            effect.SetUniformVec4("MyColor", 1.0f, 0.0f, 0.0f, 1.0f); // opaque red
            dev.Clear(Color(0, 0, 255, 255));
            effect.Bind();
            drawFullscreenTriangle(Matrix::getIdentityProperty());
            effect.Unbind();
            const Color afterRed = readCenter();
            check(afterRed.getRProperty() == 255 && afterRed.getGProperty() == 0 &&
                  afterRed.getBProperty() == 0 && afterRed.getAProperty() == 255,
                  "D3D9EffectRenderer: SetUniformVec4(\"MyColor\", 1,0,0,1) on the pixel shader's own real "
                  "register genuinely drives the rendered output to opaque red, not a hardcoded/ignored no-op");

            // Check C: a DIFFERENT uniform value changes the output accordingly.
            effect.SetUniformVec4("MyColor", 0.0f, 1.0f, 0.0f, 1.0f); // opaque green
            dev.Clear(Color(0, 0, 255, 255));
            effect.Bind();
            drawFullscreenTriangle(Matrix::getIdentityProperty());
            effect.Unbind();
            const Color afterGreen = readCenter();
            check(afterGreen.getRProperty() == 0 && afterGreen.getGProperty() == 255 &&
                  afterGreen.getBProperty() == 0 && afterGreen.getAProperty() == 255,
                  "D3D9EffectRenderer: changing the SAME uniform to a different value (0,1,0,1) genuinely "
                  "changes the rendered output to opaque green -- the upload is per-call, not a one-time latch");

            // Check D: WorldViewProj (vertex shader) is a real register upload. Split into TWO
            // halves deliberately -- a mutation-testing pass found that "far away leaves it
            // unpainted" ALONE is not discriminating: a WorldViewProj upload that silently never
            // happens at all leaves the register at its uninitialized/zero value, which ALSO
            // degenerates the triangle to nothing and ALSO leaves the background unpainted, for a
            // completely different (broken) reason. D1 closes that gap by first asserting the
            // POSITIVE case (an explicit identity re-upload genuinely still paints red) right
            // before D2's negative case, so a stuck-at-zero register fails D1, not silently
            // slipping through under D2's own "unpainted" success criterion.
            effect.SetUniformVec4("MyColor", 1.0f, 0.0f, 0.0f, 1.0f);
            effect.SetUniformMat4("WorldViewProj", identityRegs);
            dev.Clear(Color(0, 0, 255, 255));
            effect.Bind();
            drawFullscreenTriangle(Matrix::getIdentityProperty());
            effect.Unbind();
            const Color afterIdentity = readCenter();
            check(afterIdentity.getRProperty() == 255 && afterIdentity.getGProperty() == 0 &&
                  afterIdentity.getBProperty() == 0 && afterIdentity.getAProperty() == 255,
                  "D3D9EffectRenderer (D1): re-uploading WorldViewProj as identity genuinely still paints "
                  "red -- the positive-case anchor D2 below depends on to be a real discriminator");

            float farAwayRegs[16];
            ToShaderRegistersEXT(Matrix::CreateTranslation(1000.0f, 0.0f, 0.0f), farAwayRegs);
            effect.SetUniformMat4("WorldViewProj", farAwayRegs);
            dev.Clear(Color(0, 0, 255, 255));
            effect.Bind();
            drawFullscreenTriangle(Matrix::getIdentityProperty());
            effect.Unbind();
            const Color afterFarAway = readCenter();
            check(afterFarAway.getRProperty() == 0 && afterFarAway.getGProperty() == 0 &&
                  afterFarAway.getBProperty() == 255 && afterFarAway.getAProperty() == 255,
                  "D3D9EffectRenderer (D2): SetUniformMat4(\"WorldViewProj\", ...) on the vertex shader's own "
                  "real register is genuine -- translating it far outside the NDC cube leaves the background "
                  "unpainted, proven discriminating by D1's own identity-still-paints-red anchor immediately before");

            device->SetVertexShader(nullptr);
            device->SetPixelShader(nullptr);
        }

        // Check E: a deliberately broken shader fails cleanly with a real diagnostic.
        D3D9EffectRenderer brokenEffect(device, /*hiDef=*/false);
        const bool brokenCompiled = brokenEffect.CompileProgram(kBrokenShaderSrc, kPixelShaderSrc);
        check(!brokenCompiled && !brokenEffect.IsValid() && !brokenEffect.GetCompileError().empty(),
              "D3D9EffectRenderer::CompileProgram(): a deliberately invalid HLSL vertex shader fails to "
              "compile, IsValid() stays false, and GetCompileError() reports a real, non-empty diagnostic");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9EffectRendererTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }
};

int main()
{
    {
        D3D9EffectRendererTest game;
        game.Run();
    }

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
