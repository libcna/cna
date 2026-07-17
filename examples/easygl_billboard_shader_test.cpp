// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- BillboardSample's
// `Billboard.fx` (view-facing billboard expansion + wind sway + alpha-tested diffuse lighting).
// Its own vertex format (Position+Normal+TexCoord+Random, stride 36) matches none of CNA's 5
// built-in strides -- ported using Task 1080's custom-vertex-layout capability, same as
// `NormalMapping.fx`.
//
// FNA reference (`BillboardSample_4_0/Billboard/Content/Billboard.fx`):
//   VS_OUTPUT VertexShaderFunction(VS_INPUT input)
//   {
//       float squishFactor = 0.75 + abs(input.Random) / 2;
//       float width = BillboardWidth * squishFactor;
//       float height = BillboardHeight / squishFactor;
//       if (input.Random < 0) width = -width;
//       float3 viewDirection = View._m02_m12_m22;
//       float3 rightVector = normalize(cross(viewDirection, input.Normal));
//       float3 position = input.Position;
//       position += rightVector * (input.TexCoord.x - 0.5) * width;
//       position += input.Normal * (1 - input.TexCoord.y) * height;
//       float waveOffset = dot(position, WindDirection) * WindWaveSize;
//       waveOffset += input.Random * WindRandomness;
//       float wind = sin(WindTime * WindSpeed + waveOffset) * WindAmount;
//       wind *= (1 - input.TexCoord.y);
//       position += WindDirection * wind;
//       output.Position = mul(mul(float4(position, 1), View), Projection);
//       output.TexCoord = input.TexCoord;
//       float diffuseLight = max(-dot(input.Normal, LightDirection), 0);
//       output.Color.rgb = diffuseLight * LightColor + AmbientColor;
//       output.Color.a = 1;
//       return output;
//   }
//   float4 PixelShaderFunction(float2 texCoord : TEXCOORD0, float4 color : COLOR0) : COLOR0
//   {
//       color *= tex2D(TextureSampler, texCoord);
//       clip((color.a - AlphaTestThreshold) * AlphaTestDirection);
//       return color;
//   }
//
// Ported 1:1 below. Notable points:
//
// - No `World` matrix at all in this shader (unusual among this session's ports) -- each
//   billboard's `Position` is already a world-space anchor, baked directly into vertex data by
//   the real sample's own CPU-side code, not transformed by a separate model matrix.
// - `View._m02_m12_m22` (HLSL's *column* index 2, 0-indexed -- distinct from
//   `easygl_normalmapping_shader_test.cpp`'s `_m30_m31_m32`, which was a *row*) extracts the
//   camera's world-space "backward" axis. Given this session's established `ToColumnMajor()`
//   row<->column correspondence (GLSL column *i* == HLSL row *i*), recovering an HLSL *column* in
//   GLSL needs `transpose(mat3(View))[2]` -- `transpose(mat3(View))` first recovers the original
//   (non-transposed) HLSL View as a proper GLSL matrix (same trick as the NormalMapping port),
//   then `[2]` reads its column index 2 (0-indexed) = HLSL's column index 2 = exactly
//   `_m02_m12_m22`. Hand-verified against this test's own camera (eye=(0,0,3), target=origin,
//   up=(0,1,0)) to reproduce `(0,0,1)`, matching `normalize(eye-target)` directly. **Scope note**:
//   same as the NormalMapping port, this camera's rotation-free View makes its own transpose a
//   no-op, so `transpose()` here isn't independently exercised by this test either.
// - `clip(x)` (HLSL: discard the fragment if `x < 0`) is the first alpha-test/`clip()` construct
//   ported this session -- GLSL has no direct equivalent expression, ported as
//   `if (x < 0.0) discard;`, semantically identical.
// - **Documented, deliberate scope reduction**: the wind-sway term (`waveOffset`/`wind`) is ported
//   1:1 and reviewed line-by-line against the source, but not independently exercised by a
//   dedicated runtime check -- `waveOffset` depends on the *already billboard-offset* `position`,
//   which differs per vertex and per corner, making an exact hand-derived pixel-shift check
//   for this specific term meaningfully more complex than the rest of this shader combined; this
//   test instead runs with `WindAmount=0` throughout (the term's own multiplicative gate,
//   collapsing it to provably zero regardless of the rest of the formula) and focuses its
//   verification effort on the genuinely higher-value, harder-to-get-right parts: the view-facing
//   `rightVector` expansion, the alpha test, and the diffuse+ambient lighting combination.
//
// Test setup: a single billboard, world-space anchor `Position=(0,-0.5,0)`, `Normal=(0,1,0)`
// (the billboard's own "up" axis, doubling as the lighting normal, exactly as the shader itself
// uses it), `Random=0.5` for all 4 corners (`squishFactor = 0.75+abs(0.5)/2 = 1.0` exactly, and
// `Random>=0` so `width` keeps its positive sign -- chosen specifically to make `width`/`height`
// reduce to `BillboardWidth`/`BillboardHeight` unchanged, avoiding an extra derived multiplier).
// `BillboardWidth=BillboardHeight=1`. Camera as above.
//
// With `viewDirection=(0,0,1)` and `Normal=(0,1,0)`: `rightVector = normalize(cross((0,0,1),
// (0,1,0))) = normalize((-1,0,0)) = (-1,0,0)`. The 4 corners (`TexCoord` (0,0)/(1,0)/(1,1)/(0,1)):
//   (0,0): anchor + (-1,0,0)*(0-0.5)*1 + (0,1,0)*(1-0)*1 = (0,-0.5,0)+(0.5,0,0)+(0,1,0) = ( 0.5, 0.5,0)
//   (1,0): anchor + (-1,0,0)*(1-0.5)*1 + (0,1,0)*(1-0)*1 = (0,-0.5,0)+(-0.5,0,0)+(0,1,0)= (-0.5, 0.5,0)
//   (1,1): anchor + (-1,0,0)*(0.5)*1   + 0                = (0,-0.5,0)+(-0.5,0,0)      = (-0.5,-0.5,0)
//   (0,1): anchor + (-1,0,0)*(-0.5)*1  + 0                = (0,-0.5,0)+(0.5,0,0)       = ( 0.5,-0.5,0)
// -- a unit square exactly centred at the world origin (anchor deliberately offset to (0,-0.5,0)
// specifically so the *expanded* quad, not the anchor point, lands on-axis with the camera,
// keeping this session's usual "quad centred at the origin -> screen-centre pixel = quad centre"
// hand-derivation trick available despite this shader's anchor-at-the-base billboard convention).
//
// `LightDirection=(0,-1,0)` (light shining straight down, a common outdoor/foliage setup) against
// `Normal=(0,1,0)`: `diffuseLight = max(-dot((0,1,0),(0,-1,0)),0) = max(1,0) = 1`.
// `LightColor=(0.4,0.4,0.4)`, `AmbientColor=(0.2,0.2,0.2)` (test-chosen, not the shader's own
// smaller/larger defaults, picked purely to keep the final colour comfortably unsaturated):
// `Color.rgb = 1*(0.4,0.4,0.4)+(0.2,0.2,0.2) = (0.6,0.6,0.6)`, `Color.a=1`.
//
// Check A -- solid diffuse texture `(200,100,50,255)`, opaque (`AlphaTestThreshold=0.95`,
//   `AlphaTestDirection=1`, texture alpha=1.0 so `(1.0-0.95)*1=0.05>=0`, not discarded): final
//   colour = `Color * texture = (0.6*0.7843,0.6*0.3922,0.6*0.1961,1.0) ~= (0.4706,0.2353,0.1176,1.0)`
//   -> byte `(120,60,30,255)`, sampled at the viewport centre.
// Check B -- same setup, sampled at a point well outside the unit-square billboard's own screen
//   footprint (confirmed empirically, same as this session's own `VertexLighting`/`FlatShaded`
//   precedent for an off-quad sample point): expect the clear colour, proving the billboard's
//   `rightVector`/width/height expansion is genuinely bounded, not full-screen or degenerate.
// Check C -- identical to Check A except the diffuse texture's own alpha is `128` (~0.502):
//   `color.a = Color.a(1.0) * texAlpha(0.502) = 0.502`; `(0.502-0.95)*1 = -0.448 < 0` ->
//   `discard`ed. Expect the clear colour at the viewport centre (same location Check A rendered
//   the billboard), proving `clip()`/the alpha test genuinely discards the fragment rather than
//   silently passing through.
//
// Exit code 0 = all 3 PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

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

    // Matches VS_INPUT's own field order: Position, Normal, TexCoord, Random.
#pragma pack(push, 1)
    struct BillboardVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float random;
    };
#pragma pack(pop)
    static_assert(sizeof(BillboardVertex) == 36);

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in float aRandom;
out vec2 vTexCoord;
out vec4 vColor;
uniform mat4 View;
uniform mat4 Projection;
uniform vec3 LightDirection;
uniform vec3 LightColor;
uniform vec3 AmbientColor;
uniform vec3 WindDirection;
uniform float WindWaveSize;
uniform float WindRandomness;
uniform float WindSpeed;
uniform float WindAmount;
uniform float WindTime;
uniform float BillboardWidth;
uniform float BillboardHeight;
void main() {
    float squishFactor = 0.75 + abs(aRandom) / 2.0;
    float width = BillboardWidth * squishFactor;
    float height = BillboardHeight / squishFactor;
    if (aRandom < 0.0) width = -width;

    vec3 viewDirection = transpose(mat3(View))[2];
    vec3 rightVector = normalize(cross(viewDirection, aNormal));

    vec3 position = aPosition;
    position += rightVector * (aTexCoord.x - 0.5) * width;
    position += aNormal * (1.0 - aTexCoord.y) * height;

    float waveOffset = dot(position, WindDirection) * WindWaveSize;
    waveOffset += aRandom * WindRandomness;
    float wind = sin(WindTime * WindSpeed + waveOffset) * WindAmount;
    wind *= (1.0 - aTexCoord.y);
    position += WindDirection * wind;

    vec4 viewPosition = View * vec4(position, 1.0);
    gl_Position = Projection * viewPosition;

    vTexCoord = aTexCoord;

    float diffuseLight = max(-dot(aNormal, LightDirection), 0.0);
    vColor = vec4(diffuseLight * LightColor + AmbientColor, 1.0);
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec2 vTexCoord;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D TextureSampler;
uniform float AlphaTestThreshold;
uniform float AlphaTestDirection;
void main() {
    vec4 color = vColor * texture(TextureSampler, vTexCoord);
    if ((color.a - AlphaTestThreshold) * AlphaTestDirection < 0.0) discard;
    FragColor = color;
}
)";
}

class EasyGLBillboardTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D opaqueTex_;
    Texture2D transparentTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_billboard_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "bb.vert.glsl", kVertSrc);
        WriteFile(root / "bb.frag.glsl", kFragSrc);
        WriteFile(root / "bb.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "bb.vert.glsl",
  "fragment": "bb.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("bb");

        const VertexDeclaration decl(36, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Single,  VertexElementUsage::TextureCoordinate, 1),
        });

        const float ax = 0.0f, ay = -0.5f, az = 0.0f;
        const float random = 0.5f;
        const BillboardVertex verts[4] = {
            { ax,ay,az,  0,1,0,  0.0f,0.0f,  random },
            { ax,ay,az,  0,1,0,  1.0f,0.0f,  random },
            { ax,ay,az,  0,1,0,  1.0f,1.0f,  random },
            { ax,ay,az,  0,1,0,  0.0f,1.0f,  random },
        };

        vb_ = std::make_unique<VertexBuffer>(device, decl, 4, BufferUsage::None);
        vb_->SetDataRaw(verts, 4, sizeof(BillboardVertex));

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);

        opaqueTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 200, 100, 50, 255 });
        transparentTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 200, 100, 50, 128 });
    }

    Color DrawOnce(Texture2D& tex, const Rectangle& sampleRegion)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();

        device.Clear(Color(10, 10, 10, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                  Vector3(0.0f, 1.0f, 0.0f)));
        fx->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f));
        fx->Apply();

        fx->SetTexture(0, tex);
        fx->SetUniformInt("TextureSampler", 0);
        fx->SetUniformVec3("LightDirection", 0.0f, -1.0f, 0.0f);
        fx->SetUniformVec3("LightColor", 0.4f, 0.4f, 0.4f);
        fx->SetUniformVec3("AmbientColor", 0.2f, 0.2f, 0.2f);
        fx->SetUniformVec3("WindDirection", 1.0f, 0.0f, 0.0f);
        fx->SetUniformFloat("WindWaveSize", 0.1f);
        fx->SetUniformFloat("WindRandomness", 1.0f);
        fx->SetUniformFloat("WindSpeed", 4.0f);
        fx->SetUniformFloat("WindAmount", 0.0f);
        fx->SetUniformFloat("WindTime", 0.0f);
        fx->SetUniformFloat("BillboardWidth", 1.0f);
        fx->SetUniformFloat("BillboardHeight", 1.0f);
        fx->SetUniformFloat("AlphaTestThreshold", 0.95f);
        fx->SetUniformFloat("AlphaTestDirection", 1.0f);

        device.SetVertexBuffer(vb_.get());
        device.setIndicesProperty(ib_.get());
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);

        Color c(0, 0, 0, 0);
        device.GetBackBufferData(&sampleRegion, &c, 0, 1);
        return c;
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        if (!fx || !fx->IsEffectValid())
        {
            std::printf("[FAIL] EasyGLBillboard: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        const Color a = DrawOnce(opaqueTex_, Rectangle(W / 2, H / 2, 1, 1));
        const Color b = DrawOnce(opaqueTex_, Rectangle(W / 16, H / 2, 1, 1));
        const Color c = DrawOnce(transparentTex_, Rectangle(W / 2, H / 2, 1, 1));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 120) && close(a.getGProperty(), 60) &&
                        close(a.getBProperty(), 30) && close(a.getAProperty(), 255);
        const bool bOk = close(b.getRProperty(), 10) && close(b.getGProperty(), 10) &&
                        close(b.getBProperty(), 10) && close(b.getAProperty(), 255);
        const bool cOk = close(c.getRProperty(), 10) && close(c.getGProperty(), 10) &&
                        close(c.getBProperty(), 10) && close(c.getAProperty(), 255);

        std::printf("[%s] Check A (centre, opaque): rgba=(%d,%d,%d,%d) expected~=(120,60,30,255)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (off-quad, opaque): rgba=(%d,%d,%d,%d) expected~=(10,10,10,255)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());
        std::printf("[%s] Check C (centre, alpha-tested-out): rgba=(%d,%d,%d,%d) expected~=(10,10,10,255)\n",
                    cOk ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty());

        result_ = (aOk && bOk && cOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLBillboardTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLBillboardTest game;
    game.Run();
    return game.getResult();
}
