// SPDX-License-Identifier: MS-PL
// Task 877 (original scope) / Task 952 (current, open finding): verify RenderTargetCube honors
// its exact requested DepthFormat on Bgfx.
//
// METHODOLOGY, updated 2026-07-11 (Task 952 investigation): this test originally read the face
// back DIRECTLY while it was still bound, relying on GetBackBufferData reading "whatever
// framebuffer is currently active". Task 951 (2026-07-11) made GetBackBufferData always read the
// TRUE backbuffer regardless of what's bound, which is the CORRECT XNA-matching behavior but makes
// that old read-while-bound methodology fundamentally unreliable going forward. This test now
// unbinds first and samples the face back via EnvironmentMapEffect instead, matching every other
// passing RenderTargetCube test's own established pattern. Getting a correct sample also required
// fixing a second, separate methodology bug: with Identity View, EnvironmentMapEffect's
// EyePosition (= inverse(View).Translation) exactly coincides with the sampled vertex, making the
// reflection vector reflect(-E, N) degenerate/undefined -- fixed by placing the eye off-plane via
// Matrix::CreateLookAt(Vector3(0,0,5), Vector3::Zero, Vector3::Up), confirmed correct by
// temporarily filling the other 5 faces with a magenta sentinel (proved the un-fixed sampling
// direction was silently reading a neighboring face, not PositiveZ).
//
// CURRENT FINDING (Task 952, still open): with both methodology bugs fixed, DepthFormat::None
// correctly reads back the near (green) quad, proving the fixed methodology itself is sound. But
// DepthFormat::Depth24Stencil8 reads back nothing -- not the far (red) quad either, just the
// sampling loop's own background clear colour -- even with a SINGLE draw call and with
// DepthStencilState::None (depth testing fully disabled). So this is NOT "depth doesn't gate
// draws"; something about a depth attachment being present on a RenderTargetCube face's FBO
// silently blocks ALL colour output into that face on Bgfx, structurally unlike RenderTarget2D
// (whose equivalent depth test, rendertarget2d_depth_test.cpp, already passes correctly).
//
// INVESTIGATED AND RULED OUT (see plan_graphics.md Task 952 for the full write-up): stencil
// presence, specific depth format (Depth16/Depth24/Depth24Stencil8 all reproduce identically),
// sampler/mip-filter mismatch (identical GL_LINEAR params on the working and broken textures, via
// apitrace GL-call-log inspection), and framebuffer incompleteness (glCheckFramebufferStatus
// always reports GL_FRAMEBUFFER_COMPLETE, no GL error at all under MESA_DEBUG=1). Also
// investigated and RETRACTED: an "orphaned render-target view gets silently re-cleared by bgfx on
// a later unrelated frame" theory that initially looked promising from the apitrace call log --
// disproven by two independent, controlled repro attempts (a plain unbound RenderTarget2D
// surviving 5 unrelated frame flushes; the same RT WITH a Depth24Stencil8 attachment ALSO
// surviving) that could not reproduce any corruption with or without a candidate fix, so no
// engine-side fix was made from this. Root cause remains unresolved -- see NEXT.md §5/§8's Task
// 952 entry for what's needed next (RenderDoc, unavailable in this sandbox's apt repos, or a raw
// GL minimal reproduction bypassing bgfx entirely -- glretrace/eglretrace could not replay this
// trace for direct pixel/visual inspection, failing with "no current context" almost immediately,
// a known apitrace limitation with bgfx's dedicated render-thread architecture).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCubeSize = 32;

namespace
{
    void DrawFullQuad(GraphicsDevice& dev, float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3(-1.0f, -1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3( 1.0f,  1.0f, z), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class BgfxRenderTargetCubeDepthFormatTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 40)
            && closeTo(c.getGProperty(), expected.getGProperty(), 40)
            && closeTo(c.getBProperty(), expected.getBProperty(), 40);
    }

    std::unique_ptr<Texture2D> whiteTex_;

    // DIAGNOSTIC v2: renders near-green-then-far-red into the RenderTargetCube's PositiveZ face
    // (with real depth testing enabled), unbinds, then samples the face back via
    // EnvironmentMapEffect with a non-degenerate eye position (established SampleAfterUnbind
    // pattern), instead of reading while still bound (Task 951 makes that unreliable -- see
    // plan_graphics.md Task 952's write-up).
    Color RenderAndReadFace(GraphicsDevice& dev, DepthFormat depthFormat)
    {
        RenderTargetCube rtc(dev, kCubeSize, false, SurfaceFormat::Color, depthFormat);

        dev.SetRenderTarget(&rtc, CubeMapFace::PositiveZ);
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f, 0);

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        DrawFullQuad(dev, 0.2f, Color(0, 255, 0, 255));  // near, green — should win if depth-tested
        DrawFullQuad(dev, 0.8f, Color(255, 0, 0, 255));  // far, red — must be rejected if depth-tested

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color px(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(10, 10, 10, 255));

            EnvironmentMapEffect envFx(dev);
            envFx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            envFx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            envFx.setEnvironmentMapAmountProperty(1.0f);
            envFx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
            envFx.setTextureProperty(whiteTex_.get());
            envFx.setEnvironmentMapProperty(&rtc);
            envFx.setWorldProperty(Matrix::getIdentityProperty());
            // Task 952 finding: Identity View makes EyePosition (= inverse(View).Translation)
            // exactly coincide with the sampled vertex, making reflect(-E, N) degenerate/
            // undefined -- place the eye off-plane, in front of the face's own normal, so the
            // reflection direction is well-defined and actually points at PositiveZ.
            envFx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 5.0f),
                                                         Vector3(0.0f, 0.0f, 0.0f),
                                                         Vector3(0.0f, 1.0f, 0.0f)));
            envFx.setProjectionProperty(Matrix::getIdentityProperty());
            envFx.Apply();

            const Vector3 n(0.0f, 0.0f, 1.0f);
            const VertexPositionNormalTexture verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
                { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
                { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
            };
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

            const Rectangle reg(kCubeSize / 2, kCubeSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            // Break once we see anything other than the (10,10,10) background clear colour.
            if (px.getRProperty() > 20 || px.getGProperty() > 20 || px.getBProperty() > 20)
                break;
        }

        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, white));
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);

        const Color kGreen(0, 255, 0, 255);

        // DepthFormat::None: not asserted on which colour wins (see file header) -- just confirm
        // constructing and rendering into a depth-less cube face still works without crashing.
        Color gotNone = RenderAndReadFace(dev, DepthFormat::None);
        std::printf("[INFO] DepthFormat::None: got=(%d,%d,%d) (not asserted, see file header)\n",
            gotNone.getRProperty(), gotNone.getGProperty(), gotNone.getBProperty());

        Color gotDepth = RenderAndReadFace(dev, DepthFormat::Depth24Stencil8);
        check(matches(gotDepth, kGreen),
              "DepthFormat::Depth24Stencil8: near quad wins (real depth buffer gates draws)",
              gotDepth, "(0,255,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxRenderTargetCubeDepthFormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxRenderTargetCubeDepthFormatTest game;
    game.Run();
    return game.getResult();
}
