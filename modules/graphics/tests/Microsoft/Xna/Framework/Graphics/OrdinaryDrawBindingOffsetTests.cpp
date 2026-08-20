// SPDX-License-Identifier: MS-PL
// REMED-GFX-200: the public VertexBufferBinding.VertexOffset contract on the ORDINARY
// (non-instanced) draw routes.
//
// XNA/FNA define VertexBufferBinding(vertexBuffer, vertexOffset, instanceFrequency) as "bind this
// buffer so that its first usable record is element `vertexOffset`". The reconciled CNA contract
// this file locks down is therefore:
//
//   VertexOffset is a vertex-ELEMENT offset, never a byte offset; the stream converts it with its
//       OWN VertexDeclaration stride, exactly once, at the native binding
//   it shifts the stream base, so it adds to baseVertex on the indexed route and to vertexStart on
//       the non-indexed one -- each applied exactly ONCE, never twice
//   startIndex still selects index elements, never vertices, and the offset never displaces it
//   binding 0 is the per-vertex geometry stream on the ordinary routes; no later binding ever
//       supplies that stream's offset
//   the ordinary and instanced routes agree: the same binding offset selects the same records
//   a range that leaves the bound buffer once the offset is included is rejected before native
//       submission, never silently clamped
//
// Fixture geometry -- a two-axis oracle. The target is divided into `kSlotCount` equal-width
// vertical slots and `kBandCount` equal-height horizontal bands.
//
//   * the COLUMN axis says WHICH vertex records the draw consumed
//   * the BAND axis says WHICH BYTES those records were
//
// The band axis exists because a dropped offset and a wrong stream both move the column, and only
// the band tells them apart. Every buffer authors its live geometry in a band of its own:
//
//   band 0   the mesh buffer's live geometry, one triangle per slot
//   band 1   the mesh buffer's geometry after a mid-sequence SetData rewrite
//   band 2   a second bound stream's geometry
//   band 3   the mesh buffer's three-element PREFIX decoy, authored in slot 6
//
// Every buffer begins with the same three-element prefix decoy, so a binding offset of
// `kVerticesPerSlot` is exactly "skip the decoy". Each defect therefore lights a different cell
// instead of accidentally reproducing the expected pixels:
//
//   * an ignored VertexOffset          -> the prefix decoy in slot 6 band 3, or (once startIndex/
//                                         baseVertex is nonzero) the slot one offset-width left
//   * a byte-for-element multiplication -> `offset * stride` elements past the records, which is
//                                         outside the buffer, so NOTHING lights
//   * baseVertex applied twice          -> slot 6 band 0
//   * startIndex applied twice          -> slot 6 band 0
//   * the wrong stream's data           -> band 2
//   * a stale copy after a buffer update -> band 0 where band 1 was written
//   * a stale previous binding offset   -> the previous leg's slot
//
// Renderer scope. The render-target group is the portable one: it runs on every renderer that
// rasterizes 3D triangles and implements RenderTarget2D::GetData, which includes SDL_GPU (whose
// only exact-pixel oracle that is -- see REMED-GFX-117). The backbuffer group additionally needs
// GetBackBufferData and therefore carries the renderer set REMED-GFX-113 established. The
// ordinary -> instanced -> ordinary transition additionally needs a working instanced route, so it
// carries REMED-GFX-118's instanced suite set, and asserts the instanced leg's OWN binding offset
// only on the renderers whose instanced path consumes it (EasyGL via REMED-GFX-122, D3D11/D3D12 via
// REMED-GFX-123) -- whether the others honour it there is REMED-GFX-122/123's question, not this
// file's.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the compile-time guards it replaced.
using namespace CNA::Testing::Renderers;

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

// The portable pixel oracle: rasterizes 3D triangles and implements RenderTarget2D::GetData.
/// plans/plan_runtimerenderer.md RTR-P9-5: the same renderer set, evaluated at runtime so this
/// describes the ACTIVE renderer rather than the build default.
[[nodiscard]] inline bool OrdinaryBindingOffset()
{
    return CNA_RENDERER_IS(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, 
                            DirectX12, Software, SdlGpu);
}

// REMED-GFX-113's renderer set: the above, minus the renderers without backbuffer readback.
/// plans/plan_runtimerenderer.md RTR-P9-5: the same renderer set, evaluated at runtime so this
/// describes the ACTIVE renderer rather than the build default.
[[nodiscard]] inline bool OrdinaryBindingOffsetBackbuffer()
{
    return CNA_RENDERER_IS(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, 
                            Software);
}

// REMED-GFX-118's instanced suite set: the renderers whose instanced route renders the geometry.
/// plans/plan_runtimerenderer.md RTR-P9-5: the same renderer set, evaluated at runtime so this describes
/// the ACTIVE renderer rather than the build default.
[[nodiscard]] inline bool OrdinaryBindingOffsetInstancedTransition()
{
    return CNA_RENDERER_IS(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan,
                           DirectX9, DirectX11, DirectX12);
}

// The renderers whose INSTANCED route consumes VertexBufferBinding.VertexOffset.
/// plans/plan_runtimerenderer.md RTR-P9-5: the same renderer set, evaluated at runtime so this
/// describes the ACTIVE renderer rather than the build default.
[[nodiscard]] inline bool OrdinaryBindingOffsetInstancedOffset()
{
    return CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX11, DirectX12);
}

namespace
{
    /// Geometry axis: equal-width vertical slots, one triangle each. Seven leaves room for a decoy
    /// prefix, an intended interior slot and a decoy suffix in every case below.
    constexpr int kSlotCount = 7;

    /// Provenance axis: equal-height horizontal bands, one per distinguishable set of bytes.
    constexpr int kBandCount = 4;

    /// Vertices one slot's triangle owns.
    constexpr int kVerticesPerSlot = 3;

    /// Records the live geometry owns, one triangle per slot.
    constexpr int kMeshElementCount = kSlotCount * kVerticesPerSlot;

    /// Records a complete buffer owns: the prefix decoy plus the live geometry.
    constexpr int kBufferElementCount = kVerticesPerSlot + kMeshElementCount;

    /// The binding offset that means "skip the prefix decoy".
    constexpr int kPrefixOffset = kVerticesPerSlot;

    /// The slot the prefix decoy occupies, chosen at the far end so an ignored offset cannot be
    /// confused with an off-by-one in the requested slot.
    constexpr int kPrefixSlot = kSlotCount - 1;

    /// The band the prefix decoy occupies.
    constexpr int kPrefixBand = 3;

    /// The band the mesh buffer's live geometry occupies.
    constexpr int kLiveBand = 0;

    /// The band a mid-sequence SetData rewrite moves the live geometry to.
    constexpr int kRewrittenBand = 1;

    /// The band the second bound stream's geometry occupies.
    constexpr int kOtherStreamBand = 2;

    /// Square render target, sized so both axes divide exactly: 224 = 7 * 32 = 4 * 56.
    constexpr int kTargetSize = 224;

    VertexDeclaration PositionColorDeclaration()
    {
        return VertexDeclaration(
            16,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(
                    12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
    }

    /// The per-instance stream used only by the ordinary -> instanced -> ordinary transition: one
    /// 4x4 column-major world matrix, the layout every CNA instanced renderer expects.
    VertexDeclaration InstanceMatrixDeclaration()
    {
        return VertexDeclaration(
            64,
            {
                VertexElement(
                    0, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 1),
                VertexElement(
                    16, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2),
                VertexElement(
                    32, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 3),
                VertexElement(
                    48, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 4),
            });
    }

    struct InstanceMatrix
    {
        float col0[4];
        float col1[4];
        float col2[4];
        float col3[4];
    };

    /// The identity: the transition test needs an instance stream that exists and is consumed, not
    /// one that moves geometry. Identity is also its own transpose, so the bgfx Vulkan renderer's
    /// transposed per-instance matrix (REMED-GFX-121) cannot perturb this fixture.
    InstanceMatrix IdentityInstance()
    {
        return InstanceMatrix{
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f},
        };
    }

    /// Target geometry: each slot's own centre column and each band's own centre row.
    struct GridLayout
    {
        int width = 0;
        int height = 0;

        [[nodiscard]] float ColumnCenterX(int slot) const
        {
            return static_cast<float>(width) *
                   (static_cast<float>(slot) + 0.5f) / static_cast<float>(kSlotCount);
        }

        /// Exclusive slot boundary: everything a slot-`slot` triangle can touch lies strictly
        /// inside (ColumnBoundaryX(slot), ColumnBoundaryX(slot + 1)).
        [[nodiscard]] float ColumnBoundaryX(int slot) const
        {
            return static_cast<float>(width) *
                   static_cast<float>(slot) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float RowCenterY(int band) const
        {
            return static_cast<float>(height) *
                   (static_cast<float>(band) + 0.5f) / static_cast<float>(kBandCount);
        }

        [[nodiscard]] float RowBoundaryY(int band) const
        {
            return static_cast<float>(height) *
                   static_cast<float>(band) / static_cast<float>(kBandCount);
        }

        [[nodiscard]] float HalfWidth() const
        {
            return 0.30f * static_cast<float>(width) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float HalfHeight() const
        {
            return 0.30f * static_cast<float>(height) / static_cast<float>(kBandCount);
        }
    };

    /// Identity World/View/Projection is used throughout, so a vertex position *is* its clip-space
    /// position and the viewport transform maps it to the requested target pixel.
    VertexPositionColor VertexAtPixel(
        const GridLayout& layout, float pixelX, float pixelY)
    {
        const float x = (2.0f * pixelX / static_cast<float>(layout.width)) - 1.0f;
        const float y = 1.0f - (2.0f * pixelY / static_cast<float>(layout.height));
        return VertexPositionColor(Vector3(x, y, 0.5f), Color::White);
    }

    /// One slot's triangle, authored inside one band. Colour carries no information in this
    /// fixture -- the two position axes do -- so a renderer whose stock shader colours from
    /// DiffuseColor and one that colours from the vertex stream produce the same pixels.
    std::array<VertexPositionColor, kVerticesPerSlot> SlotTriangle(
        const GridLayout& layout, int slot, int band)
    {
        const float halfWidth = layout.HalfWidth();
        const float halfHeight = layout.HalfHeight();
        const float centerX = layout.ColumnCenterX(slot);
        const float centerY = layout.RowCenterY(band);
        return {
            VertexAtPixel(layout, centerX - halfWidth, centerY + halfHeight),
            VertexAtPixel(layout, centerX + halfWidth, centerY + halfHeight),
            VertexAtPixel(layout, centerX, centerY - halfHeight),
        };
    }

    /// A complete buffer image: the three-element prefix decoy (slot `kPrefixSlot`, band
    /// `kPrefixBand`) followed by one triangle per slot authored in @p band. Binding this at
    /// element offset `kPrefixOffset` makes stream record `k` the live geometry's record `k`.
    std::vector<VertexPositionColor> BuildPrefixedMesh(const GridLayout& layout, int band)
    {
        std::vector<VertexPositionColor> vertices;
        vertices.reserve(static_cast<std::size_t>(kBufferElementCount));
        const auto prefix = SlotTriangle(layout, kPrefixSlot, kPrefixBand);
        vertices.insert(vertices.end(), prefix.begin(), prefix.end());
        for (int slot = 0; slot < kSlotCount; ++slot)
        {
            const auto triangle = SlotTriangle(layout, slot, band);
            vertices.insert(vertices.end(), triangle.begin(), triangle.end());
        }
        return vertices;
    }

    /// The identity index buffer: index element `i` addresses stream record `i`. Every requested
    /// range is therefore expressed purely through the public startIndex/baseVertex/VertexOffset
    /// triple, with no index-content trickery to hide a dropped parameter.
    std::vector<std::uint16_t> BuildIdentityIndices16()
    {
        std::vector<std::uint16_t> indices;
        indices.reserve(static_cast<std::size_t>(kMeshElementCount));
        for (int i = 0; i < kMeshElementCount; ++i)
            indices.push_back(static_cast<std::uint16_t>(i));
        return indices;
    }

    std::vector<std::uint32_t> BuildIdentityIndices32()
    {
        std::vector<std::uint32_t> indices;
        indices.reserve(static_cast<std::size_t>(kMeshElementCount));
        for (int i = 0; i < kMeshElementCount; ++i)
            indices.push_back(static_cast<std::uint32_t>(i));
        return indices;
    }

    /// "Did any geometry render here?" compares RGB only: every primitive in this fixture is opaque
    /// white on a black clear, while the alpha a renderer leaves in a cleared target is its own
    /// convention and says nothing about which records a draw consumed.
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
            const int clampedX = std::clamp(x, 0, width - 1);
            const int clampedY = std::clamp(y, 0, height - 1);
            return pixels[
                static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(clampedX)];
        }

        [[nodiscard]] int CountLit(const Color& background) const
        {
            int total = 0;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    if (!HasSameRgb(At(x, y), background)) ++total;
            return total;
        }
    };

    /// Lit pixels inside the exclusive rectangle of one (slot, band) cell.
    int CountLitInCell(
        const FrameSnapshot& snapshot, const GridLayout& layout,
        int slot, int band, const Color& background)
    {
        const int x0 = static_cast<int>(layout.ColumnBoundaryX(slot) + 0.999f);
        const int x1 = static_cast<int>(layout.ColumnBoundaryX(slot + 1));
        const int y0 = static_cast<int>(layout.RowBoundaryY(band) + 0.999f);
        const int y1 = static_cast<int>(layout.RowBoundaryY(band + 1));
        int total = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (!HasSameRgb(snapshot.At(x, y), background)) ++total;
        return total;
    }

    /// A compact `band: slot0 slot1 ...` map of lit pixel counts per cell. Every failure in this
    /// file is about *which records* a draw consumed, so every failure prints this: the report
    /// names the offending cell instead of only a total.
    std::string DescribeLitCellMap(
        const FrameSnapshot& snapshot, const GridLayout& layout, const Color& background)
    {
        std::string map = "\n  lit cells (band: slot0..slot6):";
        for (int band = 0; band < kBandCount; ++band)
        {
            map += "\n    band " + std::to_string(band) + ':';
            for (int slot = 0; slot < kSlotCount; ++slot)
            {
                map += ' ';
                map += std::to_string(CountLitInCell(snapshot, layout, slot, band, background));
            }
        }
        map += "\n    total lit: " + std::to_string(snapshot.CountLit(background));
        return map;
    }

    /// The whole oracle in one assertion: the requested cell rendered, and NOTHING else in the
    /// frame did. Comparing the cell's own count against the frame total also rejects geometry
    /// that landed between cells, which a per-cell sweep alone would miss. No colour tolerance and
    /// no "the process survived" fallback -- a draw either consumed the requested records or it
    /// did not.
    void ExpectOnlyCellLit(
        const FrameSnapshot& snapshot, const GridLayout& layout,
        int slot, int band, const char* label)
    {
        const int inCell = CountLitInCell(snapshot, layout, slot, band, Color::Black);
        const int total = snapshot.CountLit(Color::Black);
        EXPECT_GT(inCell, 0)
            << label << ": nothing rendered in slot " << slot << " band " << band
            << DescribeLitCellMap(snapshot, layout, Color::Black);
        EXPECT_EQ(inCell, total)
            << label << ": geometry rendered outside slot " << slot << " band " << band
            << DescribeLitCellMap(snapshot, layout, Color::Black);
    }

    class OrdinaryDrawBindingOffsetTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        // GTEST_SKIP() only unwinds the function it is called from; called from an ordinary
        // member function like RequireOrdinaryRendering() below it cannot skip the test body that
        // invokes it. SetUp() is where GoogleTest itself checks for a skip, so the capability gate
        // has to run here too -- RequireOrdinaryRendering() keeps its own copy for the state-setup
        // calls that follow it, which only run once SetUp() has already let the test proceed.
        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
        }

        /// Explicit device state for the whole fixture: nothing here may depend on a framework
        /// default, because a default-valued no-op fallback would let a buggy path pass.
        void RequireOrdinaryRendering()
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

        [[nodiscard]] static GridLayout TargetLayout()
        {
            return GridLayout{kTargetSize, kTargetSize};
        }

        [[nodiscard]] GridLayout BackbufferLayout() const
        {
            return GridLayout{BackbufferWidth(), BackbufferHeight()};
        }

        [[nodiscard]] RenderTarget2D MakeTarget()
        {
            return RenderTarget2D(
                device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        }

        static FrameSnapshot CaptureTarget(RenderTarget2D& target)
        {
            FrameSnapshot snapshot;
            snapshot.width = kTargetSize;
            snapshot.height = kTargetSize;
            snapshot.pixels.assign(
                static_cast<std::size_t>(kTargetSize) * static_cast<std::size_t>(kTargetSize),
                Color::Transparent);
            const Rectangle region(0, 0, kTargetSize, kTargetSize);
            target.GetData(
                0, &region, snapshot.pixels.data(), 0,
                static_cast<int>(snapshot.pixels.size()));
            return snapshot;
        }

        FrameSnapshot CaptureBackbuffer()
        {
            const int width = BackbufferWidth();
            const int height = BackbufferHeight();
            FrameSnapshot snapshot;
            snapshot.width = width;
            snapshot.height = height;
            snapshot.pixels.assign(
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                Color::Transparent);
            const Rectangle region(0, 0, width, height);
            device.GetBackBufferData(
                &region, snapshot.pixels.data(), 0,
                static_cast<int>(snapshot.pixels.size()));
            return snapshot;
        }

        /// Identity transforms and an opaque white diffuse, so every renderer's stock path produces
        /// the same white triangle whether it colours from the vertex stream or from DiffuseColor.
        static void ApplyMeshEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.setAlphaProperty(1.0f);
            effect.Apply();
        }
    };
}


// ---------------------------------------------------------------------------
// Isolation control (coverage item 1): binding offset ZERO. Exactly the geometry, declaration,
// index buffer, device state and readback path every test below uses, with the offset removed.
// It asserts the prefix decoy's OWN cell rather than "nothing rendered", so this control fails if
// the fixture ever stops rendering -- a silent skip would outlive the defect it is meant to pin.
// This is also the exact frame a dropped nonzero offset produces, which is what makes the next
// test's failure unambiguous.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, ZeroOffsetIndexedDrawRendersThePrefixDecoy)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyCellLit(
        CaptureTarget(target), layout, kPrefixSlot, kPrefixBand, "zero-offset indexed control");
}

// ---------------------------------------------------------------------------
// Coverage item 2: one stream, nonzero VertexOffset, every other public offset zero. The offset
// alone must move the consumed records off the prefix decoy and onto slot 0's live triangle.
// A dropped offset reproduces the control above exactly: slot 6, band 3.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, NonzeroOffsetIndexedDrawSkipsThePrefixDecoy)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kPrefixOffset, 0)});
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyCellLit(CaptureTarget(target), layout, 0, kLiveBand, "nonzero-offset indexed draw");
}

// ---------------------------------------------------------------------------
// Coverage item 4: nonzero VertexOffset combined with a nonzero startIndex. startIndex selects
// index ELEMENTS and the offset shifts the stream base; the two are independent and each is
// applied exactly once.
//   offset dropped        -> slot 2 (one offset-width left of the request)
//   startIndex doubled    -> slot 6 band 0
//   offset as a byte count -> 48 records into a 24-record buffer, so nothing lights at all
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, NonzeroOffsetCombinesWithStartIndex)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kPrefixOffset, 0)});
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    // Slot 3's triangle owns index elements 9..11.
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyCellLit(CaptureTarget(target), layout, 3, kLiveBand, "offset + startIndex");
}

// ---------------------------------------------------------------------------
// Coverage item 5: nonzero VertexOffset combined with a nonzero baseVertex. Both shift the fetched
// record and both are element counts, which is exactly why applying either twice must be visible:
//   offset dropped     -> slot 2
//   baseVertex doubled -> slot 6 band 0
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, NonzeroOffsetCombinesWithBaseVertex)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kPrefixOffset, 0)});
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    // Index elements 0..2 decode to 0..2; baseVertex 9 moves them onto slot 3's records.
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 9, 0, kVerticesPerSlot, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyCellLit(CaptureTarget(target), layout, 3, kLiveBand, "offset + baseVertex");
}

// ---------------------------------------------------------------------------
// All three element offsets at once, each nonzero and each a different value, so a fix that
// accidentally swapped two of them still fails: 3 (binding) + 6 (baseVertex) + 3 (startIndex)
// selects slot 3, and no other assignment of {3, 6, 3} to the three roles does.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, NonzeroOffsetCombinesWithStartIndexAndBaseVertex)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kPrefixOffset, 0)});
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    // Index elements 3..5 decode to 3..5; baseVertex 6 moves them onto slot 3's records.
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 6, 3, kVerticesPerSlot, 3, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyCellLit(
        CaptureTarget(target), layout, 3, kLiveBand, "offset + startIndex + baseVertex");
}

// ---------------------------------------------------------------------------
// Coverage item 7: 32-bit indices. The index element SIZE must not leak into the vertex stream's
// element offset -- a renderer that scaled the binding offset by the index size would light a
// different slot here than with the 16-bit buffer above.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, NonzeroOffsetHonoredWithThirtyTwoBitIndices)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint32_t> indices = BuildIdentityIndices32();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kPrefixOffset, 0)});
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyCellLit(
        CaptureTarget(target), layout, 3, kLiveBand, "offset with 32-bit indices");
}

// ---------------------------------------------------------------------------
// The non-indexed route's zero-offset control, and its nonzero-offset counterpart. DrawPrimitives
// has no index buffer at all, so here the binding offset composes with vertexStart instead of with
// baseVertex -- the same element arithmetic through a different public entry point.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, ZeroOffsetNonIndexedDrawRendersThePrefixDecoy)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyCellLit(
        CaptureTarget(target), layout, kPrefixSlot, kPrefixBand, "zero-offset non-indexed control");
}

TEST_F(OrdinaryDrawBindingOffsetTest, NonzeroOffsetNonIndexedDrawCombinesWithVertexStart)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kPrefixOffset, 0)});
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    // Stream record 9 is slot 3's first vertex once the prefix decoy is skipped.
    device.DrawPrimitives(PrimitiveType::TriangleList, 9, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyCellLit(
        CaptureTarget(target), layout, 3, kLiveBand, "non-indexed offset + vertexStart");
}

// ---------------------------------------------------------------------------
// Coverage item 3: two bound streams with DIFFERENT offsets. Binding 0 is the geometry stream on
// the ordinary routes, so its offset -- and only its offset -- may reach the draw.
//   binding 1's offset used instead (6) -> slot 4
//   binding 1's BUFFER used instead     -> band 2
// The second stream deliberately carries a different offset AND different bytes so one frame
// separates "took the wrong binding's number" from "took the wrong binding's data".
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, MultipleStreamsUseOnlyTheGeometryStreamsOwnOffset)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<VertexPositionColor> other =
        BuildPrefixedMesh(layout, kOtherStreamBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    VertexBuffer otherBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    otherBuffer.SetData(other.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&otherBuffer, 2 * kVerticesPerSlot, 0),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    ExpectOnlyCellLit(
        CaptureTarget(target), layout, 3, kLiveBand, "two streams, different offsets");
}

// ---------------------------------------------------------------------------
// Coverage items 9 and 10: a DYNAMIC vertex buffer, and buffer contents rewritten between two
// draws that share one binding. The second draw must read the NEW bytes at the SAME offset --
// a cached or shadowed copy captured at bind time lights band 0 twice instead of band 0 then
// band 1.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, DynamicBufferRewrittenBetweenDrawsIsReadAtTheOffset)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> live = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<VertexPositionColor> rewritten =
        BuildPrefixedMesh(layout, kRewrittenBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    DynamicVertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(live.data(), 0, kBufferElementCount, SetDataOptions::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kPrefixOffset, 0)});
    device.SetIndexBuffer(&indexBuffer);

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    ExpectOnlyCellLit(
        CaptureTarget(target), layout, 3, kLiveBand, "dynamic buffer, first contents");

    meshBuffer.SetData(rewritten.data(), 0, kBufferElementCount, SetDataOptions::Discard);

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    ExpectOnlyCellLit(
        CaptureTarget(target), layout, 3, kRewrittenBand, "dynamic buffer, rewritten contents");
}

// ---------------------------------------------------------------------------
// Coverage items 11 and 13: A -> B -> A offset reuse against one buffer and one binding slot.
// Each leg must render its own offset's records; the third leg returning to the first offset
// catches a native binding, layout or descriptor cached on first use and never revisited, and a
// stale offset left behind by the intervening leg.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, OffsetReuseAcrossDrawsIsExact)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    auto renderWithOffset = [&](int offset) {
        device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, offset, 0)});
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return CaptureTarget(target);
    };

    // A: offset 3 selects slot 3's records.
    ExpectOnlyCellLit(renderWithOffset(kPrefixOffset), layout, 3, kLiveBand, "reuse leg A");
    // B: offset 0 selects slot 2's records -- the prefix decoy displaces the whole stream.
    ExpectOnlyCellLit(renderWithOffset(0), layout, 2, kLiveBand, "reuse leg B");
    // A again: identical to the first leg, from a state the second leg has already touched.
    ExpectOnlyCellLit(renderWithOffset(kPrefixOffset), layout, 3, kLiveBand, "reuse leg A again");
}

// ---------------------------------------------------------------------------
// Coverage item 16: disposal, address/handle reuse and device teardown. The first buffer is
// destroyed and a second is created immediately, so the allocator (and the native object name)
// very often reuses the freed slot. A binding offset cached against a buffer address or a native
// handle rather than against the live binding therefore reappears on the new buffer -- and the new
// buffer deliberately asks for a DIFFERENT offset than the dead one, so the stale value lights a
// different slot. Device teardown itself is exercised by the fixture's own destructor.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, BindingOffsetSurvivesDisposalAndHandleReuse)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    auto makeBuffer = [&]() {
        auto buffer = std::make_unique<VertexBuffer>(
            device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
        buffer->SetData(mesh.data(), kBufferElementCount);
        return buffer;
    };

    auto renderWithOffset = [&](VertexBuffer& buffer, int offset) {
        device.SetVertexBuffers({VertexBufferBinding(&buffer, offset, 0)});
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return CaptureTarget(target);
    };

    std::unique_ptr<VertexBuffer> first = makeBuffer();
    ExpectOnlyCellLit(
        renderWithOffset(*first, kPrefixOffset), layout, 3, kLiveBand, "before disposal");

    device.SetVertexBuffers({});
    device.SetVertexBuffer(nullptr);
    first.reset();

    std::unique_ptr<VertexBuffer> second = makeBuffer();
    ExpectOnlyCellLit(
        renderWithOffset(*second, 2 * kVerticesPerSlot), layout, 4, kLiveBand,
        "after disposal and handle reuse");
}

// ---------------------------------------------------------------------------
// Range validation (REMED-GFX-113/118's gate, extended by the offset): once the binding offset is
// included, a request that leaves the bound buffer must be rejected before native submission
// rather than silently clamped or allowed to read past the records. The rejected request is legal
// at offset zero and illegal only because of the offset, so this cannot pass by rejecting
// everything -- the accepted sibling below is the exact boundary case that must still work.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, OffsetIsIncludedInTheDrawRangeValidation)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);
    ApplyMeshEffect(effect);

    // Exactly reaches the last record: 3 (offset) + 21 (declared range) == 24 records.
    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kPrefixOffset, 0)});
    EXPECT_NO_THROW(device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kMeshElementCount, 0, 1));
    EXPECT_NO_THROW(device.DrawPrimitives(
        PrimitiveType::TriangleList, kMeshElementCount - kVerticesPerSlot, 1));

    // One record too far, only because of the offset: 3 + 22 > 24.
    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kMeshElementCount + 1, 0, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(
            PrimitiveType::TriangleList, kMeshElementCount - kVerticesPerSlot + 1, 1),
        System::ArgumentOutOfRangeException);

    // The same two requests are legal with the offset removed, which is what makes the rejections
    // above attributable to the offset and to nothing else.
    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
    EXPECT_NO_THROW(device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kMeshElementCount + 1, 0, 1));
    EXPECT_NO_THROW(device.DrawPrimitives(
        PrimitiveType::TriangleList, kMeshElementCount - kVerticesPerSlot + 1, 1));
}

// ---------------------------------------------------------------------------
// SetVertexBuffer's own offset overload feeds the same binding slot, and its offsetless overload
// must CLEAR a previously set offset rather than inherit it. A stale offset here would light
// slot 3 where slot 2 is required.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, SetVertexBufferOverloadsCarryAndClearTheOffset)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffset())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    auto render = [&]() {
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return CaptureTarget(target);
    };

    device.SetVertexBuffer(&meshBuffer, kPrefixOffset);
    ExpectOnlyCellLit(render(), layout, 3, kLiveBand, "SetVertexBuffer with an offset");

    device.SetVertexBuffer(&meshBuffer);
    ExpectOnlyCellLit(render(), layout, 2, kLiveBand, "SetVertexBuffer clears the offset");
}



// ---------------------------------------------------------------------------
// Coverage item 14: the BACKBUFFER destination. Same contract, same arithmetic, a different
// destination and a different readback path -- a renderer that applies the offset only while a
// render target is bound (or only while one is not) fails exactly one of these two groups.
// The zero-offset control runs here too, so the backbuffer group is self-contained.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, BackbufferDestinationHonorsTheBindingOffset)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffsetBackbuffer())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = BackbufferLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    auto renderWithOffset = [&](int offset) {
        device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, offset, 0)});
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1);
        return CaptureBackbuffer();
    };

    ExpectOnlyCellLit(renderWithOffset(0), layout, 2, kLiveBand, "backbuffer zero-offset control");
    ExpectOnlyCellLit(
        renderWithOffset(kPrefixOffset), layout, 3, kLiveBand, "backbuffer nonzero offset");
}

TEST_F(OrdinaryDrawBindingOffsetTest, BackbufferDestinationHonorsTheOffsetOnNonIndexedDraws)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffsetBackbuffer())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();

    const GridLayout layout = BackbufferLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);

    BasicEffect effect(device);

    auto renderWithOffset = [&](int offset) {
        device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, offset, 0)});
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawPrimitives(PrimitiveType::TriangleList, 9, 1);
        return CaptureBackbuffer();
    };

    ExpectOnlyCellLit(
        renderWithOffset(0), layout, 2, kLiveBand, "backbuffer non-indexed zero-offset control");
    ExpectOnlyCellLit(
        renderWithOffset(kPrefixOffset), layout, 3, kLiveBand,
        "backbuffer non-indexed nonzero offset");
}



// ---------------------------------------------------------------------------
// Coverage item 12: ordinary -> instanced -> ordinary. The two routes populate the shared draw
// parameters independently, so this pins that neither leaks into the other: the instanced leg
// binds a second stream and a different offset, and the ordinary legs on either side of it must be
// pixel-identical. The instanced leg's OWN offset is asserted only on the renderers whose instanced
// path consumes it (REMED-GFX-122/123); elsewhere the leg still runs, because what this test is
// about is whether it corrupts the ordinary route around it.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawBindingOffsetTest, OrdinaryInstancedOrdinaryTransitionsKeepEachRoutesOffset)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryBindingOffsetInstancedTransition())
        GTEST_SKIP() << "this renderer\'s instanced route does not render the geometry";
    RequireOrdinaryRendering();
    // The middle leg of this transition is a hardware-instanced draw -- unavailable on a
    // renderer profile that reports GraphicsCapability::Instancing = false (e.g. the OPENGLES2
    // GL profile; core OpenGL ES 2.0 has no instancing entry points). The pure-ordinary legs of
    // this file keep running there; only this mixed-route test skips.
    if (!device.SupportsCapability(GraphicsCapability::Instancing))
        GTEST_SKIP() << "Renderer reports GraphicsCapability::Instancing = false: the instanced "
                        "middle leg of this transition cannot run on this renderer profile";

    const GridLayout layout = TargetLayout();
    const std::vector<VertexPositionColor> mesh = BuildPrefixedMesh(layout, kLiveBand);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    const InstanceMatrix instance = IdentityInstance();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kBufferElementCount, BufferUsage::None);
    meshBuffer.SetData(mesh.data(), kBufferElementCount);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);
    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(&instance, 1, static_cast<int>(sizeof(InstanceMatrix)));

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    auto renderOrdinary = [&]() {
        device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kPrefixOffset, 0)});
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return CaptureTarget(target);
    };

    auto renderInstanced = [&]() {
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, 2 * kVerticesPerSlot, 0),
            VertexBufferBinding(&instanceBuffer, 0, 1),
        });
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 9, kVerticesPerSlot, 9, 1, 1);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return CaptureTarget(target);
    };

    ExpectOnlyCellLit(renderOrdinary(), layout, 3, kLiveBand, "ordinary before the instanced leg");

    const FrameSnapshot instanced = renderInstanced();
    if (OrdinaryBindingOffsetInstancedOffset())
    {
        // Offset 6 selects record 6+9=15, which is live record 12 -- slot 4, where the ordinary legs
        // select slot 3, so the two routes cannot satisfy each other's expectation.
        ExpectOnlyCellLit(instanced, layout, 4, kLiveBand, "instanced leg between the ordinary draws");
    }
    else
    {
        (void)instanced;
    }

    ExpectOnlyCellLit(renderOrdinary(), layout, 3, kLiveBand, "ordinary after the instanced leg");
}

