// SPDX-License-Identifier: MS-PL
// REMED-GFX-125: a built-in vertex type's public VertexDeclaration describes the packed GPU
// vertex stream, never the C++ object that carries the same values.
//
// XNA's own vertex types are blittable value types, so over there sizeof(T) *is* the stride the
// GPU walks and the two facts are interchangeable. CNA cannot reproduce that identity. Color
// implements IPackedVector and every multi-element vertex type implements IVertexType, both of
// which are polymorphic C++ base classes here, so each built-in vertex object carries a vtable
// pointer and places its members at alignment-driven offsets that have nothing to do with the
// stream. The object layout and the stream layout are genuinely different, and the contract this
// file locks down is:
//
//   VertexDeclaration.VertexStride == the packed extent of the declaration's own elements
//   every element's [offset, offset + formatSize) lies inside that stride
//   sizeof(T) is an object-model fact and never becomes a GPU stride
//   the object -> stream conversion happens exactly once, inside the typed DrawUser overloads
//   the raw const void* overloads keep their unchanged "the caller already supplied a GPU
//       stream at the declared stride" contract, so a correctly packed custom POD vertex is the
//       control that proves the fix did not double-pack anything
//
// Section A (layout) needs no rendering and runs on every renderer. Section B renders the same
// public fixture through every DrawUser path on the renderers that raster 3D triangles and can
// read the backbuffer back; Section C is the SDL_GPU equivalent through its render-target oracle.
// Section D pins the declaration/range validation contract and needs only a device.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the guards it replaced.
using namespace CNA::Testing::Renderers;

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTextureSkinned;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;

namespace
{
    // =====================================================================================
    // Section A support — the public layout facts, independent of any renderer
    // =====================================================================================

    int FormatSize(VertexElementFormat format)
    {
        switch (format)
        {
        case VertexElementFormat::Single:           return 4;
        case VertexElementFormat::Vector2:          return 8;
        case VertexElementFormat::Vector3:          return 12;
        case VertexElementFormat::Vector4:          return 16;
        case VertexElementFormat::Color:            return 4;
        case VertexElementFormat::Byte4:            return 4;
        case VertexElementFormat::Short2:           return 4;
        case VertexElementFormat::Short4:           return 8;
        case VertexElementFormat::NormalizedShort2: return 4;
        case VertexElementFormat::NormalizedShort4: return 8;
        case VertexElementFormat::HalfVector2:      return 4;
        case VertexElementFormat::HalfVector4:      return 8;
        }
        return 0;
    }

    /// The byte extent the declaration's own elements occupy — the only honest source of truth
    /// for a stride, because it is derived from the layout the declaration itself describes.
    int PackedElementExtent(const VertexDeclaration& declaration)
    {
        int extent = 0;
        for (const VertexElement& element : declaration.GetVertexElements())
        {
            const int end =
                element.getOffsetProperty() + FormatSize(element.getVertexElementFormatProperty());
            if (end > extent)
                extent = end;
        }
        return extent;
    }

    /// One built-in vertex type's complete evidence row.
    struct BuiltInLayout
    {
        const char* name;
        const VertexDeclaration* declaration;
        /// The stride XNA/CNA publish for this layout: what every renderer, shader and content
        /// path in this repository already assumes for it.
        int gpuStride;
        std::size_t objectSize;
        /// Byte offsets of the type's own C++ members, measured on a live object.
        std::vector<std::ptrdiff_t> memberOffsets;
    };

    template <typename VertexT>
    std::ptrdiff_t MemberOffset(const VertexT& vertex, const void* member)
    {
        return static_cast<const std::uint8_t*>(member) -
               reinterpret_cast<const std::uint8_t*>(&vertex);
    }

    std::vector<BuiltInLayout> BuiltInLayouts()
    {
        std::vector<BuiltInLayout> rows;

        {
            const VertexPositionColor v{Vector3(), Color(1, 2, 3, 4)};
            rows.push_back(BuiltInLayout{
                "VertexPositionColor", &VertexPositionColor::getVertexDeclarationStatic(), 16,
                sizeof(VertexPositionColor),
                {MemberOffset(v, &v.Position), MemberOffset(v, &v.Color)}});
        }
        {
            const VertexPositionTexture v{Vector3(), Vector2()};
            rows.push_back(BuiltInLayout{
                "VertexPositionTexture", &VertexPositionTexture::getVertexDeclarationStatic(), 20,
                sizeof(VertexPositionTexture),
                {MemberOffset(v, &v.Position), MemberOffset(v, &v.TextureCoordinate)}});
        }
        {
            const VertexPositionColorTexture v{Vector3(), Color(1, 2, 3, 4), Vector2()};
            rows.push_back(BuiltInLayout{
                "VertexPositionColorTexture",
                &VertexPositionColorTexture::getVertexDeclarationStatic(), 24,
                sizeof(VertexPositionColorTexture),
                {MemberOffset(v, &v.Position), MemberOffset(v, &v.Color),
                 MemberOffset(v, &v.TextureCoordinate)}});
        }
        {
            const VertexPositionNormalTexture v{Vector3(), Vector3(), Vector2()};
            rows.push_back(BuiltInLayout{
                "VertexPositionNormalTexture",
                &VertexPositionNormalTexture::getVertexDeclarationStatic(), 32,
                sizeof(VertexPositionNormalTexture),
                {MemberOffset(v, &v.Position), MemberOffset(v, &v.Normal),
                 MemberOffset(v, &v.TextureCoordinate)}});
        }
        {
            const VertexPositionNormalTextureSkinned v{
                Vector3(), Vector3(), Vector2(), Vector4(), std::array<std::uint8_t, 4>{}};
            rows.push_back(BuiltInLayout{
                "VertexPositionNormalTextureSkinned",
                &VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 52,
                sizeof(VertexPositionNormalTextureSkinned),
                {MemberOffset(v, &v.Position), MemberOffset(v, &v.Normal),
                 MemberOffset(v, &v.TextureCoordinate), MemberOffset(v, &v.BlendWeight),
                 MemberOffset(v, &v.BlendIndices)}});
        }
        {
            const VertexPositionNormalTangentTexture v{Vector3(), Vector3(), Vector4(), Vector2()};
            rows.push_back(BuiltInLayout{
                "VertexPositionNormalTangentTexture",
                &VertexPositionNormalTangentTexture::getVertexDeclarationStatic(), 48,
                sizeof(VertexPositionNormalTangentTexture),
                {MemberOffset(v, &v.Position), MemberOffset(v, &v.Normal),
                 MemberOffset(v, &v.Tangent), MemberOffset(v, &v.TextureCoordinate)}});
        }
        {
            const VertexPositionNormalTangentTextureSkinned v{
                Vector3(), Vector3(), Vector4(), Vector2(), Vector4(),
                std::array<std::uint8_t, 4>{}};
            rows.push_back(BuiltInLayout{
                "VertexPositionNormalTangentTextureSkinned",
                &VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic(), 68,
                sizeof(VertexPositionNormalTangentTextureSkinned),
                {MemberOffset(v, &v.Position), MemberOffset(v, &v.Normal),
                 MemberOffset(v, &v.Tangent), MemberOffset(v, &v.TextureCoordinate),
                 MemberOffset(v, &v.BlendWeight), MemberOffset(v, &v.BlendIndices)}});
        }
        return rows;
    }

    // =====================================================================================
    // Section B/C support — the shared rendering fixture
    // =====================================================================================

    /// The target is split into this many equal-width vertical slots. Each slot holds exactly one
    /// triangle, so a lit slot names the vertex range a draw actually consumed and a wrong stride
    /// shows up as the wrong slots rather than as a nondescript pixel mismatch.
    constexpr int kSlotCount = 5;

    struct SlotLayout
    {
        int width = 0;
        int height = 0;

        [[nodiscard]] float CenterX(int slot) const
        {
            return static_cast<float>(width) *
                   (static_cast<float>(slot) + 0.5f) / static_cast<float>(kSlotCount);
        }
        [[nodiscard]] float Boundary(int slot) const
        {
            return static_cast<float>(width) *
                   static_cast<float>(slot) / static_cast<float>(kSlotCount);
        }
        [[nodiscard]] float HalfWidth() const
        {
            return 0.30f * static_cast<float>(width) / static_cast<float>(kSlotCount);
        }
        [[nodiscard]] float MidY() const { return 0.5f * static_cast<float>(height); }
        [[nodiscard]] float HalfHeight() const { return 0.25f * static_cast<float>(height); }
    };

    /// Identity World/View/Projection is used throughout, so a vertex position *is* its clip-space
    /// position and the viewport transform maps it to the requested window pixel.
    Vector3 ClipAtPixel(const SlotLayout& layout, float pixelX, float pixelY)
    {
        return Vector3(
            (2.0f * pixelX / static_cast<float>(layout.width)) - 1.0f,
            1.0f - (2.0f * pixelY / static_cast<float>(layout.height)),
            0.5f);
    }

    std::array<Vector3, 3> SlotTriangle(const SlotLayout& layout, int slot)
    {
        const float cx = layout.CenterX(slot);
        const float hw = layout.HalfWidth();
        const float midY = layout.MidY();
        const float hh = layout.HalfHeight();
        return {
            ClipAtPixel(layout, cx - hw, midY + hh),
            ClipAtPixel(layout, cx + hw, midY + hh),
            ClipAtPixel(layout, cx, midY - hh),
        };
    }

    /// Distinct per-slot colours. Built per call rather than as a namespace-scope constant: Color
    /// is not a literal type, and a file-scope Color would depend on static-initialisation order.
    std::array<Color, kSlotCount> SlotColors()
    {
        return {Color::Red, Color::Lime, Color::Blue, Color::Yellow, Color::Cyan};
    }

    /// Probe pixel for a slot's triangle: inside it, away from every edge.
    void SlotProbe(const SlotLayout& layout, int slot, int& x, int& y)
    {
        x = static_cast<int>(layout.CenterX(slot));
        y = static_cast<int>(layout.MidY() + layout.HalfHeight() / 3.0f);
    }

    bool HasSameRgb(const Color& left, const Color& right)
    {
        return left.getRProperty() == right.getRProperty() &&
               left.getGProperty() == right.getGProperty() &&
               left.getBProperty() == right.getBProperty();
    }

    struct FrameSnapshot
    {
        int width = 0;
        int height = 0;
        std::vector<Color> pixels;

        [[nodiscard]] Color At(int x, int y) const
        {
            const int cx = x < 0 ? 0 : (x >= width ? width - 1 : x);
            const int cy = y < 0 ? 0 : (y >= height ? height - 1 : y);
            return pixels[static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) +
                          static_cast<std::size_t>(cx)];
        }

        [[nodiscard]] bool HasExactWithin(
            int centerX, int centerY, int radius, const Color& color) const
        {
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx)
                    if (At(centerX + dx, centerY + dy) == color)
                        return true;
            return false;
        }

        [[nodiscard]] int CountLitInColumns(int x0, int x1, const Color& background) const
        {
            const int firstX = x0 < 0 ? 0 : (x0 > width ? width : x0);
            const int lastX = x1 < 0 ? 0 : (x1 > width ? width : x1);
            int total = 0;
            for (int y = 0; y < height; ++y)
                for (int x = firstX; x < lastX; ++x)
                    if (!HasSameRgb(At(x, y), background))
                        ++total;
            return total;
        }
    };

    /// Slots holding rendered geometry, as a comma-separated list. Because each slot is an
    /// exclusive screen region owned by exactly one triangle, the lit slots *are* the vertices a
    /// draw consumed, so a failure message reports the actual range instead of a pixel address.
    std::string DescribeLitSlots(
        const FrameSnapshot& snapshot, const SlotLayout& layout, const Color& background)
    {
        std::string lit;
        for (int slot = 0; slot < kSlotCount; ++slot)
        {
            const int x0 = static_cast<int>(layout.Boundary(slot));
            const int x1 = static_cast<int>(layout.Boundary(slot + 1) + 0.999f);
            if (snapshot.CountLitInColumns(x0, x1, background) > 0)
            {
                if (!lit.empty())
                    lit += ',';
                lit += std::to_string(slot);
            }
        }
        return lit.empty() ? "none" : lit;
    }

    FrameSnapshot CaptureBackbuffer(GraphicsDevice& device, int width, int height)
    {
        FrameSnapshot snapshot;
        snapshot.width = width;
        snapshot.height = height;
        snapshot.pixels.assign(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
            Color::Transparent);
        const Rectangle region(0, 0, width, height);
        device.GetBackBufferData(
            &region, snapshot.pixels.data(), 0, static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    FrameSnapshot CaptureTarget(RenderTarget2D& target, int width, int height)
    {
        FrameSnapshot snapshot;
        snapshot.width = width;
        snapshot.height = height;
        snapshot.pixels.assign(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
            Color::Transparent);
        const Rectangle region(0, 0, width, height);
        target.GetData(
            0, &region, snapshot.pixels.data(), 0, static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    /// The exactly-packed GPU stream for VertexPositionColor. The control for every object-array
    /// case below: a custom POD vertex whose C++ object layout already *is* the stream, which the
    /// raw `const void*` overload must keep consuming byte-for-byte as before.
    struct PackedPositionColor
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(PackedPositionColor) == 16);

    PackedPositionColor Pack(const VertexPositionColor& v)
    {
        return PackedPositionColor{
            v.Position.X, v.Position.Y, v.Position.Z,
            v.Color.getRProperty(), v.Color.getGProperty(),
            v.Color.getBProperty(), v.Color.getAProperty()};
    }

    VertexDeclaration PackedPositionColorDeclaration()
    {
        return VertexDeclaration(
            16,
            {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
    }

    class BuiltInVertexLayoutTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        // GTEST_SKIP() only unwinds the function it is called from; called from an ordinary
        // member function like RequireLayoutRendering() below it cannot skip the test body that
        // invokes it. SetUp() is where GoogleTest itself checks for a skip, so the capability gate
        // has to run here too -- RequireLayoutRendering() keeps its own copy for the state-setup
        // calls that follow it, which only run once SetUp() has already let the test proceed.
        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
        }

        /// Explicit device state for the whole fixture: nothing here may depend on a framework
        /// default, because a default-valued no-op fallback would let a buggy path pass.
        void RequireLayoutRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setScissorRectangleProperty(
                Rectangle(0, 0, BackbufferWidth(), BackbufferHeight()));
        }

        [[nodiscard]] int BackbufferWidth() const
        {
            return device.getViewportProperty().getWidthProperty();
        }
        [[nodiscard]] int BackbufferHeight() const
        {
            return device.getViewportProperty().getHeightProperty();
        }
        [[nodiscard]] SlotLayout BackbufferLayout() const
        {
            return SlotLayout{BackbufferWidth(), BackbufferHeight()};
        }
    };

    /// Every slot in [firstSlot, firstSlot + count) rendered in its own colour, and no slot
    /// outside that range rendered at all.
    void ExpectExactlySlots(
        const FrameSnapshot& snapshot,
        const SlotLayout& layout,
        int firstSlot,
        int count,
        const char* label,
        bool checkColors)
    {
        std::string expected;
        for (int i = 0; i < count; ++i)
        {
            if (!expected.empty())
                expected += ',';
            expected += std::to_string(firstSlot + i);
        }
        EXPECT_EQ(expected, DescribeLitSlots(snapshot, layout, Color::Black))
            << label << ": a different vertex range reached the rasterizer";

        if (!checkColors)
            return;
        const std::array<Color, kSlotCount> colors = SlotColors();
        for (int i = 0; i < count; ++i)
        {
            const int slot = firstSlot + i;
            int px = 0;
            int py = 0;
            SlotProbe(layout, slot, px, py);
            EXPECT_TRUE(snapshot.HasExactWithin(px, py, 2, colors[static_cast<std::size_t>(slot)]))
                << label << ": slot " << slot
                << " did not carry its own vertex colour (probe " << px << ',' << py << ')';
        }
    }
}

// =========================================================================================
// Section A — the public layout contract (no rendering, every renderer)
// =========================================================================================

TEST(BuiltInVertexLayout, DeclaredStrideIsThePackedGpuStride)
{
    for (const BuiltInLayout& row : BuiltInLayouts())
    {
        EXPECT_EQ(row.gpuStride, row.declaration->getVertexStrideProperty())
            << row.name
            << ": VertexStride must be the packed GPU stream stride, not the C++ object size "
            << row.objectSize;
    }
}

TEST(BuiltInVertexLayout, DeclaredStrideEqualsItsOwnElementExtent)
{
    for (const BuiltInLayout& row : BuiltInLayouts())
    {
        EXPECT_EQ(PackedElementExtent(*row.declaration),
                  row.declaration->getVertexStrideProperty())
            << row.name << ": the stride must be derived from the element layout itself";
    }
}

TEST(BuiltInVertexLayout, EveryElementFitsInsideTheDeclaredStride)
{
    for (const BuiltInLayout& row : BuiltInLayouts())
    {
        const int stride = row.declaration->getVertexStrideProperty();
        for (const VertexElement& element : row.declaration->GetVertexElements())
        {
            const int offset = element.getOffsetProperty();
            const int size = FormatSize(element.getVertexElementFormatProperty());
            EXPECT_GE(offset, 0) << row.name;
            EXPECT_GT(size, 0) << row.name;
            EXPECT_LE(offset + size, stride)
                << row.name << ": element at offset " << offset << " (size " << size
                << ") leaves the declared stride " << stride;
        }
    }
}

// The reason a stride-only correction would be a half-fix: the C++ objects are not the stream.
TEST(BuiltInVertexLayout, ObjectSizeIsLargerThanTheGpuStride)
{
    for (const BuiltInLayout& row : BuiltInLayouts())
    {
        EXPECT_GT(row.objectSize, static_cast<std::size_t>(row.gpuStride))
            << row.name
            << ": this type is expected to carry non-stream object-model bytes (vtable/padding)";
    }
}

TEST(BuiltInVertexLayout, ObjectMemberOffsetsDoNotMatchTheDeclaredElementOffsets)
{
    for (const BuiltInLayout& row : BuiltInLayouts())
    {
        const auto& elements = row.declaration->GetVertexElements();
        ASSERT_EQ(elements.size(), row.memberOffsets.size()) << row.name;
        bool anyDifferent = false;
        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            if (static_cast<std::ptrdiff_t>(elements[i].getOffsetProperty()) !=
                row.memberOffsets[i])
            {
                anyDifferent = true;
            }
        }
        EXPECT_TRUE(anyDifferent)
            << row.name
            << ": if the object offsets ever equalled the stream offsets this type would no "
               "longer need packing -- revisit the DrawUser object path before relaxing this";
    }
}

// =========================================================================================
// Section B — the public DrawUser paths, on the renderers that raster and read back
// =========================================================================================


// The core reproduction. An array of the built-in objects plus that same type's own public
// static declaration is the most ordinary thing a game can write, and it must render the exact
// requested slots in their own vertex colours.
TEST_F(BuiltInVertexLayoutTest, PositionColorObjectsWithTheirOwnStaticDeclarationRender)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();
    const std::array<Color, kSlotCount> colors = SlotColors();

    std::vector<VertexPositionColor> vertices;
    for (int slot = 0; slot < kSlotCount; ++slot)
        for (const Vector3& corner : SlotTriangle(layout, slot))
            vertices.emplace_back(corner, colors[static_cast<std::size_t>(slot)]);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.Apply();
    device.SetVertexBuffer(nullptr);
    device.Clear(Color::Black);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 3, 3,
        VertexPositionColor::getVertexDeclarationStatic());

    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 1, 3,
        "VertexPositionColor objects + static declaration", true);
}

// The same source values in an exactly-packed custom POD vertex through the raw `const void*`
// overload. This is the control: that overload's contract is unchanged, so it must produce the
// identical frame -- and it fails if the fix ever packs an already-packed stream a second time.
TEST_F(BuiltInVertexLayoutTest, PackedPodControlMatchesTheObjectArrayResult)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();
    const std::array<Color, kSlotCount> colors = SlotColors();

    std::vector<PackedPositionColor> packed;
    for (int slot = 0; slot < kSlotCount; ++slot)
        for (const Vector3& corner : SlotTriangle(layout, slot))
            packed.push_back(Pack(VertexPositionColor(corner, colors[static_cast<std::size_t>(slot)])));

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.Apply();
    device.SetVertexBuffer(nullptr);
    device.Clear(Color::Black);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, static_cast<const void*>(packed.data()), 3, 3,
        PackedPositionColorDeclaration());

    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 1, 3,
        "packed POD control", true);
}

// The inferred-declaration overload and the explicit-declaration overload are two spellings of
// the same draw and owe the same frame.
TEST_F(BuiltInVertexLayoutTest, InferredAndExplicitDeclarationsAgree)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();
    const std::array<Color, kSlotCount> colors = SlotColors();

    std::vector<VertexPositionColor> vertices;
    for (int slot = 0; slot < kSlotCount; ++slot)
        for (const Vector3& corner : SlotTriangle(layout, slot))
            vertices.emplace_back(corner, colors[static_cast<std::size_t>(slot)]);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.SetVertexBuffer(nullptr);

    effect.Apply();
    device.Clear(Color::Black);
    device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 3, 3);
    const FrameSnapshot inferred = CaptureBackbuffer(device, layout.width, layout.height);

    effect.Apply();
    device.Clear(Color::Black);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 3, 3,
        VertexPositionColor::getVertexDeclarationStatic());
    const FrameSnapshot explicitDecl = CaptureBackbuffer(device, layout.width, layout.height);

    ExpectExactlySlots(inferred, layout, 1, 3, "inferred declaration", true);
    ExpectExactlySlots(explicitDecl, layout, 1, 3, "explicit static declaration", true);
    EXPECT_EQ(inferred.pixels, explicitDecl.pixels)
        << "the inferred and explicit declarations produced different frames";
}

// Indexed, 16-bit, with a nonzero source vertex offset and a nonzero startIndex: the two offsets
// are applied in different units and neither may be folded into the other.
TEST_F(BuiltInVertexLayoutTest, ExplicitDeclarationIndexed16HonorsBothOffsets)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();
    const std::array<Color, kSlotCount> colors = SlotColors();

    std::vector<VertexPositionColor> vertices;
    for (int slot = 0; slot < kSlotCount; ++slot)
        for (const Vector3& corner : SlotTriangle(layout, slot))
            vertices.emplace_back(corner, colors[static_cast<std::size_t>(slot)]);

    // Rebased at vertex 3 (slot 1), so local indices 3..5 name slot 2. The three leading decoy
    // indices are skipped by indexOffset 3.
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 3, 4, 5};

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.Apply();
    device.SetVertexBuffer(nullptr);
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 3, 6, indices.data(), 3, 1,
        VertexPositionColor::getVertexDeclarationStatic());

    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 2, 1,
        "explicit declaration, 16-bit indices", true);
}

TEST_F(BuiltInVertexLayoutTest, ExplicitDeclarationIndexed32HonorsBothOffsets)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();
    const std::array<Color, kSlotCount> colors = SlotColors();

    std::vector<VertexPositionColor> vertices;
    for (int slot = 0; slot < kSlotCount; ++slot)
        for (const Vector3& corner : SlotTriangle(layout, slot))
            vertices.emplace_back(corner, colors[static_cast<std::size_t>(slot)]);

    const std::array<std::uint32_t, 6> indices{0, 1, 2, 3, 4, 5};

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.Apply();
    device.SetVertexBuffer(nullptr);
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 3, 6, indices.data(), 3, 1,
        VertexPositionColor::getVertexDeclarationStatic());

    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 2, 1,
        "explicit declaration, 32-bit indices", true);
}

// A second, wider layout family: three elements and a 24-byte stream whose colour element sits
// between two float channels, so a shifted element is visible in the colour as well as the
// position.
TEST_F(BuiltInVertexLayoutTest, PositionColorTextureObjectsWithTheirOwnStaticDeclarationRender)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();
    const std::array<Color, kSlotCount> colors = SlotColors();

    Texture2D white(device, 2, 2);
    const std::array<Color, 4> whitePixels{
        Color::White, Color::White, Color::White, Color::White};
    white.SetData(whitePixels.data(), 4);

    std::vector<VertexPositionColorTexture> vertices;
    for (int slot = 0; slot < kSlotCount; ++slot)
        for (const Vector3& corner : SlotTriangle(layout, slot))
            vertices.emplace_back(
                corner, colors[static_cast<std::size_t>(slot)], Vector2(0.5f, 0.5f));

    device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    BasicEffect effect(device);
    effect.setTextureEnabledProperty(true);
    effect.setTextureProperty(&white);
    effect.VertexColorEnabled = true;
    effect.Apply();
    device.SetVertexBuffer(nullptr);
    device.Clear(Color::Black);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 3, 3,
        VertexPositionColorTexture::getVertexDeclarationStatic());

    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 1, 3,
        "VertexPositionColorTexture objects + static declaration", true);
}

// A layout with no colour channel at all: every position sits behind the vtable in the object,
// so the whole frame depends on the object -> stream conversion being applied.
TEST_F(BuiltInVertexLayoutTest, PositionTextureObjectsWithTheirOwnStaticDeclarationRender)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();

    Texture2D white(device, 2, 2);
    const std::array<Color, 4> whitePixels{
        Color::White, Color::White, Color::White, Color::White};
    white.SetData(whitePixels.data(), 4);

    std::vector<VertexPositionTexture> vertices;
    for (int slot = 0; slot < kSlotCount; ++slot)
        for (const Vector3& corner : SlotTriangle(layout, slot))
            vertices.emplace_back(corner, Vector2(0.5f, 0.5f));

    device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    BasicEffect effect(device);
    effect.setTextureEnabledProperty(true);
    effect.setTextureProperty(&white);
    effect.Apply();
    device.SetVertexBuffer(nullptr);
    device.Clear(Color::Black);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 3, 3,
        VertexPositionTexture::getVertexDeclarationStatic());

    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 1, 3,
        "VertexPositionTexture objects + static declaration", false);
}

TEST_F(BuiltInVertexLayoutTest, PositionNormalTextureObjectsWithTheirOwnStaticDeclarationRender)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();

    Texture2D white(device, 2, 2);
    const std::array<Color, 4> whitePixels{
        Color::White, Color::White, Color::White, Color::White};
    white.SetData(whitePixels.data(), 4);

    std::vector<VertexPositionNormalTexture> vertices;
    for (int slot = 0; slot < kSlotCount; ++slot)
        for (const Vector3& corner : SlotTriangle(layout, slot))
            vertices.emplace_back(corner, Vector3(0.0f, 0.0f, 1.0f), Vector2(0.5f, 0.5f));

    // A normal-bearing vertex belongs to the lit layout: the position/normal/texcoord stream is
    // the one every renderer exposes with lighting on, and D3D9 exposes it only that way. Ambient
    // alone keeps the lit colour independent of any light direction.
    device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    BasicEffect effect(device);
    effect.setTextureEnabledProperty(true);
    effect.setTextureProperty(&white);
    effect.setLightingEnabledProperty(true);
    effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    effect.Apply();
    device.SetVertexBuffer(nullptr);
    device.Clear(Color::Black);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 3, 3,
        VertexPositionNormalTexture::getVertexDeclarationStatic());

    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 1, 3,
        "VertexPositionNormalTexture objects + static declaration", false);
}

// The three remaining multi-element families through the indexed explicit-declaration path, each
// with a nonzero source vertex offset and a nonzero startIndex. A stride error in any of them
// lands somewhere other than the requested slot, and the three strides (20, 24, 32) differ, so a
// single hardcoded stride cannot satisfy all three.
TEST_F(BuiltInVertexLayoutTest, ExplicitDeclarationIndexedRendersEveryTexturedFamily)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();
    const std::array<Color, kSlotCount> colors = SlotColors();

    Texture2D white(device, 2, 2);
    const std::array<Color, 4> whitePixels{
        Color::White, Color::White, Color::White, Color::White};
    white.SetData(whitePixels.data(), 4);

    std::vector<VertexPositionTexture> positionTexture;
    std::vector<VertexPositionColorTexture> positionColorTexture;
    std::vector<VertexPositionNormalTexture> positionNormalTexture;
    for (int slot = 0; slot < kSlotCount; ++slot)
    {
        for (const Vector3& corner : SlotTriangle(layout, slot))
        {
            positionTexture.emplace_back(corner, Vector2(0.5f, 0.5f));
            positionColorTexture.emplace_back(
                corner, colors[static_cast<std::size_t>(slot)], Vector2(0.5f, 0.5f));
            positionNormalTexture.emplace_back(
                corner, Vector3(0.0f, 0.0f, 1.0f), Vector2(0.5f, 0.5f));
        }
    }

    // Rebased at vertex 3 (slot 1); indexOffset 3 skips three decoy indices, so local 3,4,5 name
    // slot 2. Both offsets are nonzero and carried in different units.
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 3, 4, 5};

    device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    BasicEffect effect(device);
    effect.setTextureEnabledProperty(true);
    effect.setTextureProperty(&white);
    device.SetVertexBuffer(nullptr);

    effect.Apply();
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, positionTexture.data(), 3, 6, indices.data(), 3, 1,
        VertexPositionTexture::getVertexDeclarationStatic());
    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 2, 1,
        "VertexPositionTexture indexed, stride 20", false);

    effect.VertexColorEnabled = true;
    effect.Apply();
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, positionColorTexture.data(), 3, 6, indices.data(), 3, 1,
        VertexPositionColorTexture::getVertexDeclarationStatic());
    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 2, 1,
        "VertexPositionColorTexture indexed, stride 24", true);

    // The position/normal/texcoord stream is the lit layout on every renderer, and D3D9 exposes it
    // only with lighting enabled; ambient alone keeps the result light-direction independent.
    effect.VertexColorEnabled = false;
    effect.setLightingEnabledProperty(true);
    effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    effect.Apply();
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, positionNormalTexture.data(), 3, 6, indices.data(), 3, 1,
        VertexPositionNormalTexture::getVertexDeclarationStatic());
    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 2, 1,
        "VertexPositionNormalTexture indexed, stride 32", false);
}

// The indexed counterpart of the packed-POD control: the raw overload's contract is that the
// caller already supplied a stream, and it must still consume one byte-for-byte.
TEST_F(BuiltInVertexLayoutTest, PackedPodControlMatchesTheObjectArrayResultWhenIndexed)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();
    const std::array<Color, kSlotCount> colors = SlotColors();

    std::vector<VertexPositionColor> vertices;
    std::vector<PackedPositionColor> packed;
    for (int slot = 0; slot < kSlotCount; ++slot)
    {
        for (const Vector3& corner : SlotTriangle(layout, slot))
        {
            vertices.emplace_back(corner, colors[static_cast<std::size_t>(slot)]);
            packed.push_back(Pack(vertices.back()));
        }
    }
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 3, 4, 5};

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.SetVertexBuffer(nullptr);

    effect.Apply();
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 3, 6, indices.data(), 3, 1,
        VertexPositionColor::getVertexDeclarationStatic());
    const FrameSnapshot fromObjects = CaptureBackbuffer(device, layout.width, layout.height);

    effect.Apply();
    device.Clear(Color::Black);
    device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, static_cast<const void*>(packed.data()), 3, 6,
        indices.data(), 3, 1, PackedPositionColorDeclaration());
    const FrameSnapshot fromStream = CaptureBackbuffer(device, layout.width, layout.height);

    ExpectExactlySlots(fromObjects, layout, 2, 1, "indexed object array", true);
    ExpectExactlySlots(fromStream, layout, 2, 1, "indexed packed POD control", true);
    EXPECT_EQ(fromObjects.pixels, fromStream.pixels)
        << "the object array and the equivalent packed stream produced different frames";
}

// VertexBuffer was never affected -- it repacks through its typed SetData overloads and uploads
// its own stride -- so correcting the declaration must leave it byte-for-byte where it was.
TEST_F(BuiltInVertexLayoutTest, VertexBufferPathIsUnchangedByTheCorrectedDeclaration)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();
    const std::array<Color, kSlotCount> colors = SlotColors();

    std::vector<VertexPositionColor> vertices;
    for (int slot = 0; slot < kSlotCount; ++slot)
        for (const Vector3& corner : SlotTriangle(layout, slot))
            vertices.emplace_back(corner, colors[static_cast<std::size_t>(slot)]);
    const int vertexCount = static_cast<int>(vertices.size());

    VertexBuffer buffer(
        device, VertexPositionColor::getVertexDeclarationStatic(), vertexCount,
        BufferUsage::None);
    buffer.SetData(vertices.data(), vertexCount);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.Apply();
    device.Clear(Color::Black);
    device.SetVertexBuffer(&buffer);
    device.DrawPrimitives(PrimitiveType::TriangleList, 3, 3);

    ExpectExactlySlots(
        CaptureBackbuffer(device, layout.width, layout.height), layout, 1, 3,
        "VertexBuffer + static declaration", true);
    device.SetVertexBuffer(nullptr);
}


// =========================================================================================
// Section C — SDL_GPU, whose only exact-pixel oracle is a render target
// =========================================================================================

TEST_F(BuiltInVertexLayoutTest, SdlGpuObjectArrayWithStaticDeclarationRendersIntoATarget)
{
    // plan_runtimerenderer.md RTR-P9-5: the renderers that raster and read back. This was a
    // compile-time gate around all of section B, so on every other renderer these tests did not
    // exist and reported nothing. It must sit in the test BODY: GTEST_SKIP() returns from the
    // function it appears in, so a skip inside a shared helper marks the test skipped and then
    // lets the caller run on to fail anyway.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2,
                                    WebGPU, Vulkan, DirectX9, DirectX11, Software);
    // plan_runtimerenderer.md RTR-P9-5: SDL_GPU's own render-target oracle.
    CNA_SKIP_IF_RENDERER_IS_NOT(SdlGpu);
    RequireLayoutRendering();
    const SlotLayout layout = BackbufferLayout();
    const std::array<Color, kSlotCount> colors = SlotColors();

    std::vector<VertexPositionColor> vertices;
    for (int slot = 0; slot < kSlotCount; ++slot)
        for (const Vector3& corner : SlotTriangle(layout, slot))
            vertices.emplace_back(corner, colors[static_cast<std::size_t>(slot)]);

    RenderTarget2D target(
        device, layout.width, layout.height, false, SurfaceFormat::Color,
        DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    device.SetVertexBuffer(nullptr);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, vertices.data(), 3, 3,
        VertexPositionColor::getVertexDeclarationStatic());
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectExactlySlots(
        CaptureTarget(target, layout.width, layout.height), layout, 1, 3,
        "SDL_GPU object array + static declaration", true);
}


// =========================================================================================
// Section D — declaration and range validation (device only, every renderer)
// =========================================================================================

namespace
{
    class BuiltInVertexValidationTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;
        BasicEffect effect{device};

        std::vector<VertexPositionColor> vertices{
            VertexPositionColor(Vector3(0.0f, 0.0f, 0.0f), Color::Red),
            VertexPositionColor(Vector3(1.0f, 0.0f, 0.0f), Color::Lime),
            VertexPositionColor(Vector3(0.0f, 1.0f, 0.0f), Color::Blue),
        };
        std::array<std::uint16_t, 3> indices16{0, 1, 2};
        std::array<std::uint32_t, 3> indices32{0, 1, 2};

        void SetUp() override
        {
            // DrawUserPrimitives/DrawUserIndexedPrimitives are inherently 3D-pipeline entry
            // points -- a renderer that honestly reports no 3D pipeline rejects them before ever
            // reaching the declaration/range validation this fixture exercises.
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
            effect.Apply();
        }
    };
}

// An element that reaches past its own declared stride would make the renderer read the next
// vertex's bytes. Rejected deterministically, before anything is uploaded.
TEST_F(BuiltInVertexValidationTest, ElementBeyondTheDeclaredStrideIsRejected)
{
    const VertexDeclaration tooSmall(
        14,
        {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    EXPECT_THROW(
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()), 0, 1,
            tooSmall),
        System::ArgumentException);
    EXPECT_THROW(
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()), 0, 3,
            indices16.data(), 0, 1, tooSmall),
        System::ArgumentException);
    EXPECT_THROW(
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()), 0, 3,
            indices32.data(), 0, 1, tooSmall),
        System::ArgumentException);
}

// A default-constructed VertexDeclaration describes nothing at all and has stride zero. It can
// never be a source layout, so a draw that is handed one is rejected instead of uploading at
// stride zero.
TEST_F(BuiltInVertexValidationTest, EmptyDeclarationIsRejected)
{
    const VertexDeclaration empty;
    EXPECT_THROW(
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()), 0, 1, empty),
        System::ArgumentException);
    EXPECT_THROW(
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()), 0, 3,
            indices16.data(), 0, 1, empty),
        System::ArgumentException);
}

// A negative source offset or vertex count can only produce an out-of-range read.
TEST_F(BuiltInVertexValidationTest, NegativeSourceRangeIsRejected)
{
    const VertexDeclaration declaration = PackedPositionColorDeclaration();
    EXPECT_THROW(
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()), -1, 1,
            declaration),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()), 0, -1,
            indices16.data(), 0, 1, declaration),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()), 0, 3,
            indices16.data(), -1, 1, declaration),
        System::ArgumentOutOfRangeException);
}

// vertexOffset * stride and vertexCount * stride are computed in 64 bits: a range that would
// wrap a 32-bit byte count is rejected rather than silently addressing a small offset.
TEST_F(BuiltInVertexValidationTest, SourceByteRangeOverflowIsRejected)
{
    const VertexDeclaration declaration = PackedPositionColorDeclaration();
    EXPECT_THROW(
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()),
            0x7FFFFFF0, 1, declaration),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, static_cast<const void*>(vertices.data()),
            0, 0x7FFFFFF0, indices16.data(), 0, 1, declaration),
        System::ArgumentOutOfRangeException);
}

// A built-in object array can only be converted to the stream its own fields describe, so a
// declaration claiming a different stride is refused rather than silently reinterpreted.
TEST_F(BuiltInVertexValidationTest, TypedObjectArrayRejectsAForeignStride)
{
    const VertexDeclaration foreign(
        20,
        {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
    EXPECT_THROW(
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1, foreign),
        System::ArgumentException);
    EXPECT_THROW(
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, vertices.data(), 0, 3, indices16.data(), 0, 1, foreign),
        System::ArgumentException);
}

// The declaration a built-in type publishes is exactly the stride its packed stream occupies, so
// a raw upload of that stream through a buffer carrying the same declaration is accepted -- and
// a stream at any other stride is not.
TEST_F(BuiltInVertexValidationTest, RawUploadMatchesTheBuiltInStaticDeclarationStride)
{
    if (!device.SupportsCapability(GraphicsCapability::ThreeD))
        GTEST_SKIP() << "Renderer explicitly does not support vertex buffers";
    VertexBuffer buffer(
        device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
    std::array<PackedPositionColor, 3> packed{};
    for (std::size_t i = 0; i < packed.size(); ++i)
        packed[i] = Pack(vertices[i]);

    EXPECT_NO_THROW(buffer.SetDataRaw(packed.data(), 3, 16));
    EXPECT_THROW(buffer.SetDataRaw(packed.data(), 3, 24), System::ArgumentException);
}

// =========================================================================================
// Section E — the remaining built-in types, which have no rendering path of their own
// =========================================================================================
//
// VertexPositionNormalTextureSkinned has a typed VertexBuffer::SetData, so its own stream is
// reachable and round-trippable. VertexPositionNormalTangentTexture and
// VertexPositionNormalTangentTextureSkinned have neither a typed SetData nor a typed DrawUser
// overload: they carry no object-to-stream conversion at all, which is an explicit capability
// boundary rather than an oversight. All three are streamable only through a caller-packed raw
// upload, and the raw upload path is exactly the one that demands the declared stride and the
// upload stride agree -- so before this correction none of the seven built-in declarations could
// be used with it.

// Every built-in type, including the three with no rendering path: a stream packed at the
// declared stride is accepted through the buffer's own declaration, and one at any other stride
// is not. This is the whole-inventory proof that the declared stride *is* the stream stride.
TEST_F(BuiltInVertexValidationTest, EveryBuiltInDeclarationAcceptsARawUploadAtItsOwnStride)
{
    if (!device.SupportsCapability(GraphicsCapability::ThreeD))
        GTEST_SKIP() << "Renderer explicitly does not support vertex buffers";
    for (const BuiltInLayout& row : BuiltInLayouts())
    {
        const int stride = row.declaration->getVertexStrideProperty();
        ASSERT_EQ(row.gpuStride, stride) << row.name;

        // Three vertices of the exact declared stride, with a distinct byte pattern per vertex so
        // an accepted upload is genuinely the caller's stream and not a zero-filled placeholder.
        std::vector<std::uint8_t> stream(static_cast<std::size_t>(stride) * 3);
        for (std::size_t i = 0; i < stream.size(); ++i)
            stream[i] = static_cast<std::uint8_t>(i & 0xFF);

        VertexBuffer buffer(device, *row.declaration, 3, BufferUsage::None);
        EXPECT_NO_THROW(buffer.SetDataRaw(stream.data(), 3, stride))
            << row.name << ": a stream at the declared stride " << stride << " was refused";
        EXPECT_THROW(buffer.SetDataRaw(stream.data(), 3, stride + 4), System::ArgumentException)
            << row.name << ": a stream at a foreign stride was accepted";
        EXPECT_THROW(buffer.SetDataRaw(stream.data(), 3, stride - 4), System::ArgumentException)
            << row.name << ": a stream at a foreign stride was accepted";
    }
}

// The skinned type's own values, through its typed SetData and back out through GetData. Its
// stream carries every element family the built-in set uses -- float3, float2, float4 and a
// packed byte4 -- so a shifted element in the 52-byte layout shows up as a wrong value rather
// than as a size mismatch.
TEST_F(BuiltInVertexValidationTest, SkinnedVertexBufferRoundTripsEveryElementFamily)
{
    if (!device.SupportsCapability(GraphicsCapability::ThreeD))
        GTEST_SKIP() << "Renderer explicitly does not support vertex buffers";
    const std::array<VertexPositionNormalTextureSkinned, 2> source{
        VertexPositionNormalTextureSkinned(
            Vector3(1.0f, 2.0f, 3.0f), Vector3(0.0f, 1.0f, 0.0f), Vector2(0.25f, 0.75f),
            Vector4(0.5f, 0.25f, 0.125f, 0.0625f), std::array<std::uint8_t, 4>{1, 2, 3, 4}),
        VertexPositionNormalTextureSkinned(
            Vector3(4.0f, 5.0f, 6.0f), Vector3(1.0f, 0.0f, 0.0f), Vector2(0.5f, 0.125f),
            Vector4(1.0f, 2.0f, 3.0f, 4.0f), std::array<std::uint8_t, 4>{5, 6, 7, 8})};

    VertexBuffer buffer(
        device, VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 2,
        BufferUsage::None);
    buffer.SetData(source.data(), 2);

    std::array<VertexPositionNormalTextureSkinned, 2> readBack{};
    buffer.GetData(readBack.data(), 2);

    for (std::size_t i = 0; i < source.size(); ++i)
    {
        EXPECT_EQ(source[i].Position, readBack[i].Position) << "vertex " << i;
        EXPECT_EQ(source[i].Normal, readBack[i].Normal) << "vertex " << i;
        EXPECT_EQ(source[i].TextureCoordinate, readBack[i].TextureCoordinate) << "vertex " << i;
        EXPECT_EQ(source[i].BlendWeight, readBack[i].BlendWeight) << "vertex " << i;
        EXPECT_EQ(source[i].BlendIndices, readBack[i].BlendIndices) << "vertex " << i;
    }
}
