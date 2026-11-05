// SPDX-License-Identifier: MS-PL
// GAP-07b: source effects receive homogeneous positions. Exact interior probes use
// the shared GAP-07a arithmetic, not CNA-generated images.
// Oracle: Refresh SpriteBatch.SetRenderState IL_0104-0188 retains full T*P;
// FNA SpriteEffect.fx:22-33 forwards the full position and smooth UV. WGSL vec4
// input is this renderer's source contract; legacy narrow inputs retain authored W=1.
#include "common/PixelTestGame.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
namespace {
constexpr int kSize = 64;
struct Probe { int x, y; Color expected; };

std::string VertexSource(bool descriptor, bool homogeneous) {
    const std::string type = homogeneous ? "vec4f" : descriptor ? "vec2f" : "vec3f";
    const std::string globals = descriptor
        ? "struct Scalars { viewport: vec2f, padding: vec2f, };\n"
          "@group(3) @binding(0) var<uniform> pc: Scalars;\n"
        : "struct Dummy { unused: vec4f, };\n"
          "@group(0) @binding(0) var<uniform> dummy: Dummy;\n";
    const std::string position = descriptor
        ? (homogeneous
            ? "vec4f(2.0*p.x/pc.viewport.x-p.w, p.w-2.0*p.y/pc.viewport.y, p.z, p.w)"
            : "vec4f(2.0*p.x/pc.viewport.x-1.0, 1.0-2.0*p.y/pc.viewport.y, 0.0, 1.0)")
        : homogeneous ? "p" : "vec4f(p, 1.0)";
    return globals + "struct Out { @builtin(position) position: vec4f, @location(0) uv: vec2f, };\n"
        "@vertex fn vs_main(@location(0) p: " + type + ", @location(1) uv: vec2f,\n"
        " @location(2) color: vec4f) -> Out { var o: Out; o.position = " + position +
        "; o.uv = uv; return o; }\n";
}
std::string FragmentSource(bool vertical) {
    return std::string("@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {\n let t = uv.") +
        (vertical ? "y" : "x") + ";\n"
        " if t < 0.25 { return vec4f(1.0,0.0,0.0,1.0); }\n"
        " if t < 0.5 { return vec4f(0.0,128.0/255.0,0.0,1.0); }\n"
        " if t < 0.75 { return vec4f(0.0,0.0,1.0,1.0); }\n"
        " return vec4f(1.0,1.0,0.0,1.0); }\n";
}
class HomogeneousEffectTest final : public CNA::Examples::PixelTestGame {
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::string route_;
    void Case(const char* name, ShaderEffect& effect, SpriteSortMode mode,
              const Matrix& matrix, const Rectangle& destination, float depth,
              std::initializer_list<Probe> probes, bool depthCompetition = false,
              bool insetViewport = false) {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24);
        Texture2D white(device, 1, 1);
        const Color texel = Color::White;
        white.SetData(&texel, 1);
        SpriteBatch batch(device);
        SamplerState sampler = SamplerState::PointClamp;
        RasterizerState raster = RasterizerState::CullNone;
        const auto& state = depthCompetition ? DepthStencilState::Default : DepthStencilState::None;
        device.SetRenderTarget(&target);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        if (depthCompetition) {
            // .375 competes with .5/(1+x/64): x=10 loses, x=24 wins.
            // Dividing depth twice incorrectly lets the x=10 fragment through.
            batch.Begin(mode, BlendState::Opaque, &sampler, &state, &raster);
            batch.Draw(white, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 1, 1),
                       Color::Magenta, 0.0f, Vector2::Zero, SpriteEffects::None, 0.375f);
            batch.End();
        }
        if (insetViewport) device.setViewportProperty(Viewport(16, 0, 48, kSize));
        batch.Begin(mode, BlendState::Opaque, &sampler, &state, &raster, &effect, matrix);
        batch.Draw(white, destination, Rectangle(0, 0, 1, 1), Color::White, 0.0f,
                   Vector2::Zero, SpriteEffects::None, depth);
        batch.End();
        device.SetRenderTarget(nullptr);
        for (const auto& probe : probes) {
            Color pixel(17, 19, 23, 29);
            const Rectangle region(probe.x, probe.y, 1, 1);
            target.GetData(0, &region, &pixel, 0, 1);
            const std::string label = route_ + "/" + name + " mode=" + std::to_string(static_cast<int>(mode)) +
                " at=" + std::to_string(probe.x) + "," + std::to_string(probe.y) +
                " got=" + std::to_string(pixel.getPackedValueProperty()) +
                " expected=" + std::to_string(probe.expected.getPackedValueProperty());
            ExpectTrue(label.c_str(), pixel == probe.expected);
        }
    }
    void AffineImage(ShaderEffect& effect, SpriteSortMode mode) {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kSize, kSize);
        Texture2D white(device, 1, 1);
        const Color texel = Color::White; white.SetData(&texel, 1);
        SpriteBatch batch(device);
        SamplerState sampler = SamplerState::PointClamp;
        RasterizerState raster = RasterizerState::CullNone;
        // Hand image: input [2,10)x[2,10), scale 2 then translate (4,4)
        // gives [8,24)x[8,24). Four equal-width UV bands, black everywhere else.
        const Matrix matrix = Matrix::CreateScale(2.0f, 2.0f, 1.0f) * Matrix::CreateTranslation(4, 4, 0);
        device.SetRenderTarget(&target); device.Clear(Color::Black);
        batch.Begin(mode, BlendState::Opaque, &sampler, &DepthStencilState::None, &raster, &effect, matrix);
        batch.Draw(white, Rectangle(2, 2, 8, 8), Color::White); batch.End();
        device.SetRenderTarget(nullptr);
        std::vector<Color> pixels(kSize*kSize);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        const Color bands[] = {Color::Red, Color(0,128,0,255), Color::Blue, Color::Yellow};
        int bad = 0;
        for (int y=0;y<kSize;++y) for(int x=0;x<kSize;++x) {
            const Color expected = x>=8 && x<24 && y>=8 && y<24 ? bands[(x-8)/4] : Color::Black;
            if (pixels[static_cast<std::size_t>(y*kSize+x)] != expected) ++bad;
        }
        const std::string label = route_ + "/AffineByteImage mode=" +
            std::to_string(static_cast<int>(mode)) + " mismatches=" + std::to_string(bad);
        ExpectTrue(label.c_str(), bad == 0);
    }
public:
    HomogeneousEffectTest() {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }
    void RunTest() override {
        auto& device = getGraphicsDeviceProperty();
        for (const bool descriptor : {false, true}) {
            for (const bool homogeneous : {false, true}) {
                route_ = std::string(descriptor ? "descriptor" : "legacy") + (homogeneous ? "4" : "narrow");
                ShaderEffect effect(device, VertexSource(descriptor, homogeneous), FragmentSource(false));
                if (!ExpectTrue("source effect compiles", effect.IsEffectValid())) return;
                if (!descriptor) {
                    const char* names[] = {"unused"}; const int offsets[] = {0};
                    effect.DeclareUniformBlockEXT(16, names, offsets, 1);
                }
                for (const auto mode : {SpriteSortMode::Deferred, SpriteSortMode::Immediate}) {
                    AffineImage(effect, mode);
                    if (!homogeneous) continue;
                    // .5*16 shifts X/Y by 8, independently of source UV bands.
                    Matrix m = Matrix::getIdentityProperty(); m.M31 = 16;
                    Case("LayerDepthM31", effect, mode, m, Rectangle(8,8,16,16), .5f,
                         {{12,12,Color::Black},{24,12,Color::Blue}});
                    m = Matrix::getIdentityProperty(); m.M32 = 16;
                    Case("LayerDepthM32", effect, mode, m, Rectangle(8,8,16,16), .5f,
                         {{10,12,Color::Black},{10,24,Color::Red}});
                    m = Matrix::getIdentityProperty(); m.M14 = 1.0f/64;
                    // Shared GAP-07a: u(24.5)=49/79 -> blue. CPU-divided W=1 gives
                    // u=49/64 -> yellow; HEAD's undivided XY gives u=49/128 -> green.
                    Case("PerspectiveU", effect, mode, m, Rectangle(0,0,64,64), .5f,
                         {{24,8,Color::Blue},{40,8,Color::Black},{8,40,Color::Red}});
                    Case("PerspectiveDepth", effect, mode, m, Rectangle(0,0,64,64), .5f,
                         {{10,8,Color::Magenta},{24,8,Color::Blue},{40,8,Color::Magenta}}, true);
                    // Valid inset viewport: target x=40 samples local x=24; x=56 is outside.
                    Case("InsetViewport", effect, mode, m, Rectangle(0,0,64,64), .5f,
                         {{24,8,Color::Red},{40,8,Color::Blue},{56,8,Color::Black}}, false, true);
                    m = Matrix::getIdentityProperty(); m.M34 = 2;
                    Case("LayerDepthM34", effect, mode, m, Rectangle(0,0,64,64), .5f,
                         {{24,8,Color::Yellow},{40,8,Color::Black}});
                    m = Matrix::getIdentityProperty(); m.M44 = 2;
                    Case("ConstantW", effect, mode, m, Rectangle(0,0,64,64), .5f,
                         {{24,8,Color::Yellow},{40,8,Color::Black}});
                    m.M44 = 0;
                    Case("ZeroW", effect, mode, m, Rectangle(8,8,16,16), 0,
                         {{12,12,Color::Black},{20,20,Color::Black}});
                    m.M44 = -1;
                    Case("NegativeW", effect, mode, m, Rectangle(8,8,16,16), 0,
                         {{12,12,Color::Black},{20,20,Color::Black}});
                    m = Matrix::getIdentityProperty(); m.M11 = 0; m.M41 = 8;
                    m.M14 = 1.0f/64; m.M44 = 0;
                    // W=0 at the left edge. u=8/s: .64 at 12.5, .39024 at 20.5.
                    Case("ZeroWEdge", effect, mode, m, Rectangle(0,0,64,64), .25f,
                         {{4,16,Color::Black},{12,8,Color::Blue},{20,8,Color(0,128,0,255)}});
                    m = Matrix::getIdentityProperty(); m.M13 = 1.0f/64; m.M43 = -.25f;
                    Case("NearPlane", effect, mode, m, Rectangle(0,0,64,64), 0,
                         {{8,8,Color::Black},{32,8,Color::Blue}});
                    m.M13 = 1.0f/32; m.M43 = .25f;
                    Case("FarPlane", effect, mode, m, Rectangle(0,0,64,64), 0,
                         {{8,8,Color::Red},{40,8,Color::Black}});
                    // Inverse x=16*s/(s-16). At s=26.5, u=53/84 -> blue;
                    // at s=56.5, u=113/324 -> green. W<=0 cannot rasterize.
                    m = Matrix::getIdentityProperty(); m.M14 = 1.0f/16; m.M44 = -1;
                    Case("MixedSignW", effect, mode, m, Rectangle(0,0,64,64), 0,
                         {{8,8,Color::Black},{26,8,Color::Blue},{56,48,Color(0,128,0,255)}});
                }
            }
            route_ = descriptor ? "descriptor4/vertical" : "legacy4/vertical";
            ShaderEffect vertical(device, VertexSource(descriptor, true), FragmentSource(true));
            if (!ExpectTrue("vertical source effect compiles", vertical.IsEffectValid())) return;
            if (!descriptor) {
                const char* names[] = {"unused"}; const int offsets[] = {0};
                vertical.DeclareUniformBlockEXT(16, names, offsets, 1);
            }
            Matrix m = Matrix::getIdentityProperty(); m.M24 = 1.0f/64;
            for (const auto mode : {SpriteSortMode::Deferred, SpriteSortMode::Immediate})
                Case("PerspectiveV", vertical, mode, m, Rectangle(0,0,64,64), .5f,
                     {{8,24,Color::Blue},{8,40,Color::Black},{40,8,Color::Red}});
        }
    }
};
}
int main() {
    CNA::GraphicsRendererSelection::SetPreferred("WEBGPU");
    return CNA::Examples::RunPixelTest<HomogeneousEffectTest>();
}
