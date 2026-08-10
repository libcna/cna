// SPDX-License-Identifier: MS-PL
// Task 899: DualTextureEffect linear fog pixel integration test — Vulkan renderer.
//
// dual_texture3d was one of 5 Vulkan pipelines sharing FillExtPushConst()'s fully-packed
// 128-byte push constant (zero spare bytes for fog). Fixed here by extending
// descriptorSetLayout2Tex_ from 2 bindings (sampler0@0, sampler1@1) to 3 (adding a dynamic fog
// UBO @2), and by splitting a dedicated dual_texture3d.vert.glsl off of textured3d.vert.glsl
// (which now has its OWN fog UBO at binding=1, conflicting with dual_texture3d's own layout).
//
// Formula (matches EasyGL's already-tested formula exactly, Task 195):
//   fogFactor = clamp((Z + FogEnd) / (FogEnd - FogStart), 0, 1)   (raw object-space Z)
//   finalRGB  = mix(FogColor, geomRGB, fogFactor)
//
// `Texture2=gray(128,128,128)` deliberately cancels the DualTextureEffect frag shader's `*2`
// doubling factor (`1(white)*2*0.502(gray)≈1.004`, clamped to 1.0 on write) so the pre-fog
// material color reduces to exactly DiffuseColor, isolating fog cleanly (same technique as
// examples/bgfx_dualtextureeffect_fog_test.cpp / easygl_dualtextureeffect_fog_test.cpp).
//
// Unlike the Bgfx/EasyGL siblings (which sweep Z across [-0.9, 0.9], relying on their
// OpenGL-convention [-1,1] clip-space Z range), this test uses Z in [0, 1] with an Identity
// Projection matrix -- Vulkan's DirectX-convention clip volume clips any primitive with
// (pre-divide) Z < 0, so negative Z values are not usable here (matches this project's other
// Vulkan fog tests, e.g. vulkan_basiceffect_fog_test.cpp/vulkan_alphatest_fog_test.cpp).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kBlack(0, 0, 0, 255);
static const Color kBlue(0, 0, 255, 255);
static const Color kRed(255, 0, 0, 255);

#if defined(CNA_RENDERER_DIRECTX9)
// D3D9's stock DualTextureEffect declaration includes both texture-coordinate channels.
// Keeping the two channels equal makes this reachable public fixture renderer-independent.
struct DualTextureGpuVertex
{
    float px, py, pz;
    float u0, v0;
    float u1, v1;
};
static_assert(sizeof(DualTextureGpuVertex) == 28, "dual-texture vertex must be 28 bytes");
#endif

class DualTextureFogVulkanTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

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

    static bool matches(const Color& c, const Color& expected, int tol = 30)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), tol)
            && closeTo(c.getGProperty(), expected.getGProperty(), tol)
            && closeTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color renderQuad(GraphicsDevice& dev, Texture2D& texWhite, Texture2D& texGray, float z,
                      bool fogEnabled, const Vector3& fogColor, float fogStart, float fogEnd)
    {
        DualTextureEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&texWhite);
        fx.setTexture2Property(&texGray);
        fx.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f)); // blue
        fx.setFogEnabledProperty(fogEnabled);
        fx.setFogColorProperty(fogColor);
        fx.setFogStartProperty(fogStart);
        fx.setFogEndProperty(fogEnd);
        fx.Apply();

        const Vector3 tl(-1.0f,  1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br( 1.0f, -1.0f, z), tr( 1.0f,  1.0f, z);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
#if defined(CNA_RENDERER_DIRECTX9)
        const DualTextureGpuVertex quad[6] = {
            { -1.0f,  1.0f, z, 0.0f, 0.0f, 0.0f, 0.0f },
            { -1.0f, -1.0f, z, 0.0f, 1.0f, 0.0f, 1.0f },
            {  1.0f, -1.0f, z, 1.0f, 1.0f, 1.0f, 1.0f },
            { -1.0f,  1.0f, z, 0.0f, 0.0f, 0.0f, 0.0f },
            {  1.0f, -1.0f, z, 1.0f, 1.0f, 1.0f, 1.0f },
            {  1.0f,  1.0f, z, 1.0f, 0.0f, 1.0f, 0.0f },
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(quad, 6, static_cast<int>(sizeof(DualTextureGpuVertex)));
#else
        const VertexPositionTexture quad[6] = {
            { tl, uv0 }, { bl, uv1 }, { br, uv2 },
            { tl, uv0 }, { br, uv2 }, { tr, uv3 },
        };
#endif

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's
            // winding is culled under FNA's real default RasterizerState.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setDepthStencilStateProperty(DepthStencilState::None);
#if defined(CNA_RENDERER_DIRECTX9)
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
#else
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
#endif
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        const Color kWhite(255, 255, 255, 255);
        const Color kGrayHalf(128, 128, 128, 255);
        Texture2D texWhite(dev, 1, 1); texWhite.SetData(&kWhite, 1);
        Texture2D texGray(dev, 1, 1);  texGray.SetData(&kGrayHalf, 1);

        // (a) Fog disabled: blue material (white*2*gray≈white cancellation) -> pure blue.
        const Color gotOff = renderQuad(dev, texWhite, texGray, 0.0f, false, Vector3(1, 0, 0), 0.0f, 1.0f);
        check(matches(gotOff, kBlue), "(a) fog OFF: blue material -> pure blue", gotOff, "(0,0,255)");

        // (b) Fog 50%: Z=0.45, FogStart=0, FogEnd=-0.9 (see header) -> factor=(0.45-0.9)/(-0.9)=0.5 -> mix(red,blue,0.5)=(128,0,128).
        const Color gotHalf = renderQuad(dev, texWhite, texGray, 0.45f, true, Vector3(1, 0, 0), 0.0f, -0.9f);
        check(matches(gotHalf, Color(128, 0, 128, 255)),
              "(b) fog 50%: Z=0.45 -> purple mix", gotHalf, "(128,0,128)");

        // (c) Full fog: FogStart=0, FogEnd=-0.9, Z=0.9 -> factor=(0.9-0.9)/(-0.9)=0 -> pure red.
        const Color gotFull = renderQuad(dev, texWhite, texGray, 0.9f, true, Vector3(1, 0, 0), 0.0f, -0.9f);
        check(matches(gotFull, kRed), "(c) full fog: FogStart=0,FogEnd=-0.9,Z=0.9 -> full red", gotFull, "(255,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DualTextureFogVulkanTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    DualTextureFogVulkanTest game;
    game.Run();
    return game.getResult();
}
