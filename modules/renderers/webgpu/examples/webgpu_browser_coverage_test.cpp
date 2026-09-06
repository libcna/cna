// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-196: the five behaviours whose BROWSER answer is not implied by their
// native one, driven as one page through scripts/run-webgpu-browser-test.sh.
//
// Why these five and not the whole suite. The Emscripten path shares `WebGPURenderer.cpp` and every
// WGSL shader with the native one, so most behaviour is settled by the native CTests. What is NOT
// settled is anything that touches one of the `#if defined(__EMSCRIPTEN__)` seams (surface source,
// callback mode and the Asyncify pump, no explicit present, depth/MSAA resync to the canvas backing
// store) or anything that depends on WHICH shader compiler runs: native goes through wgpu-native's
// Naga, the browser through emdawnwebgpu's Tint, and a WGSL module accepted by one is not thereby
// accepted by the other.
//
//   A -- FillMode::WireFrame (WEBGPU-153/154). The renderer produces a wireframe by rewriting the
//        INDEX stream into a line list at queue time rather than by asking for a polygon mode --
//        which is exactly why it can work here at all, since browser WebGPU has no polygon mode.
//        Checked on a 3D quad and, since WEBGPU-154, on a SpriteBatch quad too.
//   B -- Semantic vertex layouts (WEBGPU-155). Two declarations that differ in element ORDER and
//        OFFSET must produce the same picture; the layout is built from the declaration's semantics
//        rather than from its stride.
//   C -- Multi-stream input (WEBGPU-172). Position from stream 0, colour from stream 1, in one draw.
//   D -- Custom-WGSL ShaderEffect. What differs between the targets is whether a hand-written WGSL
//        pair that Naga accepts is also accepted by Tint, and whether the draw lands. This is the
//        CNAEXT `ShaderEffect` source API, NOT a compiled XNA Effect -- the two are independent.
//        Compiled (bytecode) Effects were out of scope for this renderer when this page was
//        written; WEBGPU-203 gave them a browser route (SPIR-V translated to WGSL) and they have
//        their own page, `webgpu_browser_compiled_effect_test.cpp`.
//   E -- Device loss (WEBGPU-182). DebugSimulateContextLoss/DebugRestoreContext carry no Emscripten
//        seam of their own, but the device REQUEST underneath them does, so a destroy-and-recreate
//        cycle is a genuinely open browser question rather than an implied answer.
//
// Every check reads pixels back rather than trusting an absence of errors, and each one runs in its
// OWN frame: WEBGPU-133 measured that a readback yields to the browser, which presents and
// invalidates the canvas surface texture during the yield. One draw and one read per frame is the
// pattern that fix was built for.
//
// Exit code is not the signal in a browser (emscripten does not call Module.onExit on a normal
// main() return without EXIT_RUNTIME); the harness greps for the printed summary and a clean
// teardown, so this prints "=== N/M PASS ===" exactly as the native examples do.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    const Color kClear(9, 13, 17, 255);
    const Color kInk(230, 70, 40, 255);

    bool IsClear(const Color& c)
    {
        return c.getRProperty() == kClear.getRProperty() &&
               c.getGProperty() == kClear.getGProperty() &&
               c.getBProperty() == kClear.getBProperty();
    }

    /// The custom WGSL pair for check D. Deliberately the SAME shape the native ShaderEffect tests
    /// use, so a difference here is Tint-versus-Naga and not a different shader.
    const char* const kVertWgsl = R"WGSL(
struct VOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};
@vertex fn vs_main(@location(0) position: vec3f, @location(1) color: vec4f) -> VOut {
    var o: VOut;
    o.position = vec4f(position, 1.0);
    o.color = color;
    return o;
}
)WGSL";

    const char* const kFragWgsl = R"WGSL(
struct U { uTint: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(@location(0) color: vec4f) -> @location(0) vec4f {
    return color * u.uTint;
}
)WGSL";

    const char* const kUniformNames[] = {"uTint"};
    const int kUniformOffsets[] = {0};

    struct PosColor { float x, y, z; float r, g, b, a; };
}

class WebGpuBrowserCoverageTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int step_ = 0;
    int pass_ = 0;
    int total_ = 0;
    int width_ = 0;
    int height_ = 0;

    void check(bool ok, const std::string& label)
    {
        ++total_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++pass_;
    }

    /// Reads the centre quarter of the backbuffer and counts pixels that are not the clear colour.
    [[nodiscard]] int LitInCentre(GraphicsDevice& device) const
    {
        const int x = width_ / 4;
        const int y = height_ / 4;
        const int w = width_ / 2;
        const int h = height_ / 2;
        if (w <= 0 || h <= 0) return -1;
        std::vector<Color> pixels(static_cast<std::size_t>(w) * h, Color(0, 0, 0, 0));
        const Rectangle region(x, y, w, h);
        device.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
        int lit = 0;
        for (const Color& p : pixels)
            if (!IsClear(p)) ++lit;
        return lit;
    }

    [[nodiscard]] Color Centre(GraphicsDevice& device) const
    {
        const Rectangle region(width_ / 2, height_ / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void ResetState(GraphicsDevice& device, FillMode fill) const
    {
        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        rs.setFillModeProperty(fill);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
    }

    /// A centred quad as a triangle strip, in NDC, through the given declaration/stride.
    static std::array<PosColor, 4> CentredQuad(const Color& c)
    {
        const float r = static_cast<float>(c.getRProperty()) / 255.0f;
        const float g = static_cast<float>(c.getGProperty()) / 255.0f;
        const float b = static_cast<float>(c.getBProperty()) / 255.0f;
        return {PosColor{-0.5f,  0.5f, 0.0f, r, g, b, 1.0f},
                PosColor{-0.5f, -0.5f, 0.0f, r, g, b, 1.0f},
                PosColor{ 0.5f,  0.5f, 0.0f, r, g, b, 1.0f},
                PosColor{ 0.5f, -0.5f, 0.0f, r, g, b, 1.0f}};
    }

    static VertexDeclaration PositionThenColor()
    {
        return VertexDeclaration(
            28, {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                 VertexElement(12, VertexElementFormat::Vector4, VertexElementUsage::Color, 0)});
    }

    /// The same 28-byte record described with its elements listed in the OTHER order. WEBGPU-155
    /// made the layout follow the declared semantics and offsets rather than the stride, so this
    /// must produce the same picture as the one above.
    static VertexDeclaration ColorThenPositionDeclaredOutOfOrder()
    {
        return VertexDeclaration(
            28, {VertexElement(12, VertexElementFormat::Vector4, VertexElementUsage::Color, 0),
                 VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
    }

    void DrawQuad(GraphicsDevice& device, BasicEffect& effect, const VertexDeclaration& decl,
                  const std::array<PosColor, 4>& verts)
    {
        VertexBuffer vb(device, decl, 4, BufferUsage::None);
        vb.SetDataRaw(verts.data(), 4, 28);
        device.SetVertexBuffer(&vb);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
        device.SetVertexBuffer(nullptr);
    }

    void ConfigureUnlit(BasicEffect& effect)
    {
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(true);
    }

    // --- the five checks, one frame each ---------------------------------------------------

    int solidLit_ = 0;
    int semanticsOrderedLit_ = 0;
    Color semanticsOrderedCentre_{0, 0, 0, 0};

    void StepWireframe3D(GraphicsDevice& device)
    {
        ResetState(device, FillMode::Solid);
        device.Clear(kClear);
        BasicEffect effect(device);
        ConfigureUnlit(effect);
        DrawQuad(device, effect, PositionThenColor(), CentredQuad(kInk));
        solidLit_ = LitInCentre(device);
        check(solidLit_ > 0, "A1: a solid 3D quad renders in the browser (" +
                                 std::to_string(solidLit_) + " lit)");
    }

    void StepWireframe3DLines(GraphicsDevice& device)
    {
        ResetState(device, FillMode::WireFrame);
        device.Clear(kClear);
        BasicEffect effect(device);
        ConfigureUnlit(effect);
        DrawQuad(device, effect, PositionThenColor(), CentredQuad(kInk));
        const int wireLit = LitInCentre(device);
        check(wireLit > 0 && wireLit * 4 < solidLit_,
              "A2: FillMode::WireFrame draws EDGES in the browser -- " + std::to_string(wireLit) +
                  " lit against the solid quad's " + std::to_string(solidLit_) +
                  " (browser WebGPU has no polygon mode; this is the index-expansion route)");
    }

    void StepWireframeSprite(GraphicsDevice& device)
    {
        ResetState(device, FillMode::WireFrame);
        device.Clear(kClear);
        Texture2D texture(device, 1, 1);
        const Color white(255, 255, 255, 255);
        texture.SetData(&white, 1);
        const SamplerState sampler = SamplerState::PointClamp;
        const RasterizerState rasterizer = device.getRasterizerStateProperty();
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler,
                    &DepthStencilState::None, &rasterizer);
        batch.Draw(texture, Rectangle(width_ / 4, height_ / 4, width_ / 2, height_ / 2),
                   Color::White);
        batch.End();
        const int wireLit = LitInCentre(device);
        const int area = (width_ / 2) * (height_ / 2);
        check(wireLit > 0 && wireLit * 4 < area,
              "A3: WEBGPU-154's SpriteBatch wireframe is an OUTLINE in the browser too -- " +
                  std::to_string(wireLit) + " lit against a filled quad's " + std::to_string(area));
    }

    void StepSemanticsOrdered(GraphicsDevice& device)
    {
        ResetState(device, FillMode::Solid);
        device.Clear(kClear);
        BasicEffect effect(device);
        ConfigureUnlit(effect);
        DrawQuad(device, effect, PositionThenColor(), CentredQuad(kInk));
        semanticsOrderedLit_ = LitInCentre(device);
        semanticsOrderedCentre_ = Centre(device);
        check(semanticsOrderedLit_ > 0,
              "B1: the reference declaration (Position then Color) renders");
    }

    void StepSemanticsReordered(GraphicsDevice& device)
    {
        ResetState(device, FillMode::Solid);
        device.Clear(kClear);
        BasicEffect effect(device);
        ConfigureUnlit(effect);
        DrawQuad(device, effect, ColorThenPositionDeclaredOutOfOrder(), CentredQuad(kInk));
        const int lit = LitInCentre(device);
        const Color centre = Centre(device);
        check(lit == semanticsOrderedLit_ &&
                  centre.getRProperty() == semanticsOrderedCentre_.getRProperty() &&
                  centre.getGProperty() == semanticsOrderedCentre_.getGProperty() &&
                  centre.getBProperty() == semanticsOrderedCentre_.getBProperty(),
              "B2: WEBGPU-155 -- the SAME record declared with its elements in the other order "
              "gives the same picture in the browser (" + std::to_string(lit) + " vs " +
                  std::to_string(semanticsOrderedLit_) + " lit)");
    }

    void StepMultiStream(GraphicsDevice& device)
    {
        ResetState(device, FillMode::Solid);
        device.Clear(kClear);
        struct Pos { float x, y, z; };
        struct Col { float r, g, b, a; };
        const std::array<Pos, 4> positions{Pos{-0.5f, 0.5f, 0.0f}, Pos{-0.5f, -0.5f, 0.0f},
                                           Pos{0.5f, 0.5f, 0.0f}, Pos{0.5f, -0.5f, 0.0f}};
        const float r = static_cast<float>(kInk.getRProperty()) / 255.0f;
        const float g = static_cast<float>(kInk.getGProperty()) / 255.0f;
        const float b = static_cast<float>(kInk.getBProperty()) / 255.0f;
        const std::array<Col, 4> colors{Col{r, g, b, 1.0f}, Col{r, g, b, 1.0f},
                                        Col{r, g, b, 1.0f}, Col{r, g, b, 1.0f}};
        VertexBuffer positionStream(
            device,
            VertexDeclaration(12, {VertexElement(0, VertexElementFormat::Vector3,
                                                 VertexElementUsage::Position, 0)}),
            4, BufferUsage::None);
        positionStream.SetDataRaw(positions.data(), 4, 12);
        VertexBuffer colorStream(
            device,
            VertexDeclaration(16, {VertexElement(0, VertexElementFormat::Vector4,
                                                 VertexElementUsage::Color, 0)}),
            4, BufferUsage::None);
        colorStream.SetDataRaw(colors.data(), 4, 16);

        BasicEffect effect(device);
        ConfigureUnlit(effect);
        device.SetVertexBuffers({VertexBufferBinding(&positionStream, 0, 0),
                                 VertexBufferBinding(&colorStream, 0, 0)});
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
        device.SetVertexBuffers({});

        const Color centre = Centre(device);
        check(std::abs(centre.getRProperty() - kInk.getRProperty()) <= 12 &&
                  std::abs(centre.getGProperty() - kInk.getGProperty()) <= 12,
              "C: WEBGPU-172 -- position from stream 0 and colour from stream 1 both reach the "
              "browser draw (read " + std::to_string(centre.getRProperty()) + "," +
                  std::to_string(centre.getGProperty()) + "," +
                  std::to_string(centre.getBProperty()) + ")");
    }

    void StepShaderEffect(GraphicsDevice& device)
    {
        ResetState(device, FillMode::Solid);
        device.Clear(kClear);
        ShaderEffect fx(device, kVertWgsl, kFragWgsl);
        if (!fx.IsEffectValid())
        {
            check(false, std::string("D: a custom WGSL pair Naga accepts was REJECTED by the "
                                     "browser's Tint: ") + fx.GetCompileErrorEXT());
            return;
        }
        fx.DeclareUniformBlockEXT(16, kUniformNames, kUniformOffsets, 1);
        fx.SetUniformVec4("uTint", 1.0f, 1.0f, 1.0f, 1.0f);

        const std::array<PosColor, 4> verts = CentredQuad(kInk);
        VertexBuffer vb(device, PositionThenColor(), 4, BufferUsage::None);
        vb.SetDataRaw(verts.data(), 4, 28);
        device.SetVertexBuffer(&vb);
        fx.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
        device.SetVertexBuffer(nullptr);

        const Color centre = Centre(device);
        check(std::abs(centre.getRProperty() - kInk.getRProperty()) <= 12,
              "D: a custom-WGSL ShaderEffect compiles under the browser's Tint AND draws (read " +
                  std::to_string(centre.getRProperty()) + "," +
                  std::to_string(centre.getGProperty()) + "," +
                  std::to_string(centre.getBProperty()) + ")");
    }

    void StepDeviceLoss(GraphicsDevice& device)
    {
        auto& renderer = static_cast<WebGPURenderer&>(device.GetRenderer());
        const std::size_t errorsBefore = renderer.GetUncapturedErrorCountEXT();
        int lost = 0, reset = 0;
        device.DeviceLost += [&lost](System::Object*, const System::EventArgs&) { ++lost; };
        device.DeviceReset += [&reset](System::Object*, const System::EventArgs&) { ++reset; };

        renderer.DebugSimulateContextLoss();
        const bool gateClosed = !renderer.CanBeginDrawEXT();
        renderer.DebugRestoreContext();
        const bool gateOpen = renderer.CanBeginDrawEXT();
        check(gateClosed && gateOpen && lost == 1 && reset == 1,
              "E1: WEBGPU-182 -- a destroy-and-recreate cycle completes in the browser and raises "
              "DeviceLost/DeviceReset exactly once each");

        ResetState(device, FillMode::Solid);
        device.Clear(kClear);
        BasicEffect effect(device);
        ConfigureUnlit(effect);
        DrawQuad(device, effect, PositionThenColor(), CentredQuad(kInk));
        const Color centre = Centre(device);
        check(std::abs(centre.getRProperty() - kInk.getRProperty()) <= 12 &&
                  renderer.GetUncapturedErrorCountEXT() == errorsBefore,
              "E2: and the browser device still draws afterwards, with no uncaptured WebGPU error");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        width_ = device.getViewportProperty().getWidthProperty();
        height_ = device.getViewportProperty().getHeightProperty();
        if (width_ <= 8 || height_ <= 8) return;   // wait for the canvas to be sized

        switch (step_)
        {
        case 0: StepWireframe3D(device); break;
        case 1: StepWireframe3DLines(device); break;
        case 2: StepWireframeSprite(device); break;
        case 3: StepSemanticsOrdered(device); break;
        case 4: StepSemanticsReordered(device); break;
        case 5: StepMultiStream(device); break;
        case 6: StepShaderEffect(device); break;
        case 7: StepDeviceLoss(device); break;
        default:
            std::printf("[INFO] compiled (bytecode) Effects were out of scope when this page was "
                        "written; WEBGPU-203 translated their SPIR-V to WGSL and they now run in "
                        "the browser too. Their own page is "
                        "cna_webgpu_compiled_effect_page. This page keeps measuring the "
                        "custom-WGSL ShaderEffect route, check D, which is a different API.\n");
            std::printf("=== %d/%d PASS ===\n", pass_, total_);
            Exit();
            return;
        }
        ++step_;
    }

public:
    WebGpuBrowserCoverageTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    [[nodiscard]] int getResultProperty() const { return pass_ == total_ && total_ > 0 ? 0 : 1; }
};

int main()
{
    WebGpuBrowserCoverageTest game;
    game.Run();
    return game.getResultProperty();
}
