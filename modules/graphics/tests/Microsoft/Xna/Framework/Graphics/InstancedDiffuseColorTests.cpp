// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-215 -- `BasicEffect.DiffuseColor` AND `BasicEffect.VertexColorEnabled` on the
// instanced draw route.
//
// THE AUTHORITATIVE CONTRACT, cited from the reference rather than summarised:
//
//   * FNA `Common.fxh:36-45` -- `ComputeCommonVSOutput(position)` opens with
//     `vout.Diffuse = DiffuseColor;`. Every BasicEffect vertex shader begins there, so DiffuseColor
//     is the *base* of the diffuse term, never an optional extra.
//   * FNA `BasicEffect.fx:78-88` -- `VSBasicVc`, the vertex-colour permutation, is that same
//     `ComputeCommonVSOutput` followed by exactly one more line: `vout.Diffuse *= vin.Color;`.
//     Its seven siblings (lines 99, 142, 157, 183, 212, 238, 267) repeat the identical multiply.
//   * FNA `BasicEffect.fx:66-73` -- `VSBasic`, the NON-vertex-colour permutation, is
//     `ComputeCommonVSOutput` and nothing else. `vin.Color` appears nowhere in it.
//   * FNA `BasicEffect.fx:336/347` -- `PSBasic` is `float4 color = pin.Diffuse;`, so the vertex
//     stage's `Diffuse` IS the pixel, with no further colour term.
//   * FNA `GraphicsDevice.cs:1257` -- `DrawInstancedPrimitives` is `ApplyState()` +
//     `PrepareVertexBindingArray(baseVertex)` + the FNA3D call. It touches no effect state, and
//     `BasicEffect.OnApply` (FNA `BasicEffect.cs:490-511`) derives its shader index from fog,
//     vertex colour, texture and lighting -- there is no instancing term anywhere.
//
// So, for ordinary, indexed-ordinary and instanced draws alike:
//
//   VertexColorEnabled = false -> result is DiffuseColor. COLOR0 must not modulate it, and a bound
//                                 colour attribute must not silently override the disabled property.
//   VertexColorEnabled = true  -> result is DiffuseColor * COLOR0. Raw COLOR0 alone is WRONG unless
//                                 DiffuseColor happens to be exactly neutral.
//
// WHY THIS FILE EXISTS SEPARATELY FROM InstancedVertexColorTests.cpp. That file's oracle already
// separates "COLOR0 applied" from "COLOR0 dropped". It cannot separate "DiffuseColor applied" from
// "DiffuseColor dropped" on a renderer that consumes COLOR0, because REMED-GFX-212's own checkpoint
// triage used a WHITE DiffuseColor -- and under white,
//
//                        COLOR0 * white  ==  COLOR0
//
// so a shader that emits the raw COLOR0 produces byte-identical pixels to a correct one. bgfx was
// certified a "correct control" by exactly that degenerate input. THIS file's DiffuseColor is
// non-neutral in every channel, so the two are never equal, and its neutral-DiffuseColor leg
// asserts the degeneracy itself so the false positive can never be reconstructed by accident.
//
// THE COLOUR ORACLE. With D = (0.80, 0.35, 0.55) and column 0's C = (255, 128, 64), every failure
// mode this class of defect produces lands on a different measurable colour:
//
//   DiffuseColor * COLOR0 (correct) -> (204,  45,  35)
//   DiffuseColor ignored            -> (255, 128,  64)   == COLOR0 itself, the measured pre-fix bgfx
//   COLOR0 ignored                  -> (204,  89, 140)   == DiffuseColor
//   COLOR0 applied twice            -> (204,  22,   9)
//   channel swizzle (BGR)           -> ( 35,  45, 204)
//   read un-normalized              -> (255, 255, 255)   every product saturates
//   stale colour from a prior draw  -> that draw's own known colour, in a known column
//   instance record read as colour  -> the world matrix's own bytes, nowhere near any of these
//   constant fallback               -> opaque white or black, neither of which any leg expects
//
// Four columns carry four different COLOR0 records, and column 3 is deliberately opaque white --
// the one record whose product IS DiffuseColor, which is what separates "DiffuseColor was dropped"
// from "COLOR0 was dropped" inside a single frame.

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
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

// plans/plan_runtimerenderer.md RTR-P9-9: this file's bgfx blocks call bgfx:: directly and hold a
// BgfxRenderer pointer, so they stay COMPILE-time -- no runtime predicate makes a type exist. The
// condition widens from the DEFAULT renderer's macro to "compiled into this build", so a
// multi-renderer build holding bgfx without selecting it still compiles them; each test inside then
// checks at runtime that bgfx is the ACTIVE renderer.
#if defined(CNA_RENDERER_BGFX) || defined(CNA_RENDERER_PRESENT_BGFX)
#define CNA_TEST_BGFX_AVAILABLE 1
#endif

#ifdef CNA_TEST_BGFX_AVAILABLE
#include <bgfx/bgfx.h>
#endif

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
// result back -- InstancedVertexColorTests.cpp's own suite set, for the same reason.
/// plans/plan_runtimerenderer.md RTR-P9-5: the same renderer set, evaluated at runtime so this
/// describes the ACTIVE renderer rather than the build default.
[[nodiscard]] inline bool InstancedDiffuse()
{
    return CNA_RENDERER_IS(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, 
                            DirectX12);
}

// The renderers whose instanced route this file has MEASURED on a real display. D3D9/D3D11/D3D12
// stay outside it because no D3D display is reachable here (SDL reports "x11 not available" under
// Wine on the Xvfb displays this environment permits), and an unmeasured renderer must not be
// asserted in either direction. Every leg still PRINTS its reading there, which is the evidence
// those renderers lack.
//
// UNLIKE InstancedVertexColorTests.cpp, this file grants NO renderer an exemption: every measured
// renderer is asserted against the CONTRACT above, never against its own measured behaviour. That is
// what makes it red-first -- it failed on bgfx before REMED-GFX-215 and passes after.
/// plans/plan_runtimerenderer.md RTR-P9-5: the measured set, asked of the ACTIVE renderer.
[[nodiscard]] inline bool InstancedDiffuseMeasured()
{
    return CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, Bgfx, Vulkan, WebGPU);
}


namespace
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a hand-maintained #if/#elif chain of renderer display
    // names, which had to be extended for every new renderer and answered "unknown" when it was
    // not. The runtime API already knows the active renderer's name, and knows it for all 46.
    inline std::string RendererName()
    {
        return std::string(CNA::getGraphicsRendererName(
            CNA::GraphicsRendererSelection::GetSelected()));
    }

    /// Square render target: 256 = 4 * 64, so the column axis divides exactly.
    constexpr int kTargetSize = 256;

    constexpr int kColumnCount = 4;
    constexpr int kColumnWidth = kTargetSize / kColumnCount;

    /// Pixels the geometry stays away from a column boundary, so neighbouring quads never touch.
    constexpr int kGeometryInset = 6;

    /// Pixels the sampling box stays away from a column boundary -- comfortably inside the geometry
    /// inset, so no sampled pixel can be a rasterization edge case.
    constexpr int kSampleInset = 20;

    constexpr int kVerticesPerQuad = 4;
    constexpr int kIndicesPerQuad = 6;
    constexpr int kMeshVertexCount = kColumnCount * kVerticesPerQuad;
    constexpr int kMeshIndexCount = kColumnCount * kIndicesPerQuad;
    constexpr int kMeshPrimitiveCount = kMeshIndexCount / 3;

    /// The decoy quad every "skip the prefix" leg puts in front of the live mesh.
    constexpr int kDecoyVertexCount = kVerticesPerQuad;

    /// The NON-NEUTRAL `BasicEffect.DiffuseColor` this file exists for. No channel is 0 or 1 and no
    /// two channels are equal, so a dropped, doubled or swizzled multiplication all land elsewhere.
    constexpr float kDiffuseR = 0.80f;
    constexpr float kDiffuseG = 0.35f;
    constexpr float kDiffuseB = 0.55f;

    /// The degenerate value REMED-GFX-212's triage used. Under it, `COLOR0 * D == COLOR0`, which is
    /// precisely why that triage certified bgfx correct while its shader emitted the raw COLOR0.
    constexpr float kNeutral = 1.0f;

    /// A second non-neutral DiffuseColor, for the legs that must prove a CHANGED DiffuseColor
    /// reaches the shader rather than a cached first value.
    constexpr float kAltDiffuseR = 0.25f;
    constexpr float kAltDiffuseG = 0.90f;
    constexpr float kAltDiffuseB = 0.40f;

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
    /// product IS DiffuseColor, which separates "DiffuseColor dropped" from "COLOR0 dropped" in the
    /// same frame.
    constexpr std::array<Rgba, kColumnCount> kColumnColors{
        Rgba{255, 128, 64, 255},
        Rgba{64, 255, 128, 255},
        Rgba{128, 64, 255, 255},
        Rgba{255, 255, 255, 255},
    };

    /// The same records with four different NON-TRIVIAL alphas, so the alpha channel carries its own
    /// independent reading of both terms rather than a constant 255.
    constexpr std::array<Rgba, kColumnCount> kAlphaColumnColors{
        Rgba{255, 128, 64, 255},
        Rgba{64, 255, 128, 192},
        Rgba{128, 64, 255, 128},
        Rgba{255, 255, 255, 64},
    };

    /// The replacement records the "buffer contents changed" leg writes over the originals. Every
    /// one differs from its predecessor in every channel, so a cached or stale read is unmistakable.
    constexpr std::array<Rgba, kColumnCount> kReplacementColors{
        Rgba{64, 255, 128, 255},
        Rgba{128, 64, 255, 255},
        Rgba{255, 255, 255, 255},
        Rgba{255, 128, 64, 255},
    };

    /// The decoy quad's COLOR0 -- pure green, which neither a live column nor any product of one
    /// with either DiffuseColor can produce.
    constexpr Rgba kDecoyColor{0, 255, 0, 255};

    /// One `BasicEffect` colour state: the three DiffuseColor channels plus Alpha.
    struct DiffuseState
    {
        float r = kDiffuseR;
        float g = kDiffuseG;
        float b = kDiffuseB;
        float a = 1.0f;
    };

    constexpr DiffuseState kNonNeutral{kDiffuseR, kDiffuseG, kDiffuseB, 1.0f};
    constexpr DiffuseState kNeutralState{kNeutral, kNeutral, kNeutral, 1.0f};
    constexpr DiffuseState kAltState{kAltDiffuseR, kAltDiffuseG, kAltDiffuseB, 1.0f};
    constexpr DiffuseState kHalfAlphaState{kDiffuseR, kDiffuseG, kDiffuseB, 0.5f};

    int Quantize(float linear)
    {
        const float clamped = linear < 0.0f ? 0.0f : (linear > 1.0f ? 1.0f : linear);
        return static_cast<int>(clamped * 255.0f + 0.5f);
    }

    /// The public contract's own arithmetic, and nothing else.
    ///
    /// `BasicEffect::FillGpuDrawParams` forwards `diffuseColor = (DiffuseColor.rgb * Alpha, Alpha)`,
    /// exactly as FNA's `EffectHelpers.SetMaterialColor` builds it, so the base term already carries
    /// Alpha in all four channels. `VertexColorEnabled` then multiplies COLOR0 into all four --
    /// FNA's `vout.Diffuse *= vin.Color` is a float4 multiply, not an RGB one.
    Rgba ExpectedColor(const Rgba& color0, bool vertexColorEnabled, const DiffuseState& d)
    {
        const float baseR = d.r * d.a;
        const float baseG = d.g * d.a;
        const float baseB = d.b * d.a;
        const float baseA = d.a;
        if (!vertexColorEnabled)
            return Rgba{Quantize(baseR), Quantize(baseG), Quantize(baseB), Quantize(baseA)};
        return Rgba{
            Quantize(baseR * static_cast<float>(color0.r) / 255.0f),
            Quantize(baseG * static_cast<float>(color0.g) / 255.0f),
            Quantize(baseB * static_cast<float>(color0.b) / 255.0f),
            Quantize(baseA * static_cast<float>(color0.a) / 255.0f)};
    }

    /// The pre-fix bgfx reading: the raw COLOR0, with neither term applied. Not asserted anywhere --
    /// it is printed by the false-positive leg so the record carries the wrong answer next to the
    /// right one.
    Rgba RawColor0(const Rgba& color0) { return color0; }

    /// Slot 0's record: XNA's VertexPositionColor.
    struct PackedVertex
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(PackedVertex) == 16);

    /// XNA's VertexPositionColorTexture -- a second COLOR0-bearing stride, so the layout/program
    /// axis is exercised too.
    struct PackedTexVertex
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
        float u, v;
    };
    static_assert(sizeof(PackedTexVertex) == 24);

    /// A position-ONLY record: no COLOR0 element at all. What `VertexColorEnabled = false` must
    /// render without requiring a colour attribute.
    struct PositionOnlyVertex
    {
        float x, y, z;
    };
    static_assert(sizeof(PositionOnlyVertex) == 12);

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

    VertexDeclaration PositionOnlyDeclaration()
    {
        return VertexDeclaration(
            12,
            {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            });
    }

    /// The per-instance record: the whole world matrix in one binding.
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

    /// A column-major world matrix that translates by @p columnShift whole columns along X.
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
    /// column taken from @p colors.
    std::vector<PackedVertex> BuildPackedMesh(
        bool withDecoy, const std::array<Rgba, kColumnCount>& colors)
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
            emit(column, colors[static_cast<std::size_t>(column)]);
        return vertices;
    }

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

    std::vector<PositionOnlyVertex> BuildPositionOnlyMesh()
    {
        std::vector<PositionOnlyVertex> vertices;
        for (int column = 0; column < kColumnCount; ++column)
        {
            const auto corners = QuadCorners(column);
            for (int i = 0; i < kVerticesPerQuad; ++i)
                vertices.push_back(PositionOnlyVertex{
                    corners[static_cast<std::size_t>(i)][0],
                    corners[static_cast<std::size_t>(i)][1],
                    0.5f});
        }
        return vertices;
    }

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

    /// The colour column @p column carries, plus how many sampled pixels differ from the column's
    /// own first sample. A nonzero spread means the quad is not flat, which no correct reading of a
    /// flat-coloured quad can produce.
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

    /// Every column's reading in one line, printed by every leg whether it passes or fails -- a gate
    /// that merely excludes a renderer records only that somebody once believed it broken.
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
        std::cout << "[ GFX-215  ] " << RendererName() << ' ' << leg << ':'
                  << DescribeFrame(snapshot) << std::endl;
    }

    /// Every distinct non-black colour anywhere in the frame, with how many pixels carry it. This
    /// reads the WHOLE target rather than a per-column box, so it stays meaningful even when a
    /// renderer places the geometry wrongly -- what colour was written is then still answerable even
    /// though where is not.
    std::vector<std::pair<Rgba, int>> DistinctLitColors(const FrameSnapshot& snapshot)
    {
        std::vector<std::pair<Rgba, int>> found;
        for (int y = 0; y < kTargetSize; ++y)
        {
            for (int x = 0; x < kTargetSize; ++x)
            {
                const Color pixel = snapshot.At(x, y);
                const Rgba sample{pixel.getRProperty(), pixel.getGProperty(),
                                  pixel.getBProperty(), pixel.getAProperty()};
                if (sample.r == 0 && sample.g == 0 && sample.b == 0)
                    continue;
                bool merged = false;
                for (auto& entry : found)
                {
                    if (entry.first.r == sample.r && entry.first.g == sample.g &&
                        entry.first.b == sample.b && entry.first.a == sample.a)
                    {
                        ++entry.second;
                        merged = true;
                        break;
                    }
                }
                if (!merged)
                    found.emplace_back(sample, 1);
            }
        }
        return found;
    }

    std::string DescribeLitColors(const std::vector<std::pair<Rgba, int>>& colors)
    {
        std::ostringstream os;
        for (const auto& entry : colors)
            os << "\n    " << entry.first.ToString() << " x" << entry.second;
        if (colors.empty())
            os << "\n    (nothing lit)";
        return os.str();
    }

    /// 8-bit render-target quantization tolerance. Every failure mode the oracle separates differs
    /// by at least 20 in some channel, so this stays far below the smallest real distinction.
    constexpr int kTolerance = 3;

    bool NearlyEqual(const Rgba& a, const Rgba& b)
    {
        const auto close = [](int lhs, int rhs) {
            return (lhs > rhs ? lhs - rhs : rhs - lhs) <= kTolerance;
        };
        return close(a.r, b.r) && close(a.g, b.g) && close(a.b, b.b) && close(a.a, b.a);
    }
}   // namespace

class InstancedDiffuseColorTest : public ::testing::Test
{
protected:
    GraphicsDevice device;

    /// GTEST_SKIP() only suppresses the remaining TEST BODY when raised from SetUp() --
    /// raised inside a helper the body calls, it merely returns from the helper (the exact
    /// reason the multi-stream skip is a macro). Hardware instancing is this whole file's
    /// subject, so a renderer profile reporting GraphicsCapability::Instancing = false
    /// (e.g. OPENGLES2 -- core OpenGL ES 2.0 has no glDrawElementsInstanced/
    /// glVertexAttribDivisor, docs/opengles2-renderer.md) skips every leg here up front.
    void SetUp() override
    {
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
    static void ApplyEffect(BasicEffect& effect, bool vertexColorEnabled, const DiffuseState& d)
    {
        effect.VertexColorEnabled = vertexColorEnabled;
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setFogEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(d.r, d.g, d.b));
        effect.setAlphaProperty(d.a);
        effect.Apply();
    }

    /// Asserts every column against the CONTRACT -- never against measured behaviour -- and prints
    /// the whole frame either way.
    static void ExpectColumns(
        const FrameSnapshot& snapshot, bool vertexColorEnabled, const DiffuseState& d,
        const std::array<Rgba, kColumnCount>& colors, const char* leg)
    {
        PrintMeasurement(leg, snapshot);
        if (InstancedDiffuseMeasured())
        {
            for (int column = 0; column < kColumnCount; ++column)
            {
                int spread = 0;
                int lit = 0;
                const Rgba sample = SampleColumn(snapshot, column, spread, lit);
                const Rgba expected =
                    ExpectedColor(colors[static_cast<std::size_t>(column)], vertexColorEnabled, d);
                EXPECT_GT(lit, 0) << leg << ": column " << column << " rendered nothing"
                                  << DescribeFrame(snapshot);
                EXPECT_EQ(spread, 0) << leg << ": column " << column << " is not flat"
                                     << DescribeFrame(snapshot);
                EXPECT_TRUE(NearlyEqual(sample, expected))
                    << leg << ": column " << column << " carried " << sample.ToString()
                    << ", expected " << expected.ToString()
                    << " (DiffuseColor=(" << d.r << ',' << d.g << ',' << d.b << ") Alpha=" << d.a
                    << " VertexColorEnabled=" << (vertexColorEnabled ? "true" : "false")
                    << " COLOR0=" << colors[static_cast<std::size_t>(column)].ToString()
                    << "). REMED-GFX-215: raw COLOR0 would read "
                    << RawColor0(colors[static_cast<std::size_t>(column)]).ToString()
                    << " and a dropped COLOR0 "
                    << ExpectedColor(colors[static_cast<std::size_t>(column)], false, d).ToString()
                    << DescribeFrame(snapshot);
            }
        }
        else
        {
            (void)vertexColorEnabled;
            (void)d;
            (void)colors;
            (void)leg;
        }
    }

    /// The route-agreement half: BasicEffect's shader index has no instancing term, so the two
    /// frames must be the same frame.
    static void ExpectRoutesAgree(
        const FrameSnapshot& ordinary, const FrameSnapshot& instanced, const char* leg)
    {
        if (InstancedDiffuseMeasured())
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
                    << ". BasicEffect's shader index has no instancing term, so the same DiffuseColor "
                       "and VertexColorEnabled calculation must run for both";
            }
        }
        else
        {
            (void)ordinary;
            (void)instanced;
            (void)leg;
        }
    }

    /// One complete frame of the four-column mesh, through whichever route @p instanced selects.
    /// Returns the read-back frame.
    FrameSnapshot RenderMesh(
        VertexBuffer& meshBuffer, VertexBuffer& instanceBuffer, BasicEffect& effect,
        bool instanced, bool vertexColorEnabled, const DiffuseState& d)
    {
        RenderTarget2D target = MakeTarget();
        if (instanced)
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                                     VertexBufferBinding(&instanceBuffer, 0, 1)});
        else
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyEffect(effect, vertexColorEnabled, d);
        if (instanced)
            device.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
        else
            device.DrawIndexedPrimitives(
                PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount);
        device.SetRenderTarget(nullptr);
        return CaptureTarget(target);
    }
};

// ---------------------------------------------------------------------------
// The core contract: DiffuseColor is the base term, VertexColorEnabled chooses whether COLOR0
// multiplies it, and instancing changes neither.
// ---------------------------------------------------------------------------

TEST_F(InstancedDiffuseColorTest, NonNeutralDiffuseColorMultipliesVertexColorOnBothRoutes)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false, kColumnColors);
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

    const FrameSnapshot ordinary =
        RenderMesh(meshBuffer, instanceBuffer, effect, false, true, kNonNeutral);
    const FrameSnapshot instanced =
        RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kNonNeutral);

    ExpectColumns(ordinary, true, kNonNeutral, kColumnColors, "true/nonneutral/ordinary-route");
    ExpectColumns(instanced, true, kNonNeutral, kColumnColors, "true/nonneutral/instanced-route");
    ExpectRoutesAgree(ordinary, instanced, "true/nonneutral");
}

TEST_F(InstancedDiffuseColorTest, VertexColorDisabledYieldsDiffuseColorOnBothRoutes)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false, kColumnColors);
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

    // A COLOR0 attribute IS bound in every column and it must not reach the pixel: all four columns
    // must read the SAME DiffuseColor, even though all four carry different vertex colours.
    const FrameSnapshot ordinary =
        RenderMesh(meshBuffer, instanceBuffer, effect, false, false, kNonNeutral);
    const FrameSnapshot instanced =
        RenderMesh(meshBuffer, instanceBuffer, effect, true, false, kNonNeutral);

    ExpectColumns(ordinary, false, kNonNeutral, kColumnColors, "false/nonneutral/ordinary-route");
    ExpectColumns(instanced, false, kNonNeutral, kColumnColors, "false/nonneutral/instanced-route");
    ExpectRoutesAgree(ordinary, instanced, "false/nonneutral");
}

// ---------------------------------------------------------------------------
// The false positive itself. This leg asserts the DEGENERACY, so the mistake REMED-GFX-212's
// triage made can never be reconstructed by accident: with a neutral DiffuseColor the correct
// answer and the raw COLOR0 are the same bytes, and only the non-neutral legs above can tell them
// apart.
// ---------------------------------------------------------------------------

TEST_F(InstancedDiffuseColorTest, NeutralDiffuseColorCannotDistinguishRawColor0)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    // The arithmetic first, with no GPU involved: this is why a white-DiffuseColor oracle certified
    // bgfx correct while its instanced shader emitted the raw COLOR0.
    for (int column = 0; column < kColumnCount; ++column)
    {
        const Rgba& c = kColumnColors[static_cast<std::size_t>(column)];
        EXPECT_TRUE(NearlyEqual(ExpectedColor(c, true, kNeutralState), RawColor0(c)))
            << "neutral DiffuseColor must be indistinguishable from raw COLOR0 -- that is the "
               "degeneracy REMED-GFX-215's oracle exists to avoid";
        EXPECT_FALSE(NearlyEqual(ExpectedColor(c, true, kNonNeutral), RawColor0(c)))
            << "column " << column << ": this file's non-neutral DiffuseColor must SEPARATE the "
               "correct product " << ExpectedColor(c, true, kNonNeutral).ToString()
            << " from the raw COLOR0 " << RawColor0(c).ToString();
    }

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false, kColumnColors);
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

    // Neutral DiffuseColor, VertexColorEnabled = true: the degenerate case, which a defective and a
    // correct shader both pass. Recorded so the frame is on file next to the discriminating ones.
    ExpectColumns(RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kNeutralState),
                  true, kNeutralState, kColumnColors, "true/neutral/instanced-route");
    // Neutral DiffuseColor, VertexColorEnabled = false: NOT degenerate. Opaque white everywhere,
    // which the raw COLOR0 cannot produce for three of the four columns.
    ExpectColumns(RenderMesh(meshBuffer, instanceBuffer, effect, true, false, kNeutralState),
                  false, kNeutralState, kColumnColors, "false/neutral/instanced-route");
}

// ---------------------------------------------------------------------------
// Alpha. `BasicEffect.Alpha` scales all four channels of the base term and COLOR0's own alpha
// multiplies into it, so the alpha channel carries an independent reading of both terms.
// ---------------------------------------------------------------------------

TEST_F(InstancedDiffuseColorTest, NonTrivialAlphaAppliesToBothTerms)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false, kAlphaColumnColors);
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

    const FrameSnapshot ordinary =
        RenderMesh(meshBuffer, instanceBuffer, effect, false, true, kHalfAlphaState);
    const FrameSnapshot instanced =
        RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kHalfAlphaState);

    ExpectColumns(ordinary, true, kHalfAlphaState, kAlphaColumnColors, "alpha/ordinary-route");
    ExpectColumns(instanced, true, kHalfAlphaState, kAlphaColumnColors, "alpha/instanced-route");
    ExpectRoutesAgree(ordinary, instanced, "alpha");

    // The same alpha with the colour term disabled: the alpha channel must then be Alpha alone.
    ExpectColumns(RenderMesh(meshBuffer, instanceBuffer, effect, true, false, kHalfAlphaState),
                  false, kHalfAlphaState, kAlphaColumnColors, "alpha/false/instanced-route");
}

// ---------------------------------------------------------------------------
// Transitions. Nothing a previous draw selected -- shader, layout, program or uniform -- may reach
// the next one, in either direction and on either route.
// ---------------------------------------------------------------------------

TEST_F(InstancedDiffuseColorTest, ColorStateTransitionsDoNotLeakBetweenFrames)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false, kColumnColors);
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

    const auto leg = [&](bool instanced, bool vce, const DiffuseState& d, const char* name) {
        ExpectColumns(RenderMesh(meshBuffer, instanceBuffer, effect, instanced, vce, d),
                      vce, d, kColumnColors, name);
    };

    //  1 false -> true                     (matrix 7)
    leg(true, false, kNonNeutral, "transition/instanced-false-1");
    leg(true, true, kNonNeutral, "transition/instanced-true-1");
    //  2 true -> false                     (matrix 8)
    leg(true, false, kNonNeutral, "transition/instanced-false-2");
    //  3 true with a CHANGED DiffuseColor  (matrix 9)
    leg(true, true, kAltState, "transition/instanced-true-alt-diffuse");
    //  4 back to the first DiffuseColor    (matrix 14: return to an earlier COMPLETE state)
    leg(true, true, kNonNeutral, "transition/instanced-true-2");
    //  5 neutral in between, so a shader that bakes one DiffuseColor is caught
    leg(true, true, kNeutralState, "transition/instanced-true-neutral");
    leg(true, true, kNonNeutral, "transition/instanced-true-3");
    //  6 ordinary true -> instanced true   (matrix 11)
    leg(false, true, kNonNeutral, "transition/ordinary-true");
    leg(true, true, kNonNeutral, "transition/instanced-true-4");
    //  7 instanced true -> ordinary true   (matrix 12)
    leg(false, true, kNonNeutral, "transition/ordinary-true-2");
    //  8 repeated state, twice             (matrix 13)
    leg(true, true, kNonNeutral, "transition/instanced-true-repeat-1");
    leg(true, true, kNonNeutral, "transition/instanced-true-repeat-2");
    //  9 disabled with the alternate DiffuseColor, so the false branch is exercised for both
    leg(true, false, kAltState, "transition/instanced-false-alt-diffuse");
    leg(false, false, kAltState, "transition/ordinary-false-alt-diffuse");
}

// ---------------------------------------------------------------------------
// The geometry colour buffer is re-read, not cached. Only the buffer's CONTENTS change between the
// two frames -- same buffer object, same declaration, same effect state.
// ---------------------------------------------------------------------------

TEST_F(InstancedDiffuseColorTest, ReplacedGeometryColorBufferIsRereadNotCached)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> first = BuildPackedMesh(false, kColumnColors);
    const std::vector<PackedVertex> second = BuildPackedMesh(false, kReplacementColors);
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(kColumnCount);
    const std::array<MatrixRecord, 1> instances{ShiftMatrix(0)};

    VertexBuffer meshBuffer(device, PackedDeclaration(), kMeshVertexCount, BufferUsage::None);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshIndexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshIndexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);

    meshBuffer.SetDataRaw(first.data(), kMeshVertexCount, 16);
    ExpectColumns(RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kNonNeutral),
                  true, kNonNeutral, kColumnColors, "rebuffer/first-contents");

    meshBuffer.SetDataRaw(second.data(), kMeshVertexCount, 16);
    ExpectColumns(RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kNonNeutral),
                  true, kNonNeutral, kReplacementColors, "rebuffer/second-contents");

    // And back, so a one-shot correct read is not enough.
    meshBuffer.SetDataRaw(first.data(), kMeshVertexCount, 16);
    ExpectColumns(RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kNonNeutral),
                  true, kNonNeutral, kColumnColors, "rebuffer/first-contents-again");
}

// ---------------------------------------------------------------------------
// Two instanced draws in ONE render-target cycle, with a different DiffuseColor and a different
// VertexColorEnabled between them. Each must carry its OWN colour state -- no shared uniform may
// survive from the first into the second, and no live re-read may replace the first with the
// second's value.
// ---------------------------------------------------------------------------

TEST_F(InstancedDiffuseColorTest, TwoDrawsInOneFrameKeepTheirOwnColorState)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    // Draw A owns columns 0 and 1, draw B owns columns 2 and 3, so both survive in one frame.
    std::vector<PackedVertex> meshA;
    std::vector<PackedVertex> meshB;
    for (int column = 0; column < 2; ++column)
    {
        const auto quadA =
            BuildSingleQuadMesh(column, kColumnColors[static_cast<std::size_t>(column)]);
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

    VertexBuffer bufferA(device, PackedDeclaration(), vertexCount, BufferUsage::None);
    bufferA.SetDataRaw(meshA.data(), vertexCount, 16);
    VertexBuffer bufferB(device, PackedDeclaration(), vertexCount, BufferUsage::None);
    bufferB.SetDataRaw(meshB.data(), vertexCount, 16);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, indexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), indexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);
    RenderTarget2D target = MakeTarget();
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    // Draw A: VertexColorEnabled = true, the primary DiffuseColor.
    device.SetVertexBuffers({VertexBufferBinding(&bufferA, 0, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 1)});
    ApplyEffect(effect, true, kNonNeutral);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, vertexCount, 0, quadCount * 2, 1);

    // Draw B: VertexColorEnabled = false, the ALTERNATE DiffuseColor. Both terms differ.
    device.SetVertexBuffers({VertexBufferBinding(&bufferB, 0, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 1)});
    ApplyEffect(effect, false, kAltState);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, vertexCount, 0, quadCount * 2, 1);

    device.SetRenderTarget(nullptr);
    const FrameSnapshot snapshot = CaptureTarget(target);
    PrintMeasurement("two-draws/one-frame", snapshot);
    if (InstancedDiffuseMeasured())
    {
        for (int column = 0; column < kColumnCount; ++column)
        {
            const bool fromDrawA = column < 2;
            const Rgba expected = ExpectedColor(
                kColumnColors[static_cast<std::size_t>(column)], fromDrawA,
                fromDrawA ? kNonNeutral : kAltState);
            int spread = 0;
            int lit = 0;
            const Rgba sample = SampleColumn(snapshot, column, spread, lit);
            EXPECT_GT(lit, 0) << "two-draws: column " << column << " rendered nothing"
                              << DescribeFrame(snapshot);
            EXPECT_TRUE(NearlyEqual(sample, expected))
                << "two-draws: column " << column << " (draw " << (fromDrawA ? 'A' : 'B')
                << ") carried " << sample.ToString() << ", expected " << expected.ToString()
                << DescribeFrame(snapshot);
        }
    }
}

// ---------------------------------------------------------------------------
// Declarations and semantics. The stride-24 COLOR0-bearing sibling, a nonzero geometry
// VertexOffset (REMED-GFX-211) and an instance divisor above one (REMED-GFX-213) must all keep the
// full colour contract, not just the vertex-colour half of it.
// ---------------------------------------------------------------------------

TEST_F(InstancedDiffuseColorTest, PackedColorTextureStrideKeepsTheFullContract)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
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
    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 1)});
    RenderTarget2D target = MakeTarget();
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyEffect(effect, true, kNonNeutral);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
    device.SetRenderTarget(nullptr);

    ExpectColumns(CaptureTarget(target), true, kNonNeutral, kColumnColors,
                  "stride24/instanced-route");
}

TEST_F(InstancedDiffuseColorTest, GeometryVertexOffsetKeepsTheFullContract)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(true, kColumnColors);
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
    // REMED-GFX-211: VertexOffset skips the decoy quad, so a colour taken from the wrong record
    // reads as the decoy's pure green -- which no product of a live column with either DiffuseColor
    // can produce.
    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, kDecoyVertexCount, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 1)});
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyEffect(effect, true, kNonNeutral);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
    device.SetRenderTarget(nullptr);

    ExpectColumns(CaptureTarget(target), true, kNonNeutral, kColumnColors,
                  "vertexOffset/instanced-route");
}

TEST_F(InstancedDiffuseColorTest, InstanceFrequencyKeepsTheFullContract)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    // One quad in column 0 carrying column 0's own COLOR0. Every instance takes a zero shift, so
    // column 0 is drawn repeatedly and nothing else is drawn at all -- the colour must still be
    // column 0's own product for every divisor.
    const std::vector<PackedVertex> mesh = BuildSingleQuadMesh(0, kColumnColors[0]);
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(1);
    const std::array<MatrixRecord, 3> instances{
        ShiftMatrix(0), ShiftMatrix(0), ShiftMatrix(0)};

    VertexBuffer meshBuffer(device, PackedDeclaration(), kVerticesPerQuad, BufferUsage::None);
    meshBuffer.SetDataRaw(mesh.data(), kVerticesPerQuad, 16);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 3, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 3, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kIndicesPerQuad, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kIndicesPerQuad);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);

    // REMED-GFX-213: frequencies 1, 2 and 3, each with as many instances as the divisor needs.
    for (int frequency = 1; frequency <= 3; ++frequency)
    {
        const int instanceCount = frequency * 2;
        RenderTarget2D target = MakeTarget();
        device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                                 VertexBufferBinding(&instanceBuffer, 0, frequency)});
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyEffect(effect, true, kNonNeutral);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerQuad, 0, 2, instanceCount);
        device.SetRenderTarget(nullptr);

        const FrameSnapshot snapshot = CaptureTarget(target);
        const std::string leg = "frequency" + std::to_string(frequency) + "/instanced-route";
        PrintMeasurement(leg.c_str(), snapshot);
        if (InstancedDiffuseMeasured())
        {
            int spread = 0;
            int lit = 0;
            const Rgba sample = SampleColumn(snapshot, 0, spread, lit);
            const Rgba expected = ExpectedColor(kColumnColors[0], true, kNonNeutral);
            EXPECT_GT(lit, 0) << leg << ": column 0 rendered nothing" << DescribeFrame(snapshot);
            EXPECT_TRUE(NearlyEqual(sample, expected))
                << leg << ": column 0 carried " << sample.ToString() << ", expected "
                << expected.ToString() << DescribeFrame(snapshot);
        }
    }
}

// ---------------------------------------------------------------------------
// A declaration with NO COLOR0 element at all, drawn with VertexColorEnabled = false. The contract
// says the disabled property must not require a colour attribute, so the ONLY colour that may
// appear anywhere in the frame is the plain DiffuseColor -- never a byte read from the vertex.
//
// This leg reads the WHOLE target rather than per-column boxes, because WHAT colour was written and
// WHERE it was written are separable questions here, and only the first belongs to REMED-GFX-215.
// bgfx derives its native vertex layout from the buffer STRIDE, not from the VertexDeclaration
// (`MakeBgfxLayout`, called from `BgfxVertexBufferRenderer::SetData` before any draw route is
// chosen): a stride of 12 is not in its table, so the fallback adds Position + Color0 and produces
// a 16-byte native layout over a 12-byte buffer. The geometry is consequently strided wrongly --
// which is a DIFFERENT defect from this ticket's, is route-independent, and is recorded as
// REMED-GFX-216 rather than folded in here. The two routes are measured against each other below
// so that classification rests on evidence rather than on reading `MakeBgfxLayout`.
// ---------------------------------------------------------------------------

TEST_F(InstancedDiffuseColorTest, PositionOnlyDeclarationRendersDiffuseColorWhenColorDisabled)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PositionOnlyVertex> mesh = BuildPositionOnlyMesh();
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(kColumnCount);
    const std::array<MatrixRecord, 1> instances{ShiftMatrix(0)};

    VertexBuffer meshBuffer(device, PositionOnlyDeclaration(), kMeshVertexCount, BufferUsage::None);
    meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, 12);
    VertexBuffer instanceBuffer(device, MatrixDeclaration(), 1, BufferUsage::None);
    instanceBuffer.SetDataRaw(instances.data(), 1, 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshIndexCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshIndexCount);
    device.SetIndexBuffer(&indexBuffer);

    BasicEffect effect(device);

    // A renderer is entitled to REJECT a stride its native layout cannot express, and some do. What
    // it may never do is accept the draw and return wrong pixels. So each route is measured
    // through its own rejection barrier: a throw is recorded verbatim as that renderer's declared
    // boundary, and anything that RENDERS is held to the full contract.
    struct RouteResult
    {
        FrameSnapshot frame;
        bool rendered = false;
        std::string rejection;
    };

    const auto render = [&](bool instanced) {
        RouteResult result;
        RenderTarget2D target = MakeTarget();
        try
        {
            if (instanced)
                device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                                         VertexBufferBinding(&instanceBuffer, 0, 1)});
            else
                device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            ApplyEffect(effect, false, kNonNeutral);
            if (instanced)
                device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, kMeshVertexCount,
                                               0, kMeshPrimitiveCount, 1);
            else
                device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, kMeshVertexCount,
                                             0, kMeshPrimitiveCount);
            device.SetRenderTarget(nullptr);
            result.frame = CaptureTarget(target);
            result.rendered = true;
        }
        catch (const std::exception& e)
        {
            result.rejection = e.what();
            device.SetRenderTarget(nullptr);
        }
        return result;
    };

    const RouteResult ordinary = render(false);
    const RouteResult instanced = render(true);

    const auto report = [&](const char* leg, const RouteResult& r) {
        if (!r.rendered)
        {
            std::cout << "[ GFX-215  ] " << RendererName() << ' ' << leg
                      << ": REJECTED -- \"" << r.rejection << '"' << std::endl;
            return std::vector<std::pair<Rgba, int>>{};
        }
        PrintMeasurement(leg, r.frame);
        const auto lit = DistinctLitColors(r.frame);
        std::cout << "[ GFX-215  ] " << RendererName() << ' ' << leg
                  << " distinct lit colours:" << DescribeLitColors(lit) << std::endl;
        return lit;
    };

    const auto ordinaryLit = report("positionOnly/false/ordinary-route", ordinary);
    const auto instancedLit = report("positionOnly/false/instanced-route", instanced);

    if (InstancedDiffuseMeasured())
    {
        const Rgba expected = ExpectedColor(kColumnColors[0], false, kNonNeutral);

        // A rejection must be LOUD and it must name the reason. WebGPU declines a stride its
        // DrawColoredPrimitives cannot express and says so -- the same stride-16 requirement
        // REMED-GFX-214 already tracks on its ordinary route, reached here with stride 12 instead of
        // 24. That is a boundary, not a defect of this ticket: no WebGPU production is touched here and
        // REMED-GFX-214 stays open and uninvestigated. What matters for REMED-GFX-215 is that the draw
        // is refused rather than silently miscoloured.
        const auto expectLoudRejection = [](const RouteResult& r, const char* leg) {
            if (r.rendered)
                return;
            EXPECT_FALSE(r.rejection.empty())
                << leg << ": the draw was refused without saying why. A renderer may decline a stride "
                          "it cannot express, but the refusal has to be legible";
        };
        expectLoudRejection(ordinary, "positionOnly/ordinary");
        expectLoudRejection(instanced, "positionOnly/instanced");

        // REMED-GFX-215's own guarantee, and the whole of it: on any route that DID render, with the
        // property disabled, the plain DiffuseColor is the ONLY colour that may reach the target. This
        // holds no matter how the renderer derives its native layout, because a disabled COLOR0 is never
        // consumed at all.
        for (const auto& entry : instancedLit)
        {
            EXPECT_TRUE(NearlyEqual(entry.first, expected))
                << "positionOnly/instanced: " << entry.second << " pixels carried "
                << entry.first.ToString() << ", but VertexColorEnabled is false so the only colour "
                   "that may appear is the plain DiffuseColor " << expected.ToString()
                << DescribeLitColors(instancedLit);
        }
        for (const auto& entry : ordinaryLit)
        {
            EXPECT_TRUE(NearlyEqual(entry.first, expected))
                << "positionOnly/ordinary: " << entry.second << " pixels carried "
                << entry.first.ToString() << ", but VertexColorEnabled is false so the only colour "
                   "that may appear is the plain DiffuseColor " << expected.ToString()
                << DescribeLitColors(ordinaryLit);
        }

        int coverageOrdinary = 0;
        for (const auto& entry : ordinaryLit)
            coverageOrdinary += entry.second;
        int coverageInstanced = 0;
        for (const auto& entry : instancedLit)
            coverageInstanced += entry.second;

        // The COVERAGE half is a separate question from the colour one. When both routes rendered they
        // must agree exactly, because a vertex layout derived before either route is chosen cannot
        // differ between them -- and REMED-GFX-215 changed neither.
        if (ordinary.rendered && instanced.rendered)
        {
            EXPECT_EQ(coverageOrdinary, coverageInstanced)
                << "positionOnly: the ordinary route lit " << coverageOrdinary << " pixels and the "
                   "instanced route " << coverageInstanced
                << ". A vertex layout derived before either route is chosen cannot differ between them";
        }

        // Every measured renderer that ACCEPTS the stride builds its native layout from the
        // declaration, so the geometry lands exactly where it belongs and the full contract holds.
        //
        // bgfx carried a declared exemption here until REMED-GFX-216: its `MakeBgfxLayout` keyed on the
        // buffer stride alone, so a stride-12 declaration fell to a fallback that emitted a 16-byte
        // Position+Color0 layout over a 12-byte buffer and lit 27385 of these 50752 pixels. That arm
        // asserted the measured shortfall precisely so it would FAIL the moment the defect was fixed --
        // which is what happened, and why it is gone. Every colour was already correct under the wrong
        // layout, which is what separated the two tickets in the first place.
        constexpr int kFullCoverage = kColumnCount * (kColumnWidth - 2 * kGeometryInset) *
                                      (kTargetSize - 2 * kGeometryInset);
        if (instanced.rendered)
        {
            EXPECT_GT(coverageInstanced, kFullCoverage * 9 / 10)
                << "positionOnly: only " << coverageInstanced << " of about " << kFullCoverage
                << " pixels rendered";
        }
    }
}

// ---------------------------------------------------------------------------
// Program and cache identity. REMED-GFX-215 supplies two uniforms to an EXISTING program instead of
// selecting a new one, so the whole colour matrix must be served by exactly the programs that
// already existed -- no variant per DiffuseColor value, none per VertexColorEnabled setting, none
// per buffer identity, and no growth at all across repeated and returning states.
//
// bgfx-only, because `bgfx::getStats()` is the native cache. Its resource counters are the LIVE
// object counts, and they lag one frame, so every reading is taken after two Presents.
// ---------------------------------------------------------------------------

#ifdef CNA_TEST_BGFX_AVAILABLE

class BgfxInstancedColorCardinalityTest : public InstancedDiffuseColorTest
{
protected:
    struct Cardinality
    {
        std::uint16_t programs = 0;
        std::uint16_t shaders = 0;
        std::uint16_t uniforms = 0;
        std::uint16_t vertexBuffers = 0;
        std::uint16_t dynamicVertexBuffers = 0;
        std::uint16_t dynamicIndexBuffers = 0;
        std::uint32_t draws = 0;

        [[nodiscard]] std::string ToString() const
        {
            std::ostringstream os;
            os << "programs=" << programs << " shaders=" << shaders << " uniforms=" << uniforms
               << " vb=" << vertexBuffers << " dynVb=" << dynamicVertexBuffers
               << " dynIb=" << dynamicIndexBuffers << " draws=" << draws;
            return os.str();
        }
    };

    /// bgfx's counters lag one frame, so settle before reading.
    Cardinality Measure()
    {
        device.Present();
        device.Present();
        const bgfx::Stats* stats = bgfx::getStats();
        EXPECT_NE(nullptr, stats);
        Cardinality out;
        if (stats == nullptr)
            return out;
        out.programs = stats->numPrograms;
        out.shaders = stats->numShaders;
        out.uniforms = stats->numUniforms;
        out.vertexBuffers = stats->numVertexBuffers;
        out.dynamicVertexBuffers = stats->numDynamicVertexBuffers;
        out.dynamicIndexBuffers = stats->numDynamicIndexBuffers;
        out.draws = stats->numDraw;
        return out;
    }

    static void ExpectNoCacheGrowth(
        const Cardinality& baseline, const Cardinality& after, const char* leg)
    {
        EXPECT_EQ(baseline.programs, after.programs)
            << leg << ": a program was created. Colour state is carried by uniforms, so no "
               "DiffuseColor value and no VertexColorEnabled setting may select a new program. "
               "baseline [" << baseline.ToString() << "] after [" << after.ToString() << ']';
        EXPECT_EQ(baseline.shaders, after.shaders)
            << leg << ": a shader was created. baseline [" << baseline.ToString()
            << "] after [" << after.ToString() << ']';
        EXPECT_EQ(baseline.uniforms, after.uniforms)
            << leg << ": a uniform was created. REMED-GFX-215 reuses u_diffuseColor and "
               "u_vertexColorEnabled3D, which the ordinary route already owns. baseline ["
            << baseline.ToString() << "] after [" << after.ToString() << ']';
        EXPECT_LE(after.dynamicVertexBuffers, baseline.dynamicVertexBuffers)
            << leg << ": a per-draw vertex buffer was allocated. baseline ["
            << baseline.ToString() << "] after [" << after.ToString() << ']';
        EXPECT_LE(after.dynamicIndexBuffers, baseline.dynamicIndexBuffers)
            << leg << ": a per-draw index buffer was allocated. baseline ["
            << baseline.ToString() << "] after [" << after.ToString() << ']';
    }
};

TEST_F(BgfxInstancedColorCardinalityTest, ColorStateCreatesNoProgramAndReusesTheCache)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false, kColumnColors);
    const std::vector<PackedVertex> replaced = BuildPackedMesh(false, kReplacementColors);
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

    // Warm every program the matrix can reach ONCE, so the baseline is the steady state rather
    // than a cold cache. Both routes and both settings.
    (void)RenderMesh(meshBuffer, instanceBuffer, effect, false, true, kNonNeutral);
    (void)RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kNonNeutral);
    (void)RenderMesh(meshBuffer, instanceBuffer, effect, false, false, kNonNeutral);
    (void)RenderMesh(meshBuffer, instanceBuffer, effect, true, false, kNonNeutral);
    const Cardinality baseline = Measure();
    std::cout << "[ GFX-215  ] bgfx cardinality baseline: " << baseline.ToString() << std::endl;

    // 1. Changing ONLY VertexColorEnabled, repeatedly and in both directions.
    for (int repeat = 0; repeat < 4; ++repeat)
    {
        (void)RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kNonNeutral);
        (void)RenderMesh(meshBuffer, instanceBuffer, effect, true, false, kNonNeutral);
    }
    ExpectNoCacheGrowth(baseline, Measure(), "vertexColorEnabled toggled 8 times");

    // 2. Changing ONLY DiffuseColor -- eight distinct values, none of which may create anything.
    for (int step = 0; step < 8; ++step)
    {
        const float t = static_cast<float>(step) / 8.0f;
        const DiffuseState d{0.1f + 0.8f * t, 0.9f - 0.8f * t, 0.2f + 0.6f * t, 1.0f};
        (void)RenderMesh(meshBuffer, instanceBuffer, effect, true, true, d);
    }
    ExpectNoCacheGrowth(baseline, Measure(), "eight distinct DiffuseColor values");

    // 3. Changing ONLY the buffer CONTENTS.
    for (int repeat = 0; repeat < 4; ++repeat)
    {
        meshBuffer.SetDataRaw(replaced.data(), kMeshVertexCount, 16);
        (void)RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kNonNeutral);
        meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, 16);
        (void)RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kNonNeutral);
    }
    ExpectNoCacheGrowth(baseline, Measure(), "geometry colour buffer rewritten 8 times");

    // 4. Returning to an earlier COMPLETE state must reuse, not recreate -- and the frame must
    //    still be correct, so "no growth" cannot be satisfied by a stale program.
    const FrameSnapshot returned =
        RenderMesh(meshBuffer, instanceBuffer, effect, true, true, kNonNeutral);
    ExpectColumns(returned, true, kNonNeutral, kColumnColors, "cardinality/returned-state");
    const Cardinality settled = Measure();
    ExpectNoCacheGrowth(baseline, settled, "returned to an earlier complete state");
    std::cout << "[ GFX-215  ] bgfx cardinality settled:  " << settled.ToString() << std::endl;
}

TEST_F(BgfxInstancedColorCardinalityTest, InstancedColorDrawSubmitsExactlyOnce)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!InstancedDiffuse())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireInstancedRendering();

    const std::vector<PackedVertex> mesh = BuildPackedMesh(false, kColumnColors);
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
    device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                             VertexBufferBinding(&instanceBuffer, 0, 1)});

    BasicEffect effect(device);

    // A clear-only frame first, so this backbuffer's own per-frame floor is measured rather than
    // assumed. bgfx charges the clear one submit of its own.
    device.Clear(Color::Black);
    device.Present();
    device.Clear(Color::Black);
    device.Present();
    const bgfx::Stats* emptyStats = bgfx::getStats();
    ASSERT_NE(nullptr, emptyStats);
    const std::uint32_t clearOnlyDraws = emptyStats->numDraw;
    std::cout << "[ GFX-215  ] bgfx submits for a clear-only frame: " << clearOnlyDraws
              << std::endl;

    /// n public draws through @p instanced, in one frame, returning that frame's native submit
    /// count. Every draw carries a DIFFERENT colour state, which is the case REMED-GFX-215 touches.
    ///
    /// The frame is rendered TWICE and the reading taken after the second Present, because
    /// `bgfx::getStats()` reports the previous frame. Reading after a single Present would report
    /// whatever ran before this call instead -- which is exactly the off-by-one-frame that makes an
    /// n-draw frame look like an (n-1)-draw one.
    const auto submitsFor = [&](bool instanced, int publicDraws) {
        for (int pass = 0; pass < 2; ++pass)
        {
            device.Clear(Color::Black);
            for (int i = 0; i < publicDraws; ++i)
            {
                ApplyEffect(effect, i % 2 == 0, i % 2 == 0 ? kNonNeutral : kAltState);
                if (instanced)
                    device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                                   kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
                else
                    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                                 kMeshVertexCount, 0, kMeshPrimitiveCount);
            }
            device.Present();
        }
        const bgfx::Stats* stats = bgfx::getStats();
        EXPECT_NE(nullptr, stats);
        return stats != nullptr ? stats->numDraw : 0u;
    };

    // THE A/B. The ordinary route has supplied these same two uniforms since Task 364, so if the
    // instanced route now costs exactly what the ordinary route costs, the uniforms cost nothing.
    // Both must also be exactly one native submit per public draw -- no extra draw, submit, view,
    // frame, wait, readback or Present anywhere.
    for (int publicDraws = 1; publicDraws <= 4; ++publicDraws)
    {
        device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
        const std::uint32_t ordinary = submitsFor(false, publicDraws);
        device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0),
                                 VertexBufferBinding(&instanceBuffer, 0, 1)});
        const std::uint32_t instanced = submitsFor(true, publicDraws);
        std::cout << "[ GFX-215  ] bgfx submits for " << publicDraws
                  << " public draw(s): ordinary=" << ordinary << " instanced=" << instanced
                  << std::endl;

        EXPECT_EQ(clearOnlyDraws + static_cast<std::uint32_t>(publicDraws), instanced)
            << publicDraws << " public instanced draws must produce exactly " << publicDraws
            << " native submits above the measured clear-only floor of " << clearOnlyDraws
            << ". REMED-GFX-215 adds two setUniform calls to an existing submit";
        EXPECT_EQ(ordinary, instanced)
            << "the instanced route submitted " << instanced << " times for " << publicDraws
            << " draws where the ordinary route -- which has supplied the same two uniforms since "
               "Task 364 -- submitted " << ordinary
            << ". Supplying a uniform costs no submit, so the two must agree";

        const bgfx::Stats* stats = bgfx::getStats();
        ASSERT_NE(nullptr, stats);
        EXPECT_EQ(0u, stats->numBlit) << "the instanced colour path must issue no blit";
    }
}

#endif   // CNA_TEST_BGFX_AVAILABLE

