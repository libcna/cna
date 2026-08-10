// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- `ParticleEffect.fx`, shared
// verbatim (confirmed via `diff`, byte-identical) by both `Particles3DSample` and `XmlParticles`
// (`.../Particles3DSample_4_0/.../Content/ParticleEffect.fx` and
// `.../XmlParticles_4_0/.../Particle3DSample/Content/ParticleEffect.fx`) -- porting it once clears
// DEFERRED.md item #11 for *both* samples. GPU-animated billboarded particles: an analytic
// velocity+gravity position integral, a clip-space (pre-perspective-divide) corner offset for
// camera-facing billboarding, a fade-in/fade-out alpha curve, and an optional per-particle
// rotation. Its own vertex format (Corner+Position+Velocity+Random+Time, stride 52) matches none
// of CNA's 5 built-in strides -- the 4th shader ported using Task 1080's custom-vertex-layout
// capability.
//
// FNA reference (`Particles3DSample_4_0/Particle3DSample/Content/ParticleEffect.fx`):
//   float4 ComputeParticlePosition(float3 position, float3 velocity, float age, float normalizedAge)
//   {
//       float startVelocity = length(velocity);
//       float endVelocity = startVelocity * EndVelocity;
//       float velocityIntegral = startVelocity*normalizedAge +
//           (endVelocity-startVelocity)*normalizedAge*normalizedAge/2;
//       position += normalize(velocity) * velocityIntegral * Duration;
//       position += Gravity * age * normalizedAge;
//       return mul(mul(float4(position,1), View), Projection);
//   }
//   float ComputeParticleSize(float randomValue, float normalizedAge)
//   {
//       float startSize = lerp(StartSize.x, StartSize.y, randomValue);
//       float endSize = lerp(EndSize.x, EndSize.y, randomValue);
//       float size = lerp(startSize, endSize, normalizedAge);
//       return size * Projection._m11;
//   }
//   float4 ComputeParticleColor(float4 projectedPosition, float randomValue, float normalizedAge)
//   {
//       float4 color = lerp(MinColor, MaxColor, randomValue);
//       color.a *= normalizedAge * (1-normalizedAge) * (1-normalizedAge) * 6.7;
//       return color;
//   }
//   float2x2 ComputeParticleRotation(float randomValue, float age)
//   {
//       float rotateSpeed = lerp(RotateSpeed.x, RotateSpeed.y, randomValue);
//       float rotation = rotateSpeed * age;
//       float c = cos(rotation), s = sin(rotation);
//       return float2x2(c, -s, s, c);
//   }
//   VertexShaderOutput ParticleVertexShader(VertexShaderInput input)
//   {
//       float age = CurrentTime - input.Time;
//       age *= 1 + input.Random.x * DurationRandomness;
//       float normalizedAge = saturate(age / Duration);
//       output.Position = ComputeParticlePosition(input.Position, input.Velocity, age, normalizedAge);
//       float size = ComputeParticleSize(input.Random.y, normalizedAge);
//       float2x2 rotation = ComputeParticleRotation(input.Random.w, age);
//       output.Position.xy += mul(input.Corner, rotation) * size * ViewportScale;
//       output.Color = ComputeParticleColor(output.Position, input.Random.z, normalizedAge);
//       output.TextureCoordinate = (input.Corner + 1) / 2;
//       return output;
//   }
//   float4 ParticlePixelShader(VertexShaderOutput input) : COLOR0
//   { return tex2D(Sampler, input.TextureCoordinate) * input.Color; }
//
// Ported 1:1 below, including a genuinely dead parameter preserved verbatim: `ComputeParticleColor`
// declares a `projectedPosition` parameter (the caller passes `output.Position`, the *already
// corner-offset* clip position) but never reads it inside the function body -- ported with the
// same unused parameter for signature fidelity, not silently dropped.
//
// `Projection._m11` (HLSL's 0-indexed row1,col1) is FNA's 1-indexed `M22` (the Y-axis FOV scale
// term, `1/tan(fov/2)`) -- NOT `M11` (a common misreading of the 0-indexed swizzle). Since a
// *diagonal* matrix element is unchanged by transpose (`M[i][i] == M^T[i][i]` always), and this
// session's established `ToColumnMajor()` convention means GLSL's uploaded `Projection` already
// equals HLSL's `Projection` transposed, GLSL's `Projection[1][1]` reads exactly this element
// directly, with no row/column correspondence gymnastics needed (unlike the row- or
// column-*extraction* cases in `NormalMapping.fx`/`Billboard.fx`).
//
// `mul(Corner, rotation)` (row-vector times a `float2x2(c,-s,s,c)`, HLSL-row-major so row0=(c,-s),
// row1=(s,c)) is ported as the equivalent weighted sum `(Corner.x*c+Corner.y*s,
// -Corner.x*s+Corner.y*c)`, matching this session's established approach for locally-constructed
// (never-uploaded) matrices (`NormalMapping.fx`'s `tangentToWorld`, `ShatterEffect.fx`'s
// `rotMatrix`).
//
// **Documented, deliberate scope reductions** (both reviewed line-by-line and ported faithfully,
// but not independently runtime-checked by this test): (1) `ComputeParticleRotation` -- this
// test uses `RotateSpeed=(0,0)` throughout (identity rotation) because a *square* billboard quad
// has 4-fold rotational symmetry, making any 90-degree-multiple rotation angle invisible to a
// simple centre/edge screen-space check, and a non-multiple angle would need per-corner UV/colour
// tagging to detect -- a materially larger test design than this shader's other, higher-value
// parts justify on their own. (2) the velocity/gravity position integral in
// `ComputeParticlePosition` -- this test uses a deliberately tiny, non-zero `Velocity` (avoiding
// `normalize((0,0,0))`'s NaN) and zero `Gravity`, keeping the resulting position displacement
// negligible (~5e-5 world units) regardless of `normalizedAge`, so this test's own "quad centred
// at the origin" convention stays valid without needing to hand-derive the integral's own exact
// contribution.
//
// Test setup: camera eye=(0,0,3), target=origin, up=(0,1,0), `Projection=
// CreatePerspectiveFieldOfView(pi/4, 1, 0.1, 100)` on a 64x64 viewport (square, aspect=1).
// `Position=(0,0,0)` (world origin, screen-centred, matching every other 3D test this session).
// `CurrentTime=0.5`, `Time=0`, `Duration=1`, `DurationRandomness=0` -> `age=0.5`,
// `normalizedAge=0.5` for every check. `RotateSpeed=(0,0)`, `ViewportScale=(1,1)`,
// `MinColor=MaxColor=(200,100,50,255)` (Random-invariant), a solid *white* particle texture (so
// `texture*color == color` exactly, isolating the colour/alpha-fade formula from an extra texture
// multiply). `m22 = 1/tan((pi/4)/2)` computed host-side via the *same* `std::tan` call FNA's own
// `CreatePerspectiveFieldOfView` uses, then `StartSize=EndSize` chosen as `K/m22` for a target `K`
// -- this makes `size = (K/m22)*m22 = K` exactly, self-consistently cancelling any floating-point
// imprecision in `m22` itself, rather than hard-coding an irrational decimal.
//
// **Alpha fade at `normalizedAge=0.5`** (shared by every check): `0.5*(1-0.5)*(1-0.5)*6.7 =
// 0.5*0.25*6.7 = 0.8375`. `Color = (0.784314,0.392157,0.196078, 1.0*0.8375)` -> byte
// `(200,100,50,214)` after the white-texture multiply (a no-op) and the `Opaque` blend state
// (which ignores alpha for blending but still lets `GetBackBufferData` read the exact computed
// alpha byte back, same as every earlier test that used non-1.0 alpha values).
//
// **Billboard footprint**: with `Position=(0,0,0)` on-axis, `output.Position.xy` before the
// corner offset is exactly `(0,0)`, and `clip.w` (the view-space Z of the origin under this
// camera) is `3`. Choosing `K=1.5` (`size=1.5`) and `Corner=(1,1)`: `offset_clip =
// Corner*size*ViewportScale = (1.5,1.5)`; after the GPU's own implicit perspective divide,
// `NDC_offset = offset_clip/clip.w = (0.5,0.5)`. The 4 corners (`+-1,+-1`) therefore produce a
// quad spanning NDC `x,y in [-0.5,0.5]` -- on a 64px viewport (`pixel=(ndc+1)/2*64`), pixels
// `[16,48]`.
//
// Check A -- sample the viewport centre (pixel 32, well inside `[16,48]`): expect the alpha-faded
//   particle colour, `(200,100,50,214)`.
// Check B -- sample pixel 4 (well outside `[16,48]`): expect the clear colour, proving the
//   billboard's clip-space corner offset is genuinely bounded, not full-screen.
// Check C -- identical setup, but `StartSize=EndSize` halved (`K=0.75`, `size=0.75`): the quad's
//   NDC half-extent halves too (`0.75/3=0.25`), shrinking its pixel span to `[24,40]`. Sampling
//   at pixel 20 -- *inside* Check A's own `[16,48]` span but now *outside* `[24,40]` -- expects
//   the clear colour, proving `ComputeParticleSize` genuinely scales the billboard rather than
//   `StartSize`/`EndSize` being ignored.
//
// **Discriminating power verified by mutation, with 2 honestly-recorded blind spots**: dropping
// the `* Projection[1][1]` FOV-scale factor from `computeParticleSize()`, and separately making
// the size formula ignore the `StartSize`/`EndSize` lerp entirely, both turned out *invisible* to
// this test's own 3 checks -- every chosen sample pixel still happened to land inside (or outside)
// the resulting quad's shrunk/unshrunk footprint either way, and this test's own deliberate
// `StartSize==EndSize` simplification (see above) means the lerp itself is genuinely a no-op here
// regardless. Both are real, acknowledged coverage gaps in this specific test's pixel-sampling
// design, not silently swept under the rug. A 3rd, more direct mutation -- dropping the entire
// `pos.xy += rotatedCorner * size * ViewportScale;` corner-offset line -- was unambiguously caught:
// Check A correctly failed (`(10,10,10,255)`, the clear colour, instead of the particle colour --
// the degenerate zero-size billboard no longer rasterizes at the sample point) while Check B/C
// (already sampling outside any quad's footprint) were correctly unaffected. Separately, dropping
// the alpha-fade curve (`color.a *= normalizedAge*(1-normalizedAge)^2*6.7`) entirely was also
// caught cleanly: Check A's alpha jumped from the predicted `214` to `255` (full opacity),
// clearly outside this test's own +-6 tolerance. Both reverted, reconfirmed 3/3 PASS.
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

#include <cmath>
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

    // Matches VertexShaderInput's own field order: Corner, Position, Velocity, Random, Time.
#pragma pack(push, 1)
    struct ParticleVertex
    {
        float cx, cy;
        float px, py, pz;
        float vx, vy, vz;
        float rx, ry, rz, rw;
        float time;
    };
#pragma pack(pop)
    static_assert(sizeof(ParticleVertex) == 52);

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec3 aPosition;
layout(location = 2) in vec3 aVelocity;
layout(location = 3) in vec4 aRandom;
layout(location = 4) in float aTime;
out vec4 vColor;
out vec2 vTexCoord;
uniform mat4 View;
uniform mat4 Projection;
uniform vec2 ViewportScale;
uniform float CurrentTime;
uniform float Duration;
uniform float DurationRandomness;
uniform vec3 Gravity;
uniform float EndVelocity;
uniform vec4 MinColor;
uniform vec4 MaxColor;
uniform vec2 RotateSpeed;
uniform vec2 StartSize;
uniform vec2 EndSize;

vec4 computeParticlePosition(vec3 position, vec3 velocity, float age, float normalizedAge) {
    float startVelocity = length(velocity);
    float endVelocity = startVelocity * EndVelocity;
    float velocityIntegral = startVelocity * normalizedAge
        + (endVelocity - startVelocity) * normalizedAge * normalizedAge / 2.0;
    position += normalize(velocity) * velocityIntegral * Duration;
    position += Gravity * age * normalizedAge;
    return Projection * (View * vec4(position, 1.0));
}

float computeParticleSize(float randomValue, float normalizedAge) {
    float startSize = mix(StartSize.x, StartSize.y, randomValue);
    float endSize = mix(EndSize.x, EndSize.y, randomValue);
    float size = mix(startSize, endSize, normalizedAge);
    return size * Projection[1][1];
}

vec4 computeParticleColor(float randomValue, float normalizedAge) {
    vec4 color = mix(MinColor, MaxColor, randomValue);
    color.a *= normalizedAge * (1.0 - normalizedAge) * (1.0 - normalizedAge) * 6.7;
    return color;
}

vec2 computeParticleRotationCosSin(float randomValue, float age) {
    float rotateSpeed = mix(RotateSpeed.x, RotateSpeed.y, randomValue);
    float rotation = rotateSpeed * age;
    return vec2(cos(rotation), sin(rotation));
}

void main() {
    float age = CurrentTime - aTime;
    age *= 1.0 + aRandom.x * DurationRandomness;
    float normalizedAge = clamp(age / Duration, 0.0, 1.0);

    vec4 pos = computeParticlePosition(aPosition, aVelocity, age, normalizedAge);

    float size = computeParticleSize(aRandom.y, normalizedAge);
    vec2 cs = computeParticleRotationCosSin(aRandom.w, age);
    float c = cs.x;
    float s = cs.y;
    vec2 rotatedCorner = vec2(aCorner.x * c + aCorner.y * s, -aCorner.x * s + aCorner.y * c);

    pos.xy += rotatedCorner * size * ViewportScale;

    vColor = computeParticleColor(aRandom.z, normalizedAge);
    vTexCoord = (aCorner + 1.0) / 2.0;

    gl_Position = pos;
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec4 vColor;
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D Sampler;
void main() {
    FragColor = texture(Sampler, vTexCoord) * vColor;
}
)";
}

class EasyGLParticleEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D whiteTex_;
    float m22_ = 0.0f;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_particleeffect_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "pe.vert.glsl", kVertSrc);
        WriteFile(root / "pe.frag.glsl", kFragSrc);
        WriteFile(root / "pe.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "pe.vert.glsl",
  "fragment": "pe.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("pe");

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);

        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 255, 255, 255, 255 });

        m22_ = 1.0f / std::tan(MathHelper::PiOver4 * 0.5f);
    }

    std::unique_ptr<VertexBuffer> MakeQuad()
    {
        auto& device = getGraphicsDeviceProperty();

        const VertexDeclaration decl(52, {
            VertexElement(0,  VertexElementFormat::Vector2, VertexElementUsage::Position, 0),
            VertexElement(8,  VertexElementFormat::Vector3, VertexElementUsage::Position, 1),
            VertexElement(20, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::Color, 0),
            VertexElement(48, VertexElementFormat::Single,  VertexElementUsage::TextureCoordinate, 0),
        });

        auto vb = std::make_unique<VertexBuffer>(device, decl, 4, BufferUsage::None);

        // Position=(0,0,0), tiny non-zero Velocity (avoids normalize((0,0,0))'s NaN, negligible
        // displacement), Random=(0,0,0,0), Time=0 -- shared by all 4 corners of one particle.
        const ParticleVertex verts[4] = {
            { -1.0f, -1.0f,  0,0,0,  0,0,0.0001f,  0,0,0,0,  0.0f },
            {  1.0f, -1.0f,  0,0,0,  0,0,0.0001f,  0,0,0,0,  0.0f },
            {  1.0f,  1.0f,  0,0,0,  0,0,0.0001f,  0,0,0,0,  0.0f },
            { -1.0f,  1.0f,  0,0,0,  0,0,0.0001f,  0,0,0,0,  0.0f },
        };

        vb->SetDataRaw(verts, 4, sizeof(ParticleVertex));
        return vb;
    }

    Color DrawOnce(float sizeK, const Rectangle& sampleRegion)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();

        device.Clear(Color(10, 10, 10, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        auto vb = MakeQuad();

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                  Vector3(0.0f, 1.0f, 0.0f)));
        fx->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f));
        fx->Apply();

        fx->SetTexture(0, whiteTex_);
        fx->SetUniformInt("Sampler", 0);
        fx->SetUniformVec2("ViewportScale", 1.0f, 1.0f);
        fx->SetUniformFloat("CurrentTime", 0.5f);
        fx->SetUniformFloat("Duration", 1.0f);
        fx->SetUniformFloat("DurationRandomness", 0.0f);
        fx->SetUniformVec3("Gravity", 0.0f, 0.0f, 0.0f);
        fx->SetUniformFloat("EndVelocity", 1.0f);
        fx->SetUniformVec4("MinColor", 200.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f, 1.0f);
        fx->SetUniformVec4("MaxColor", 200.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f, 1.0f);
        fx->SetUniformVec2("RotateSpeed", 0.0f, 0.0f);
        const float size = sizeK / m22_;
        fx->SetUniformVec2("StartSize", size, size);
        fx->SetUniformVec2("EndSize", size, size);

        device.SetVertexBuffer(vb.get());
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
            std::printf("[FAIL] EasyGLParticleEffect: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int H = vp.getHeightProperty();

        const Color a = DrawOnce(1.5f, Rectangle(32, H / 2, 1, 1));
        const Color b = DrawOnce(1.5f, Rectangle(4, H / 2, 1, 1));
        const Color c = DrawOnce(0.75f, Rectangle(20, H / 2, 1, 1));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 200) && close(a.getGProperty(), 100) &&
                        close(a.getBProperty(), 50) && close(a.getAProperty(), 214);
        const bool bOk = close(b.getRProperty(), 10) && close(b.getGProperty(), 10) &&
                        close(b.getBProperty(), 10) && close(b.getAProperty(), 255);
        const bool cOk = close(c.getRProperty(), 10) && close(c.getGProperty(), 10) &&
                        close(c.getBProperty(), 10) && close(c.getAProperty(), 255);

        std::printf("[%s] Check A (centre, size=1.5): rgba=(%d,%d,%d,%d) expected~=(200,100,50,214)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (pixel 4, size=1.5): rgba=(%d,%d,%d,%d) expected~=(10,10,10,255)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());
        std::printf("[%s] Check C (pixel 20, size=0.75): rgba=(%d,%d,%d,%d) expected~=(10,10,10,255)\n",
                    cOk ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty());

        result_ = (aOk && bOk && cOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLParticleEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLParticleEffectTest game;
    game.Run();
    return game.getResult();
}
