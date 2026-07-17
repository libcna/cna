// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- ShipGame's own `Particle.fx`
// (distinct from `Particles3DSample`/`XmlParticles`'s own `ParticleEffect.fx`, ported earlier
// this session -- see `easygl_animsprite_shader_test.cpp`'s header for the full ShipGame
// background). With this, all 4 of ShipGame's distinct custom shaders are ported, fully
// clearing DEFERRED.md item #11 for the sample.
//
// Unlike `ParticleEffect.fx` (a quad-corner clip-space-offset billboard technique), this shader
// uses **real GPU point sprites**: a single vertex per particle, an HLSL `PSIZE`-semantic output
// controlling its on-screen pixel size, and the pixel shader reading an automatically-generated
// per-point UV (`TEXCOORD0`/`SPRITETEXCOORD`). GLSL's own equivalents -- `gl_PointSize` (vertex
// shader output, pixels) and `gl_PointCoord` (fragment shader input, `[0,1]^2`) -- are native to
// GLSL ES 3.00 with no special enable needed (unlike desktop GL's `GL_PROGRAM_POINT_SIZE`), and
// `PrimitiveType::PointListEXT` was already wired to `GL_POINTS` in the existing primitive-type
// dispatch -- so this port needed **no new backend capability at all**, just point-sprite-shaped
// GLSL, confirmed by this test actually rendering and reading back real point-sprite pixels.
//
// FNA reference (`ShipGame_4_0/ShipGame/Content/shaders/Particle.fx`):
//   void ParticleVS(in float4 InPosition:POSITION, in float3 InVelocity:NORMAL,
//                  in float2 InTexCoord:TEXCOORD0, out float4 OutPosition:POSITION,
//                  out float4 OutColor:COLOR0, out float OutSize:PSIZE, out float4 OutRotation:COLOR1)
//   {
//       float time = ElapsedTime + ParticleOffset * ParticleTime;   // ParticleOffset=InTexCoord.y
//       float4 Pos = InPosition;
//       if (time < 0) Pos.xyz = 1e10;                               // not yet alive -> move away
//       time = fmod(time, ParticleTime);
//       float norm_time = time / ParticleTime;
//       float vel_len = length(InVelocity);
//       float integral = vel_len * (norm_time - 0.5 * norm_time * norm_time);
//       float3 norm_vel = normalize(InVelocity);
//       Pos.xyz += VelocityScale * norm_vel * integral * ParticleTime;
//       OutPosition = mul(Pos, WorldViewProj);
//       float color_factor = 1.0 - (1.0 - norm_time) * VelocityScale;
//       OutColor = lerp(StartColor, EndColor, color_factor);
//       float2 screen_vel = mul(norm_vel, (float3x3)WorldViewProj).xy;
//       float angle_scale = length(screen_vel);
//       screen_vel = normalize(screen_vel);
//       float4 rot = float4(screen_vel.y, -screen_vel.x, screen_vel.x, screen_vel.y);
//       OutRotation = rot * 0.5 + 0.5;
//       float size = lerp(PointSize.x, PointSize.y, ParticleSize);   // ParticleSize=InTexCoord.x
//       if (TotalTime == ParticleTime) size *= angle_scale;          // burst mode only
//       OutSize = size / OutPosition.w * 360;
//   }
//   float4 ParticlePS(in float4 Color:COLOR0, in float4 Rotation:COLOR1, in float2 TexCoord:TEXCOORD0):COLOR0
//   {
//       Rotation = Rotation * 2 - 1;
//       float2 tc = 0.5 + mul(TexCoord - 0.5, float2x2(Rotation));
//       return Color * tex2D(TextureSampler, tc);
//   }
//
// Ported 1:1 below. Like `AnimSprite.fx`/`Blur.fx`/`ShatterEffect.fx`, a single pre-combined
// `WorldViewProj` (no separate `World`). `mul(norm_vel, (float3x3)WorldViewProj)` uses the
// **standard** `mul(v, M)` row-vector convention (unlike `NormalMapping.fx`'s `mul(matrix,
// vector)`) -- ported as `mat3(WorldViewProj) * norm_vel`, the same established pattern used
// throughout this session. `mul(TexCoord-0.5, float2x2(Rotation))` (a locally-unpacked,
// never-uploaded matrix) is ported as the equivalent explicit weighted sum, same approach as
// every other locally-constructed matrix this session.
//
// **Documented, deliberate scope reductions** (all ported 1:1, reviewed line-by-line, but not
// independently value-checked by a dedicated assertion): the velocity-aligned sprite *rotation*
// (`OutRotation`/`ParticlePS`'s texcoord-rotation) -- this test uses a **solid white** particle
// texture specifically so any rotation is visually undetectable (`Color * white == Color`
// regardless of `tc`), isolating the checks below from needing to hand-derive the rotation
// matrix's own exact value; and burst-mode size scaling (`TotalTime==ParticleTime`) -- this test
// keeps `TotalTime != ParticleTime` throughout, taking the non-burst branch, matching
// `ParticleEffect.fx`'s own earlier "rotation not independently exercised" scope note.
//
// Test setup: camera eye=(0,0,3)/target=origin/up=(0,1,0) (matching every other 3D test this
// session), a single point-sprite vertex at world `Position=(0,0,0)` (screen-centred, `WorldView
// Proj`'s own clip.w at the origin is `3`, the camera distance, established and reused from this
// session's own `NormalMapping.fx`/`ShatterEffect.fx` derivations), tiny non-zero
// `Velocity=(0,0,0.0001)` (avoids `normalize((0,0,0))`'s NaN; kept small enough that the position
// *integral*'s own displacement stays negligible -- ~4e-5 world units -- at every `norm_time` used
// below, so the point's screen position stays effectively fixed at the origin throughout).
// `PointSize=(K,K)` with `K=1/12` chosen so `OutSize = K/3*360 = 10` pixels exactly on this test's
// 64x64 viewport. `VelocityScale=1`, `ParticleTime=1`, `TotalTime=100` (`!=ParticleTime`, so the
// burst-mode size branch is skipped). `StartColor=(1,0,0,1)` (red), `EndColor=(0,0,1,1)` (blue).
//
// **`norm_time` derivation**: with `ParticleOffset=0` (`InTexCoord.y=0`), `time=ElapsedTime`
// directly. Check A/B/C use `ElapsedTime=0` -> `norm_time=0` -> `color_factor=1-(1-0)*1=0` ->
// `OutColor=StartColor` (red) exactly. Check D uses `ElapsedTime=0.5` -> `norm_time=0.5` ->
// `color_factor=1-(1-0.5)*1=0.5` -> `OutColor=lerp(red,blue,0.5)=(0.5,0,0.5,1)` (purple).
//
// Check A -- viewport centre (inside the 10px point sprite), `ElapsedTime=0`: expect red,
//   `(255,0,0,255)` -- proves the point sprite genuinely rasterizes at the correct position with
//   the correct colour.
// Check B -- a point clearly outside the sprite's own 10px radius: expect the clear colour --
//   proves `gl_PointSize` genuinely bounds the sprite's footprint, not full-screen.
// Check C -- viewport centre, `ElapsedTime=-10` (`time=-10<0`): `Pos.xyz` becomes `1e10`, moving
//   the particle far outside the camera frustum -- expect the clear colour, proving the "not yet
//   alive" culling branch genuinely displaces the vertex.
// Check D -- viewport centre, `ElapsedTime=0.5` (`norm_time=0.5`): expect purple, `(128,0,128,
//   255)` -- distinct from Check A's red, proving the `StartColor`/`EndColor` lerp genuinely
//   depends on the particle's own lifecycle time, not a fluke value at `norm_time=0`.
//
// **Discriminating power verified by 2 mutations**: (1) swapped the `mix(StartColor, EndColor,
// color_factor)` argument order -- Check A correctly failed (`(0,0,255,255)`, `EndColor`/blue,
// instead of the predicted red) while B/C/D were unaffected (D lands on the symmetric midpoint
// either way at `color_factor=0.5`, a known, accepted limit of this specific mutation, not a gap
// in the check itself -- Check A alone is sufficient evidence the argument order matters).
// (2) commented out the "not yet alive" culling branch entirely -- Check C correctly failed
// (`(255,0,0,255)`, the particle rendering un-culled at the origin, exactly the un-mutated Check
// A's own colour) while A/B/D were correctly unaffected. Both reverted, reconfirmed 4/4 PASS.
//
// Exit code 0 = all 4 PASS, 1 = any FAIL.

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

    // Matches ParticleVS's own field order: Position, Velocity, TexCoord.
#pragma pack(push, 1)
    struct ParticlePointVertex
    {
        float px, py, pz;
        float vx, vy, vz;
        float u, v;
    };
#pragma pack(pop)
    static_assert(sizeof(ParticlePointVertex) == 32);

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aVelocity;
layout(location = 2) in vec2 aTexCoord;
out vec4 vColor;
out vec4 vRotation;
uniform mat4 WorldViewProj;
uniform vec4 StartColor;
uniform vec4 EndColor;
uniform vec2 PointSize;
uniform float VelocityScale;
uniform vec3 Times;

void main() {
    float ParticleSize = aTexCoord.x;
    float ParticleOffset = aTexCoord.y;
    float ElapsedTime = Times.x;
    float ParticleTime = Times.y;
    float TotalTime = Times.z;

    float time = ElapsedTime + ParticleOffset * ParticleTime;

    vec4 pos = vec4(aPosition, 1.0);
    if (time < 0.0) {
        pos.xyz = vec3(1e10);
    }

    time = mod(time, ParticleTime);
    float norm_time = time / ParticleTime;

    float vel_len = length(aVelocity);
    float integral = vel_len * (norm_time - 0.5 * norm_time * norm_time);
    vec3 norm_vel = normalize(aVelocity);

    pos.xyz += VelocityScale * norm_vel * integral * ParticleTime;

    gl_Position = WorldViewProj * pos;

    float color_factor = 1.0 - (1.0 - norm_time) * VelocityScale;
    vColor = mix(StartColor, EndColor, color_factor);

    vec2 screen_vel = (mat3(WorldViewProj) * norm_vel).xy;
    float angle_scale = length(screen_vel);
    screen_vel = normalize(screen_vel);

    vec4 rot = vec4(screen_vel.y, -screen_vel.x, screen_vel.x, screen_vel.y);
    vRotation = rot * 0.5 + 0.5;

    float size = mix(PointSize.x, PointSize.y, ParticleSize);
    if (TotalTime == ParticleTime) {
        size *= angle_scale;
    }

    gl_PointSize = size / gl_Position.w * 360.0;
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec4 vColor;
in vec4 vRotation;
out vec4 FragColor;
uniform sampler2D TextureSampler;
void main() {
    vec4 rotation = vRotation * 2.0 - 1.0;
    vec2 tc = vec2(0.5, 0.5) + vec2(
        (gl_PointCoord.x - 0.5) * rotation.x + (gl_PointCoord.y - 0.5) * rotation.z,
        (gl_PointCoord.x - 0.5) * rotation.y + (gl_PointCoord.y - 0.5) * rotation.w
    );
    FragColor = vColor * texture(TextureSampler, tc);
}
)";
}

class EasyGLShipGameParticleTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    Texture2D whiteTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_shipgame_particle_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "spf.vert.glsl", kVertSrc);
        WriteFile(root / "spf.frag.glsl", kFragSrc);
        WriteFile(root / "spf.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "spf.vert.glsl",
  "fragment": "spf.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("spf");

        const VertexDeclaration decl(32, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });

        // Position=(0,0,0), tiny non-zero Velocity, ParticleSize=0.5 (irrelevant, PointSize.x==
        // PointSize.y), ParticleOffset=0.
        const ParticlePointVertex verts[1] = {
            { 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 0.0001f,  0.5f, 0.0f },
        };

        vb_ = std::make_unique<VertexBuffer>(device, decl, 1, BufferUsage::None);
        vb_->SetDataRaw(verts, 1, sizeof(ParticlePointVertex));

        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 255, 255, 255, 255 });
    }

    Color DrawOnce(float elapsedTime, const Rectangle& sampleRegion)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();

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
        fx->SetUniformMat4("WorldViewProj", wvpColMajor);

        fx->SetTexture(0, whiteTex_);
        fx->SetUniformInt("TextureSampler", 0);
        fx->SetUniformVec4("StartColor", 1.0f, 0.0f, 0.0f, 1.0f);
        fx->SetUniformVec4("EndColor", 0.0f, 0.0f, 1.0f, 1.0f);
        fx->SetUniformVec2("PointSize", 1.0f / 12.0f, 1.0f / 12.0f);
        fx->SetUniformFloat("VelocityScale", 1.0f);
        fx->SetUniformVec3("Times", elapsedTime, 1.0f, 100.0f);

        device.SetVertexBuffer(vb_.get());
        device.DrawPrimitives(PrimitiveType::PointListEXT, 0, 1);

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
            std::printf("[FAIL] EasyGLShipGameParticle: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        const Color a = DrawOnce(0.0f, Rectangle(W / 2, H / 2, 1, 1));
        const Color b = DrawOnce(0.0f, Rectangle(4, H / 2, 1, 1));
        const Color c = DrawOnce(-10.0f, Rectangle(W / 2, H / 2, 1, 1));
        const Color d = DrawOnce(0.5f, Rectangle(W / 2, H / 2, 1, 1));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 255) && close(a.getGProperty(), 0) &&
                        close(a.getBProperty(), 0) && close(a.getAProperty(), 255);
        const bool bOk = close(b.getRProperty(), 10) && close(b.getGProperty(), 10) &&
                        close(b.getBProperty(), 10) && close(b.getAProperty(), 255);
        const bool cOk = close(c.getRProperty(), 10) && close(c.getGProperty(), 10) &&
                        close(c.getBProperty(), 10) && close(c.getAProperty(), 255);
        const bool dOk = close(d.getRProperty(), 128) && close(d.getGProperty(), 0) &&
                        close(d.getBProperty(), 128) && close(d.getAProperty(), 255);

        std::printf("[%s] Check A (centre, ElapsedTime=0): rgba=(%d,%d,%d,%d) expected~=(255,0,0,255)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (off-sprite): rgba=(%d,%d,%d,%d) expected~=(10,10,10,255)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());
        std::printf("[%s] Check C (culled, ElapsedTime=-10): rgba=(%d,%d,%d,%d) expected~=(10,10,10,255)\n",
                    cOk ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty());
        std::printf("[%s] Check D (centre, ElapsedTime=0.5): rgba=(%d,%d,%d,%d) expected~=(128,0,128,255)\n",
                    dOk ? "PASS" : "FAIL", d.getRProperty(), d.getGProperty(), d.getBProperty(), d.getAProperty());

        result_ = (aOk && bOk && cOk && dOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLShipGameParticleTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLShipGameParticleTest game;
    game.Run();
    return game.getResult();
}
