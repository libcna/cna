// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-212 -- `BasicEffect.VertexColorEnabled` on the instanced draw route.
//
// The authoritative contract is draw-call-independent, and the reference says so three times over:
//
//   * `GraphicsDevice.DrawInstancedPrimitives` (FNA `GraphicsDevice.cs:1257`) is `ApplyState()` +
//     `PrepareVertexBindingArray(baseVertex)` + the FNA3D call. It touches no effect state.
//   * `BasicEffect.OnApply` (FNA `BasicEffect.cs:490-511`) derives its shader index from fog,
//     vertex colour, texture and lighting. There is no instancing term.
//   * every vertex-colour permutation in FNA's `BasicEffect.fx` multiplies `vout.Diffuse *=
//     vin.Color` (line 85 and its seven siblings), and the file contains no instancing construct
//     at all.
//
// The same vertex shader therefore runs for both routes, so the public contract is:
//
//   VertexColorEnabled = false -> the vertex COLOR0 stream must NOT modulate the diffuse result;
//   VertexColorEnabled = true  -> the bound per-vertex COLOR0 semantic multiplies it.
//
// This file measures that on the ONE thing that needs no reference at runtime: the same renderer's
// own ordinary route, under identical effect state, with identical buffers and identical geometry.
// Every leg renders the frame twice -- once through `DrawIndexedPrimitives`, once through
// `DrawInstancedPrimitives` with a single identity per-instance record -- and compares them
// against each other AND against the analytic product, so neither "both routes are wrong the same
// way" nor "the routes agree on the wrong colour" can pass.
//
// THE COLOUR ORACLE. `DiffuseColor` and every COLOR0 record are chosen so that each failure mode
// this class of defect produces lands on a different measurable colour. With
// D = (0.500, 1.000, 0.250) and column 0's C = (255, 128, 64):
//
//   COLOR0 applied once (correct) -> (128, 128,  16)
//   COLOR0 ignored                -> (128, 255,  64)   == DiffuseColor, the measured pre-fix result
//   COLOR0 applied twice          -> (128,  64,   4)
//   DiffuseColor ignored          -> (255, 128,  64)   == COLOR0 itself
//   channel swizzle (BGR)         -> ( 32, 128,  64)
//   read un-normalized            -> (255, 255, 255)   every product saturates
//   read at the wrong offset      -> position bytes, nowhere near any of the above
//   instance record read as colour-> the matrix's own bytes, likewise
//
// Four columns carry four different COLOR0 records, so a stale colour from a previous draw, a
// wrong stride and a wrong element offset all move a *known* colour to a *known* wrong column
// rather than merely changing a single value.

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

// The renderers whose stock instanced path rasterizes and whose RenderTarget2D::GetData reads the
// result back -- InstancedDrawMultiStreamTests.cpp's own permanent suite set, for the same reason.
/// plan_runtimerenderer.md RTR-P9-5: the same renderer set, evaluated at runtime so this
/// describes the ACTIVE renderer rather than the build default.
[[nodiscard]] inline bool InstancedVertexColor()
{
    return CNA_RENDERER_IS(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, 
                            DirectX12);
}

// The renderers whose instanced route this file has MEASURED on a real display, and which therefore
// carry an assertion in one direction or the other. D3D9/D3D11/D3D12 stay outside it because no
// D3D display was reachable (SDL reports "x11 not available" under Wine on the Xvfb displays this
// environment permits) -- REMED-GFX-212 identifies D3D11/D3D12 from source as colouring the
// instanced route from DiffuseColor, but an unmeasured renderer must not be asserted in either
// direction. Every leg still PRINTS its reading there, which is the evidence those renderers lack.
/// plan_runtimerenderer.md RTR-P9-5: the measured set, asked of the ACTIVE renderer.
[[nodiscard]] inline bool InstancedVertexColorMeasured()
{
    return CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, Bgfx, Vulkan, WebGPU);
}

// The renderers whose instanced route was measured obeying the PUBLIC CONTRACT: EasyGL always did,
// Vulkan and WebGPU were corrected by REMED-GFX-212, and bgfx by REMED-GFX-215.
//
// bgfx was outside this set until REMED-GFX-215. Its instanced route consumed COLOR0 -- which is
// what REMED-GFX-212's own triage measured, with a WHITE DiffuseColor that cannot tell "COLOR0
// times DiffuseColor" apart from "COLOR0 alone" -- but a non-white DiffuseColor separated them and
// showed bgfx emitting the RAW vertex colour, ignoring DiffuseColor AND VertexColorEnabled in both
// settings: `vs_instanced3d.sc`'s whole colour body was `v_color0 = a_color0;` and it declared no
// diffuse uniform, so neither term had anywhere to arrive. That was the exact mirror image of
// REMED-GFX-212 -- Vulkan and WebGPU kept DiffuseColor and dropped COLOR0, bgfx kept COLOR0 and
// dropped DiffuseColor -- so it was tracked separately as REMED-GFX-215 and fixed there. The arm
// that asserted bgfx's measured defect did what it was written to do: it failed the moment bgfx was
// corrected and forced its own removal. `InstancedDiffuseColorTests.cpp` is that ticket's permanent
// non-neutral-DiffuseColor oracle, and it is what keeps this file's white-DiffuseColor blind spot
// from ever certifying a renderer again.
/// plan_runtimerenderer.md RTR-P9-5: the public-contract set, asked of the ACTIVE renderer.
[[nodiscard]] inline bool InstancedVertexColorContract()
{
    return CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, Vulkan, WebGPU, Bgfx);
}


namespace
{
    // plan_runtimerenderer.md RTR-P9-5: was a hand-maintained #if/#elif chain of renderer display
    // names, which had to be extended for every new renderer and answered "unknown" when it was
    // not. The runtime API already knows the active renderer's name, and knows it for all 46.
    inline std::string RendererName()
    {
        return std::string(CNA::getGraphicsRendererName(
            CNA::GraphicsRendererSelection::GetSelected()));
    }

    /// Square render target: 256 = 4 * 64, so the column axis divides exactly.
    constexpr int kTargetSize = 256;

    /// Columns the geometry mesh owns, one flat-coloured quad each.
    constexpr int kColumnCount = 4;
    constexpr int kColumnWidth = kTargetSize / kColumnCount;

    /// Pixels the geometry stays away from a column boundary, so neighbouring quads never touch.
    constexpr int kGeometryInset = 6;

    /// Pixels the sampling box stays away from a column boundary -- comfortably inside the
    /// geometry inset, so no sampled pixel can be a rasterization edge case.
    constexpr int kSampleInset = 20;

    /// Vertices and indices one quad owns.
    constexpr int kVerticesPerQuad = 4;
    constexpr int kIndicesPerQuad = 6;
    constexpr int kMeshVertexCount = kColumnCount * kVerticesPerQuad;
    constexpr int kMeshIndexCount = kColumnCount * kIndicesPerQuad;
    constexpr int kMeshPrimitiveCount = kMeshIndexCount / 3;

    /// The decoy quad every "skip the prefix" leg puts in front of the live mesh. Deliberately a
    /// colour no live column carries, so consuming it is unmistakable.
    constexpr int kDecoyVertexCount = kVerticesPerQuad;

    /// `BasicEffect.DiffuseColor`. Every channel is different and none is 0 or 1, so a dropped,
    /// doubled or swizzled COLOR0 multiplication all land somewhere else.
    constexpr float kDiffuseR = 0.5f;
    constexpr float kDiffuseG = 1.0f;
    constexpr float kDiffuseB = 0.25f;

    struct Rgba
    {
        int r = 0;
        int g = 0;
        int b = 0;
        int a = 0;

        [[nodiscard]] std::string ToString() const
        {
            std::ostringstream os;
            os << '(' << r << ',' << g << ',' << b << ',' << a << ')';
            return os.str();
        }
    };

    /// The four COLOR0 records. Column 3 is deliberately opaque white -- the one record whose
    /// product IS DiffuseColor, which is what separates "DiffuseColor was dropped" from
    /// "COLOR0 was dropped" in the same frame.
    constexpr std::array<Rgba, kColumnCount> kColumnColors{
        Rgba{255, 128, 64, 255},
        Rgba{128, 255, 64, 255},
        Rgba{64, 128, 255, 255},
        Rgba{255, 255, 255, 255},
    };

    /// The decoy quad's COLOR0 -- pure green, which no live column and no product of a live column
    /// with DiffuseColor can produce.
    constexpr Rgba kDecoyColor{0, 255, 0, 255};

    int Quantize(float linear)
    {
        const float clamped = linear < 0.0f ? 0.0f : (linear > 1.0f ? 1.0f : linear);
        return static_cast<int>(clamped * 255.0f + 0.5f);
    }

    /// The public contract's own arithmetic: `vout.Diffuse = DiffuseColor * Alpha` and then
    /// `vout.Diffuse *= vin.Color` when VertexColorEnabled is set. Alpha is 1 in every leg, so the
    /// forwarded alpha is COLOR0's own.
    Rgba ExpectedColor(const Rgba& color0, bool vertexColorEnabled)
    {
        if (!vertexColorEnabled)
            return Rgba{Quantize(kDiffuseR), Quantize(kDiffuseG), Quantize(kDiffuseB), 255};
        return Rgba{
            Quantize(kDiffuseR * static_cast<float>(color0.r) / 255.0f),
            Quantize(kDiffuseG * static_cast<float>(color0.g) / 255.0f),
            Quantize(kDiffuseB * static_cast<float>(color0.b) / 255.0f),
            Quantize(static_cast<float>(color0.a) / 255.0f)};
    }

    /// Which of the two routes a measurement came from. The contract is the same for both, but a
    /// renderer may be correct on one and not the other, so a measured-defect arm has to say which.
    enum class Route
    {
        Ordinary,
        Instanced,
    };

    /// What THIS renderer is asserted to produce. Every measured renderer is now held to the public
    /// contract on both routes: the bgfx instanced exemption this function used to carry was
    /// removed when REMED-GFX-215 fixed the defect it recorded.
    Rgba AssertedColor(const Rgba& color0, bool vertexColorEnabled, Route route)
    {
        (void)route;
        return ExpectedColor(color0, vertexColorEnabled);
    }

    /// The suffix a measured-defect message would carry. No renderer carries one any more.
    constexpr const char* kAssertionBasis = "";


    /// Slot 0's record: the packed 16-byte position+colour vertex every instancing renderer
    /// recognizes -- XNA's VertexPositionColor layout.
    struct PackedVertex
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(PackedVertex) == 16);

    /// The same layout with a texture coordinate appended -- XNA's VertexPositionColorTexture.
    /// A second COLOR0-bearing stride, so the layout/pipeline-variant axis is exercised too.
    struct PackedTexVertex
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
        float u, v;
    };
    static_assert(sizeof(PackedTexVertex) == 24);

    VertexDeclaration PackedDeclaration()
    {
        return VertexDeclaration(
            16,
            {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
    }

    VertexDeclaration PackedTexDeclaration()
    {
        return VertexDeclaration(
            24,
            {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                VertexElement(16, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
            });
    }

    /// The per-instance record: the whole world matrix in one binding, the classic shape every
    /// CNA instancing renderer already renders.
    struct MatrixRecord
    {
        float c0[4];
        float c1[4];
        float c2[4];
        float c3[4];
    };
    static_assert(sizeof(MatrixRecord) == 64);

    VertexDeclaration MatrixDeclaration()
    {
        return VertexDeclaration(
            64,
            {
                VertexElement(0, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 1),
                VertexElement(16, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 2),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 3),
                VertexElement(48, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 4),
            });
    }

    /// A column-major world matrix that translates by @p columnShift whole columns along X. The
    /// true identity (columnShift 0) is what every route-agreement leg uses, so the instanced
    /// route's own transform cannot displace the geometry the ordinary route drew.
    MatrixRecord ShiftMatrix(int columnShift)
    {
        const float ndcShift =
            2.0f * static_cast<float>(columnShift) / static_cast<float>(kColumnCount);
        MatrixRecord record{};
        record.c0[0] = 1.0f;
        record.c1[1] = 1.0f;
        record.c2[2] = 1.0f;
        record.c3[0] = ndcShift;
        record.c3[3] = 1.0f;
        return record;
    }

    /// Identity World/View/Projection everywhere, so a vertex position IS its clip-space position
    /// on the ordinary route and `VP * World * pos` reduces to it on the instanced one.
    void NdcFromPixel(float pixelX, float pixelY, float& outX, float& outY)
    {
        outX = (2.0f * pixelX / static_cast<float>(kTargetSize)) - 1.0f;
        outY = 1.0f - (2.0f * pixelY / static_cast<float>(kTargetSize));
    }

    /// The four corners of column @p column's quad, inset from the column boundary on every side.
    std::array<std::array<float, 2>, kVerticesPerQuad> QuadCorners(int column)
    {
        const float left = static_cast<float>(column * kColumnWidth + kGeometryInset);
        const float right = static_cast<float>((column + 1) * kColumnWidth - kGeometryInset);
        const float top = static_cast<float>(kGeometryInset);
        const float bottom = static_cast<float>(kTargetSize - kGeometryInset);
        std::array<std::array<float, 2>, kVerticesPerQuad> corners{};
        const float px[kVerticesPerQuad] = {left, right, right, left};
        const float py[kVerticesPerQuad] = {top, top, bottom, bottom};
        for (int i = 0; i < kVerticesPerQuad; ++i)
            NdcFromPixel(px[i], py[i], corners[static_cast<std::size_t>(i)][0],
                         corners[static_cast<std::size_t>(i)][1]);
        return corners;
    }

    /// The geometry stream: an optional decoy quad in column 0, then one flat-coloured quad per
    /// column. `withDecoy` is what a nonzero `VertexBufferBinding.VertexOffset` must skip.
    std::vector<PackedVertex> BuildPackedMesh(bool withDecoy)
    {
        std::vector<PackedVertex> vertices;
        const auto emit = [&vertices](int column, const Rgba& color) {
            const auto corners = QuadCorners(column);
            for (int i = 0; i < kVerticesPerQuad; ++i)
                vertices.push_back(PackedVertex{
                    corners[static_cast<std::size_t>(i)][0],
                    corners[static_cast<std::size_t>(i)][1],
                    0.5f,
                    static_cast<std::uint8_t>(color.r), static_cast<std::uint8_t>(color.g),
                    static_cast<std::uint8_t>(color.b), static_cast<std::uint8_t>(color.a)});
        };
        if (withDecoy)
            emit(0, kDecoyColor);
        for (int column = 0; column < kColumnCount; ++column)
            emit(column, kColumnColors[static_cast<std::size_t>(column)]);
        return vertices;
    }

    /// The stride-24 sibling of BuildPackedMesh, carrying the same positions and the same COLOR0
    /// records with a texture coordinate appended.
    std::vector<PackedTexVertex> BuildPackedTexMesh()
    {
        std::vector<PackedTexVertex> vertices;
        for (int column = 0; column < kColumnCount; ++column)
        {
            const Rgba& color = kColumnColors[static_cast<std::size_t>(column)];
            const auto corners = QuadCorners(column);
            for (int i = 0; i < kVerticesPerQuad; ++i)
                vertices.push_back(PackedTexVertex{
                    corners[static_cast<std::size_t>(i)][0],
                    corners[static_cast<std::size_t>(i)][1],
                    0.5f,
                    static_cast<std::uint8_t>(color.r), static_cast<std::uint8_t>(color.g),
                    static_cast<std::uint8_t>(color.b), static_cast<std::uint8_t>(color.a),
                    0.5f, 0.5f});
        }
        return vertices;
    }

    /// A single-quad mesh in column @p column, so an instance stream that displaces it can be read
    /// without the other columns' geometry in the way.
    std::vector<PackedVertex> BuildSingleQuadMesh(int column, const Rgba& color)
    {
        std::vector<PackedVertex> vertices;
        const auto corners = QuadCorners(column);
        for (int i = 0; i < kVerticesPerQuad; ++i)
            vertices.push_back(PackedVertex{
                corners[static_cast<std::size_t>(i)][0],
                corners[static_cast<std::size_t>(i)][1],
                0.5f,
                static_cast<std::uint8_t>(color.r), static_cast<std::uint8_t>(color.g),
                static_cast<std::uint8_t>(color.b), static_cast<std::uint8_t>(color.a)});
        return vertices;
    }

    template <typename T>
    std::vector<T> BuildQuadIndices(int quadCount)
    {
        std::vector<T> indices;
        indices.reserve(static_cast<std::size_t>(quadCount) * kIndicesPerQuad);
        for (int quad = 0; quad < quadCount; ++quad)
        {
            const T base = static_cast<T>(quad * kVerticesPerQuad);
            indices.push_back(static_cast<T>(base + 0));
            indices.push_back(static_cast<T>(base + 1));
            indices.push_back(static_cast<T>(base + 2));
            indices.push_back(static_cast<T>(base + 0));
            indices.push_back(static_cast<T>(base + 2));
            indices.push_back(static_cast<T>(base + 3));
        }
        return indices;
    }

    struct FrameSnapshot
    {
        std::vector<Color> pixels;

        [[nodiscard]] Color At(int x, int y) const
        {
            return pixels[static_cast<std::size_t>(y) * kTargetSize + static_cast<std::size_t>(x)];
        }
    };

    /// The colour column @p column carries, plus how many of its sampled pixels differ from the
    /// column's own first sample. A nonzero spread means the quad is not flat, which no correct
    /// reading of a flat-coloured quad can produce.
    Rgba SampleColumn(const FrameSnapshot& snapshot, int column, int& spreadOut, int& litOut)
    {
        const int x0 = column * kColumnWidth + kSampleInset;
        const int x1 = (column + 1) * kColumnWidth - kSampleInset;
        const int y0 = kSampleInset + kColumnWidth / 4;
        const int y1 = kTargetSize - kSampleInset - kColumnWidth / 4;
        Rgba first{};
        bool haveFirst = false;
        spreadOut = 0;
        litOut = 0;
        for (int y = y0; y < y1; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                const Color pixel = snapshot.At(x, y);
                const Rgba sample{pixel.getRProperty(), pixel.getGProperty(),
                                  pixel.getBProperty(), pixel.getAProperty()};
                if (!haveFirst)
                {
                    first = sample;
                    haveFirst = true;
                }
                else if (sample.r != first.r || sample.g != first.g || sample.b != first.b ||
                         sample.a != first.a)
                {
                    ++spreadOut;
                }
                if (sample.r != 0 || sample.g != 0 || sample.b != 0)
                    ++litOut;
            }
        }
        return first;
    }

    /// Every column's reading in one line, printed by every leg whether it passes or fails --
    /// a gate that merely excludes a renderer records only that somebody once believed it broken.
    std::string DescribeFrame(const FrameSnapshot& snapshot)
    {
        std::ostringstream os;
        for (int column = 0; column < kColumnCount; ++column)
        {
            int spread = 0;
            int lit = 0;
            const Rgba sample = SampleColumn(snapshot, column, spread, lit);
            os << "\n    column " << column << ": " << sample.ToString()
               << " lit=" << lit << " spread=" << spread;
        }
        return os.str();
    }

    void PrintMeasurement(const char* leg, const FrameSnapshot& snapshot)
    {
        std::cout << "[ GFX-212  ] " << RendererName() << ' ' << leg << ':'
                  << DescribeFrame(snapshot) << std::endl;
    }

    /// 8-bit render-target quantization tolerance. Every failure mode the oracle separates differs
    /// by at least 32 in some channel, so this is far below the smallest real distinction.
    constexpr int kTolerance = 3;

    bool NearlyEqual(const Rgba& a, const Rgba& b)
    {
        const auto close = [](int lhs, int rhs) {
            return (lhs > rhs ? lhs - rhs : rhs - lhs) <= kTolerance;
        };
        return close(a.r, b.r) && close(a.g, b.g) && close(a.b, b.b) && close(a.a, b.a);
    }
}   // namespace

class InstancedVertexColorTest : public ::testing::Test
{
protected:
    GraphicsDevice device;

    /// GTEST_SKIP() only suppresses the remaining TEST BODY when raised from SetUp() --
    /// raised inside a helper the body calls, it merely returns from the helper (the exact
    /// reason the multi-stream skip is a macro). Hardware instancing is this whole file's
    /// subject and needs BOTH a 3D pipeline and an instancing path, so both capabilities
    /// are gated here: a renderer with no 3D pipeline at all (e.g. OPENVG) and a renderer
    /// whose profile reports GraphicsCapability::Instancing = false (e.g. OPENGLES2 -- core
    /// OpenGL ES 2.0 has no glDrawElementsInstanced/glVertexAttribDivisor, see
    /// docs/opengles2-renderer.md) each skip every leg here up front.
    void SetUp() override
    {
        if (!device.SupportsCapability(GraphicsCapability::ThreeD))
            GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
        if (!device.SupportsCapability(GraphicsCapability::Instancing))
            GTEST_SKIP() << "Renderer reports GraphicsCapability::Instancing = false: hardware "
                            "instancing is unavailable on this renderer profile";
    }

    void RequireInstancedRendering()
    {
        if (!device.SupportsCapability(GraphicsCapability::ThreeD))
            GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setScissorRectangleProperty(Rectangle(0, 0, kTargetSize, kTargetSize));
    }

    [[nodiscard]] RenderTarget2D MakeTarget()
    {
        return RenderTarget2D(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    }

    [[nodiscard]] FrameSnapshot CaptureTarget(RenderTarget2D& target)
    {
        FrameSnapshot snapshot;
        snapshot.pixels.assign(
            static_cast<std::size_t>(kTargetSize) * static_cast<std::size_t>(kTargetSize),
            Color::Transparent);
        const Rectangle region(0, 0, kTargetSize, kTargetSize);
        target.GetData(0, &region, snapshot.pixels.data(), 0,
                       static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    /// The whole effect state both routes share. Lighting, texturing and fog stay off, so the
    /// selected permutation is exactly "vertex colour on/off" and nothing else can contribute a
    /// colour term.
    static void ApplyEffect(BasicEffect& effect, bool vertexColorEnabled)
    {
        effect.VertexColorEnabled = vertexColorEnabled;
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setFogEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(kDiffuseR, kDiffuseG, kDiffuseB));
        effect.setAlphaProperty(1.0f);
        effect.Apply();
    }

    /// Asserts every column against what this renderer is asserted to produce, and prints the whole
    /// frame either way -- a gate that merely excludes a renderer records only that somebody once
    /// believed it broken.
    static void ExpectColumns(
        const FrameSnapshot& snapshot, bool vertexColorEnabled, Route route, const char* leg)
    {
        PrintMeasurement(leg, snapshot);
        if (InstancedVertexColorMeasured())
        {
            for (int column = 0; column < kColumnCount; ++column)
            {
                int spread = 0;
                int lit = 0;
                const Rgba sample = SampleColumn(snapshot, column, spread, lit);
                const Rgba expected = AssertedColor(
                    kColumnColors[static_cast<std::size_t>(column)], vertexColorEnabled, route);
                EXPECT_GT(lit, 0) << leg << ": column " << column << " rendered nothing"
                                  << DescribeFrame(snapshot);
                EXPECT_EQ(spread, 0) << leg << ": column " << column
                                     << " is not flat" << DescribeFrame(snapshot);
                EXPECT_TRUE(NearlyEqual(sample, expected))
                    << leg << ": column " << column << " carried " << sample.ToString()
                    << ", expected " << expected.ToString() << kAssertionBasis
                    << DescribeFrame(snapshot);
            }
        }
        else
        {
            (void)vertexColorEnabled;
            (void)route;
            (void)leg;
        }
    }

    /// The route-agreement half: the two frames must be the same frame.
    static void ExpectRoutesAgree(
        const FrameSnapshot& ordinary, const FrameSnapshot& instanced, const char* leg)
    {
        if (InstancedVertexColorContract())
        {
            for (int column = 0; column < kColumnCount; ++column)
            {
                int spreadO = 0;
                int litO = 0;
                int spreadI = 0;
                int litI = 0;
                const Rgba o = SampleColumn(ordinary, column, spreadO, litO);
                const Rgba i = SampleColumn(instanced, column, spreadI, litI);
                EXPECT_TRUE(NearlyEqual(o, i))
                    << leg << ": column " << column << " -- the ordinary route rendered "
                    << o.ToString() << " and the instanced route " << i.ToString()
                    << ". BasicEffect's shader index has no instancing term, so the same "
                       "VertexColorEnabled calculation must run for both";
            }
        }
        else
        {
            (void)ordinary;
            (void)instanced;
            (void)leg;
        }
    }
};

// ---------------------------------------------------------------------------
// The core contract, on both routes and both settings.
// ---------------------------------------------------------------------------

TEST_F(InstancedVertexColorTest, VertexColorEnabledTrueConsumesGeometryColorOnBothRoutes)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedVertexColor())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false);
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(kColumnCount);
    const std::array<MatrixRecord, 1> instances{ShiftMatrix(0)};

    VertexBuffer meshBuffer(device, PackedDeclaration(), kMeshVertexCount, BufferUsage::None);
    meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, 16);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshIndexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshIndexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);

    const auto render = [&](bool instanced) {
        RenderTarget2D target = MakeTarget();
        if (instanced)
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                                     VertexBufferBinding(&instanceBuffer, 0, 1)});
        else
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyEffect(effect, true);
        if (instanced)
            device.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
        else
            device.DrawIndexedPrimitives(
                PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount);
        device.SetRenderTarget(nullptr);
        return CaptureTarget(target);
    };

    const FrameSnapshot ordinary = render(false);
    const FrameSnapshot instanced = render(true);

    // The calibration first: if the ordinary route does not carry the stream colour, this leg is
    // measuring its own bug rather than the instanced route's.
    ExpectColumns(ordinary, true, Route::Ordinary, "true/ordinary-route");
    ExpectColumns(instanced, true, Route::Instanced, "true/instanced-route");
    ExpectRoutesAgree(ordinary, instanced, "VertexColorEnabled=true");
}

TEST_F(InstancedVertexColorTest, VertexColorEnabledFalseIgnoresGeometryColorOnBothRoutes)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedVertexColor())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false);
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(kColumnCount);
    const std::array<MatrixRecord, 1> instances{ShiftMatrix(0)};

    VertexBuffer meshBuffer(device, PackedDeclaration(), kMeshVertexCount, BufferUsage::None);
    meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, 16);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshIndexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshIndexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);

    const auto render = [&](bool instanced) {
        RenderTarget2D target = MakeTarget();
        if (instanced)
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                                     VertexBufferBinding(&instanceBuffer, 0, 1)});
        else
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyEffect(effect, false);
        if (instanced)
            device.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
        else
            device.DrawIndexedPrimitives(
                PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount);
        device.SetRenderTarget(nullptr);
        return CaptureTarget(target);
    };

    const FrameSnapshot ordinary = render(false);
    const FrameSnapshot instanced = render(true);

    // Every column must be DiffuseColor: a bound COLOR0 stream that the effect did not ask for
    // must not reach the result on either route.
    ExpectColumns(ordinary, false, Route::Ordinary, "false/ordinary-route");
    ExpectColumns(instanced, false, Route::Instanced, "false/instanced-route");
    ExpectRoutesAgree(ordinary, instanced, "VertexColorEnabled=false");
}

// ---------------------------------------------------------------------------
// The second COLOR0-bearing declaration: VertexPositionColorTexture (stride 24). Its geometry
// layout differs from the stride-16 one, so it must select its own pipeline variant and must not
// reuse the stride-16 one -- with TextureEnabled off, the colour contract is identical.
//
// A DECLARED BOUNDARY, measured rather than assumed: WebGPU's ORDINARY route rejects this exact
// call. `DrawIndexedPrimitivesEx` sends stride 24 to QueueTexturedDraw only when `texture0` is
// non-null, so a stride-24 draw with TextureEnabled=false falls through to
// DrawIndexedColoredPrimitives, whose QueueColoredDraw throws "requires a stride-16
// (VertexPositionColor) vertex buffer". That is an ordinary-route defect of a different class --
// a supported public call that throws rather than one that renders the wrong colour -- it
// reproduces identically on unfixed HEAD, and it is recorded as REMED-GFX-214, not fixed here.
// The instanced half of this leg is REMED-GFX-212's own and is asserted unconditionally; the
// route-agreement half is asserted only where the ordinary route actually rendered, and the
// rejection text is printed when it did not.
// ---------------------------------------------------------------------------

TEST_F(InstancedVertexColorTest, PackedColorTextureStrideConsumesGeometryColorOnBothRoutes)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedVertexColor())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedTexVertex> mesh = BuildPackedTexMesh();
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(kColumnCount);
    const std::array<MatrixRecord, 1> instances{ShiftMatrix(0)};

    VertexBuffer meshBuffer(device, PackedTexDeclaration(), kMeshVertexCount, BufferUsage::None);
    meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, 24);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshIndexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshIndexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);

    std::string ordinaryRejection;
    const auto render = [&](bool instanced) {
        RenderTarget2D target = MakeTarget();
        if (instanced)
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                                     VertexBufferBinding(&instanceBuffer, 0, 1)});
        else
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyEffect(effect, true);
        try
        {
            if (instanced)
                device.DrawInstancedPrimitives(
                    PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
            else
                device.DrawIndexedPrimitives(
                    PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount);
        }
        catch (const std::exception& e)
        {
            if (!instanced)
                ordinaryRejection = e.what();
            else
                throw;
        }
        device.SetRenderTarget(nullptr);
        return CaptureTarget(target);
    };

    const FrameSnapshot ordinary = render(false);
    const FrameSnapshot instanced = render(true);

    // REMED-GFX-212's own half. The instanced route must produce the analytic product for this
    // declaration too, whatever the ordinary route does with it.
    ExpectColumns(instanced, true, Route::Instanced, "stride24/instanced-route");
    if (ordinaryRejection.empty())
    {
        ExpectColumns(ordinary, true, Route::Ordinary, "stride24/ordinary-route");
        ExpectRoutesAgree(ordinary, instanced, "stride 24, VertexColorEnabled=true");
    }
    else
    {
        std::cout << "[ GFX-212  ] " << RendererName()
                  << " stride24/ordinary-route: REJECTED -- " << ordinaryRejection
                  << " (REMED-GFX-214, an ordinary-route boundary this leg does not close)"
                  << std::endl;
    }
}

// ---------------------------------------------------------------------------
// 32-bit indices: the same contract must not depend on the index element size.
// ---------------------------------------------------------------------------

TEST_F(InstancedVertexColorTest, ThirtyTwoBitIndicesConsumeGeometryColorOnTheInstancedRoute)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedVertexColor())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false);
    const std::vector<std::uint32_t> indices = BuildQuadIndices<std::uint32_t>(kColumnCount);
    const std::array<MatrixRecord, 1> instances{ShiftMatrix(0)};

    VertexBuffer meshBuffer(device, PackedDeclaration(), kMeshVertexCount, BufferUsage::None);
    meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, 16);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, kMeshIndexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshIndexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);
    RenderTarget2D target = MakeTarget();
    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 1)});
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyEffect(effect, true);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
    device.SetRenderTarget(nullptr);

    ExpectColumns(CaptureTarget(target), true, Route::Instanced, "index32/instanced-route");
}

// ---------------------------------------------------------------------------
// REMED-GFX-211 and REMED-GFX-213 compose with the colour: a nonzero geometry VertexOffset must
// skip the decoy quad AND the surviving quads must keep their own COLOR0 records, and an instance
// divisor greater than one must repeat a record without touching the colour.
// ---------------------------------------------------------------------------

TEST_F(InstancedVertexColorTest, GeometryVertexOffsetSkipsTheDecoyAndKeepsItsOwnColors)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedVertexColor())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(true);
    const int vertexCount = kDecoyVertexCount + kMeshVertexCount;
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(kColumnCount);
    const std::array<MatrixRecord, 1> instances{ShiftMatrix(0)};

    VertexBuffer meshBuffer(device, PackedDeclaration(), vertexCount, BufferUsage::None);
    meshBuffer.SetDataRaw(mesh.data(), vertexCount, 16);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshIndexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshIndexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);
    RenderTarget2D target = MakeTarget();
    // VertexOffset skips the decoy quad; every live column then renders its own colour, so a
    // colour taken from the wrong record reads as the decoy's pure green in column 0.
    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kDecoyVertexCount, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 1)});
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyEffect(effect, true);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
    device.SetRenderTarget(nullptr);

    ExpectColumns(CaptureTarget(target), true, Route::Instanced, "vertexOffset/instanced-route");
}

TEST_F(InstancedVertexColorTest, InstanceFrequencyTwoRepeatsARecordWithoutTouchingTheColor)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedVertexColor())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    // One quad, in column 0, carrying column 0's own COLOR0. Two instances at frequency two both
    // consume record 0 (a zero shift), so column 0 is drawn twice and nothing else is drawn at
    // all -- the colour must still be column 0's own product.
    const std::vector<PackedVertex> mesh =
        BuildSingleQuadMesh(0, kColumnColors[0]);
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(1);
    const std::array<MatrixRecord, 2> instances{ShiftMatrix(0), ShiftMatrix(2)};

    VertexBuffer meshBuffer(device, PackedDeclaration(), kVerticesPerQuad, BufferUsage::None);
    meshBuffer.SetDataRaw(mesh.data(), kVerticesPerQuad, 16);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 2, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 2, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kIndicesPerQuad, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kIndicesPerQuad);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);
    RenderTarget2D target = MakeTarget();
    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 2)});
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyEffect(effect, true);
    device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, kVerticesPerQuad, 0, 2, 2);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    PrintMeasurement("frequency2/instanced-route", snapshot);
    if (InstancedVertexColorMeasured())
    {
        int spread = 0;
        int lit = 0;
        const Rgba sample = SampleColumn(snapshot, 0, spread, lit);
        const Rgba expected = AssertedColor(kColumnColors[0], true, Route::Instanced);
        EXPECT_GT(lit, 0) << "frequency 2: column 0 rendered nothing" << DescribeFrame(snapshot);
        EXPECT_TRUE(NearlyEqual(sample, expected))
            << "frequency 2: column 0 carried " << sample.ToString() << ", expected "
            << expected.ToString() << kAssertionBasis << DescribeFrame(snapshot);
        // Both instances took record 0, so the column-2 shift must NOT appear.
        int decoySpread = 0;
        int decoyLit = 0;
        (void)SampleColumn(snapshot, 2, decoySpread, decoyLit);
        EXPECT_EQ(decoyLit, 0)
            << "frequency 2: instance 1 consumed record 1 rather than repeating record 0"
            << DescribeFrame(snapshot);
    }
}

// ---------------------------------------------------------------------------
// Property transitions. Nothing a previous draw selected -- shader, layout, pipeline or uniform --
// may reach the next one.
// ---------------------------------------------------------------------------

TEST_F(InstancedVertexColorTest, VertexColorEnabledTransitionsDoNotLeakBetweenFrames)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedVertexColor())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false);
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(kColumnCount);
    const std::array<MatrixRecord, 1> instances{ShiftMatrix(0)};

    VertexBuffer meshBuffer(device, PackedDeclaration(), kMeshVertexCount, BufferUsage::None);
    meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, 16);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshIndexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshIndexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);

    const auto render = [&](bool instanced, bool vertexColorEnabled) {
        RenderTarget2D target = MakeTarget();
        if (instanced)
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                                     VertexBufferBinding(&instanceBuffer, 0, 1)});
        else
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyEffect(effect, vertexColorEnabled);
        if (instanced)
            device.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
        else
            device.DrawIndexedPrimitives(
                PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount);
        device.SetRenderTarget(nullptr);
        return CaptureTarget(target);
    };

    // 1: false -> true.  2: true -> false.  3: instanced false -> instanced true.
    // 4: ordinary true -> instanced true.   5: instanced true -> ordinary true.
    // 6: return to an earlier complete state, twice, so a cache that produced a correct object
    //    once must produce it again.
    ExpectColumns(render(true, false), false, Route::Instanced, "transition/instanced-false-1");
    ExpectColumns(render(true, true), true, Route::Instanced, "transition/instanced-true-1");
    ExpectColumns(render(true, false), false, Route::Instanced, "transition/instanced-false-2");
    ExpectColumns(render(false, true), true, Route::Ordinary, "transition/ordinary-true");
    ExpectColumns(render(true, true), true, Route::Instanced, "transition/instanced-true-2");
    ExpectColumns(render(false, true), true, Route::Ordinary, "transition/ordinary-true-2");
    ExpectColumns(render(true, true), true, Route::Instanced, "transition/instanced-true-3");
}

// ---------------------------------------------------------------------------
// Deferred capture and lifetime. Two instanced draws are queued into ONE render-target cycle with
// different VertexColorEnabled settings and different buffer contents between them, and only the
// final SetRenderTarget(nullptr) flushes. Each draw must replay its OWN captured state and its OWN
// captured geometry -- no read of live effect or GraphicsDevice state at replay time.
// ---------------------------------------------------------------------------

TEST_F(InstancedVertexColorTest, QueuedInstancedDrawsKeepTheirOwnVertexColorStateAndData)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedVertexColor())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    // Draw A owns columns 0 and 1, draw B owns columns 2 and 3, so both survive in one frame.
    std::vector<PackedVertex> meshA;
    std::vector<PackedVertex> meshB;
    for (int column = 0; column < 2; ++column)
    {
        const auto quadA = BuildSingleQuadMesh(column, kColumnColors[static_cast<std::size_t>(column)]);
        meshA.insert(meshA.end(), quadA.begin(), quadA.end());
        const auto quadB =
            BuildSingleQuadMesh(column + 2, kColumnColors[static_cast<std::size_t>(column + 2)]);
        meshB.insert(meshB.end(), quadB.begin(), quadB.end());
    }
    const int quadCount = 2;
    const int vertexCount = quadCount * kVerticesPerQuad;
    const int indexCount = quadCount * kIndicesPerQuad;
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(quadCount);
    const std::array<MatrixRecord, 1> instances{ShiftMatrix(0)};

    // ONE geometry buffer, rewritten between the two queued draws.
    VertexBuffer meshBuffer(device, PackedDeclaration(), vertexCount, BufferUsage::None);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, indexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), indexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);
    RenderTarget2D target = MakeTarget();
    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 1)});
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    meshBuffer.SetDataRaw(meshA.data(), vertexCount, 16);
    ApplyEffect(effect, true);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, vertexCount, 0, quadCount * 2, 1);

    // Everything draw A captured now changes: the effect's VertexColorEnabled AND the bytes of the
    // very buffer it drew from.
    meshBuffer.SetDataRaw(meshB.data(), vertexCount, 16);
    ApplyEffect(effect, false);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, vertexCount, 0, quadCount * 2, 1);

    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    PrintMeasurement("deferred/two-queued-draws", snapshot);
    if (InstancedVertexColorMeasured())
    {
        for (int column = 0; column < 2; ++column)
        {
            int spread = 0;
            int lit = 0;
            const Rgba sample = SampleColumn(snapshot, column, spread, lit);
            const Rgba expected =
                AssertedColor(kColumnColors[static_cast<std::size_t>(column)], true, Route::Instanced);
            EXPECT_GT(lit, 0) << "deferred: draw A's column " << column << " rendered nothing"
                              << DescribeFrame(snapshot);
            EXPECT_TRUE(NearlyEqual(sample, expected))
                << "deferred: draw A's column " << column << " carried " << sample.ToString()
                << " -- it must keep its own VertexColorEnabled=true and its own captured geometry, "
                   "expected " << expected.ToString() << kAssertionBasis << DescribeFrame(snapshot);
        }
        for (int column = 2; column < kColumnCount; ++column)
        {
            int spread = 0;
            int lit = 0;
            const Rgba sample = SampleColumn(snapshot, column, spread, lit);
            const Rgba expected =
                AssertedColor(kColumnColors[static_cast<std::size_t>(column)], false, Route::Instanced);
            EXPECT_GT(lit, 0) << "deferred: draw B's column " << column << " rendered nothing"
                              << DescribeFrame(snapshot);
            EXPECT_TRUE(NearlyEqual(sample, expected))
                << "deferred: draw B's column " << column << " carried " << sample.ToString()
                << " -- it must keep its own VertexColorEnabled=false, expected "
                << expected.ToString() << kAssertionBasis << DescribeFrame(snapshot);
        }
    }
}

// ---------------------------------------------------------------------------
// Wrapper destruction and address reuse: a queued draw's geometry buffer is destroyed before the
// flush. The command must have captured everything it needs.
// ---------------------------------------------------------------------------

TEST_F(InstancedVertexColorTest, QueuedInstancedDrawSurvivesItsGeometryWrapperBeingDestroyed)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedVertexColor())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false);
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(kColumnCount);
    const std::array<MatrixRecord, 1> instances{ShiftMatrix(0)};

    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshIndexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshIndexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);
    RenderTarget2D target = MakeTarget();
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    auto doomed = std::make_unique<VertexBuffer>(
        device, PackedDeclaration(), kMeshVertexCount, BufferUsage::None);
    doomed->SetDataRaw(mesh.data(), kMeshVertexCount, 16);
    device.SetVertexBuffers({VertexBufferBinding(doomed.get(), 0, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 1)});
    ApplyEffect(effect, true);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount, 1);

    // Rebind to a different buffer, then destroy the one the queued draw named. A fresh buffer of
    // exactly the same size and declaration is very likely to reuse the freed address, so a
    // command that resolved its geometry through the wrapper pointer at replay would render this
    // decoy's pure green instead of the four live columns.
    const std::vector<PackedVertex> decoyQuad = BuildSingleQuadMesh(0, kDecoyColor);
    std::vector<PackedVertex> filler;
    while (static_cast<int>(filler.size()) < kMeshVertexCount)
        filler.push_back(decoyQuad[filler.size() % kVerticesPerQuad]);
    VertexBuffer reused(device, PackedDeclaration(), kMeshVertexCount, BufferUsage::None);
    reused.SetDataRaw(filler.data(), kMeshVertexCount, 16);
    device.SetVertexBuffers({VertexBufferBinding(&reused, 0, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 1)});
    doomed.reset();
    device.SetRenderTarget(nullptr);

    ExpectColumns(CaptureTarget(target), true, Route::Instanced, "lifetime/destroyed-wrapper");
}

