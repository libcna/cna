// SPDX-License-Identifier: MS-PL
// Task 248: Vulkan vertex input mapping test — all supported vertex formats.
//
// Verifies that the VkVertexInputAttributeDescription (location, format, offset)
// for each supported vertex stride matches the expected XNA vertex layout.
// Each pass renders a full-screen colored quad at a known color and asserts the
// center pixel via GetBackBufferData, proving the vertex attribute at location=0
// (position) and location=1 (color/uv) are wired to the correct shader inputs.
//
// Stride 16 (VertexPositionColor):      float3 pos + ubyte4 RGBA → draw red
// Stride 20 (VertexPositionTexture):    float3 pos + float2 UV   → draw with
//                                        a 1×1 green texture
// Stride 24 (VertexPositionColorTexture): float3 + ubyte4 + float2 → draw blue
//                                        (vertex color overrides white texture)
// Stride 32 (VertexPositionNormalTexture): float3 + float3 + float2 → draw
//                                        with magenta texture, no lighting
//
// Also validates the VulkanVertexFormatHelper mapping table for all 12
// VertexElementFormat values.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanVertexFormatHelper.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Vulkan;

// ── Compact GPU vertex structs (packed, no vtable) ───────────────────────────

struct GpuVPC  { float x, y, z; uint8_t r, g, b, a; };          // stride 16
struct GpuVPT  { float x, y, z; float u, v; };                   // stride 20
struct GpuVPCT { float x, y, z; uint8_t r, g, b, a; float u, v; };  // stride 24
struct GpuVPNT { float x, y, z, nx, ny, nz, u, v; };             // stride 32

static_assert(sizeof(GpuVPC)  == 16);
static_assert(sizeof(GpuVPT)  == 20);
static_assert(sizeof(GpuVPCT) == 24);
static_assert(sizeof(GpuVPNT) == 32);

// ── Helper: fill 6 vertices for a full-screen triangle-list quad ─────────────

template<class V>
static void FillQuad(V* out, auto fn)
{
    // NDC corners: TL(-1,1) BL(-1,-1) BR(1,-1) TR(1,1)
    const float xs[4] = { -1.f,  1.f, -1.f,  1.f };
    const float ys[4] = {  1.f,  1.f, -1.f, -1.f };
    const float us[4] = {  0.f,  1.f,  0.f,  1.f };
    const float vs[4] = {  0.f,  0.f,  1.f,  1.f };
    // TL, BL, BR  +  TL, BR, TR
    const int idx[6] = { 0, 2, 3, 0, 3, 1 };
    for (int i = 0; i < 6; ++i) {
        int k = idx[i];
        fn(out[i], xs[k], ys[k], us[k], vs[k]);
    }
}

// ── 1×1 texture helper ────────────────────────────────────────────────────────

static std::unique_ptr<Texture2D> Make1x1(GraphicsDevice& dev, uint8_t r, uint8_t g, uint8_t b)
{
    auto tex = std::make_unique<Texture2D>(dev, 1, 1);
    Color px(r, g, b, 255);
    tex->SetData(&px, 1);
    return tex;
}

// ── Test class ────────────────────────────────────────────────────────────────

class VulkanVertexFormatTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    bool colorClose(Color got, Color want, int tol = 30)
    {
        return std::abs(int(got.getRProperty())  - int(want.getRProperty()))  <= tol
            && std::abs(int(got.getGProperty())  - int(want.getGProperty()))  <= tol
            && std::abs(int(got.getBProperty())  - int(want.getBProperty()))  <= tol;
    }

    // ── Part 1: Mapping table validation ──────────────────────────────────────

    void testMappingTable()
    {
        using VEF = VertexElementFormat;
        struct Row { VEF vef; VkFormat expected; int expectedSize; };
        static const Row rows[] = {
            { VEF::Single,           VK_FORMAT_R32_SFLOAT,            4  },
            { VEF::Vector2,          VK_FORMAT_R32G32_SFLOAT,         8  },
            { VEF::Vector3,          VK_FORMAT_R32G32B32_SFLOAT,      12 },
            { VEF::Vector4,          VK_FORMAT_R32G32B32A32_SFLOAT,   16 },
            { VEF::Color,            VK_FORMAT_R8G8B8A8_UNORM,        4  },
            { VEF::Byte4,            VK_FORMAT_R8G8B8A8_UINT,         4  },
            { VEF::Short2,           VK_FORMAT_R16G16_SINT,           4  },
            { VEF::Short4,           VK_FORMAT_R16G16B16A16_SINT,     8  },
            { VEF::NormalizedShort2, VK_FORMAT_R16G16_SNORM,          4  },
            { VEF::NormalizedShort4, VK_FORMAT_R16G16B16A16_SNORM,    8  },
            { VEF::HalfVector2,      VK_FORMAT_R16G16_SFLOAT,         4  },
            { VEF::HalfVector4,      VK_FORMAT_R16G16B16A16_SFLOAT,   8  },
        };

        static const char* kNames[] = {
            "Single","Vector2","Vector3","Vector4","Color","Byte4",
            "Short2","Short4","NormalizedShort2","NormalizedShort4",
            "HalfVector2","HalfVector4"
        };

        for (int i = 0; i < 12; ++i) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "VertexElementFormatToVk(%s)", kNames[i]);
            check(VertexElementFormatToVk(rows[i].vef) == rows[i].expected, buf);

            std::snprintf(buf, sizeof(buf), "VertexElementFormatSize(%s)==%d", kNames[i], rows[i].expectedSize);
            check(VertexElementFormatSize(rows[i].vef) == rows[i].expectedSize, buf);
        }

        // VkFormat for Color (ubyte4 RGBA) must match what Vulkan colored3d pipeline uses.
        check(VertexElementFormatToVk(VEF::Color) == VK_FORMAT_R8G8B8A8_UNORM,
              "Color→VK_FORMAT_R8G8B8A8_UNORM (matches colored3d pipeline attr[1])");
        // VkFormat for Vector3 must match position attribute.
        check(VertexElementFormatToVk(VEF::Vector3) == VK_FORMAT_R32G32B32_SFLOAT,
              "Vector3→VK_FORMAT_R32G32B32_SFLOAT (matches position attr[0])");
    }

    // ── Part 2: Pixel readback for each supported stride ──────────────────────

    void testStride16(GraphicsDevice& dev)
    {
        // Stride 16: float3 pos (loc=0) + ubyte4 RGBA (loc=1).
        // Upload red quad. If loc=0/1 are correct → center pixel = red.
        GpuVPC verts[6];
        FillQuad<GpuVPC>(verts, [](GpuVPC& v, float x, float y, float, float) {
            v.x = x; v.y = y; v.z = 0.f;
            v.r = 255; v.g = 0; v.b = 0; v.a = 255;
        });

        VertexDeclaration decl(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        });
        VertexBuffer vb(dev, decl, 6, BufferUsage::None);
        vb.SetDataRaw(verts, 6, 16);

        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        Color got = readCenter(dev);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "Stride16 VPC red quad: center=(%d,%d,%d) expected≈(255,0,0)",
            got.getRProperty(), got.getGProperty(), got.getBProperty());
        check(colorClose(got, Color(255, 0, 0, 255)), buf);
    }

    void testStride20(GraphicsDevice& dev)
    {
        // Stride 20: float3 pos (loc=0) + float2 UV (loc=1).
        // Sample a green 1×1 texture; if UV attr is at wrong location the
        // color will be wrong (e.g. black or garbage).
        auto tex = Make1x1(dev, 0, 255, 0);

        GpuVPT verts[6];
        FillQuad<GpuVPT>(verts, [](GpuVPT& v, float x, float y, float u, float vv) {
            v.x = x; v.y = y; v.z = 0.f; v.u = u; v.v = vv;
        });

        VertexDeclaration decl(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
        VertexBuffer vb(dev, decl, 6, BufferUsage::None);
        vb.SetDataRaw(verts, 6, 20);

        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(tex.get());
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        Color got = readCenter(dev);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "Stride20 VPT green tex: center=(%d,%d,%d) expected≈(0,255,0)",
            got.getRProperty(), got.getGProperty(), got.getBProperty());
        check(colorClose(got, Color(0, 255, 0, 255)), buf);
    }

    void testStride24(GraphicsDevice& dev)
    {
        // Stride 24: float3 pos (loc=0) + ubyte4 RGBA (loc=1) + float2 UV (loc=2).
        // White texture + blue vertex color → blue pixel.
        auto tex = Make1x1(dev, 255, 255, 255);

        GpuVPCT verts[6];
        FillQuad<GpuVPCT>(verts, [](GpuVPCT& v, float x, float y, float u, float vv) {
            v.x = x; v.y = y; v.z = 0.f;
            v.r = 0; v.g = 0; v.b = 255; v.a = 255;
            v.u = u; v.v = vv;
        });

        VertexDeclaration decl(24, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,             0),
            VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
        VertexBuffer vb(dev, decl, 6, BufferUsage::None);
        vb.SetDataRaw(verts, 6, 24);

        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.VertexColorEnabled = true;
        fx.setTextureProperty(tex.get());
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        Color got = readCenter(dev);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "Stride24 VPCT blue vertex+white tex: center=(%d,%d,%d) expected≈(0,0,255)",
            got.getRProperty(), got.getGProperty(), got.getBProperty());
        check(colorClose(got, Color(0, 0, 255, 255)), buf);
    }

    void testStride32(GraphicsDevice& dev)
    {
        // Stride 32: float3 pos (loc=0) + float3 normal (loc=1) + float2 UV (loc=2).
        // Magenta texture, no lighting (PreferPerPixelLighting=false, no ambient/diffuse).
        auto tex = Make1x1(dev, 255, 0, 255);

        GpuVPNT verts[6];
        FillQuad<GpuVPNT>(verts, [](GpuVPNT& v, float x, float y, float u, float vv) {
            v.x = x; v.y = y; v.z = 0.f;
            v.nx = 0.f; v.ny = 0.f; v.nz = 1.f;
            v.u = u; v.v = vv;
        });

        VertexDeclaration decl(32, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal,             0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
        VertexBuffer vb(dev, decl, 6, BufferUsage::None);
        vb.SetDataRaw(verts, 6, 32);

        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(tex.get());
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        Color got = readCenter(dev);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "Stride32 VPNT magenta tex: center=(%d,%d,%d) expected≈(255,0,255)",
            got.getRProperty(), got.getGProperty(), got.getBProperty());
        check(colorClose(got, Color(255, 0, 255, 255)), buf);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding: FillQuad()'s TL/BL/BR winding is CCW/back-facing under CNA's real
        // default RasterizerState — needs CullNone (shared by all 4 stride sub-tests below).
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        testMappingTable();
        testStride16(dev);
        testStride20(dev);
        testStride24(dev);
        testStride32(dev);

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VulkanVertexFormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanVertexFormatTest g;
    g.Run();
    return g.getResult();
}
