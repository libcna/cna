// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-255 -- a `ShaderEffect` drives a 3D draw, with the caller's own
// `VertexDeclaration` deciding where its attributes are.
//
// Before this row `VulkanEffectRenderer::GetOrCreatePipeline` baked the `SpriteBatch` vertex input
// and the 3D draw path never looked at `activeCustomEffect_` at all: a game that applied a
// `ShaderEffect` and issued `DrawUserPrimitives` got its geometry drawn **by a stock program**, and
// nothing said so. That is the same silent mis-bind `VULKAN-156`, `VULKAN-165` and `VULKAN-265`
// each removed elsewhere, and it is what this row closes.
//
// The layout under test matches EasyGL's own custom-layout proof (Task 1080) element for element,
// so the two renderers are answering the same question:
//   offset  0  Position     Vector3
//   offset 12  Normal       Vector3
//   offset 24  Tangent      Vector3
//   offset 36  TextureCoord Vector2
//   offset 44  Color        Color (4 normalized bytes)
//   stride 48 -- a stride this renderer's stock table does not list, which is the point: a custom
//   shader's inputs cannot be inferred from a stride, and the declaration is the only thing that
//   knows where they are.
//
// The shader writes ONE attribute per output channel -- R from `Normal.x`, G from `Tangent.y`,
// B from `Color.r`, A from `TexCoord.x` -- so a wrong offset is one wrong channel rather than a
// uniformly wrong colour, and every element of the declaration is checked by a different channel.
//
//   A  The custom shader runs, and each of the four attributes arrives at its own offset.
//   B  A second buffer with different attribute values gives a different pixel. Without it, A
//      could pass on a shader that ignored its inputs.
//   C  The indexed route reaches the same place. It is a separate hook in a separate function, so
//      one leg cannot speak for both.
//   D  The same five attributes split across two vertex streams render on the non-indexed route.
//   E  The split layout also renders on the indexed route, including its separate compaction path.
//   F  A buffer with NO VertexDeclaration is refused BY NAME. The stock routes may fall back to a
//      stride table; for a custom shader that table is a guess about someone else's program.
//   G  `DrawInstancedPrimitives` consumes its per-instance binding through the custom shader.
//   H  No validation message.
//
// The transform: the effect's `IEffectMatrices` properties supply world/view/projection, and this
// renderer writes their product into the push-constant block's one matrix slot because it has no
// reflection to deliver them by name. Leg A's shader reads exactly that slot, so a failure to
// deliver them shows up as nothing drawn at all.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/NotSupportedException.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

// ---------------------------------------------------------------------------
// Pre-compiled SPIR-V, through modules/renderers/vulkan/src/shaders/compile_shaders.py.
//   vert: five inputs at locations 0..4, gl_Position = pc.uMatrix * vec4(inPosition, 1)
//         vProbe = vec4(inNormal.x, inTangent.y, inColor.r, inTexCoord.x)
//   frag: outColor = vProbe
// ---------------------------------------------------------------------------
static const uint32_t kCustomLayoutVertSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000037, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x000c000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x0000000d, 0x0000001b, 0x00000025,
    0x00000026, 0x0000002b, 0x0000002f, 0x00000033, 0x00030047, 0x0000000b,
    0x00000002, 0x00050048, 0x0000000b, 0x00000000, 0x0000000b, 0x00000000,
    0x00050048, 0x0000000b, 0x00000001, 0x0000000b, 0x00000001, 0x00050048,
    0x0000000b, 0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x0000000b,
    0x00000003, 0x0000000b, 0x00000004, 0x00030047, 0x00000012, 0x00000002,
    0x00050048, 0x00000012, 0x00000000, 0x00000023, 0x00000000, 0x00050048,
    0x00000012, 0x00000001, 0x00000023, 0x00000008, 0x00040048, 0x00000012,
    0x00000002, 0x00000005, 0x00050048, 0x00000012, 0x00000002, 0x00000007,
    0x00000010, 0x00050048, 0x00000012, 0x00000002, 0x00000023, 0x00000010,
    0x00040047, 0x0000001b, 0x0000001e, 0x00000000, 0x00040047, 0x00000025,
    0x0000001e, 0x00000000, 0x00040047, 0x00000026, 0x0000001e, 0x00000001,
    0x00040047, 0x0000002b, 0x0000001e, 0x00000002, 0x00040047, 0x0000002f,
    0x0000001e, 0x00000004, 0x00040047, 0x00000033, 0x0000001e, 0x00000003,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004,
    0x00040015, 0x00000008, 0x00000020, 0x00000000, 0x0004002b, 0x00000008,
    0x00000009, 0x00000001, 0x0004001c, 0x0000000a, 0x00000006, 0x00000009,
    0x0006001e, 0x0000000b, 0x00000007, 0x00000006, 0x0000000a, 0x0000000a,
    0x00040020, 0x0000000c, 0x00000003, 0x0000000b, 0x0004003b, 0x0000000c,
    0x0000000d, 0x00000003, 0x00040015, 0x0000000e, 0x00000020, 0x00000001,
    0x0004002b, 0x0000000e, 0x0000000f, 0x00000000, 0x00040017, 0x00000010,
    0x00000006, 0x00000002, 0x00040018, 0x00000011, 0x00000007, 0x00000004,
    0x0005001e, 0x00000012, 0x00000010, 0x00000010, 0x00000011, 0x00040020,
    0x00000013, 0x00000009, 0x00000012, 0x0004003b, 0x00000013, 0x00000014,
    0x00000009, 0x0004002b, 0x0000000e, 0x00000015, 0x00000002, 0x00040020,
    0x00000016, 0x00000009, 0x00000011, 0x00040017, 0x00000019, 0x00000006,
    0x00000003, 0x00040020, 0x0000001a, 0x00000001, 0x00000019, 0x0004003b,
    0x0000001a, 0x0000001b, 0x00000001, 0x0004002b, 0x00000006, 0x0000001d,
    0x3f800000, 0x00040020, 0x00000023, 0x00000003, 0x00000007, 0x0004003b,
    0x00000023, 0x00000025, 0x00000003, 0x0004003b, 0x0000001a, 0x00000026,
    0x00000001, 0x0004002b, 0x00000008, 0x00000027, 0x00000000, 0x00040020,
    0x00000028, 0x00000001, 0x00000006, 0x0004003b, 0x0000001a, 0x0000002b,
    0x00000001, 0x00040020, 0x0000002e, 0x00000001, 0x00000007, 0x0004003b,
    0x0000002e, 0x0000002f, 0x00000001, 0x00040020, 0x00000032, 0x00000001,
    0x00000010, 0x0004003b, 0x00000032, 0x00000033, 0x00000001, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x00050041, 0x00000016, 0x00000017, 0x00000014, 0x00000015, 0x0004003d,
    0x00000011, 0x00000018, 0x00000017, 0x0004003d, 0x00000019, 0x0000001c,
    0x0000001b, 0x00050051, 0x00000006, 0x0000001e, 0x0000001c, 0x00000000,
    0x00050051, 0x00000006, 0x0000001f, 0x0000001c, 0x00000001, 0x00050051,
    0x00000006, 0x00000020, 0x0000001c, 0x00000002, 0x00070050, 0x00000007,
    0x00000021, 0x0000001e, 0x0000001f, 0x00000020, 0x0000001d, 0x00050091,
    0x00000007, 0x00000022, 0x00000018, 0x00000021, 0x00050041, 0x00000023,
    0x00000024, 0x0000000d, 0x0000000f, 0x0003003e, 0x00000024, 0x00000022,
    0x00050041, 0x00000028, 0x00000029, 0x00000026, 0x00000027, 0x0004003d,
    0x00000006, 0x0000002a, 0x00000029, 0x00050041, 0x00000028, 0x0000002c,
    0x0000002b, 0x00000009, 0x0004003d, 0x00000006, 0x0000002d, 0x0000002c,
    0x00050041, 0x00000028, 0x00000030, 0x0000002f, 0x00000027, 0x0004003d,
    0x00000006, 0x00000031, 0x00000030, 0x00050041, 0x00000028, 0x00000034,
    0x00000033, 0x00000027, 0x0004003d, 0x00000006, 0x00000035, 0x00000034,
    0x00070050, 0x00000007, 0x00000036, 0x0000002a, 0x0000002d, 0x00000031,
    0x00000035, 0x0003003e, 0x00000025, 0x00000036, 0x000100fd, 0x00010038,
};
static const uint32_t kCustomLayoutFragSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x0000000d, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000b, 0x00030010,
    0x00000004, 0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000,
    0x00040047, 0x0000000b, 0x0000001e, 0x00000000, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
    0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
    0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003,
    0x00040020, 0x0000000a, 0x00000001, 0x00000007, 0x0004003b, 0x0000000a,
    0x0000000b, 0x00000001, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007, 0x0000000c,
    0x0000000b, 0x0003003e, 0x00000009, 0x0000000c, 0x000100fd, 0x00010038,
};

// plan_vulkan.md VULKAN-168: the same shader with a sixth input, the per-instance vec4 at
// location 5 (binding 1).
static const uint32_t kInstancedVertSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000038, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0009000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x0000000d, 0x0000001b, 0x00000021,
    0x00000030, 0x00030047, 0x0000000b, 0x00000002, 0x00050048, 0x0000000b,
    0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x0000000b, 0x00000001,
    0x0000000b, 0x00000001, 0x00050048, 0x0000000b, 0x00000002, 0x0000000b,
    0x00000003, 0x00050048, 0x0000000b, 0x00000003, 0x0000000b, 0x00000004,
    0x00030047, 0x00000012, 0x00000002, 0x00050048, 0x00000012, 0x00000000,
    0x00000023, 0x00000000, 0x00050048, 0x00000012, 0x00000001, 0x00000023,
    0x00000008, 0x00040048, 0x00000012, 0x00000002, 0x00000005, 0x00050048,
    0x00000012, 0x00000002, 0x00000007, 0x00000010, 0x00050048, 0x00000012,
    0x00000002, 0x00000023, 0x00000010, 0x00040047, 0x0000001b, 0x0000001e,
    0x00000000, 0x00040047, 0x00000021, 0x0000001e, 0x00000001, 0x00040047,
    0x00000030, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017,
    0x00000007, 0x00000006, 0x00000004, 0x00040015, 0x00000008, 0x00000020,
    0x00000000, 0x0004002b, 0x00000008, 0x00000009, 0x00000001, 0x0004001c,
    0x0000000a, 0x00000006, 0x00000009, 0x0006001e, 0x0000000b, 0x00000007,
    0x00000006, 0x0000000a, 0x0000000a, 0x00040020, 0x0000000c, 0x00000003,
    0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000003, 0x00040015,
    0x0000000e, 0x00000020, 0x00000001, 0x0004002b, 0x0000000e, 0x0000000f,
    0x00000000, 0x00040017, 0x00000010, 0x00000006, 0x00000002, 0x00040018,
    0x00000011, 0x00000007, 0x00000004, 0x0005001e, 0x00000012, 0x00000010,
    0x00000010, 0x00000011, 0x00040020, 0x00000013, 0x00000009, 0x00000012,
    0x0004003b, 0x00000013, 0x00000014, 0x00000009, 0x0004002b, 0x0000000e,
    0x00000015, 0x00000002, 0x00040020, 0x00000016, 0x00000009, 0x00000011,
    0x00040017, 0x00000019, 0x00000006, 0x00000003, 0x00040020, 0x0000001a,
    0x00000001, 0x00000019, 0x0004003b, 0x0000001a, 0x0000001b, 0x00000001,
    0x0004002b, 0x00000006, 0x0000001e, 0x3f000000, 0x00040020, 0x00000020,
    0x00000001, 0x00000007, 0x0004003b, 0x00000020, 0x00000021, 0x00000001,
    0x0004002b, 0x00000008, 0x00000025, 0x00000002, 0x00040020, 0x00000026,
    0x00000001, 0x00000006, 0x0004002b, 0x00000006, 0x00000029, 0x3f800000,
    0x00040020, 0x0000002e, 0x00000003, 0x00000007, 0x0004003b, 0x0000002e,
    0x00000030, 0x00000003, 0x0004002b, 0x00000008, 0x00000033, 0x00000003,
    0x0004002b, 0x00000006, 0x00000036, 0x00000000, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00050041,
    0x00000016, 0x00000017, 0x00000014, 0x00000015, 0x0004003d, 0x00000011,
    0x00000018, 0x00000017, 0x0004003d, 0x00000019, 0x0000001c, 0x0000001b,
    0x0007004f, 0x00000010, 0x0000001d, 0x0000001c, 0x0000001c, 0x00000000,
    0x00000001, 0x0005008e, 0x00000010, 0x0000001f, 0x0000001d, 0x0000001e,
    0x0004003d, 0x00000007, 0x00000022, 0x00000021, 0x0007004f, 0x00000010,
    0x00000023, 0x00000022, 0x00000022, 0x00000000, 0x00000001, 0x00050081,
    0x00000010, 0x00000024, 0x0000001f, 0x00000023, 0x00050041, 0x00000026,
    0x00000027, 0x0000001b, 0x00000025, 0x0004003d, 0x00000006, 0x00000028,
    0x00000027, 0x00050051, 0x00000006, 0x0000002a, 0x00000024, 0x00000000,
    0x00050051, 0x00000006, 0x0000002b, 0x00000024, 0x00000001, 0x00070050,
    0x00000007, 0x0000002c, 0x0000002a, 0x0000002b, 0x00000028, 0x00000029,
    0x00050091, 0x00000007, 0x0000002d, 0x00000018, 0x0000002c, 0x00050041,
    0x0000002e, 0x0000002f, 0x0000000d, 0x0000000f, 0x0003003e, 0x0000002f,
    0x0000002d, 0x00050041, 0x00000026, 0x00000031, 0x00000021, 0x00000025,
    0x0004003d, 0x00000006, 0x00000032, 0x00000031, 0x00050041, 0x00000026,
    0x00000034, 0x00000021, 0x00000033, 0x0004003d, 0x00000006, 0x00000035,
    0x00000034, 0x00070050, 0x00000007, 0x00000037, 0x00000032, 0x00000035,
    0x00000036, 0x00000029, 0x0003003e, 0x00000030, 0x00000037, 0x000100fd,
    0x00010038,
};

namespace
{
constexpr int kN = 8;
const Color kClear(0, 255, 0, 255);

#pragma pack(push, 1)
struct CustomVertex
{
    float px, py, pz;      // offset  0  Position
    float nx, ny, nz;      // offset 12  Normal
    float tx, ty, tz;      // offset 24  Tangent
    float u, v;            // offset 36  TextureCoordinate
    std::uint8_t r, g, b, a;  // offset 44  Color
};
#pragma pack(pop)
static_assert(sizeof(CustomVertex) == 48, "the declaration below promises a 48-byte stride");

struct SplitPositionNormal
{
    float px, py, pz;
    float nx, ny, nz;
};
static_assert(sizeof(SplitPositionNormal) == 24);

#pragma pack(push, 1)
struct SplitTangentTextureColor
{
    float tx, ty, tz;
    float u, v;
    std::uint8_t r, g, b, a;
};
#pragma pack(pop)
static_assert(sizeof(SplitTangentTextureColor) == 24);
}  // namespace

class VulkanShaderEffect3DTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<ShaderEffect> fx_;
    std::unique_ptr<IndexBuffer> ib_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }

    /// Within one 8-bit step, which is all a float-to-unorm round trip promises.
    static bool Near(int got, int want) { return got >= want - 1 && got <= want + 1; }

    static VertexDeclaration Declaration()
    {
        return VertexDeclaration(48, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
            VertexElement(36, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(44, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }

    /// A screen-filling quad in clip space, every vertex carrying the same probe values, so the
    /// centre texel reads them back without interpolation entering the answer.
    std::unique_ptr<VertexBuffer> MakeQuad(float nx, float ty, std::uint8_t cr, float u)
    {
        auto& dev = getGraphicsDeviceProperty();
        const VertexDeclaration decl = Declaration();
        auto vb = std::make_unique<VertexBuffer>(dev, decl, 4, BufferUsage::None);
        const CustomVertex v[4] = {
            { -1.f,  1.f, 0.f,  nx, 0.f, 0.f,  0.f, ty, 0.f,  u, 0.f,  cr, 0, 0, 255 },
            { -1.f, -1.f, 0.f,  nx, 0.f, 0.f,  0.f, ty, 0.f,  u, 0.f,  cr, 0, 0, 255 },
            {  1.f, -1.f, 0.f,  nx, 0.f, 0.f,  0.f, ty, 0.f,  u, 0.f,  cr, 0, 0, 255 },
            {  1.f,  1.f, 0.f,  nx, 0.f, 0.f,  0.f, ty, 0.f,  u, 0.f,  cr, 0, 0, 255 },
        };
        vb->SetDataRaw(v, 4, sizeof(CustomVertex));
        return vb;
    }

    Color DrawQuad(VertexBuffer& vb, bool indexed)
    {
        auto& dev = getGraphicsDeviceProperty();
        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetRenderTarget(&rt);
        dev.Clear(kClear);
        fx_->Apply();
        dev.SetVertexBuffer(&vb);
        if (indexed) {
            dev.setIndicesProperty(ib_.get());
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.setIndicesProperty(nullptr);
        } else {
            dev.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
        }
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        rt.GetData(p.data(), 0, kN * kN);
        return p[kN * kN / 2];
    }

    Color DrawSplitQuad(bool indexed)
    {
        auto& dev = getGraphicsDeviceProperty();
        const VertexDeclaration positionNormal(24, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
        });
        const VertexDeclaration tangentTextureColor(24, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
        VertexBuffer a(dev, positionNormal, 4, BufferUsage::None);
        VertexBuffer b(dev, tangentTextureColor, 4, BufferUsage::None);
        const SplitPositionNormal av[4] = {
            { -1.f,  1.f, 0.f, 0.2f, 0.f, 0.f },
            { -1.f, -1.f, 0.f, 0.2f, 0.f, 0.f },
            {  1.f, -1.f, 0.f, 0.2f, 0.f, 0.f },
            {  1.f,  1.f, 0.f, 0.2f, 0.f, 0.f },
        };
        const SplitTangentTextureColor bv[4] = {
            { 0.f, 0.4f, 0.f, 0.8f, 0.f, 153, 0, 0, 255 },
            { 0.f, 0.4f, 0.f, 0.8f, 0.f, 153, 0, 0, 255 },
            { 0.f, 0.4f, 0.f, 0.8f, 0.f, 153, 0, 0, 255 },
            { 0.f, 0.4f, 0.f, 0.8f, 0.f, 153, 0, 0, 255 },
        };
        a.SetDataRaw(av, 4, sizeof(SplitPositionNormal));
        b.SetDataRaw(bv, 4, sizeof(SplitTangentTextureColor));

        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetRenderTarget(&rt);
        dev.Clear(kClear);
        fx_->Apply();
        dev.SetVertexBuffers({ VertexBufferBinding(&a, 0, 0), VertexBufferBinding(&b, 0, 0) });
        if (indexed) {
            dev.setIndicesProperty(ib_.get());
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
            dev.setIndicesProperty(nullptr);
        } else {
            dev.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
        }
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        rt.GetData(p.data(), 0, kN * kN);
        return p[kN * kN / 2];
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        const std::size_t messagesBefore = Renderer().GetValidationMessagesEXT().size();

        const std::string vert(reinterpret_cast<const char*>(kCustomLayoutVertSpv),
                               sizeof(kCustomLayoutVertSpv));
        const std::string frag(reinterpret_cast<const char*>(kCustomLayoutFragSpv),
                               sizeof(kCustomLayoutFragSpv));
        fx_ = std::make_unique<ShaderEffect>(dev, vert, frag);
        // Identity throughout: the quad is already in clip space, so what this asserts about the
        // matrix is only that the renderer delivered ONE -- an all-zero uMatrix collapses the quad
        // to a point and every leg reads the clear colour.
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());

        const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
        ib_ = std::make_unique<IndexBuffer>(dev, IndexElementSize::SixteenBits, 6,
                                            BufferUsage::None);
        ib_->SetData(indices.data(), 6);

        {
            auto vb = MakeQuad(0.2f, 0.4f, 153, 0.8f);
            const Color got = DrawQuad(*vb, /*indexed=*/false);
            check(Near(got.getRProperty(), 51) && Near(got.getGProperty(), 102) &&
                      Near(got.getBProperty(), 153) && Near(got.getAProperty(), 204),
                  "A a custom shader drives a 3D draw and reads Normal.x, Tangent.y, Color.r and "
                  "TexCoord.x each from its own offset: " + Text(got) + " (want ~(51,102,153,204); "
                  + Text(kClear) + " means nothing was drawn)");
        }
        {
            auto vb = MakeQuad(0.8f, 0.6f, 51, 0.2f);
            const Color got = DrawQuad(*vb, /*indexed=*/false);
            check(Near(got.getRProperty(), 204) && Near(got.getGProperty(), 153) &&
                      Near(got.getBProperty(), 51) && Near(got.getAProperty(), 51),
                  "B different attribute values give a different pixel: " + Text(got) +
                      " (want ~(204,153,51,51))");
        }
        {
            auto vb = MakeQuad(0.2f, 0.4f, 153, 0.8f);
            const Color got = DrawQuad(*vb, /*indexed=*/true);
            check(Near(got.getRProperty(), 51) && Near(got.getGProperty(), 102) &&
                      Near(got.getBProperty(), 153) && Near(got.getAProperty(), 204),
                  "C the indexed route reaches the same place -- it is a separate hook: " +
                      Text(got) + " (want ~(51,102,153,204))");
        }
        {
            const Color got = DrawSplitQuad(/*indexed=*/false);
            check(Near(got.getRProperty(), 51) && Near(got.getGProperty(), 102) &&
                      Near(got.getBProperty(), 153) && Near(got.getAProperty(), 204),
                  "D a custom non-indexed draw combines two vertex streams without dropping "
                  "either declaration: " + Text(got) + " (want ~(51,102,153,204))");
        }
        {
            const Color got = DrawSplitQuad(/*indexed=*/true);
            check(Near(got.getRProperty(), 51) && Near(got.getGProperty(), 102) &&
                      Near(got.getBProperty(), 153) && Near(got.getAProperty(), 204),
                  "E a custom indexed draw combines the same two streams through its compact "
                  "index window: " + Text(got) + " (want ~(51,102,153,204))");
        }
        {
            // No declaration: the VertexBuffer(device, count) convenience constructor.
            VertexBuffer bare(dev, 4);
            const CustomVertex v[4]{};
            bare.SetDataRaw(v, 4, sizeof(CustomVertex));
            bool threw = false;
            std::string what;
            {
                // The target and the bindings are set up and torn down OUTSIDE the try, so a leg
                // that throws cannot leave the device pointing at a destroyed render target and
                // quietly change what the next leg measures.
                RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
                dev.SetRenderTarget(&rt);
                fx_->Apply();
                dev.SetVertexBuffer(&bare);
                try { dev.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2); }
                catch (const System::NotSupportedException& e) { threw = true; what = e.what(); }
                catch (const std::exception& e) {
                    what = std::string("the wrong exception type: ") + e.what();
                }
                dev.SetVertexBuffer(nullptr);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            }
            check(threw && what.find("VertexDeclaration") != std::string::npos,
                  "F a buffer with no VertexDeclaration is refused by name: " +
                      (threw ? what : std::string("NOT REFUSED")));
        }
        {
            // plan_vulkan.md VULKAN-168: this leg was a REFUSAL under `VULKAN-255` -- the custom
            // pipeline declared one vertex binding, so an instanced draw could only have been
            // drawn with a stock shader. It declares two now, and this asserts the thing that
            // makes the second one worth having: the two instances read DIFFERENT per-instance
            // records, so a divisor or binding mistake shows up as one colour across both halves.
            const std::string instVert(reinterpret_cast<const char*>(kInstancedVertSpv),
                                       sizeof(kInstancedVertSpv));
            ShaderEffect instFx(dev, instVert, frag);
            instFx.setWorldProperty(Matrix::getIdentityProperty());
            instFx.setViewProperty(Matrix::getIdentityProperty());
            instFx.setProjectionProperty(Matrix::getIdentityProperty());

            // A position-only mesh, so the pipeline declares no attribute this shader ignores:
            // the layer warns about an unconsumed one, and without SPIR-V reflection -- which this
            // renderer does not do -- a declaration-driven layout cannot know which those are.
            const VertexDeclaration meshDecl(12, {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            });
            VertexBuffer meshVb(dev, meshDecl, 4, BufferUsage::None);
            const float meshData[12] = { -1.f,  1.f, 0.f,
                                         -1.f, -1.f, 0.f,
                                          1.f, -1.f, 0.f,
                                          1.f,  1.f, 0.f };
            meshVb.SetDataRaw(meshData, 4, 12);
            const VertexDeclaration instDecl(16, {
                VertexElement(0, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 1),
            });
            VertexBuffer instVb(dev, instDecl, 2, BufferUsage::None);
            // instance 0: left half, probe (0.2, 0.8); instance 1: right half, probe (0.8, 0.2)
            const float instData[8] = { -0.5f, 0.0f, 0.2f, 0.8f,
                                         0.5f, 0.0f, 0.8f, 0.2f };
            instVb.SetDataRaw(instData, 2, 16);

            RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetRenderTarget(&rt);
            dev.Clear(kClear);
            instFx.Apply();
            dev.SetVertexBuffers({ VertexBufferBinding(&meshVb, 0, 0),
                                   VertexBufferBinding(&instVb, 0, 1) });
            dev.setIndicesProperty(ib_.get());
            std::string what;
            bool threw = false;
            try { dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 2); }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            dev.setIndicesProperty(nullptr);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
            rt.GetData(p.data(), 0, kN * kN);
            const Color left  = p[kN * (kN / 2) + 2];
            const Color right = p[kN * (kN / 2) + 5];
            check(!threw && Near(left.getRProperty(), 51) && Near(left.getGProperty(), 204) &&
                      Near(right.getRProperty(), 204) && Near(right.getGProperty(), 51),
                  "G an instanced draw through a ShaderEffect reads a DIFFERENT per-instance "
                  "record per instance: left=" + Text(left) + " right=" + Text(right) +
                      " (want ~(51,204,0) and ~(204,51,0)" +
                      (threw ? std::string("; threw: ") + what : std::string("")) + ")");
        }

        {
            const std::size_t after = Renderer().GetValidationMessagesEXT().size();
            check(!VulkanRenderer::IsValidationActiveEXT() || after == messagesBefore,
                  "H no validation message: " + std::to_string(messagesBefore) + " -> " +
                      std::to_string(after));
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        fx_.reset();
        ib_.reset();
        Exit();
    }

public:
    VulkanShaderEffect3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanShaderEffect3DTest game;
    game.Run();
    return game.getResult();
}
