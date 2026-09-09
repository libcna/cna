// SPDX-License-Identifier: MS-PL
#pragma once

// REMED-GFX-209's asymmetric-triangle wireframe oracle, shared by every suite that measures a
// renderer's FillMode contract. It lives in a header rather than being copied because two copies of
// a pixel oracle drift, and the whole point of this one is that the readings taken by different
// suites are comparable: `GraphicsDeviceCapabilityTests.cpp` measures the per-renderer contract,
// `WebGpuWireFrameContractTests.cpp` measures WEBGPU-115's rejection boundary, and both must be
// judging the same geometry, the same probes and the same colours.

#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

// Renderers whose CnaTests build reaches a rasterizing device and reads pixels back through
// RenderTarget2D::GetData -- VertexDeclarationLayoutTests.cpp's own oracle set.
/// plans/plan_runtimerenderer.md RTR-P9-7: the same four sets, asked of the ACTIVE renderer instead of
/// the build default, so a multi-renderer binary answers them per run rather than once at compile
/// time. Each predicate keeps the name and the meaning its macro had.
namespace CnaTest::WireFrameOracle
{
    // Lets CNA_RENDERER_IS name identities bare, exactly as the `defined(CNA_RENDERER_X)` guards it
    // replaced did. Scoped to this namespace: a using-directive at header scope would reach every
    // suite that includes the oracle.
    using namespace ::CNA::Testing::Renderers;   // NOLINT(google-build-using-namespace)

    /** @brief Whether the active renderer rasterizes and reads back, so pixels can be asserted. */
    [[nodiscard]] inline bool HasPixelOracle()
    {
        return CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, Software, Vulkan, Bgfx, WebGPU, SdlGpu,
                               DirectX9, DirectX11, DirectX12, OpenGL4, OpenGL1, OpenGL2,
                               Wicked, Magnum, Sokol, Diligent);
    }

    // The subset actually measured. D3D12 is excluded because no D3D12 runtime exists in this
    // environment: its device creation aborts under Wine for every device test in this file,
    // including the untouched `SupportsThreeD`, so calling it clean would be a fabrication. It
    // still compiles the oracle, and gains its reading the day a D3D12 runtime is available.
    /** @brief Whether the active renderer's wireframe behaviour has actually been measured. */
    [[nodiscard]] inline bool IsMeasured()
    {
        return HasPixelOracle() && !CNA_RENDERER_IS(DirectX12);
    }

    // Measured to render a genuine wireframe: edges lit, interior empty.
    //
    // plans/plan_webgpu.md WEBGPU-153: WebGPU used to be excluded here, on the grounds that it "has
    // no polygon-mode API at all and now refuses the request outright (WEBGPU-115)". The first half
    // was beside the point and the second is no longer true: a wireframe never needed a polygon
    // mode -- the reference renderer has always produced one by expanding triangle edges into a
    // line list -- and WebGPU now does exactly that, on every 3D route. It is measured by this
    // oracle like every other renderer.
    /** @brief Whether the active renderer draws a genuine wireframe: edges lit, interior empty. */
    [[nodiscard]] inline bool RendersEdges()
    {
        return IsMeasured();
    }

    // The renderers that answer a WireFrame request with a deterministic refusal instead of pixels.
    //
    // This set is EMPTY again, and that is a reading rather than an omission. REMED-GFX-209 found it
    // empty and recorded the absence rather than manufacturing a member; WEBGPU-115 filled it with
    // WebGPU; plans/plan_webgpu.md WEBGPU-153 implemented the edge expansion and emptied it once
    // more. The arm below is kept for the day a renderer legitimately needs it -- a capability
    // boundary that has no test until something fails is not a boundary -- and skips while no
    // renderer refuses.
    /** @brief Whether the active renderer refuses a WireFrame request deterministically. */
    [[nodiscard]] inline bool RejectsWireFrame()
    {
        return false;
    }

    /** @brief The active renderer's display name. */
    [[nodiscard]] inline std::string RendererName()
    {
        return std::string(CNA::getGraphicsRendererName(
            CNA::GraphicsRendererSelection::GetSelected()));
    }

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::BufferUsage;
    using Microsoft::Xna::Framework::Graphics::CullMode;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::DepthStencilState;
    using Microsoft::Xna::Framework::Graphics::FillMode;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::VertexBuffer;
    using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
    using Microsoft::Xna::Framework::Graphics::VertexElement;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

    inline constexpr int kSize = 256;

    /// THE GEOMETRY IS ASYMMETRIC ON PURPOSE. No two edges share a slope, no edge is axis-aligned,
    /// and the three edge probes below are mutually disjoint -- so a single missing edge moves a
    /// KNOWN probe to zero instead of merely changing a total. A symmetric triangle would let one
    /// dropped edge hide behind the other two.
    ///
    ///   A = (32,224) bottom-left   B = (224,192) bottom-right   C = (96,24) top, pulled left
    inline constexpr std::array<std::array<int, 2>, 3> kTriangle{
        std::array<int, 2>{32, 224}, std::array<int, 2>{224, 192}, std::array<int, 2>{96, 24}};

    /// Exact triangle area in pixels: |AB x AC| / 2 == 36352 / 2.
    inline constexpr int kSolidArea = 18176;

    struct Box
    {
        int x0, y0, x1, y1;
        [[nodiscard]] constexpr int Area() const { return (x1 - x0 + 1) * (y1 - y0 + 1); }
    };

    /// THE INTERIOR PROBE, centred on the centroid (117,147). Every edge is at least 40 px away,
    /// so a one-pixel-wide wireframe cannot reach it and a solid fill cannot miss it. This is the
    /// single measurement that separates a wireframe from a solid fill.
    inline constexpr Box kInterior{101, 131, 133, 163};
    inline constexpr int kInteriorArea = kInterior.Area();

    /// ONE PROBE PER EDGE, each a 25x25 box centred on that edge's midpoint. Verified disjoint,
    /// and verified to contain exactly one of the three edges: AB(128,208), BC(160,108),
    /// CA(64,124). None of them overlaps the interior probe.
    inline constexpr std::array<Box, 3> kEdgeProbes{
        Box{116, 196, 140, 220},
        Box{148, 96, 172, 120},
        Box{52, 112, 76, 136}};
    inline constexpr std::array<const char*, 3> kEdgeNames{"AB", "BC", "CA"};

    /// Non-neutral in every channel, so a colour read from the wrong place is measurable rather
    /// than merely dark.
    inline constexpr std::array<int, 4> kInk{255, 96, 32, 255};

    /// DELIBERATELY NOT BLACK. Against a black clear, "the draw was dropped" and "the geometry
    /// rendered with a lost colour attribute" are the same picture; against this they separate.
    inline constexpr std::array<int, 4> kClear{17, 34, 51, 255};

    /// One quantisation step of slack. Software's fixed-point interpolation lands one unit low on
    /// a flat-coloured triangle's interior; nothing here needs finer resolution than that.
    inline constexpr int kTolerance = 2;

    inline void NdcFromPixel(float px, float py, float& x, float& y)
    {
        x = (2.0f * px / static_cast<float>(kSize)) - 1.0f;
        y = 1.0f - (2.0f * py / static_cast<float>(kSize));
    }

    inline VertexDeclaration PositionColorDeclaration()
    {
        return VertexDeclaration(
            16,
            {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
             VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
    }

    /// The three triangle corners in the packed stream layout every route in this oracle uses.
    inline std::array<VertexPositionColor, 3> TriangleVertices()
    {
        std::array<VertexPositionColor, 3> verts{};
        for (std::size_t i = 0; i < verts.size(); ++i)
        {
            float x = 0.0f, y = 0.0f;
            NdcFromPixel(static_cast<float>(kTriangle[i][0]),
                         static_cast<float>(kTriangle[i][1]), x, y);
            verts[i] = VertexPositionColor(
                Vector3(x, y, 0.0f), Color(kInk[0], kInk[1], kInk[2], kInk[3]));
        }
        return verts;
    }

    struct Frame
    {
        std::vector<Color> pixels;

        [[nodiscard]] Color At(int x, int y) const
        {
            return pixels[static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)];
        }

        static bool IsClear(const Color& p)
        {
            return static_cast<int>(p.getRProperty()) == kClear[0] &&
                   static_cast<int>(p.getGProperty()) == kClear[1] &&
                   static_cast<int>(p.getBProperty()) == kClear[2];
        }

        [[nodiscard]] int LitTotal() const
        {
            int n = 0;
            for (const Color& p : pixels)
                if (!IsClear(p))
                    ++n;
            return n;
        }

        [[nodiscard]] int LitIn(const Box& b) const
        {
            int n = 0;
            for (int y = b.y0; y <= b.y1; ++y)
                for (int x = b.x0; x <= b.x1; ++x)
                    if (!IsClear(At(x, y)))
                        ++n;
            return n;
        }

        /// The first non-clear pixel in @p b, or the clear colour when there is none.
        [[nodiscard]] Color FirstLitIn(const Box& b) const
        {
            for (int y = b.y0; y <= b.y1; ++y)
                for (int x = b.x0; x <= b.x1; ++x)
                    if (!IsClear(At(x, y)))
                        return At(x, y);
            return Color(kClear[0], kClear[1], kClear[2], kClear[3]);
        }

        /// Every lit pixel anywhere in the frame carries the ink colour. A second, differently
        /// coloured draw -- or a retry that blended over the first -- cannot survive this.
        [[nodiscard]] bool EveryLitPixelIsInk() const
        {
            for (const Color& p : pixels)
                if (!IsClear(p) && !NearInk(p))
                    return false;
            return true;
        }

        static bool NearInk(const Color& p)
        {
            const auto close = [](int l, int r) { return (l > r ? l - r : r - l) <= kTolerance; };
            return close(static_cast<int>(p.getRProperty()), kInk[0]) &&
                   close(static_cast<int>(p.getGProperty()), kInk[1]) &&
                   close(static_cast<int>(p.getBProperty()), kInk[2]) &&
                   close(static_cast<int>(p.getAProperty()), kInk[3]);
        }

        [[nodiscard]] std::string Describe() const
        {
            std::ostringstream os;
            os << "total=" << LitTotal() << " interior=" << LitIn(kInterior) << '/'
               << kInteriorArea;
            for (std::size_t i = 0; i < kEdgeProbes.size(); ++i)
                os << ' ' << kEdgeNames[i] << '=' << LitIn(kEdgeProbes[i]);
            return os.str();
        }
    };

    inline std::string Describe(const Color& c)
    {
        std::ostringstream os;
        os << '(' << static_cast<int>(c.getRProperty()) << ','
           << static_cast<int>(c.getGProperty()) << ','
           << static_cast<int>(c.getBProperty()) << ','
           << static_cast<int>(c.getAProperty()) << ')';
        return os.str();
    }

    /// One rendered frame, or the renderer's own refusal.
    struct Result
    {
        Frame frame;
        bool rendered = false;
        std::string rejection;
    };

    /// A frame of nothing but the clear colour -- what a target must still hold after a refused
    /// draw, and the only picture that proves the refusal mutated nothing.
    inline Frame ClearFrame()
    {
        Frame f;
        f.pixels.assign(static_cast<std::size_t>(kSize) * kSize,
                        Color(kClear[0], kClear[1], kClear[2], kClear[3]));
        return f;
    }

    /// Installs the fixture's shared device state: CullMode::None so culling can never be the
    /// reason a pixel is missing, no depth test, opaque blend, full-target scissor.
    ///
    /// `CullMode::None` is load-bearing rather than decorative, and was verified by injection:
    /// setting `CullCounterClockwiseFace` here removes the SOLID draw entirely (total 0) while
    /// EasyGL's GL_LINES wireframe emulation survives untouched. The two modes would then no
    /// longer be comparable at all, and the difference between them would measure culling instead
    /// of fill mode.
    inline void ApplyFixtureState(GraphicsDevice& device, FillMode fill)
    {
        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        rs.setFillModeProperty(fill);
        device.setRasterizerStateProperty(rs);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
    }

    /// The fixture's stock effect: flat vertex colour, no lighting, no texture, no fog.
    inline void ApplyFixtureEffect(BasicEffect& effect)
    {
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setFogEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAlphaProperty(1.0f);
    }

    /// Reads the whole target back into @p out. The only readback any route in this oracle does.
    inline void ReadTarget(RenderTarget2D& target, Frame& out)
    {
        out.pixels.assign(static_cast<std::size_t>(kSize) * kSize, Color::Transparent);
        const Rectangle region(0, 0, kSize, kSize);
        target.GetData(0, &region, out.pixels.data(), 0, static_cast<int>(out.pixels.size()));
    }

    /// Renders the asymmetric triangle once, through the ordinary non-indexed `VertexBuffer`
    /// route. `Solid` and `WireFrame` differ in nothing else, which is what makes the two frames
    /// comparable.
    inline Result RenderTriangle(GraphicsDevice& device, FillMode fill)
    {
        Result out;
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        try
        {
            const std::array<VertexPositionColor, 3> verts = TriangleVertices();
            VertexBuffer vb(device, PositionColorDeclaration(),
                            static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetData(verts.data(), static_cast<int>(verts.size()));

            ApplyFixtureState(device, fill);

            BasicEffect effect(device);
            ApplyFixtureEffect(effect);

            device.SetRenderTarget(&target);
            device.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
            device.Clear(Color(kClear[0], kClear[1], kClear[2], kClear[3]));
            effect.Apply();
            device.SetVertexBuffer(&vb);
            // ONE public draw. No Present, no wait, no retry, no second frame -- the single
            // GetData below is the only readback, and it happens after the target is unbound.
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(nullptr);

            ReadTarget(target, out.frame);
            out.rendered = true;
        }
        catch (const std::exception& e)
        {
            out.rejection = e.what();
            device.SetRenderTarget(nullptr);
            // A refused draw still owes an answer about the target: "the draw was rejected" and
            // "the draw was rejected AND scribbled on the target anyway" are different outcomes,
            // and only reading the pixels back separates them. Wrapped so a readback that itself
            // fails cannot overwrite the rejection this run exists to report.
            try
            {
                ReadTarget(target, out.frame);
            }
            catch (const std::exception&)
            {
                out.frame = ClearFrame();
            }
        }
        return out;
    }

    /// Asserts @p frame holds nothing but the clear colour -- the picture a target must still show
    /// after a refused draw.
    inline void ExpectClearOnly(const Frame& frame, const char* what)
    {
        EXPECT_EQ(0, frame.LitTotal())
            << RendererName() << ' ' << what << " mutated the target -- " << frame.Describe();
        EXPECT_EQ(0, frame.LitIn(kInterior))
            << RendererName() << ' ' << what << " filled the triangle interior -- "
            << frame.Describe();
    }

    /// Every reading is PRINTED before it is judged, on every renderer including the unmeasured
    /// one -- a boundary that never states its measurement outlives the thing it describes.
    inline void PrintReading(const char* label, const Result& r)
    {
        std::cout << "[ GFX-209  ] " << RendererName() << ' ' << label << ": ";
        if (!r.rendered)
            std::cout << "REJECTED, target " << r.frame.Describe() << " -- \"" << r.rejection << '"'
                      << std::endl;
        else
            std::cout << r.frame.Describe() << std::endl;
    }

    /// The Solid control, shared by every arm. It is asserted as hard as the wireframe case: an
    /// oracle whose control is weak cannot tell "wireframe worked" from "nothing rendered".
    inline void ExpectSolidTriangle(const Result& solid)
    {
        ASSERT_TRUE(solid.rendered)
            << RendererName() << " refused an ordinary Solid draw: " << solid.rejection;
        EXPECT_EQ(kInteriorArea, solid.frame.LitIn(kInterior))
            << RendererName() << " Solid left part of the triangle interior unfilled -- "
            << solid.frame.Describe();
        EXPECT_TRUE(Frame::NearInk(solid.frame.FirstLitIn(kInterior)))
            << RendererName() << " Solid filled the interior with "
            << Describe(solid.frame.FirstLitIn(kInterior)) << ", not the ink colour";
        // The rasterized area is a fixed property of the geometry; a few pixels of slack absorbs
        // each renderer's own top-left/pixel-centre rule and nothing more.
        EXPECT_GE(solid.frame.LitTotal(), kSolidArea - 64)
            << RendererName() << " Solid covered less than the triangle -- "
            << solid.frame.Describe();
        EXPECT_LE(solid.frame.LitTotal(), kSolidArea + 64)
            << RendererName() << " Solid covered more than the triangle -- "
            << solid.frame.Describe();
        EXPECT_TRUE(solid.frame.EveryLitPixelIsInk())
            << RendererName() << " Solid produced a lit pixel that is neither ink nor clear";
    }
}   // namespace CnaTest::WireFrameOracle

