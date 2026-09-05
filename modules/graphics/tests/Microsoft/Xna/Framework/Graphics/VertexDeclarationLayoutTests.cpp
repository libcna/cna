// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-216 -- the native vertex layout must be derived from the `VertexDeclaration`, not
// guessed from the byte stride.
//
// THE CONTRACT. A `VertexDeclaration` states, for every active element, its usage, usage index,
// byte offset within the stream, format, component count and normalization. The stride states only
// the distance between records. Stride therefore does NOT determine element composition:
//
//   * two declarations with the SAME stride may need different native layouts
//     (`Position@0 + Color@12` and `Color@0 + Position@4` are both stride 16);
//   * two declarations with DIFFERENT strides may carry equivalent semantics with different
//     padding (`Position@0 + Color@12` at stride 16 and at stride 32);
//   * a declaration whose stride is not in any built-in table is still fully specified.
//
// A renderer that switches on the stride alone is therefore guessing, and its guess is
// unfalsifiable from the stride: `MakeBgfxLayout(12)` fell through to a fallback that appends
// Position + Color0 and ends at a SIXTEEN-byte native layout, which bgfx then strides through a
// TWELVE-byte buffer. Every record after the first is read from the wrong address.
//
// WHAT THIS FILE MEASURES, AND WHY IT IS NOT A PIXEL COUNT. Several different wrong layouts can
// light the same number of pixels, so a total count cannot identify the mechanism. Every leg here
// reads three independent signals per column:
//
//   * the COLOUR, which isolates attribute offset/format -- a colour read from the wrong byte
//     offset is wrong even when the geometry is perfect;
//   * the LIT COUNT, which isolates record advancement -- the four quads form a staircase
//     (full, 3/4, 1/2, 1/4 height), so a desynchronised record moves a KNOWN quantity of pixels
//     to a KNOWN column rather than merely changing a total;
//   * the TOP ROW of the lit region, which isolates position-attribute offset independently of
//     how many pixels survive.
//
// A decoy record precedes the live mesh in the offset legs, carrying a colour and a geometry no
// live column owns, so consuming the wrong record is unmistakable rather than merely numeric.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

#include <string>

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
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
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
#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
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
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

// The renderers that rasterize a stock 3D draw and read the result back through
// RenderTarget2D::GetData -- InstancedDiffuseColorTests.cpp's own suite set.
/// plans/plan_runtimerenderer.md RTR-P9-5: the same renderer set, evaluated at runtime so this
/// describes the ACTIVE renderer rather than the build default.
[[nodiscard]] inline bool DeclarationLayout()
{
    return CNA_RENDERER_IS(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, Software, SdlGpu, 
                            DirectX9, DirectX11, DirectX12, Wicked);
}

// The renderers measured on a real display here. D3D9/D3D11/D3D12 stay outside it because no D3D
// display is reachable in this environment; every leg still PRINTS its reading there.
/// plans/plan_runtimerenderer.md RTR-P9-6: the same set, evaluated at runtime.
[[nodiscard]] inline bool DeclarationLayoutMeasured()
{
    return CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, Bgfx,
                           Vulkan, WebGPU, Software, SdlGpu, Wicked);
}

/// REMED-GFX-234: does this renderer derive its native layout from the DECLARATION?
///
/// One predicate, because the refusal arms below and their translating control are two halves of
/// the same statement and must never disagree about which renderer is which. bgfx has always
/// translated (REMED-GFX-216). EasyGL now does too: `ConfigureDeclarationForStockProgramEXT` binds
/// every stock attribute at the declared element's own `getOffsetProperty()`, so a declaration
/// whose element ORDER differs from the stock program's input order is read from its own bytes
/// rather than reinterpreted. All five GL profiles share that one implementation; the reading was
/// taken on OPENGLES3, and a profile that diverges fails its own run and says so.
///
/// This is not the same as "the stock program is chosen from the declaration". It is not --
/// REMED-GFX-217 is still open, and the stride cases that can be ambiguous have to ask the
/// declaration by hand (see REMED-GFX-234's stride-32 case).
///
/// plans/plan_vulkan.md VULKAN-146: Vulkan now does the same. Its BasicEffect-family pipelines
/// take their VkVertexInputAttributeDescription offsets from the declaration's own elements,
/// matched by (usage, usageIndex), and the layout is part of the pipeline key so two declarations
/// of one stride cannot share a pipeline. Stride 32 asks the declaration for a normal, exactly as
/// REMED-GFX-234 describes above. The families this plan has not converted yet -- alpha test, dual
/// texture, environment map, skinned, PBR, instanced -- still infer from the stride and still
/// refuse by name what they cannot express; none of the cases below reaches them.
[[nodiscard]] inline bool TranslatesDeclarations()
{
    return CNA_RENDERER_IS(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, Vulkan);
}


namespace
{
    // plans/plan_runtimerenderer.md RTR-P9-6: was a hand-maintained #if/#elif chain of display names that
    // had to be extended for every renderer and answered with a placeholder when it was not. The
    // runtime accessor knows the ACTIVE renderer, and knows all 46 names.
    inline std::string RendererName()
    {
        return std::string(CNA::getGraphicsRendererName(
            CNA::GraphicsRendererSelection::GetSelected()));
    }

    constexpr int kTargetSize = 256;
    constexpr int kColumnCount = 4;
    constexpr int kColumnWidth = kTargetSize / kColumnCount;

    /// Pixels the geometry stays away from a column's left/right boundary.
    constexpr int kGeometryInset = 6;
    /// Pixels the sampling box stays away from that boundary.
    constexpr int kSampleInset = 20;
    /// Row the quads start at; every quad's TOP is identical so a moved top row is unambiguous.
    constexpr int kQuadTop = 8;

    constexpr int kVerticesPerQuad = 4;
    constexpr int kIndicesPerQuad = 6;
    constexpr int kMeshVertexCount = kColumnCount * kVerticesPerQuad;
    constexpr int kMeshIndexCount = kColumnCount * kIndicesPerQuad;
    constexpr int kMeshPrimitiveCount = kMeshIndexCount / 3;
    constexpr int kDecoyVertexCount = kVerticesPerQuad;

    /// THE STAIRCASE. Each column's quad has its own height, so the per-column lit count is a
    /// signature rather than a bare total: a record read one slot late moves a KNOWN number of
    /// pixels into a KNOWN column.
    constexpr std::array<int, kColumnCount> kQuadBottom{248, 188, 128, 68};

    /// Non-neutral in every channel, so a dropped or mis-offset colour lands somewhere measurable.
    constexpr float kDiffuseR = 0.80f;
    constexpr float kDiffuseG = 0.35f;
    constexpr float kDiffuseB = 0.55f;

    struct Rgba
    {
        int r = 0, g = 0, b = 0, a = 0;
        [[nodiscard]] std::string ToString() const
        {
            std::ostringstream os;
            os << '(' << r << ',' << g << ',' << b << ',' << a << ')';
            return os.str();
        }
    };

    /// Four well-separated COLOR0 records. Column 3 is opaque white on purpose: its product IS
    /// DiffuseColor, which separates "DiffuseColor dropped" from "COLOR0 dropped" in one frame.
    constexpr std::array<Rgba, kColumnCount> kColumnColors{
        Rgba{255, 128, 64, 255},
        Rgba{64, 255, 128, 255},
        Rgba{128, 64, 255, 255},
        Rgba{255, 255, 255, 255},
    };

    /// The decoy record's colour -- pure green, which no live column and no product of one with
    /// DiffuseColor can produce.
    constexpr Rgba kDecoyColor{0, 255, 0, 255};

    /// THE CLEAR COLOUR, deliberately not black. A layout that loses the colour attribute makes
    /// the shader emit BLACK -- an unbound bgfx attribute reads as (0,0,0,0) and a zeroed COLOR0
    /// multiplies the diffuse term away. Against a black clear that is indistinguishable from
    /// "nothing was rasterized", which is a completely different defect with a completely
    /// different cause. Against this value the two separate cleanly, and no product of
    /// DiffuseColor with any record in this file can reproduce it.
    constexpr Rgba kClearColor{17, 34, 51, 255};

    int Quantize(float v)
    {
        const float c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<int>(c * 255.0f + 0.5f);
    }

    /// FNA's own arithmetic: `vout.Diffuse = DiffuseColor` and, when VertexColorEnabled is set,
    /// `vout.Diffuse *= vin.Color`.
    Rgba ExpectedColor(const Rgba& color0, bool vertexColorEnabled)
    {
        if (!vertexColorEnabled)
            return Rgba{Quantize(kDiffuseR), Quantize(kDiffuseG), Quantize(kDiffuseB), 255};
        return Rgba{Quantize(kDiffuseR * static_cast<float>(color0.r) / 255.0f),
                    Quantize(kDiffuseG * static_cast<float>(color0.g) / 255.0f),
                    Quantize(kDiffuseB * static_cast<float>(color0.b) / 255.0f),
                    Quantize(static_cast<float>(color0.a) / 255.0f)};
    }

    // -----------------------------------------------------------------------
    // THE DECLARATION COLLISION MATRIX. Every entry is a declaration CNA already supports; none
    // invents a public vertex format. The point of the set is the collisions inside it.
    // -----------------------------------------------------------------------

    /// One entry: how to build the declaration, and how to write one vertex through it.
    struct DeclarationCase
    {
        const char* name;
        int stride;
        bool hasColor;
        /// Byte offsets of the elements this case declares, -1 when absent.
        int positionOffset;
        int colorOffset;
        int texCoordOffset;
        /// True when this declaration collides with the byte-stride table every renderer other
        /// than bgfx still infers its native layout from. bgfx translates the declaration and must
        /// render the contract; the other nine must REFUSE the draw deterministically before any
        /// native work (REMED-GFX-DECL-GUARD), because their inferred layout would read the
        /// caller's bytes as something else. Accepting one and rendering is a regression of the
        /// guard, and rendering it CORRECTLY is REMED-GFX-217/218's translator landing -- both
        /// fail this arm, and the message says which.
        bool deviatesElsewhere = false;
    };

    /// `Position` alone at stride 12 -- REMED-GFX-216's canonical case. No built-in table entry.
    constexpr DeclarationCase kPositionOnly12{"positionOnly12", 12, false, 0, -1, -1};
    /// `VertexPositionColor`. The one layout the fallback happened to describe correctly.
    constexpr DeclarationCase kPositionColor16{"positionColor16", 16, true, 0, 12, -1};
    /// SAME STRIDE as the previous entry, elements SWAPPED. A stride-keyed guess reads both the
    /// position and the colour from the wrong bytes.
    constexpr DeclarationCase kColorPosition16{"colorPosition16", 16, true, 4, 0, -1, true};
    /// `VertexPositionTexture` -- no colour element at all, so VertexColorEnabled must be false.
    constexpr DeclarationCase kPositionTexture20{"positionTexture20", 20, false, 0, -1, 12};
    /// `VertexPositionColorTexture`.
    constexpr DeclarationCase kPositionColorTexture24{"positionColorTexture24", 24, true, 0, 12, 16};
    /// SAME STRIDE as the previous entry with the colour MOVED to the end. A stride-keyed guess
    /// reads the texture coordinate's bytes as the colour.
    constexpr DeclarationCase kPositionTextureColor24{"positionTextureColor24", 24, true, 0, 20, 12, true};
    /// The SAME SEMANTICS as `positionColor16` with legal trailing padding. A stride-keyed guess
    /// reads it as Position+Normal+TexCoord.
    constexpr DeclarationCase kPositionColorPadded32{"positionColorPadded32", 32, true, 0, 12, -1, true};

    constexpr std::array<DeclarationCase, 7> kMatrix{
        kPositionOnly12, kPositionColor16, kColorPosition16, kPositionTexture20,
        kPositionColorTexture24, kPositionTextureColor24, kPositionColorPadded32};

    VertexDeclaration BuildDeclaration(const DeclarationCase& c)
    {
        std::vector<VertexElement> elements;
        elements.emplace_back(c.positionOffset, VertexElementFormat::Vector3,
                              VertexElementUsage::Position, 0);
        if (c.colorOffset >= 0)
            elements.emplace_back(c.colorOffset, VertexElementFormat::Color,
                                  VertexElementUsage::Color, 0);
        if (c.texCoordOffset >= 0)
            elements.emplace_back(c.texCoordOffset, VertexElementFormat::Vector2,
                                  VertexElementUsage::TextureCoordinate, 0);
        std::sort(elements.begin(), elements.end(),
                  [](const VertexElement& a, const VertexElement& b) {
                      return a.getOffsetProperty() < b.getOffsetProperty();
                  });
        return VertexDeclaration(c.stride, elements);
    }

    void NdcFromPixel(float px, float py, float& x, float& y)
    {
        x = (2.0f * px / static_cast<float>(kTargetSize)) - 1.0f;
        y = 1.0f - (2.0f * py / static_cast<float>(kTargetSize));
    }

    /// The four corners of @p column's quad. `bottom` is what makes the staircase.
    std::array<std::array<float, 2>, kVerticesPerQuad> QuadCorners(int column, int bottom)
    {
        const float left = static_cast<float>(column * kColumnWidth + kGeometryInset);
        const float right = static_cast<float>((column + 1) * kColumnWidth - kGeometryInset);
        const float px[kVerticesPerQuad] = {left, right, right, left};
        const float py[kVerticesPerQuad] = {static_cast<float>(kQuadTop),
                                            static_cast<float>(kQuadTop),
                                            static_cast<float>(bottom),
                                            static_cast<float>(bottom)};
        std::array<std::array<float, 2>, kVerticesPerQuad> corners{};
        for (int i = 0; i < kVerticesPerQuad; ++i)
            NdcFromPixel(px[i], py[i], corners[static_cast<std::size_t>(i)][0],
                         corners[static_cast<std::size_t>(i)][1]);
        return corners;
    }

    /// Writes one vertex into @p record according to @p c. Bytes the declaration does not claim
    /// are left as the deliberate filler the caller supplied, so a layout that reads an unclaimed
    /// byte reads something recognizable rather than zero.
    void WriteVertex(std::uint8_t* record, const DeclarationCase& c,
                     float x, float y, float z, const Rgba& color)
    {
        const float pos[3] = {x, y, z};
        std::memcpy(record + c.positionOffset, pos, sizeof(pos));
        if (c.colorOffset >= 0)
        {
            const std::uint8_t rgba[4] = {
                static_cast<std::uint8_t>(color.r), static_cast<std::uint8_t>(color.g),
                static_cast<std::uint8_t>(color.b), static_cast<std::uint8_t>(color.a)};
            std::memcpy(record + c.colorOffset, rgba, sizeof(rgba));
        }
        if (c.texCoordOffset >= 0)
        {
            const float uv[2] = {0.5f, 0.5f};
            std::memcpy(record + c.texCoordOffset, uv, sizeof(uv));
        }
    }

    /// The whole geometry stream for @p c, optionally preceded by a decoy quad.
    std::vector<std::uint8_t> BuildMesh(const DeclarationCase& c, bool withDecoy)
    {
        const int quads = kColumnCount + (withDecoy ? 1 : 0);
        // 0xCD filler: any byte a declaration does not claim reads as a loud, non-zero pattern if
        // a wrong layout picks it up as a colour or a coordinate.
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(quads) * kVerticesPerQuad * c.stride, 0xCD);
        std::size_t v = 0;
        const auto emit = [&](int column, int bottom, const Rgba& color) {
            const auto corners = QuadCorners(column, bottom);
            for (int i = 0; i < kVerticesPerQuad; ++i, ++v)
                WriteVertex(bytes.data() + v * static_cast<std::size_t>(c.stride), c,
                            corners[static_cast<std::size_t>(i)][0],
                            corners[static_cast<std::size_t>(i)][1], 0.5f, color);
        };
        if (withDecoy)
            emit(0, kQuadBottom[0], kDecoyColor);
        for (int column = 0; column < kColumnCount; ++column)
            emit(column, kQuadBottom[static_cast<std::size_t>(column)],
                 kColumnColors[static_cast<std::size_t>(column)]);
        return bytes;
    }

    template <typename T>
    std::vector<T> BuildQuadIndices(int quadCount)
    {
        std::vector<T> indices;
        for (int q = 0; q < quadCount; ++q)
        {
            const T base = static_cast<T>(q * kVerticesPerQuad);
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

    /// The three independent signals one column carries.
    struct ColumnReading
    {
        Rgba color{};       ///< colour of the first lit pixel -- isolates attribute offset/format
        int lit = 0;        ///< lit pixels in the sample box -- isolates record advancement
        int topRow = -1;    ///< first lit row -- isolates the position attribute
        int distinct = 0;   ///< distinct non-black colours seen -- a torn quad is not flat

        [[nodiscard]] std::string ToString() const
        {
            std::ostringstream os;
            os << color.ToString() << " lit=" << lit << " top=" << topRow
               << " distinct=" << distinct;
            return os.str();
        }
    };

    ColumnReading ReadColumn(const FrameSnapshot& s, int column)
    {
        const int x0 = column * kColumnWidth + kSampleInset;
        const int x1 = (column + 1) * kColumnWidth - kSampleInset;
        ColumnReading r;
        std::vector<Rgba> seen;
        for (int y = 0; y < kTargetSize; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                const Color p = s.At(x, y);
                const Rgba c{p.getRProperty(), p.getGProperty(), p.getBProperty(),
                             p.getAProperty()};
                // "Lit" means "the draw wrote here", not "the result is bright": a pixel the
                // shader coloured black is rasterized geometry with a lost colour attribute, and
                // must never be counted as absent geometry.
                if (c.r == kClearColor.r && c.g == kClearColor.g && c.b == kClearColor.b)
                    continue;
                if (r.topRow < 0)
                {
                    r.topRow = y;
                    r.color = c;
                }
                ++r.lit;
                if (std::none_of(seen.begin(), seen.end(), [&](const Rgba& q) {
                        return q.r == c.r && q.g == c.g && q.b == c.b && q.a == c.a;
                    }))
                    seen.push_back(c);
            }
        }
        r.distinct = static_cast<int>(seen.size());
        return r;
    }

    std::string DescribeFrame(const FrameSnapshot& s)
    {
        std::ostringstream os;
        for (int c = 0; c < kColumnCount; ++c)
            os << "\n    column " << c << ": " << ReadColumn(s, c).ToString();
        return os.str();
    }

    /// The lit pixels a correct column owns inside the sample box.
    int ExpectedLit(int column)
    {
        const int width = (kColumnWidth - kSampleInset) - kSampleInset;
        return width * (kQuadBottom[static_cast<std::size_t>(column)] - kQuadTop);
    }

    constexpr int kTolerance = 3;

    bool NearlyEqual(const Rgba& a, const Rgba& b)
    {
        const auto close = [](int l, int r) { return (l > r ? l - r : r - l) <= kTolerance; };
        return close(a.r, b.r) && close(a.g, b.g) && close(a.b, b.b) && close(a.a, b.a);
    }
}   // namespace

class VertexDeclarationLayoutTest : public ::testing::Test
{
protected:
    GraphicsDevice device;

    void RequireThreeD()
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
        return RenderTarget2D(device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    }

    [[nodiscard]] FrameSnapshot Capture(RenderTarget2D& t)
    {
        FrameSnapshot s;
        s.pixels.assign(static_cast<std::size_t>(kTargetSize) * kTargetSize, Color::Transparent);
        const Rectangle region(0, 0, kTargetSize, kTargetSize);
        t.GetData(0, &region, s.pixels.data(), 0, static_cast<int>(s.pixels.size()));
        return s;
    }

    static void ApplyEffect(BasicEffect& e, bool vertexColorEnabled)
    {
        e.VertexColorEnabled = vertexColorEnabled;
        e.setLightingEnabledProperty(false);
        e.setTextureEnabledProperty(false);
        e.setFogEnabledProperty(false);
        e.setDiffuseColorProperty(Vector3(kDiffuseR, kDiffuseG, kDiffuseB));
        e.setAlphaProperty(1.0f);
        e.Apply();
    }

    /// One rendered frame, or the renderer's own refusal. A renderer may decline a declaration it
    /// cannot express; what it may never do is accept the draw and return wrong geometry.
    struct RouteResult
    {
        FrameSnapshot frame;
        bool rendered = false;
        std::string rejection;
    };

    enum class Route { OrdinaryIndexed16, OrdinaryIndexed32, OrdinaryNonIndexed, Instanced };

    static const char* RouteName(Route r)
    {
        switch (r)
        {
            case Route::OrdinaryIndexed16:  return "ordinary-indexed16";
            case Route::OrdinaryIndexed32:  return "ordinary-indexed32";
            case Route::OrdinaryNonIndexed: return "ordinary-nonindexed";
            case Route::Instanced:          return "instanced";
        }
        return "?";
    }

    /// Renders the four-column staircase declared by @p c through @p route.
    /// @p vertexOffset is the geometry binding's own VertexOffset (REMED-GFX-211).
    RouteResult Render(const DeclarationCase& c, Route route, bool withDecoy = false,
                       int vertexOffset = 0)
    {
        RouteResult out;
        const std::vector<std::uint8_t> mesh = BuildMesh(c, withDecoy);
        const int totalVertices = kMeshVertexCount + (withDecoy ? kDecoyVertexCount : 0);
        RenderTarget2D target = MakeTarget();
        try
        {
            VertexBuffer meshBuffer(device, BuildDeclaration(c), totalVertices, BufferUsage::None);
            meshBuffer.SetDataRaw(mesh.data(), totalVertices, c.stride);

            // The per-instance stream: one identity world matrix.
            struct MatrixRecord { float m[16]; };
            MatrixRecord identity{};
            identity.m[0] = identity.m[5] = identity.m[10] = identity.m[15] = 1.0f;
            VertexDeclaration instanceDecl(
                64, {VertexElement(0, VertexElementFormat::Vector4,
                                   VertexElementUsage::TextureCoordinate, 1),
                     VertexElement(16, VertexElementFormat::Vector4,
                                   VertexElementUsage::TextureCoordinate, 2),
                     VertexElement(32, VertexElementFormat::Vector4,
                                   VertexElementUsage::TextureCoordinate, 3),
                     VertexElement(48, VertexElementFormat::Vector4,
                                   VertexElementUsage::TextureCoordinate, 4)});
            VertexBuffer instanceBuffer(device, instanceDecl, 1, BufferUsage::None);
            instanceBuffer.SetDataRaw(&identity, 1, 64);

            const bool use32 = route == Route::OrdinaryIndexed32;
            const std::vector<std::uint16_t> i16 =
                BuildQuadIndices<std::uint16_t>(kColumnCount);
            const std::vector<std::uint32_t> i32 =
                BuildQuadIndices<std::uint32_t>(kColumnCount);
            IndexBuffer indexBuffer(
                device, use32 ? IndexElementSize::ThirtyTwoBits : IndexElementSize::SixteenBits,
                kMeshIndexCount, BufferUsage::None);
            if (use32)
                indexBuffer.SetData(i32.data(), kMeshIndexCount);
            else
                indexBuffer.SetData(i16.data(), kMeshIndexCount);

            if (route == Route::Instanced)
                device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, vertexOffset, 0),
                                         VertexBufferBinding(&instanceBuffer, 0, 1)});
            else
                device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, vertexOffset, 0)});
            device.SetIndexBuffer(route == Route::OrdinaryNonIndexed ? nullptr : &indexBuffer);

            BasicEffect effect(device);
            device.SetRenderTarget(&target);
            device.Clear(Color(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a));
            ApplyEffect(effect, c.hasColor);

            switch (route)
            {
                case Route::OrdinaryNonIndexed:
                    // A quad is two triangles; without indices the stream must repeat vertices,
                    // so this route draws the two triangles each quad's four vertices can form
                    // directly -- 0,1,2 per quad, which is the top-left triangle of each column.
                    device.DrawPrimitives(PrimitiveType::TriangleList, 0, kColumnCount);
                    break;
                case Route::Instanced:
                    device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                                   kMeshVertexCount, 0, kMeshPrimitiveCount, 1);
                    break;
                default:
                    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                                 kMeshVertexCount, 0, kMeshPrimitiveCount);
                    break;
            }
            device.SetRenderTarget(nullptr);
            out.frame = Capture(target);
            out.rendered = true;
        }
        catch (const std::exception& e)
        {
            out.rejection = e.what();
            device.SetRenderTarget(nullptr);
        }
        device.SetIndexBuffer(nullptr);
        return out;
    }

    static void Print(const DeclarationCase& c, Route route, const RouteResult& r)
    {
        std::cout << "[ GFX-216  ] " << RendererName() << ' ' << c.name << '/' << RouteName(route)
                  << " stride=" << c.stride;
        if (!r.rendered)
            std::cout << ": REJECTED -- \"" << r.rejection << '"' << std::endl;
        else
            std::cout << ':' << DescribeFrame(r.frame) << std::endl;
    }

    /// Asserts the full contract: every column's colour, lit count, top row and flatness.
    ///
    /// REMED-GFX-216 is scoped to bgfx production, and bgfx must therefore render every entry in
    /// the matrix. The same oracle run on the other renderers found the SAME class of defect on all
    /// of them, from two different mechanisms:
    ///
    ///   * every renderer except bgfx and EasyGL left `SetVertexDeclaration` an empty override
    ///     (Vulkan, WebGPU, Software, SdlGpu, D3D9, D3D11, D3D12), so the declaration was discarded
    ///     and a layout selected by stride -- REMED-GFX-217;
    ///   * EasyGL consumes the declaration but assigns each attribute's location from its INDEX in
    ///     the element list rather than from its semantic, so any declaration whose element order
    ///     differs from the selected stock program's input order binds the wrong attributes --
    ///     REMED-GFX-218.
    ///
    /// Neither translator may be written here. REMED-GFX-DECL-GUARD instead makes both safe: a
    /// declaration those inferred layouts cannot represent faithfully is REFUSED deterministically
    /// before any native layout, command or submission exists. So on those nine renderers the three
    /// colliding declarations assert a REJECTION, not a measured wrong picture -- an arm that
    /// starts rendering again fails whether the picture is right (the translator landed, and the
    /// arm is stale) or wrong (the guard regressed), and the message distinguishes them.
    ///
    /// Everywhere else the full contract is asserted. A renderer that REFUSES a declaration it
    /// cannot express is recorded verbatim: refusing is a capability boundary, accepting and
    /// rendering the wrong thing is not.
    static void ExpectStaircase(const DeclarationCase& c, Route route, const RouteResult& r)
    {
        Print(c, route, r);
        if (DeclarationLayoutMeasured())
        {
            // plans/plan_runtimerenderer.md RTR-P9-6: bgfx infers no byte-stride table, so this expectation
            // is not its contract. Asked at runtime, it steps aside for whichever renderer is active.
            if (!TranslatesDeclarations())
            {
                if (c.deviatesElsewhere)
                {
                    EXPECT_FALSE(r.rendered)
                        << c.name << '/' << RouteName(route) << " on " << RendererName()
                        << " was ACCEPTED. This declaration collides with the byte-stride table this "
                           "renderer infers its native layout from, so REMED-GFX-DECL-GUARD must refuse it "
                           "before any native work. If the picture below is correct, REMED-GFX-217/218's "
                           "real translator has landed and this arm is stale; if it is wrong, the guard "
                           "regressed and a draw is being rendered from the wrong bytes"
                        << DescribeFrame(r.frame);
                    if (!r.rendered)
                        EXPECT_FALSE(r.rejection.empty())
                            << c.name << '/' << RouteName(route)
                            << ": the refusal has to say why -- a silent refusal is not a boundary";
                    return;
                }
            }
            if (!r.rendered)
            {
                EXPECT_FALSE(r.rejection.empty())
                    << c.name << '/' << RouteName(route)
                    << ": the draw was refused without saying why. A renderer may decline a declaration "
                       "it cannot express, but the refusal has to be legible";
                return;
            }
            for (int column = 0; column < kColumnCount; ++column)
            {
                const ColumnReading got = ReadColumn(r.frame, column);
                const Rgba want =
                    ExpectedColor(kColumnColors[static_cast<std::size_t>(column)], c.hasColor);
                const std::string where =
                    std::string(c.name) + '/' + RouteName(route) + " column " +
                    std::to_string(column);
                EXPECT_EQ(kQuadTop, got.topRow)
                    << where << ": the quad starts at row " << got.topRow << " instead of "
                    << kQuadTop << " -- the POSITION attribute is being read from the wrong bytes"
                    << DescribeFrame(r.frame);
                EXPECT_EQ(1, got.distinct)
                    << where << ": " << got.distinct
                    << " distinct colours in one flat quad -- the records are desynchronised"
                    << DescribeFrame(r.frame);
                EXPECT_EQ(ExpectedLit(column), got.lit)
                    << where << ": " << got.lit << " lit pixels instead of " << ExpectedLit(column)
                    << " -- this column's staircase step moved, so a record was read at the wrong "
                       "stride" << DescribeFrame(r.frame);
                EXPECT_TRUE(NearlyEqual(got.color, want))
                    << where << ": carried " << got.color.ToString() << ", expected "
                    << want.ToString()
                    << " -- the COLOUR attribute is being read from the wrong byte offset or format"
                    << DescribeFrame(r.frame);
            }
        }
    }
};

// ---------------------------------------------------------------------------
// The matrix, on the ordinary indexed route. Every declaration must render its own staircase.
// ---------------------------------------------------------------------------

TEST_F(VertexDeclarationLayoutTest, EveryDeclarationRendersItsOwnLayout)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();
    for (const DeclarationCase& c : kMatrix)
        ExpectStaircase(c, Route::OrdinaryIndexed16,
                        Render(c, Route::OrdinaryIndexed16));
}

// ---------------------------------------------------------------------------
// The collision itself: two declarations that share a stride but differ in their elements must not
// share a native layout, in either order, and returning to the first must restore it.
// ---------------------------------------------------------------------------

TEST_F(VertexDeclarationLayoutTest, SameStrideDifferentElementsDoNotCollide)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();

    // Stride 16: Position@0+Color@12 versus Color@0+Position@4.
    ExpectStaircase(kPositionColor16, Route::OrdinaryIndexed16,
                    Render(kPositionColor16, Route::OrdinaryIndexed16));
    ExpectStaircase(kColorPosition16, Route::OrdinaryIndexed16,
                    Render(kColorPosition16, Route::OrdinaryIndexed16));
    ExpectStaircase(kPositionColor16, Route::OrdinaryIndexed16,
                    Render(kPositionColor16, Route::OrdinaryIndexed16));

    // Stride 24: the colour at offset 12 versus offset 20.
    ExpectStaircase(kPositionColorTexture24, Route::OrdinaryIndexed16,
                    Render(kPositionColorTexture24, Route::OrdinaryIndexed16));
    ExpectStaircase(kPositionTextureColor24, Route::OrdinaryIndexed16,
                    Render(kPositionTextureColor24, Route::OrdinaryIndexed16));
    ExpectStaircase(kPositionColorTexture24, Route::OrdinaryIndexed16,
                    Render(kPositionColorTexture24, Route::OrdinaryIndexed16));
}

TEST_F(VertexDeclarationLayoutTest, PaddedEquivalentDeclarationKeepsItsSemantics)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();

    // The same Position@0 + Color@12 semantics at stride 16 and at stride 32. Only the padding
    // differs, so both must render identically.
    ExpectStaircase(kPositionColor16, Route::OrdinaryIndexed16,
                    Render(kPositionColor16, Route::OrdinaryIndexed16));
    ExpectStaircase(kPositionColorPadded32, Route::OrdinaryIndexed16,
                    Render(kPositionColorPadded32, Route::OrdinaryIndexed16));
}

// ---------------------------------------------------------------------------
// Routes. The layout is built before any route is chosen, so every route must agree.
// ---------------------------------------------------------------------------

TEST_F(VertexDeclarationLayoutTest, EveryRouteBindsTheDeclaredLayout)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();
    for (const DeclarationCase& c : {kPositionOnly12, kPositionColor16, kColorPosition16,
                                     kPositionColorPadded32})
    {
        ExpectStaircase(c, Route::OrdinaryIndexed16, Render(c, Route::OrdinaryIndexed16));
        ExpectStaircase(c, Route::OrdinaryIndexed32, Render(c, Route::OrdinaryIndexed32));
        ExpectStaircase(c, Route::Instanced, Render(c, Route::Instanced));
    }
}

// ---------------------------------------------------------------------------
// REMED-GFX-211's geometry VertexOffset must still address whole records of THIS declaration's
// stride -- an offset resolved through a guessed 16-byte stride lands mid-record on a 12-byte one.
// ---------------------------------------------------------------------------

TEST_F(VertexDeclarationLayoutTest, GeometryVertexOffsetAddressesDeclaredRecords)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();
    for (const DeclarationCase& c : {kPositionOnly12, kPositionColor16, kColorPosition16})
    {
        const RouteResult r = Render(c, Route::Instanced, /*withDecoy=*/true, kDecoyVertexCount);
        ExpectStaircase(c, Route::Instanced, r);
    }
}

// ---------------------------------------------------------------------------
// Transitions inside one process: A -> B -> C(B's stride, different elements) -> A, plus a
// contents-only rewrite. No stale layout may survive any of them.
// ---------------------------------------------------------------------------

TEST_F(VertexDeclarationLayoutTest, DeclarationTransitionsDoNotReuseAStaleLayout)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();
    const std::array<DeclarationCase, 8> sequence{
        kPositionOnly12,          // A: stride 12
        kPositionColor16,         // B: stride 16
        kColorPosition16,         // C: B's stride, different elements
        kPositionOnly12,          // back to A
        kPositionColorTexture24,
        kPositionTextureColor24,  // same stride as the previous entry
        kPositionColorPadded32,
        kPositionColor16,         // padded equivalent's unpadded sibling, last
    };
    for (const DeclarationCase& c : sequence)
        ExpectStaircase(c, Route::OrdinaryIndexed16, Render(c, Route::OrdinaryIndexed16));
}

// ---------------------------------------------------------------------------
// REMED-GFX-DECL-GUARD. The legs above prove that a colliding declaration no longer renders. These
// prove the rest of the boundary's contract: that refusing costs the caller nothing, that the
// refusal happens before anything native is produced, and that every route into the renderer goes
// through it -- not only the one the collision matrix happens to use.
//
// bgfx is excluded from the REFUSAL legs and only from those: it has a real declaration translator
// (REMED-GFX-216), so the same declarations are representable there and must keep rendering. That
// asymmetry is the point -- the guard is a per-renderer capability boundary, not a claim that these
// declarations are globally invalid.
// ---------------------------------------------------------------------------

class DeclarationGuardTest : public VertexDeclarationLayoutTest
{
protected:
    /// True when nothing at all was rasterized into @p s -- every pixel is still the clear colour.
    static bool Untouched(const FrameSnapshot& s)
    {
        for (const Color& p : s.pixels)
            if (p.getRProperty() != kClearColor.r || p.getGProperty() != kClearColor.g ||
                p.getBProperty() != kClearColor.b)
                return false;
        return true;
    }

    /// The four-column staircase, asserted in full.
    static void ExpectContract(const FrameSnapshot& frame, const DeclarationCase& c,
                               const char* where)
    {
        for (int column = 0; column < kColumnCount; ++column)
        {
            const ColumnReading got = ReadColumn(frame, column);
            const Rgba want =
                ExpectedColor(kColumnColors[static_cast<std::size_t>(column)], c.hasColor);
            EXPECT_EQ(kQuadTop, got.topRow) << where << " column " << column << DescribeFrame(frame);
            EXPECT_EQ(ExpectedLit(column), got.lit)
                << where << " column " << column << DescribeFrame(frame);
            EXPECT_TRUE(NearlyEqual(got.color, want))
                << where << " column " << column << ": carried " << got.color.ToString()
                << ", expected " << want.ToString() << DescribeFrame(frame);
        }
    }

    /// Uploads @p c's mesh, draws it into @p target and reports what happened. Unlike
    /// VertexDeclarationLayoutTest::Render this keeps the target, so a refused draw's frame can be
    /// inspected: a boundary that refuses AFTER queueing work is not a boundary.
    struct GuardedDraw
    {
        bool rendered = false;
        std::string rejection;
        FrameSnapshot frame;
    };

    GuardedDraw DrawInto(RenderTarget2D& target, const DeclarationCase& c, bool dynamicUpload,
                         bool use32BitIndices)
    {
        GuardedDraw out;
        const std::vector<std::uint8_t> mesh = BuildMesh(c, false);
        const std::vector<std::uint16_t> i16 = BuildQuadIndices<std::uint16_t>(kColumnCount);
        const std::vector<std::uint32_t> i32 = BuildQuadIndices<std::uint32_t>(kColumnCount);
        try
        {
            VertexBuffer meshBuffer(device, BuildDeclaration(c), kMeshVertexCount,
                                    dynamicUpload ? BufferUsage::WriteOnly : BufferUsage::None);
            if (dynamicUpload)
            {
                // Two uploads through the streaming path, so the guard is proven to survive a
                // re-upload as well as a first one.
                meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, c.stride);
                meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, c.stride);
            }
            else
            {
                meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, c.stride);
            }

            IndexBuffer indexBuffer(device,
                                    use32BitIndices ? IndexElementSize::ThirtyTwoBits
                                                    : IndexElementSize::SixteenBits,
                                    kMeshIndexCount, BufferUsage::None);
            if (use32BitIndices)
                indexBuffer.SetData(i32.data(), kMeshIndexCount);
            else
                indexBuffer.SetData(i16.data(), kMeshIndexCount);

            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
            device.SetIndexBuffer(&indexBuffer);

            BasicEffect effect(device);
            device.SetRenderTarget(&target);
            device.Clear(Color(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a));
            ApplyEffect(effect, c.hasColor);
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0,
                                         kMeshPrimitiveCount);
            device.SetRenderTarget(nullptr);
            out.rendered = true;
        }
        catch (const std::exception& e)
        {
            out.rejection = e.what();
            device.SetRenderTarget(nullptr);
        }
        device.SetIndexBuffer(nullptr);
        out.frame = Capture(target);
        return out;
    }
};

// A refused draw must produce NOTHING: no partial geometry, no half-queued command that a later
// Present flushes. The target still holds exactly the clear colour it was given.
TEST_F(DeclarationGuardTest, ARefusedDeclarationRasterizesNothing)
{
    // plans/plan_runtimerenderer.md RTR-P9-6: bgfx TRANSLATES a colliding declaration rather than
    // refusing it, so the refusal contract below is not its contract -- see
    // TheTranslatingRendererStillRendersEveryCollidingDeclaration for what it does instead.
    if (TranslatesDeclarations())
        GTEST_SKIP() << RendererName()
                     << " translates colliding declarations instead of refusing them";
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();
    RenderTarget2D target = MakeTarget();
    const GuardedDraw got = DrawInto(target, kColorPosition16, false, false);
    std::cout << "[ DECL-GUARD ] " << RendererName() << " colorPosition16 refusal: "
              << (got.rendered ? std::string("ACCEPTED") : '"' + got.rejection + '"') << std::endl;
    ASSERT_FALSE(got.rendered)
        << "a declaration this renderer infers no faithful layout for was accepted"
        << DescribeFrame(got.frame);
    EXPECT_FALSE(got.rejection.empty()) << "the refusal must say why";
    EXPECT_TRUE(Untouched(got.frame))
        << "the refused draw still rasterized something -- the boundary is downstream of the "
           "work it was supposed to prevent" << DescribeFrame(got.frame);
}

// The refusal may not poison the device, the buffer allocator or any cached layout: the very next
// draw, through a freshly allocated buffer that very likely reuses the refused one's address, must
// render its own declaration exactly.
TEST_F(DeclarationGuardTest, AValidDrawAfterARefusedOneStillRenders)
{
    // plans/plan_runtimerenderer.md RTR-P9-6: bgfx TRANSLATES a colliding declaration rather than
    // refusing it, so the refusal contract below is not its contract -- see
    // TheTranslatingRendererStillRendersEveryCollidingDeclaration for what it does instead.
    if (TranslatesDeclarations())
        GTEST_SKIP() << RendererName()
                     << " translates colliding declarations instead of refusing them";
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();
    RenderTarget2D target = MakeTarget();

    const GuardedDraw before = DrawInto(target, kPositionColor16, false, false);
    ASSERT_TRUE(before.rendered) << before.rejection;
    ExpectContract(before.frame, kPositionColor16, "before the refusal");

    const GuardedDraw refused = DrawInto(target, kColorPosition16, false, false);
    ASSERT_FALSE(refused.rendered);

    const GuardedDraw after = DrawInto(target, kPositionColor16, false, false);
    ASSERT_TRUE(after.rendered)
        << "a valid declaration was refused after an invalid one -- the guard kept state: "
        << after.rejection;
    ExpectContract(after.frame, kPositionColor16, "after the refusal");

    // A different valid declaration, to prove the recovery is not specific to repeating the first.
    const GuardedDraw other = DrawInto(target, kPositionColorTexture24, false, false);
    if (other.rendered)
        ExpectContract(other.frame, kPositionColorTexture24, "a third declaration");
    else
        EXPECT_FALSE(other.rejection.empty())
            << "positionColorTexture24 was refused without saying why";
}

// Static and dynamic buffers, and both index widths, reach the same boundary. A guard that only
// covers the one upload path the collision matrix happens to use is not a boundary.
TEST_F(DeclarationGuardTest, EveryUploadAndIndexWidthReachesTheSameBoundary)
{
    // plans/plan_runtimerenderer.md RTR-P9-6: bgfx TRANSLATES a colliding declaration rather than
    // refusing it, so the refusal contract below is not its contract -- see
    // TheTranslatingRendererStillRendersEveryCollidingDeclaration for what it does instead.
    if (TranslatesDeclarations())
        GTEST_SKIP() << RendererName()
                     << " translates colliding declarations instead of refusing them";
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();
    for (const bool dynamicUpload : {false, true})
    {
        for (const bool use32 : {false, true})
        {
            RenderTarget2D target = MakeTarget();
            const GuardedDraw got = DrawInto(target, kColorPosition16, dynamicUpload, use32);
            std::cout << "[ DECL-GUARD ] " << RendererName() << " colorPosition16 "
                      << (dynamicUpload ? "dynamic" : "static") << '/'
                      << (use32 ? "index32" : "index16") << ": "
                      << (got.rendered ? std::string("ACCEPTED") : "refused") << std::endl;
            EXPECT_FALSE(got.rendered)
                << (dynamicUpload ? "dynamic" : "static") << '/'
                << (use32 ? "index32" : "index16")
                << " slipped past the declaration guard" << DescribeFrame(got.frame);
            EXPECT_TRUE(Untouched(got.frame)) << DescribeFrame(got.frame);
        }
    }
}

// DrawUserPrimitives builds its own VertexBuffer inside GraphicsDevice and propagates the caller's
// declaration to it, so it reaches the same boundary -- and a valid user draw afterwards still
// works.
TEST_F(DeclarationGuardTest, DrawUserPrimitivesReachesTheSameBoundary)
{
    // plans/plan_runtimerenderer.md RTR-P9-6: bgfx TRANSLATES a colliding declaration rather than
    // refusing it, so the refusal contract below is not its contract -- see
    // TheTranslatingRendererStillRendersEveryCollidingDeclaration for what it does instead.
    if (TranslatesDeclarations())
        GTEST_SKIP() << RendererName()
                     << " translates colliding declarations instead of refusing them";
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();
    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    const auto drawUser = [&](const DeclarationCase& c) {
        const std::vector<std::uint8_t> mesh = BuildMesh(c, false);
        const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(kColumnCount);
        std::string rejection;
        device.SetRenderTarget(&target);
        device.Clear(Color(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a));
        ApplyEffect(effect, c.hasColor);
        try
        {
            device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, mesh.data(), 0,
                                             kMeshVertexCount, indices.data(), 0,
                                             kMeshPrimitiveCount, BuildDeclaration(c));
        }
        catch (const std::exception& e)
        {
            rejection = e.what();
        }
        device.SetRenderTarget(nullptr);
        return rejection;
    };

    const std::string refused = drawUser(kColorPosition16);
    std::cout << "[ DECL-GUARD ] " << RendererName() << " DrawUser colorPosition16: "
              << (refused.empty() ? std::string("ACCEPTED") : '"' + refused + '"') << std::endl;
    EXPECT_FALSE(refused.empty())
        << "DrawUserIndexedPrimitives bypassed the declaration guard"
        << DescribeFrame(Capture(target));
    EXPECT_TRUE(Untouched(Capture(target))) << DescribeFrame(Capture(target));

    const std::string accepted = drawUser(kPositionColor16);
    EXPECT_TRUE(accepted.empty()) << "a valid user draw was refused after an invalid one: "
                                  << accepted;
    if (accepted.empty())
        ExpectContract(Capture(target), kPositionColor16, "DrawUser after the refusal");
}


// The control. bgfx derives its native layout from the declaration (REMED-GFX-216), so the very
// declarations the other renderers now refuse must still render their own contract here -- which is
// what makes the refusals a per-renderer capability boundary rather than a claim that these
// declarations are invalid.
TEST_F(DeclarationGuardTest, TheTranslatingRendererStillRendersEveryCollidingDeclaration)
{
    // plans/plan_runtimerenderer.md RTR-P9-6 / REMED-GFX-234: the contract of whichever renderer
    // translates, asked at runtime rather than named.
    if (!TranslatesDeclarations())
        GTEST_SKIP() << RendererName() << " refuses colliding declarations rather than translating";
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();
    for (const DeclarationCase& c : {kColorPosition16, kPositionTextureColor24,
                                     kPositionColorPadded32})
    {
        RenderTarget2D target = MakeTarget();
        const GuardedDraw got = DrawInto(target, c, false, false);
        std::cout << "[ DECL-GUARD ] translating control " << RendererName() << ' ' << c.name << ": "
                  << (got.rendered ? "rendered" : '"' + got.rejection + '"') << std::endl;
        ASSERT_TRUE(got.rendered)
            << c.name
            << " was refused on " << RendererName()
            << ", which translates the declaration, so the checkpoint guard must never fire "
               "here: " << got.rejection;
        ExpectContract(got.frame, c, c.name);
    }
}



// REMED-GFX-218's control. EasyGL's CUSTOM ShaderEffect path documents its own convention --
// attribute location N is the Nth element of the declaration, exactly as ApplyLayout binds it --
// and the checkpoint guard must not touch it. A ShaderEffect written to that convention has to
// keep rendering the declaration the stock programs now refuse, because for a shader the game owns
// there is no stock input list to compare against and nothing is being reinterpreted.
TEST_F(DeclarationGuardTest, CustomShaderEffectKeepsItsElementIndexConvention)
{
    // plans/plan_runtimerenderer.md RTR-P9-6: EasyGL's own convention, asked of the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2);
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();

    // Location 0 is this declaration's FIRST element (the colour at byte 0) and location 1 its
    // second (the position at byte 4) -- the colorPosition16 shape, read by index.
    static const char* kVert = R"(#version 300 es
precision highp float;
layout(location = 0) in vec4 aColorIn;
layout(location = 1) in vec3 aPosIn;
out vec4 vColor;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() {
    vColor = aColorIn;
    gl_Position = Projection * View * World * vec4(aPosIn, 1.0);
}
)";
    static const char* kFrag = R"(#version 300 es
precision mediump float;
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)";

    RenderTarget2D target = MakeTarget();
    const std::vector<std::uint8_t> mesh = BuildMesh(kColorPosition16, false);
    const std::vector<std::uint16_t> indices = BuildQuadIndices<std::uint16_t>(kColumnCount);

    std::string rejection;
    try
    {
        VertexBuffer meshBuffer(device, BuildDeclaration(kColorPosition16), kMeshVertexCount,
                                BufferUsage::None);
        meshBuffer.SetDataRaw(mesh.data(), kMeshVertexCount, kColorPosition16.stride);
        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, kMeshIndexCount,
                                BufferUsage::None);
        indexBuffer.SetData(indices.data(), kMeshIndexCount);
        device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
        device.SetIndexBuffer(&indexBuffer);

        ShaderEffect effect(device, kVert, kFrag);
        device.SetRenderTarget(&target);
        device.Clear(Color(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a));
        effect.Apply();
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, kMeshVertexCount, 0,
                                     kMeshPrimitiveCount);
        device.SetRenderTarget(nullptr);
    }
    catch (const std::exception& e)
    {
        rejection = e.what();
        device.SetRenderTarget(nullptr);
    }
    device.SetIndexBuffer(nullptr);

    const FrameSnapshot frame = Capture(target);
    std::cout << "[ DECL-GUARD ] EasyGL ShaderEffect control: "
              << (rejection.empty() ? DescribeFrame(frame) : '"' + rejection + '"') << std::endl;
    ASSERT_TRUE(rejection.empty())
        << "the stock-program guard fired on a custom ShaderEffect draw. REMED-GFX-218's "
           "checkpoint action must leave the custom path untouched: " << rejection;

    // The shader writes COLOR0 straight out with no DiffuseColor term, so each column carries its
    // own record's colour unmultiplied -- which is only true if location 0 really did bind the
    // declaration's first element.
    for (int column = 0; column < kColumnCount; ++column)
    {
        const ColumnReading got = ReadColumn(frame, column);
        const Rgba want = kColumnColors[static_cast<std::size_t>(column)];
        EXPECT_EQ(kQuadTop, got.topRow) << "column " << column << DescribeFrame(frame);
        EXPECT_EQ(ExpectedLit(column), got.lit) << "column " << column << DescribeFrame(frame);
        EXPECT_TRUE(NearlyEqual(got.color, want))
            << "column " << column << ": custom ShaderEffect carried " << got.color.ToString()
            << ", expected its own record's colour " << want.ToString()
            << " -- the element-index convention changed" << DescribeFrame(frame);
    }
}


// ---------------------------------------------------------------------------
// bgfx-only: the native layout itself. The rendering legs above prove the OUTCOME; this proves the
// DECISION, which is what REMED-GFX-216 is actually about.
// ---------------------------------------------------------------------------

#ifdef CNA_TEST_BGFX_AVAILABLE

class BgfxVertexLayoutTest : public VertexDeclarationLayoutTest
{
protected:
    struct NativeLayout
    {
        int stride = -1;
        int positionOffset = -1;
        int colorOffset = -1;
        int texCoordOffset = -1;
        bool hasPosition = false;
        bool hasColor = false;
        bool hasTexCoord = false;

        [[nodiscard]] std::string ToString() const
        {
            std::ostringstream os;
            os << "stride=" << stride
               << " Position" << (hasPosition ? "@" + std::to_string(positionOffset) : "=absent")
               << " Color0" << (hasColor ? "@" + std::to_string(colorOffset) : "=absent")
               << " TexCoord0"
               << (hasTexCoord ? "@" + std::to_string(texCoordOffset) : "=absent");
            return os.str();
        }
    };

    /// Reads the native layout bgfx actually built for @p buffer.
    static NativeLayout Inspect(VertexBuffer& buffer)
    {
        auto* renderer =
            dynamic_cast<CNA::Internal::Renderers::Bgfx::BgfxVertexBufferRenderer*>(
                &buffer.GetRenderer());
        NativeLayout n;
        if (renderer == nullptr)
            return n;
        const bgfx::VertexLayout& l = renderer->layout;
        n.stride = l.getStride();
        n.hasPosition = l.has(bgfx::Attrib::Position);
        n.hasColor = l.has(bgfx::Attrib::Color0);
        n.hasTexCoord = l.has(bgfx::Attrib::TexCoord0);
        if (n.hasPosition) n.positionOffset = l.getOffset(bgfx::Attrib::Position);
        if (n.hasColor) n.colorOffset = l.getOffset(bgfx::Attrib::Color0);
        if (n.hasTexCoord) n.texCoordOffset = l.getOffset(bgfx::Attrib::TexCoord0);
        return n;
    }

    /// Uploads @p c's mesh into a fresh buffer and returns the native layout bgfx chose.
    NativeLayout LayoutFor(const DeclarationCase& c)
    {
        const std::vector<std::uint8_t> mesh = BuildMesh(c, false);
        VertexBuffer buffer(device, BuildDeclaration(c), kMeshVertexCount, BufferUsage::None);
        buffer.SetDataRaw(mesh.data(), kMeshVertexCount, c.stride);
        return Inspect(buffer);
    }
};

TEST_F(BgfxVertexLayoutTest, NativeLayoutMatchesTheDeclaration)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();
    for (const DeclarationCase& c : kMatrix)
    {
        const NativeLayout n = LayoutFor(c);
        std::cout << "[ GFX-216  ] bgfx native layout for " << c.name
                  << " (declared stride " << c.stride << ", Position@" << c.positionOffset
                  << ", Color@" << c.colorOffset << ", TexCoord@" << c.texCoordOffset
                  << "): " << n.ToString() << std::endl;

        EXPECT_EQ(c.stride, n.stride)
            << c.name << ": the native layout strides " << n.stride
            << " bytes through a " << c.stride
            << "-byte buffer. Every record after the first is read from the wrong address";
        EXPECT_TRUE(n.hasPosition) << c.name << ": the declaration has a Position element";
        EXPECT_EQ(c.positionOffset, n.positionOffset)
            << c.name << ": Position is bound at native offset " << n.positionOffset
            << " but declared at " << c.positionOffset;
        EXPECT_EQ(c.colorOffset >= 0, n.hasColor)
            << c.name << ": the native layout " << (n.hasColor ? "declares" : "omits")
            << " Color0 while the declaration " << (c.colorOffset >= 0 ? "has" : "has no")
            << " colour element. A layout must not invent an attribute the declaration omits";
        if (c.colorOffset >= 0 && n.hasColor)
            EXPECT_EQ(c.colorOffset, n.colorOffset)
                << c.name << ": Color0 is bound at native offset " << n.colorOffset
                << " but declared at " << c.colorOffset;
        EXPECT_EQ(c.texCoordOffset >= 0, n.hasTexCoord)
            << c.name << ": the native layout " << (n.hasTexCoord ? "declares" : "omits")
            << " TexCoord0 while the declaration "
            << (c.texCoordOffset >= 0 ? "has" : "has no") << " texture-coordinate element";
        if (c.texCoordOffset >= 0 && n.hasTexCoord)
            EXPECT_EQ(c.texCoordOffset, n.texCoordOffset)
                << c.name << ": TexCoord0 is bound at native offset " << n.texCoordOffset
                << " but declared at " << c.texCoordOffset;
    }
}

TEST_F(BgfxVertexLayoutTest, SameStrideDifferentDeclarationsGetDifferentNativeLayouts)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();

    const NativeLayout a = LayoutFor(kPositionColor16);
    const NativeLayout b = LayoutFor(kColorPosition16);
    std::cout << "[ GFX-216  ] bgfx stride-16 collision: positionColor16 -> " << a.ToString()
              << " | colorPosition16 -> " << b.ToString() << std::endl;
    EXPECT_EQ(16, a.stride);
    EXPECT_EQ(16, b.stride);
    EXPECT_NE(a.positionOffset, b.positionOffset)
        << "two stride-16 declarations that place Position at 0 and at 4 produced the same "
           "native Position offset -- the layout was keyed on the stride";
    EXPECT_NE(a.colorOffset, b.colorOffset)
        << "two stride-16 declarations that place Color at 12 and at 0 produced the same native "
           "Color0 offset -- the layout was keyed on the stride";

    const NativeLayout c = LayoutFor(kPositionColorTexture24);
    const NativeLayout d = LayoutFor(kPositionTextureColor24);
    std::cout << "[ GFX-216  ] bgfx stride-24 collision: positionColorTexture24 -> "
              << c.ToString() << " | positionTextureColor24 -> " << d.ToString() << std::endl;
    EXPECT_NE(c.colorOffset, d.colorOffset)
        << "two stride-24 declarations that place Color at 12 and at 20 produced the same native "
           "Color0 offset";
    EXPECT_NE(c.texCoordOffset, d.texCoordOffset)
        << "two stride-24 declarations that place TexCoord at 16 and at 12 produced the same "
           "native TexCoord0 offset";
}

TEST_F(BgfxVertexLayoutTest, RecycledBufferAddressesDoNotInheritAStaleLayout)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();

    // A `VertexBuffer`'s declaration is immutable, so the way one native buffer object comes to
    // serve two different declarations is destruction and reallocation -- very often at the SAME
    // address. Both declarations here are stride 16, which is exactly the case a "rebuild only
    // when the stride changed" guard cannot see.
    const std::vector<std::uint8_t> a = BuildMesh(kPositionColor16, false);
    const std::vector<std::uint8_t> b = BuildMesh(kColorPosition16, false);

    const auto upload = [&](const DeclarationCase& c, const std::vector<std::uint8_t>& bytes,
                            const void** addressOut) {
        auto buffer = std::make_unique<VertexBuffer>(
            device, BuildDeclaration(c), kMeshVertexCount, BufferUsage::None);
        buffer->SetDataRaw(bytes.data(), kMeshVertexCount, c.stride);
        *addressOut = static_cast<const void*>(&buffer->GetRenderer());
        return Inspect(*buffer);
        // buffer is destroyed here, freeing the renderer for the next allocation to reuse.
    };

    const void* addr1 = nullptr;
    const void* addr2 = nullptr;
    const void* addr3 = nullptr;
    const NativeLayout first = upload(kPositionColor16, a, &addr1);
    const NativeLayout second = upload(kColorPosition16, b, &addr2);
    const NativeLayout third = upload(kPositionColor16, a, &addr3);

    std::cout << "[ GFX-216  ] bgfx recycled addresses: " << addr1 << ' ' << addr2 << ' ' << addr3
              << (addr1 == addr2 ? "  (addresses 1 and 2 REUSED)" : "  (addresses distinct)")
              << "\n[ GFX-216  ] bgfx re-declaration: " << first.ToString() << " -> "
              << second.ToString() << " -> " << third.ToString() << std::endl;

    EXPECT_EQ(0, first.positionOffset);
    EXPECT_EQ(12, first.colorOffset);
    EXPECT_EQ(4, second.positionOffset)
        << "a stride-16 declaration that places Position at 4 got Position at "
        << second.positionOffset << " -- a previous same-stride layout was reused";
    EXPECT_EQ(0, second.colorOffset)
        << "a stride-16 declaration that places Color at 0 got Color at " << second.colorOffset
        << " -- a previous same-stride layout was reused";
    EXPECT_EQ(0, third.positionOffset) << "returning to the first declaration must restore it";
    EXPECT_EQ(12, third.colorOffset) << "returning to the first declaration must restore it";
}

// Every built-in vertex type must still get exactly the layout the stride table used to hand it.
// This is the equivalence proof for REMED-GFX-216's replacement of that table: the expectations
// below are transcribed from the switch it replaced, so a derived layout that differs from the one
// the renderer shipped before is a regression here rather than a rendering surprise later.
TEST_F(BgfxVertexLayoutTest, BuiltInDeclarationsKeepTheirEstablishedLayout)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();

    struct Attribute
    {
        bgfx::Attrib::Enum attrib;
        int offset;
        const char* name;
    };
    struct BuiltIn
    {
        const char* name;
        const VertexDeclaration* declaration;
        int stride;
        std::vector<Attribute> attributes;
    };

    using namespace Microsoft::Xna::Framework::Graphics;
    const std::vector<BuiltIn> builtIns{
        {"VertexPositionColor", &VertexPositionColor::getVertexDeclarationStatic(), 16,
         {{bgfx::Attrib::Position, 0, "Position"}, {bgfx::Attrib::Color0, 12, "Color0"}}},
        {"VertexPositionTexture", &VertexPositionTexture::getVertexDeclarationStatic(), 20,
         {{bgfx::Attrib::Position, 0, "Position"}, {bgfx::Attrib::TexCoord0, 12, "TexCoord0"}}},
        {"VertexPositionColorTexture", &VertexPositionColorTexture::getVertexDeclarationStatic(),
         24,
         {{bgfx::Attrib::Position, 0, "Position"},
          {bgfx::Attrib::Color0, 12, "Color0"},
          {bgfx::Attrib::TexCoord0, 16, "TexCoord0"}}},
        {"VertexPositionNormalTexture", &VertexPositionNormalTexture::getVertexDeclarationStatic(),
         32,
         {{bgfx::Attrib::Position, 0, "Position"},
          {bgfx::Attrib::Normal, 12, "Normal"},
          {bgfx::Attrib::TexCoord0, 24, "TexCoord0"}}},
        {"VertexPositionNormalTangentTexture",
         &VertexPositionNormalTangentTexture::getVertexDeclarationStatic(), 48,
         {{bgfx::Attrib::Position, 0, "Position"},
          {bgfx::Attrib::Normal, 12, "Normal"},
          {bgfx::Attrib::Tangent, 24, "Tangent"},
          {bgfx::Attrib::TexCoord0, 40, "TexCoord0"}}},
        {"VertexPositionNormalTextureSkinned",
         &VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 52,
         {{bgfx::Attrib::Position, 0, "Position"},
          {bgfx::Attrib::Normal, 12, "Normal"},
          {bgfx::Attrib::TexCoord0, 24, "TexCoord0"},
          {bgfx::Attrib::Weight, 32, "Weight"},
          {bgfx::Attrib::Indices, 48, "Indices"}}},
        {"VertexPositionNormalTangentTextureSkinned",
         &VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic(), 68,
         {{bgfx::Attrib::Position, 0, "Position"},
          {bgfx::Attrib::Normal, 12, "Normal"},
          {bgfx::Attrib::Tangent, 24, "Tangent"},
          {bgfx::Attrib::TexCoord0, 40, "TexCoord0"},
          {bgfx::Attrib::Weight, 48, "Weight"},
          {bgfx::Attrib::Indices, 64, "Indices"}}},
    };

    for (const BuiltIn& b : builtIns)
    {
        const int stride = b.declaration->getVertexStrideProperty();
        ASSERT_EQ(b.stride, stride)
            << b.name << ": this test's transcription of the old stride table is stale";

        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(kMeshVertexCount) * static_cast<std::size_t>(stride), 0);
        VertexBuffer buffer(device, *b.declaration, kMeshVertexCount, BufferUsage::None);
        buffer.SetDataRaw(bytes.data(), kMeshVertexCount, stride);
        auto* renderer =
            dynamic_cast<CNA::Internal::Renderers::Bgfx::BgfxVertexBufferRenderer*>(
                &buffer.GetRenderer());
        ASSERT_NE(nullptr, renderer);
        const bgfx::VertexLayout& l = renderer->layout;

        std::ostringstream got;
        got << "stride=" << l.getStride();
        for (const Attribute& a : b.attributes)
            got << ' ' << a.name << (l.has(a.attrib) ? "@" + std::to_string(l.getOffset(a.attrib))
                                                     : std::string("=absent"));
        std::cout << "[ GFX-216  ] bgfx built-in " << b.name << ": " << got.str() << std::endl;

        EXPECT_EQ(stride, l.getStride()) << b.name << ": native stride diverged";
        for (const Attribute& a : b.attributes)
        {
            EXPECT_TRUE(l.has(a.attrib))
                << b.name << ": the established layout binds " << a.name << " and this one omits it";
            if (l.has(a.attrib))
                EXPECT_EQ(a.offset, l.getOffset(a.attrib))
                    << b.name << ": " << a.name << " moved from the established offset "
                    << a.offset << " to " << l.getOffset(a.attrib);
        }
        // The stride table never bound anything a declaration did not name; nor may this.
        EXPECT_FALSE(l.has(bgfx::Attrib::Color1)) << b.name << ": invented a Color1 attribute";
        EXPECT_FALSE(l.has(bgfx::Attrib::Bitangent))
            << b.name << ": invented a Bitangent attribute";
    }
}

// A declaration this renderer cannot express natively must be refused before anything is created or
// submitted -- never replaced by a nearby built-in guess, which is what the stride table did.
TEST_F(BgfxVertexLayoutTest, UnsupportedDeclarationsAreRejectedDeterministically)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();

    const auto upload = [&](const VertexDeclaration& decl, int stride) {
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(kMeshVertexCount) * static_cast<std::size_t>(stride), 0);
        VertexBuffer buffer(device, decl, kMeshVertexCount, BufferUsage::None);
        buffer.SetDataRaw(bytes.data(), kMeshVertexCount, stride);
    };

    // A semantic bgfx has no attribute for.
    EXPECT_THROW(
        upload(VertexDeclaration(16, {VertexElement(0, VertexElementFormat::Vector3,
                                                    VertexElementUsage::Position, 0),
                                      VertexElement(12, VertexElementFormat::Single,
                                                    VertexElementUsage::PointSize, 0)}),
               16),
        std::exception)
        << "a usage bgfx cannot bind must be refused, not silently dropped";

    // A usage index outside the family bgfx can express (it has Position, not Position1).
    EXPECT_THROW(
        upload(VertexDeclaration(24, {VertexElement(0, VertexElementFormat::Vector3,
                                                    VertexElementUsage::Position, 0),
                                      VertexElement(12, VertexElementFormat::Vector3,
                                                    VertexElementUsage::Position, 1)}),
               24),
        std::exception)
        << "a second Position stream must be refused, not folded onto the first";

    // The same semantic twice.
    EXPECT_THROW(
        upload(VertexDeclaration(20, {VertexElement(0, VertexElementFormat::Vector3,
                                                    VertexElementUsage::Position, 0),
                                      VertexElement(12, VertexElementFormat::Color,
                                                    VertexElementUsage::Color, 0),
                                      VertexElement(16, VertexElementFormat::Color,
                                                    VertexElementUsage::Color, 0)}),
               20),
        std::exception)
        << "a repeated semantic must be refused";

    // A supported declaration immediately afterwards must still work -- a rejection may not leave
    // the buffer or the renderer in a state that poisons the next upload.
    const std::vector<std::uint8_t> mesh = BuildMesh(kPositionColor16, false);
    VertexBuffer good(device, BuildDeclaration(kPositionColor16), kMeshVertexCount,
                      BufferUsage::None);
    ASSERT_NO_THROW(good.SetDataRaw(mesh.data(), kMeshVertexCount, kPositionColor16.stride));
    const NativeLayout n = Inspect(good);
    EXPECT_EQ(16, n.stride);
    EXPECT_EQ(0, n.positionOffset);
    EXPECT_EQ(12, n.colorOffset);
}

TEST_F(BgfxVertexLayoutTest, ContentsOnlyRewriteCreatesNoNativeResource)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    // plans/plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!DeclarationLayout())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireThreeD();

    const std::vector<std::uint8_t> mesh = BuildMesh(kPositionColor16, false);
    VertexBuffer buffer(device, BuildDeclaration(kPositionColor16), kMeshVertexCount,
                        BufferUsage::None);
    buffer.SetDataRaw(mesh.data(), kMeshVertexCount, kPositionColor16.stride);

    device.Present();
    device.Present();
    const bgfx::Stats* stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    const std::uint16_t baseline = stats->numDynamicVertexBuffers;

    // Sixteen contents-only rewrites of the SAME declaration. Nothing about the layout changed, so
    // nothing native may be created -- REMED-GFX-109's rotation still applies when a draw has
    // referenced the current version, so this is measured without any draw in between.
    for (int i = 0; i < 16; ++i)
        buffer.SetDataRaw(mesh.data(), kMeshVertexCount, kPositionColor16.stride);

    device.Present();
    device.Present();
    stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    std::cout << "[ GFX-216  ] bgfx dynamic vertex buffers: baseline " << baseline
              << ", after 16 contents-only rewrites " << stats->numDynamicVertexBuffers
              << std::endl;
    EXPECT_LE(stats->numDynamicVertexBuffers, baseline)
        << "a contents-only rewrite created a native vertex buffer -- the layout is unchanged, "
           "so nothing may be reallocated";
}

#endif   // CNA_TEST_BGFX_AVAILABLE

