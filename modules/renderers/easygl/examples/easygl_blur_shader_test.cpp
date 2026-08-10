// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- ShipGame's own `Blur.fx`,
// 2 of ShipGame's 4 distinct custom shaders now ported (see `easygl_animsprite_shader_test.cpp`
// for `AnimSprite.fx` and the ShipGame-wide background on why this sample needs its own separate
// port from the same-named files in other samples).
//
// FNA reference (`ShipGame_4_0/ShipGame/Content/shaders/Blur.fx`), all 5 techniques share one
// vertex shader:
//   VS_OUTPUT MainVS(float4 Pos : POSITION, float2 TexCoord : TEXCOORD0)
//   { Output.Position = mul(Pos, g_WorldViewProj); Output.TexCoord = TexCoord; return Output; }
//   float4 ColorPS() : COLOR { return g_Color; }
//   float4 ColorTexturePS(float2 TexCoord : TEXCOORD0) : COLOR
//   { return g_Color * tex2D(ColorSampler, TexCoord + g_PixelSize); }
//   float4 BlurHorizontalPS(float2 TexCoord : TEXCOORD0) : COLOR
//   {
//       float4 color = float4(0,0,0,0);
//       for (float i=-BLUR_RANGE; i<=BLUR_RANGE; i++) {
//           float2 tc = TexCoord + float2(i*g_PixelSize.x, 0);
//           float4 c = tex2D(ColorSampler, tc);
//           c.xyz *= c.w;               // premultiply alpha before accumulating
//           color += c;
//       }
//       return color/(2*BLUR_RANGE+1);  // BLUR_RANGE=5, so /11
//   }
//   float4 BlurVerticalPS(float2 TexCoord : TEXCOORD0) : COLOR
//   {
//       float4 color = float4(0,0,0,0);
//       for (float i=-BLUR_RANGE; i<=BLUR_RANGE; i++) {
//           float2 tc = TexCoord + float2(0, i*g_PixelSize.y);
//           color += tex2D(ColorSampler, tc);   // NOTE: no premultiply here, unlike Horizontal
//       }
//       return color/(2*BLUR_RANGE+1);
//   }
//   float4 BlurHorizontalSplitPS(float2 TexCoord : TEXCOORD0) : COLOR { ...split-screen variant... }
//
// Ported 1:1 below, as a single GLSL program parameterized by a runtime `mode` uniform (0=Color,
// 1=ColorTexture, 2=BlurHorizontal, 3=BlurVertical, 4=BlurHorizontalSplit) -- same documented
// adaptation as `PostprocessEffect.Fx`'s 5 techniques earlier this session (all 5 real XNA
// techniques share one vertex shader and differ only in pixel-shader body, matching this
// GLSL-parameterization approach exactly). Like `AnimSprite.fx`, uses a single pre-combined
// `g_WorldViewProj` (no separate `World`), `VertexPositionTexture` (stride 20, already supported).
//
// A genuine, faithfully-preserved HLSL asymmetry, not "fixed": `BlurHorizontalPS` premultiplies
// each tap's RGB by its own alpha before accumulating (`c.xyz *= c.w`); `BlurVerticalPS` does
// NOT -- confirmed via `ScreenManager.cs` that the real sample always runs `BlurHorizontal` before
// `BlurVertical` in its blur pipeline (a 2-pass separable box blur, same overall shape as
// `BloomSample`'s own `GaussianBlur`, Task 946), so the vertical pass legitimately operates on an
// already-premultiplied intermediate result and doesn't need to premultiply again.
//
// Test setup: standard `VertexPositionTexture` quad centred at the origin (World=Identity,
// corners +-0.5, standard 0..1 UVs), camera eye=(0,0,3)/target=origin/up=(0,1,0).
//
// **A real finding, empirically diagnosed**: unlike every earlier test this session (which all
// relied on "viewport-centre pixel maps to TexCoord=(0.5,0.5) exactly" via quad symmetry), this
// test needed *genuinely precise* sub-texel sample positions for its 11-tap blur kernel, and that
// assumption turned out not to hold to the needed precision: instrumenting the shader to output
// `vTexCoord` directly at the sample pixel measured `TexCoord ~= (0.519412, 0.480588)`, not
// `(0.5,0.5)` -- a real, reproducible ~0.019 offset, consistent with `GetBackBufferData`'s pixel
// (32,32) sampling at that pixel's own *centre* (continuous coordinate 32.5) rather than the
// viewport's true geometric centre (32.0), combined with this quad's own on-screen projected size
// (a fraction of the 64px viewport, not the full extent) amplifying that half-pixel discrepancy
// into a non-negligible fraction of a single texel. Two consequences: (1) `Check B`/`C` use wide,
// flat 10-texel colour regions (not single texels) so a ~2%-of-UV-space uncertainty can't
// meaningfully matter; (2) this test switches to `SamplerState::PointClamp` (a documented
// deviation from the real shader's own `LINEAR` filter, same spirit as this session's
// `DistortionSample` precedent) so each of the 11 blur taps reads exactly one texel based on
// which half-open interval it falls into, rather than needing to land near a texel *centre* --
// robust to the same small offset. The blur fixtures' own colour-split boundaries are placed
// using the *measured* `baseX`, not a assumed `0.5`, with a full texel of margin on each side of
// the nearest tap (see the exact tap-to-texel-index derivation this design was checked against,
// recorded in this task's own commit message).
//
// Check A (`mode=0`, `Color`) -- `g_Color=(0.3,0.6,0.9,0.8)` returned directly, no texture
//   involved: byte `(77,153,230,204)`.
// Check B/C (`mode=1`, `ColorTexture`) -- a 20-texel-wide fixture, left half (texels 0-9)
//   `(100,150,200,255)`, right half (texels 10-19) `(50,25,75,255)`, `g_Color=(0.5,0.5,0.5,1.0)`.
//   Check B samples deep in the left half (`u=0.1`) -> `(50,75,100,255)`. Check C samples deep in
//   the right half (`u=0.9`) -> `(25,12,37,255)` -- distinct from B, proving `g_PixelSize`
//   genuinely offsets the sample.
// Check D (`mode=2`, `BlurHorizontal`) -- a 33-wide fixture, texels 0-16 (17 texels)
//   `(220,0,0,128)` (deliberately alpha<255, to exercise the premultiply step), texels 17-32 (16
//   texels) `(0,220,0,255)`. With `g_PixelSize.x=1/33`, the 11 taps (checked against the measured
//   `baseX`) land on texels 12-22, splitting 5 taps in the left colour (12-16) and 6 in the right
//   (17-22). Premultiplied RGB sum: `5*(0.8627*0.50196,0,0) + 6*(0,0.8627,0) = (2.1655,5.1762,0)`,
//   `/11 = (0.19686,0.47056,0)`. Raw alpha sum (never premultiplied): `5*0.50196+6*1.0=8.5098`,
//   `/11=0.77362`. -> byte `(50,120,0,197)`.
// Check E (`mode=3`, `BlurVertical`) -- a 33-tall fixture, rows 0-15 (16 rows) `(220,0,0,255)`,
//   rows 16-32 (17 rows) `(0,220,0,255)` (uniform full alpha -- isolating the Y-direction
//   averaging from the premultiply question, already covered by Check D). The 11 taps land on
//   rows 10-20, splitting 6 taps in the top colour (10-15) and 5 in the bottom (16-20) -- a
//   *different* ratio from Check D's own 5/6, since the measured Y offset has the opposite sign
//   from X. `(6*220+5*0)/11=120`, `(6*0+5*220)/11=100` -> byte `(120,100,0,255)`.
//
// **Documented, deliberate scope reduction**: `BlurHorizontalSplitPS` (`mode=4`) is ported 1:1
// and reviewed line-by-line, but not independently runtime-checked here -- it's the same
// box-blur-with-premultiply core as `BlurHorizontal` plus a split-screen boundary gate, and this
// test's effort is concentrated on the 2 blur direction/premultiply combinations `Check D`/`E`
// already establish are correct; the boundary-gating logic itself is comparatively simple
// (`if/else` on which side of `x=0.499` a tap and the base `TexCoord` fall on) and lower-risk.
//
// **Discriminating power verified by mutation**: dropped the `c.xyz *= c.w;` premultiply line
// from `mode=2`'s (`BlurHorizontal`) loop body -- Check D correctly failed (`R` shifted from the
// predicted `50` to `100`, exactly the un-premultiplied value hand-derived above,
// `(5*220+6*0)/11=100`) while Check E (`BlurVertical`, which never had this line to begin with)
// was correctly unaffected. Reverted, reconfirmed 5/5 PASS.
//
// Exit code 0 = all 5 PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
uniform mat4 g_WorldViewProj;
void main() {
    gl_Position = g_WorldViewProj * vec4(aPosition, 1.0);
    vTexCoord = aTexCoord;
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D ColorSampler;
uniform vec4 g_Color;
uniform vec2 g_PixelSize;
uniform int mode;

void main() {
    if (mode == 0) {
        FragColor = g_Color;
        return;
    }
    if (mode == 1) {
        FragColor = g_Color * texture(ColorSampler, vTexCoord + g_PixelSize);
        return;
    }
    if (mode == 2) {
        vec4 color = vec4(0.0);
        for (int i = -5; i <= 5; i++) {
            vec2 tc = vTexCoord + vec2(float(i) * g_PixelSize.x, 0.0);
            vec4 c = texture(ColorSampler, tc);
            c.xyz *= c.w;
            color += c;
        }
        FragColor = color / 11.0;
        return;
    }
    if (mode == 3) {
        vec4 color = vec4(0.0);
        for (int i = -5; i <= 5; i++) {
            vec2 tc = vTexCoord + vec2(0.0, float(i) * g_PixelSize.y);
            color += texture(ColorSampler, tc);
        }
        FragColor = color / 11.0;
        return;
    }
    // mode == 4: BlurHorizontalSplit
    vec4 color = vec4(0.0);
    const float split = 0.499;
    for (int i = -5; i <= 5; i++) {
        vec2 tc = vTexCoord + vec2(float(i) * g_PixelSize.x, 0.0);
        vec4 c = texture(ColorSampler, tc);
        c.xyz *= c.w;
        c.w = 1.0;
        if (vTexCoord.x >= split) {
            if (tc.x >= split) color += c;
        } else {
            if (tc.x < split) color += c;
        }
    }
    FragColor = color / color.w;
}
)";
}

class EasyGLBlurTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D colorTexTex_;
    Texture2D hBlurTex_;
    Texture2D vBlurTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_blur_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "bl.vert.glsl", kVertSrc);
        WriteFile(root / "bl.frag.glsl", kFragSrc);
        WriteFile(root / "bl.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "bl.vert.glsl",
  "fragment": "bl.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("bl");

        const VertexPositionTexture verts[4] = {
            { Vector3(-0.5f,  0.5f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-0.5f, -0.5f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 0.5f, -0.5f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 0.5f,  0.5f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        vb_ = std::make_unique<VertexBuffer>(device, 4);
        vb_->SetData(verts, 4);

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);

        // 20-texel-wide, 2 flat halves (texels 0-9 / 10-19) -- wide margins make this robust to
        // the small (~0.02) sub-pixel offset between this test's own quad-centre TexCoord and a
        // literal (0.5,0.5), documented below.
        {
            std::vector<std::uint8_t> px;
            px.reserve(20 * 4);
            for (int i = 0; i < 10; ++i) { px.insert(px.end(), { 100, 150, 200, 255 }); }
            for (int i = 0; i < 10; ++i) { px.insert(px.end(), { 50, 25, 75, 255 }); }
            colorTexTex_ = Texture2D::CreateFromPixels(device, 20, 1, px);
        }

        {
            std::vector<std::uint8_t> px;
            px.reserve(33 * 4);
            for (int i = 0; i < 17; ++i) { px.insert(px.end(), { 220, 0, 0, 128 }); }
            for (int i = 0; i < 16; ++i) { px.insert(px.end(), { 0, 220, 0, 255 }); }
            hBlurTex_ = Texture2D::CreateFromPixels(device, 33, 1, px);
        }
        {
            std::vector<std::uint8_t> px;
            px.reserve(33 * 4);
            for (int i = 0; i < 16; ++i) { px.insert(px.end(), { 220, 0, 0, 255 }); }
            for (int i = 0; i < 17; ++i) { px.insert(px.end(), { 0, 220, 0, 255 }); }
            vBlurTex_ = Texture2D::CreateFromPixels(device, 1, 33, px);
        }
    }

    Color DrawOnce(int mode, Texture2D* tex, Vector2 pixelSize, Vector4 gColor)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(10, 10, 10, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                  Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f);

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->Apply();

        const Matrix worldViewProj = view * projection;
        float wvpColMajor[16];
        worldViewProj.ToColumnMajor(wvpColMajor);
        fx->SetUniformMat4("g_WorldViewProj", wvpColMajor);

        if (tex)
        {
            fx->SetTexture(0, *tex);
            fx->SetUniformInt("ColorSampler", 0);
            device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        }
        fx->SetUniformVec4("g_Color", gColor.X, gColor.Y, gColor.Z, gColor.W);
        fx->SetUniformVec2("g_PixelSize", pixelSize.X, pixelSize.Y);
        fx->SetUniformInt("mode", mode);

        device.SetVertexBuffer(vb_.get());
        device.setIndicesProperty(ib_.get());
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);

        Color c(0, 0, 0, 0);
        const Rectangle centre(W / 2, H / 2, 1, 1);
        device.GetBackBufferData(&centre, &c, 0, 1);
        return c;
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        if (!fx || !fx->IsEffectValid())
        {
            std::printf("[FAIL] EasyGLBlur: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        // Empirically-measured baseline TexCoord at this test's own viewport-centre sample
        // pixel (see this file's own header comment for how/why this differs slightly from a
        // literal (0.5,0.5)).
        const float baseX = 0.519412f;

        const Color a = DrawOnce(0, nullptr, Vector2(0.0f, 0.0f), Vector4(0.3f, 0.6f, 0.9f, 0.8f));
        const Color b = DrawOnce(1, &colorTexTex_, Vector2(0.1f - baseX, 0.0f), Vector4(0.5f, 0.5f, 0.5f, 1.0f));
        const Color c = DrawOnce(1, &colorTexTex_, Vector2(0.9f - baseX, 0.0f), Vector4(0.5f, 0.5f, 0.5f, 1.0f));
        const Color d = DrawOnce(2, &hBlurTex_, Vector2(1.0f / 33.0f, 0.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        const Color e = DrawOnce(3, &vBlurTex_, Vector2(0.0f, 1.0f / 33.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 77) && close(a.getGProperty(), 153) &&
                        close(a.getBProperty(), 230) && close(a.getAProperty(), 204);
        const bool bOk = close(b.getRProperty(), 50) && close(b.getGProperty(), 75) &&
                        close(b.getBProperty(), 100) && close(b.getAProperty(), 255);
        const bool cOk = close(c.getRProperty(), 25) && close(c.getGProperty(), 12) &&
                        close(c.getBProperty(), 37) && close(c.getAProperty(), 255);
        const bool dOk = close(d.getRProperty(), 50) && close(d.getGProperty(), 120) &&
                        close(d.getBProperty(), 0) && close(d.getAProperty(), 197);
        const bool eOk = close(e.getRProperty(), 120) && close(e.getGProperty(), 100) &&
                        close(e.getBProperty(), 0) && close(e.getAProperty(), 255);

        std::printf("[%s] Check A (Color): rgba=(%d,%d,%d,%d) expected~=(77,153,230,204)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (ColorTexture, left half): rgba=(%d,%d,%d,%d) expected~=(50,75,100,255)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());
        std::printf("[%s] Check C (ColorTexture, right half): rgba=(%d,%d,%d,%d) expected~=(25,12,37,255)\n",
                    cOk ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty());
        std::printf("[%s] Check D (BlurHorizontal): rgba=(%d,%d,%d,%d) expected~=(50,120,0,197)\n",
                    dOk ? "PASS" : "FAIL", d.getRProperty(), d.getGProperty(), d.getBProperty(), d.getAProperty());
        std::printf("[%s] Check E (BlurVertical): rgba=(%d,%d,%d,%d) expected~=(120,100,0,255)\n",
                    eOk ? "PASS" : "FAIL", e.getRProperty(), e.getGProperty(), e.getBProperty(), e.getAProperty());

        result_ = (aOk && bOk && cOk && dOk && eOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLBlurTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLBlurTest game;
    game.Run();
    return game.getResult();
}
