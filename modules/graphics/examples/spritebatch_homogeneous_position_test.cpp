// SPDX-License-Identifier: MS-PL
// GAP-07: shared SDL GPU/WebGPU fixture. No expectations are captured from CNA.
// XNA Refresh SpriteBatch.SetRenderState IL_0104-0188 retains T*P and Z/W;
// RenderBatch IL_0022-0055 applies the user's pass after the internal SpriteEffect.
// FNA SpriteBatch.cs:1369-1451 supplies raw XYZ; StockEffects/HLSL/SpriteEffect.fx:22-33
// multiplies the full float4 and samples perspective-interpolated UV. Arithmetic below.
#include "common/PixelTestGame.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#ifdef CNA_GAP07_SHADER_FIXTURES
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#endif
#ifdef CNA_GAP07_COMPILED_FIXTURES
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#endif
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
namespace
{
constexpr int kSize = 64;
using Pixels = std::array<Color, kSize * kSize>;
const std::array<Color, 4> kStrip{Color::Red, Color::Green, Color::Blue, Color::Yellow};

#ifdef CNA_GAP07_SHADER_FIXTURES
// CNAEXT shader contract: consume the renderer's complete transformed position.
// Legacy float2/float3 shaders retain their own explicitly authored W=1 behavior.
const char* kVertex = R"GLSL(#version 450
layout(location=0) in vec4 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=0) out vec2 fragUV;
layout(location=1) out vec4 fragColor;
layout(set=1,binding=0) uniform PC { vec4 vp; mat4 matrix; vec4 tint; vec4 slot0; } pc;
void main() {
    vec2 xy=position.xy/pc.vp.xy*2.0-position.ww;
    gl_Position=vec4(xy.x,-xy.y,position.z,position.w);
    fragUV=uv; fragColor=color;
})GLSL";
const char* kFragment = R"GLSL(#version 450
layout(location=0) in vec2 fragUV;
layout(location=1) in vec4 fragColor;
layout(location=0) out vec4 outputColor;
layout(set=2,binding=0) uniform sampler2D tex;
void main() { outputColor=texture(tex,fragUV)*fragColor; }
)GLSL";
#endif

class HomogeneousSpriteGame final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::string route_;
    std::unique_ptr<Effect> effect_;
    bool done_ = false;
    int result_ = 0;
    int checks_ = 0;

    void Check(const Pixels& pixels, int x, int y, Color expected,
               const char* name, SpriteSortMode mode)
    {
        const Color got = pixels[static_cast<std::size_t>(y * kSize + x)];
        const bool pass = got.getPackedValueProperty() == expected.getPackedValueProperty();
        ++checks_;
        if (!pass) result_ = 1;
        std::printf("[%s] %s/%s/%s (%d,%d): got=%08x expected=%08x\n",
                    pass ? "PASS" : "FAIL", route_.c_str(),
                    mode == SpriteSortMode::Immediate ? "Immediate" : "Deferred", name,
                    x, y, static_cast<unsigned>(got.getPackedValueProperty()),
                    static_cast<unsigned>(expected.getPackedValueProperty()));
    }

    void DrawSprite(SpriteBatch& batch, Texture2D& texture, SpriteSortMode mode,
                    const Matrix& matrix, float z, const Rectangle& rect,
                    bool depth = false, SpriteEffects flip = SpriteEffects::None)
    {
        Matrix beginMatrix = matrix;
#ifdef CNA_GAP07_COMPILED_FIXTURES
        if (route_ == "compiled-vertex")
        {
            // The shader owns T, independently of Begin's matrix. Applying Begin's matrix to
            // its raw input would move the entire quad offscreen and fail the control pixels.
            const Matrix projection = Matrix::CreateOrthographicOffCenter(
                0.0f, 64.0f, 64.0f, 0.0f, 0.0f, -1.0f);
            effect_->getParametersProperty()["Transform"]->SetValue(matrix * projection);
            beginMatrix = Matrix::CreateTranslation(256.0f, 256.0f, 0.0f);
        }
#endif
        batch.Begin(mode, &BlendState::Opaque, &SamplerState::PointClamp,
                    depth ? &DepthStencilState::Default : &DepthStencilState::None,
                    &RasterizerState::CullNone, effect_.get(), beginMatrix);
        batch.Draw(texture, rect, Rectangle(0, 0, texture.getWidthProperty(),
                                          texture.getHeightProperty()), Color::White,
                   0.0f, Vector2::Zero, flip, z);
        batch.End();
    }

    void SetupEffect()
    {
        auto& device = getGraphicsDeviceProperty();
        if (route_ == "stock") return;
#ifdef CNA_GAP07_SHADER_FIXTURES
        if (route_ == "shader")
        {
            auto shader = std::make_unique<ShaderEffect>(device, kVertex, kFragment);
            if (!shader->IsEffectValid()) throw std::runtime_error(shader->GetCompileErrorEXT());
            effect_ = std::move(shader);
            return;
        }
#endif
#ifdef CNA_GAP07_COMPILED_FIXTURES
        if (route_ == "compiled-pixel" || route_ == "compiled-vertex")
        {
            // These builders emit documented D3D9 tokens, independently of CNA's renderer.
            auto bytes = route_ == "compiled-pixel"
                ? CNA::TestSupport::BuildSyntheticPixelOnlySamplingEffect({})
                : CNA::TestSupport::BuildSyntheticSamplingEffect({});
            effect_ = std::make_unique<Effect>(device, bytes);
            effect_->getParametersProperty()["Tint"]->SetValue(Vector4::One);
            return;
        }
#endif
        throw std::runtime_error("unavailable homogeneous SpriteBatch route: " + route_);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        SetupEffect();
        auto& device = getGraphicsDeviceProperty();
        Texture2D white(device, 1, 1), blue(device, 1, 1);
        const Color whiteTexel = Color::White, blueTexel = Color::Blue;
        white.SetData(&whiteTexel, 1);
        blue.SetData(&blueTexel, 1);
        Texture2D horizontal(device, 4, 1), vertical(device, 1, 4);
        horizontal.SetData(kStrip.data(), 4);
        vertical.SetData(kStrip.data(), 4);
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
        SpriteBatch batch(device);
        const Matrix identity = Matrix::getIdentityProperty();
        for (auto mode : {SpriteSortMode::Deferred, SpriteSortMode::Immediate})
        {
            const auto render = [&](Texture2D& texture, const Matrix& matrix, float z = .25f,
                                    Rectangle rect = Rectangle(0, 0, 64, 64), bool depth = false,
                                    float clearDepth = 1.0f,
                                    SpriteEffects flip = SpriteEffects::None)
            {
                device.SetRenderTarget(&target);
                device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                             Color::Black, clearDepth, 0);
                DrawSprite(batch, texture, mode, matrix, z, rect, depth, flip);
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                Pixels pixels{};
                target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
                return pixels;
            };

            // Exact identity/2D controls use a literal full image, including alpha. Scale and
            // translation place a 32x32 strip rectangle at (8,16); each texel spans 8 columns.
            const auto identityPixels = render(horizontal, identity);
            Matrix affine = Matrix::CreateScale(.5f, .5f, 1.0f);
            affine.M41 = 8.0f; affine.M42 = 16.0f;
            const auto affinePixels = render(horizontal, affine);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                {
                    // Print only mismatches for full-image assertions.
                    const Color expectedIdentity = kStrip[static_cast<std::size_t>(x / 16)];
                    const Color expectedAffine = x >= 8 && x < 40 && y >= 16 && y < 48
                        ? kStrip[static_cast<std::size_t>((x - 8) / 8)] : Color::Black;
                    if (identityPixels[y * 64 + x] != expectedIdentity)
                        Check(identityPixels, x, y, expectedIdentity, "IdentityByteImage", mode);
                    if (affinePixels[y * 64 + x] != expectedAffine)
                        Check(affinePixels, x, y, expectedAffine, "AffineByteImage", mode);
                }
            checks_ += 2;
            std::printf("[INFO] %s full RGBA identity/affine images checked\n", route_.c_str());

            Matrix matrix = identity;
            matrix.M31 = 32.0f; // layer .5 adds 16 to X.
            auto p = render(white, matrix, .5f, Rectangle(0, 0, 16, 16));
            Check(p, 4, 8, Color::Black, "DepthToXOldLocation", mode);
            Check(p, 20, 8, Color::White, "DepthToXNewLocation", mode);
            matrix = identity; matrix.M32 = 32.0f;
            p = render(white, matrix, .5f, Rectangle(0, 0, 16, 16));
            Check(p, 8, 4, Color::Black, "DepthToYOldLocation", mode);
            Check(p, 8, 20, Color::White, "DepthToYNewLocation", mode);

            matrix = identity; matrix.M44 = 2.0f;
            p = render(white, matrix);
            Check(p, 8, 8, Color::White, "ConstantWInside", mode);
            Check(p, 40, 8, Color::Black, "ConstantWOutside", mode);
            matrix = identity; matrix.M34 = 2.0f;
            p = render(white, matrix, .5f); // W=1+.5*2=2, same footprint.
            Check(p, 8, 8, Color::White, "DepthToWInside", mode);
            Check(p, 40, 8, Color::Black, "DepthToWOutside", mode);

            matrix = identity; matrix.M14 = 1.0f / 64.0f;
            p = render(horizontal, matrix);
            // s=x+.5; inverse u=s/(64-s); at x=24 -> 49/79 -> blue.
            Check(p, 24, 8, Color::Blue, "PerspectiveU", mode);
            Check(p, 40, 8, Color::Black, "PerspectiveRightClip", mode);
            Check(p, 8, 40, Color::Red, "PerspectiveOtherTriangle", mode);
            p = render(horizontal, matrix, .25f, Rectangle(0, 0, 64, 64), false, 1.0f,
                       SpriteEffects::FlipHorizontally);
            Check(p, 24, 8, Color::Green, "PerspectiveFlippedU", mode); // 1-49/79=30/79.
            matrix = identity; matrix.M24 = 1.0f / 64.0f;
            p = render(vertical, matrix);
            Check(p, 8, 24, Color::Blue, "PerspectiveV", mode);
            Check(p, 8, 40, Color::Black, "PerspectiveBottomClip", mode);
            Check(p, 40, 8, Color::Red, "PerspectiveVSecondTriangle", mode);

            matrix = identity; matrix.M44 = 0.0f;
            p = render(white, matrix, .25f, Rectangle(8, 8, 16, 16));
            Check(p, 16, 16, Color::Black, "AllZeroW", mode); // 0<=Z<=0 fails.
            matrix.M11 = -1.0f; matrix.M22 = -1.0f; matrix.M44 = -1.0f;
            // Negative Z as well as W makes an erroneous full predivide look visible.
            p = render(white, matrix, -.25f);
            Check(p, 8, 8, Color::Black, "AllNegativeW", mode);

            // X=8, Y=y, Z=.25, W=x/32-1. Hardware clips at W>=.25; screen
            // x=8/W spans [8,32]. CPU endpoint divide produces [-8,8], a different polygon.
            matrix = identity; matrix.M11 = 0; matrix.M41 = 8;
            matrix.M14 = 1.0f / 32.0f; matrix.M44 = -1;
            p = render(white, matrix);
            Check(p, 4, 16, Color::Black, "CrossingWEyeClipped", mode);
            Check(p, 16, 16, Color::White, "CrossingWSurvives", mode);
            p = render(horizontal, matrix);
            // u=.5+4/s: at s=12.5 -> .82 yellow; s=20.5 -> .69512 blue.
            Check(p, 12, 8, Color::Yellow, "CrossingWClippedUVLeft", mode);
            Check(p, 20, 8, Color::Blue, "CrossingWClippedUVRight", mode);
            matrix.M14 = 1.0f / 64.0f; matrix.M44 = 0;
            p = render(white, matrix);
            Check(p, 4, 16, Color::Black, "ZeroWEdgeClipped", mode);
            Check(p, 16, 16, Color::White, "ZeroWEdgeSurvives", mode);
            p = render(horizontal, matrix);
            // u=8/s: at s=12.5 -> .64 blue; s=20.5 -> .39024 green.
            Check(p, 12, 8, Color::Blue, "ZeroWEdgeClippedUVLeft", mode);
            Check(p, 20, 8, Color::Green, "ZeroWEdgeClippedUVRight", mode);

            matrix = identity; matrix.M13 = 1.0f / 64.0f; matrix.M43 = -.25f;
            p = render(white, matrix, 0);
            Check(p, 8, 8, Color::Black, "NearPlaneRejects", mode);
            Check(p, 32, 8, Color::White, "NearPlaneRetains", mode);
            matrix.M13 = 1.0f / 32.0f; matrix.M43 = .25f;
            p = render(white, matrix, 0);
            Check(p, 8, 8, Color::White, "FarPlaneRetains", mode);
            Check(p, 40, 8, Color::Black, "FarPlaneRejects", mode);

            matrix = identity; matrix.M14 = 1.0f / 64.0f;
            p = render(blue, matrix, .75f, Rectangle(0, 0, 64, 64), true, .5f);
            // z_ndc=.75*(1-s/64): .556640625 at x=16, .462890625 at x=24.
            // Dividing Z twice changes the first to .41316223, incorrectly passing .5.
            Check(p, 16, 8, Color::Black, "ProjectiveDepthFails", mode);
            Check(p, 24, 8, Color::Blue, "ProjectiveDepthPasses", mode);
        }
        std::printf("[INFO] %d assertions/image checks, route=%s result=%d\n",
                    checks_, route_.c_str(), result_);
        Exit();
    }

public:
    explicit HomogeneousSpriteGame(std::string route) : route_(std::move(route))
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }
    int Result() const { return result_; }
};
}

int main(int argc, char** argv)
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable()) return CNA::Examples::kSkipExitCode;
    HomogeneousSpriteGame game(argc > 1 ? argv[1] : "stock");
    game.Run();
    return game.Result();
}
