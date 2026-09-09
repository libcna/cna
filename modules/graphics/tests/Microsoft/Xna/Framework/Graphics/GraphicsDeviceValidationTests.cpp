// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare (Stub, OpenVg, ...), matching how the compile-time
// guards these replaced read.
using namespace CNA::Testing::Renderers;
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::TextureCollection;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::Viewport;

TEST(GraphicsDeviceLifecycleTest, RendererFacingOperationsRejectUseAfterDeviceDisposal)
{
    GraphicsDevice gd;
    gd.Dispose();

    EXPECT_THROW(gd.Clear(Color::Black), System::ObjectDisposedException);
    EXPECT_THROW(gd.Present(), System::ObjectDisposedException);
    EXPECT_THROW(gd.Reset(), System::ObjectDisposedException);
    EXPECT_THROW(gd.setBlendStateProperty(
                     Microsoft::Xna::Framework::Graphics::BlendState::Opaque),
                 System::ObjectDisposedException);
    EXPECT_THROW(gd.setDepthStencilStateProperty(
                     Microsoft::Xna::Framework::Graphics::DepthStencilState::None),
                 System::ObjectDisposedException);
    EXPECT_THROW(gd.setRasterizerStateProperty(
                     Microsoft::Xna::Framework::Graphics::RasterizerState::CullNone),
                 System::ObjectDisposedException);
    EXPECT_THROW(gd.setViewportProperty(Microsoft::Xna::Framework::Graphics::Viewport(0, 0, 1, 1)),
                 System::ObjectDisposedException);
    EXPECT_THROW(gd.setScissorRectangleProperty(Microsoft::Xna::Framework::Rectangle(0, 0, 1, 1)),
                 System::ObjectDisposedException);
    EXPECT_THROW(static_cast<void>(gd.getViewportProperty()), System::ObjectDisposedException);
    EXPECT_THROW(static_cast<void>(gd.getScissorRectangleProperty()),
                 System::ObjectDisposedException);
    EXPECT_THROW(static_cast<void>(gd.getDisplayModeProperty()),
                 System::ObjectDisposedException);
    EXPECT_THROW(static_cast<void>(gd.getGraphicsDeviceStatusProperty()),
                 System::ObjectDisposedException);
    EXPECT_THROW(gd.SetVertexBuffer(nullptr), System::ObjectDisposedException);
    EXPECT_THROW(gd.SetVertexBuffers({}), System::ObjectDisposedException);
    EXPECT_THROW(gd.SetIndexBuffer(nullptr), System::ObjectDisposedException);
    EXPECT_THROW(gd.SetRenderTargets({}), System::ObjectDisposedException);
    EXPECT_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1),
                 System::ObjectDisposedException);
    Color pixel = Color::Magenta;
    const Microsoft::Xna::Framework::Rectangle pixelRegion(0, 0, 1, 1);
    EXPECT_THROW(gd.GetBackBufferData(&pixelRegion, &pixel, 0, 1),
                 System::ObjectDisposedException);
    EXPECT_EQ(pixel, Color::Magenta);
}

TEST(GraphicsDeviceValidationTest, ViewportRejectsInvalidValuesWithoutChangingState)
{
    GraphicsDevice gd;
    const Viewport valid = gd.getViewportProperty();
    ASSERT_GT(valid.getWidthProperty(), 0);
    ASSERT_GT(valid.getHeightProperty(), 0);

    std::array<Viewport, 11> invalid{
        Viewport(-1, 0, 1, 1),
        Viewport(0, -1, 1, 1),
        Viewport(0, 0, 0, 1),
        Viewport(0, 0, 1, 0),
        Viewport(valid.getWidthProperty(), 0, 1, 1),
        Viewport(0, valid.getHeightProperty(), 1, 1),
        Viewport(0, 0, 1, 1),
        Viewport(0, 0, 1, 1),
        Viewport(0, 0, 1, 1),
        Viewport(0, 0, 1, 1),
        Viewport(0, 0, 1, 1)};
    invalid[6].setMinDepthProperty(-0.01f);
    invalid[7].setMinDepthProperty(1.01f);
    invalid[8].setMaxDepthProperty(1.01f);
    invalid[9].setMaxDepthProperty(-0.01f);
    invalid[10].setMinDepthProperty(0.75f);
    invalid[10].setMaxDepthProperty(0.25f);

    for (const Viewport& candidate : invalid)
    {
        EXPECT_THROW(gd.setViewportProperty(candidate), System::ArgumentException);
        EXPECT_EQ(gd.getViewportProperty().ToString(), valid.ToString());
    }
}

TEST(GraphicsDeviceValidationTest, ScissorRejectsInvalidValuesWithoutChangingState)
{
    GraphicsDevice gd;
    const Viewport viewport = gd.getViewportProperty();
    const Rectangle valid(1, 1, viewport.getWidthProperty() - 1,
                          viewport.getHeightProperty() - 1);
    gd.setScissorRectangleProperty(valid);

    const std::array<Rectangle, 6> invalid{
        Rectangle(-1, 0, 1, 1),
        Rectangle(0, -1, 1, 1),
        Rectangle(0, 0, -1, 1),
        Rectangle(0, 0, 1, -1),
        Rectangle(viewport.getWidthProperty(), 0, 1, 1),
        Rectangle(0, viewport.getHeightProperty(), 1, 1)};
    for (const Rectangle& candidate : invalid)
    {
        EXPECT_THROW(gd.setScissorRectangleProperty(candidate), System::ArgumentException);
        EXPECT_EQ(gd.getScissorRectangleProperty(), valid);
    }

    EXPECT_NO_THROW(gd.setScissorRectangleProperty(
        Rectangle(viewport.getWidthProperty(), viewport.getHeightProperty(), 0, 0)));
}

TEST(GraphicsDeviceValidationTest, ViewportAndScissorUseActiveRenderTargetBounds)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice gd;
    RenderTarget2D target(gd, 8, 6);
    gd.SetRenderTarget(&target);

    const Viewport validViewport(0, 0, 8, 6);
    const Rectangle validScissor(0, 0, 8, 6);
    gd.setViewportProperty(validViewport);
    gd.setScissorRectangleProperty(validScissor);

    EXPECT_THROW(gd.setViewportProperty(Viewport(0, 0, 9, 6)),
                 System::ArgumentException);
    EXPECT_THROW(gd.setScissorRectangleProperty(Rectangle(0, 0, 8, 7)),
                 System::ArgumentException);
    EXPECT_EQ(gd.getViewportProperty().ToString(), validViewport.ToString());
    EXPECT_EQ(gd.getScissorRectangleProperty(), validScissor);
}

TEST(GraphicsDeviceLifecycleTest, ResetUnbindsActiveRenderTargets)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice gd;
    RenderTarget2D target(gd, 8, 6);
    gd.SetRenderTarget(&target);
    ASSERT_EQ(gd.GetRenderTargets().size(), 1u);

    gd.Reset();

    EXPECT_TRUE(gd.GetRenderTargets().empty());
    EXPECT_EQ(gd.getViewportProperty().getWidthProperty(),
              gd.getPresentationParametersProperty().getBackBufferWidthProperty());
    EXPECT_EQ(gd.getViewportProperty().getHeightProperty(),
              gd.getPresentationParametersProperty().getBackBufferHeightProperty());
    EXPECT_NO_THROW(gd.Present());
}

TEST(GraphicsDeviceLifecycleTest, DrawRejectsVertexBufferDisposedAfterBinding)
{
    GraphicsDevice gd;
    VertexBuffer vertices(
        gd, VertexPositionColor::getVertexDeclarationStatic(), 3,
        Microsoft::Xna::Framework::Graphics::BufferUsage::None);
    const std::array<VertexPositionColor, 3> triangle{
        VertexPositionColor({-0.5f, -0.5f, 0.0f}, Color::Red),
        VertexPositionColor({0.0f, 0.5f, 0.0f}, Color::Green),
        VertexPositionColor({0.5f, -0.5f, 0.0f}, Color::Blue)};
    vertices.SetData(triangle.data(), static_cast<int>(triangle.size()));
    gd.SetVertexBuffer(&vertices);

    Microsoft::Xna::Framework::Graphics::BasicEffect effect(gd);
    effect.setVertexColorEnabledProperty(true);
    effect.Apply();
    vertices.Dispose();

    EXPECT_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1),
                 System::ObjectDisposedException);
}

TEST(GraphicsDeviceLifecycleTest, IndexedDrawRejectsIndexBufferDisposedAfterBinding)
{
    GraphicsDevice gd;
    VertexBuffer vertices(
        gd, VertexPositionColor::getVertexDeclarationStatic(), 3,
        Microsoft::Xna::Framework::Graphics::BufferUsage::None);
    const std::array<VertexPositionColor, 3> triangle{
        VertexPositionColor({-0.5f, -0.5f, 0.0f}, Color::Red),
        VertexPositionColor({0.0f, 0.5f, 0.0f}, Color::Green),
        VertexPositionColor({0.5f, -0.5f, 0.0f}, Color::Blue)};
    vertices.SetData(triangle.data(), static_cast<int>(triangle.size()));
    IndexBuffer indices(gd, IndexElementSize::SixteenBits, 3,
                        Microsoft::Xna::Framework::Graphics::BufferUsage::None);
    const std::array<std::uint16_t, 3> values{0, 1, 2};
    indices.SetData(values.data(), static_cast<int>(values.size()));
    gd.SetVertexBuffer(&vertices);
    gd.SetIndexBuffer(&indices);

    Microsoft::Xna::Framework::Graphics::BasicEffect effect(gd);
    effect.setVertexColorEnabledProperty(true);
    effect.Apply();
    indices.Dispose();

    EXPECT_THROW(gd.DrawIndexedPrimitives(
                     PrimitiveType::TriangleList, 0, 0, 3, 0, 1),
                 System::ObjectDisposedException);
}

// =============================================================================
// TextureCollection — index bounds
// =============================================================================

TEST(TextureCollectionValidationTest, SetNullTexture_DoesNotThrow)
{
    TextureCollection col;
    EXPECT_NO_THROW(col(0, nullptr));
}

TEST(TextureCollectionValidationTest, GetNullSlot_ReturnsNull)
{
    TextureCollection col;
    EXPECT_EQ(col[0], nullptr);
}

TEST(TextureCollectionValidationTest, NegativeIndex_ThrowsOutOfRange)
{
    TextureCollection col;
    EXPECT_THROW(col(-1, nullptr), std::out_of_range);
}

TEST(TextureCollectionValidationTest, IndexAtMax_ThrowsOutOfRange)
{
    TextureCollection col;
    EXPECT_THROW(col(TextureCollection::MaxTextures, nullptr), std::out_of_range);
}

TEST(TextureCollectionValidationTest, IndexAtLastSlot_DoesNotThrow)
{
    TextureCollection col;
    EXPECT_NO_THROW(col(TextureCollection::MaxTextures - 1, nullptr));
}

// =============================================================================
// TextureCollection — disposed texture rejected
// =============================================================================

TEST(TextureCollectionValidationTest, DisposedTexture_ThrowsObjectDisposedException)
{
    TextureCollection col;
    Texture2D tex; // default constructor — no device, no GPU resource
    tex.Dispose();
    EXPECT_THROW(col(0, &tex), System::ObjectDisposedException);
}

TEST(TextureCollectionValidationTest, DisposedTexture_CatchableAsInvalidOperationException)
{
    TextureCollection col;
    Texture2D tex;
    tex.Dispose();
    EXPECT_THROW(col(0, &tex), System::InvalidOperationException);
}

TEST(TextureCollectionValidationTest, LiveTexture_DoesNotThrowForDisposedCheck)
{
    TextureCollection col;
    Texture2D tex; // not disposed
    EXPECT_NO_THROW(col(0, &tex));
}

// =============================================================================
// TextureCollection — active render targets cannot simultaneously be sampled
// =============================================================================

namespace
{
    // Renderer-neutral capability gate (not an OPENVG-specific skip): a renderer with no real
    // RenderTarget2D storage throws System::NotSupportedException out of SetRenderTarget itself
    // (GraphicsDevice::SetRenderTarget's own transactional check -- see its own comment) before
    // any of these tests' actual subject (texture-slot binding conflict validation) is reached.
    // Tied to the real runtime behavior rather than a renderer-name list, so it stays correct for
    // any current or future renderer that genuinely lacks RenderTarget2D.
    bool BindOrSkip(GraphicsDevice& gd, RenderTarget2D& target)
    {
        try
        {
            gd.SetRenderTarget(&target);
            return true;
        }
        catch (const System::NotSupportedException&)
        {
            return false;
        }
    }
}

TEST(GraphicsDeviceLifecycleTest, DisposalIsReentrantAndReleasesABoundRenderTarget)
{
    GraphicsDevice gd;
    RenderTarget2D target(gd, 4, 4);
    if (!BindOrSkip(gd, target))
        GTEST_SKIP() << "this renderer does not support RenderTarget2D";

    int disposingEvents = 0;
    gd.Disposing += [&](System::Object*, const System::EventArgs&)
    {
        ++disposingEvents;
        EXPECT_TRUE(gd.getIsDisposedProperty());
        EXPECT_NO_THROW(gd.Dispose());
        EXPECT_THROW(gd.Clear(Color::Black), System::ObjectDisposedException);
    };

    EXPECT_NO_THROW(gd.Dispose());
    EXPECT_EQ(disposingEvents, 1);
    EXPECT_TRUE(target.getIsDisposedProperty());
}

TEST(TextureCollectionValidationTest, ActiveRenderTargetCannotBindToPixelTextureSlot)
{
    GraphicsDevice gd;
    RenderTarget2D target(gd, 4, 4);
    if (!BindOrSkip(gd, target))
    {
        GTEST_SKIP() << "this renderer does not support RenderTarget2D";
    }

    EXPECT_THROW(
        gd.getTexturesProperty()(0, &target),
        System::InvalidOperationException);
    EXPECT_EQ(gd.getTexturesProperty()[0], nullptr);
}

TEST(TextureCollectionValidationTest, ActiveRenderTargetCannotBindToVertexTextureSlot)
{
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTarget2D target(gd, 4, 4);
    if (!BindOrSkip(gd, target))
    {
        GTEST_SKIP() << "this renderer does not support RenderTarget2D";
    }

    EXPECT_THROW(
        gd.getVertexTexturesProperty()(0, &target),
        System::InvalidOperationException);
    EXPECT_EQ(gd.getVertexTexturesProperty()[0], nullptr);
}

TEST(TextureCollectionValidationTest, RenderTargetCanBindForSamplingAfterUnbind)
{
    GraphicsDevice gd;
    RenderTarget2D target(gd, 4, 4);
    if (!BindOrSkip(gd, target))
    {
        GTEST_SKIP() << "this renderer does not support RenderTarget2D";
    }
    gd.SetRenderTarget(nullptr);

    EXPECT_NO_THROW(gd.getTexturesProperty()(0, &target));
    EXPECT_EQ(gd.getTexturesProperty()[0], static_cast<Texture2D*>(&target));
}

TEST(TextureCollectionValidationTest, OwnedCollectionsRespectProfileSlotCounts)
{
    GraphicsDevice gd;

    EXPECT_NO_THROW(gd.getTexturesProperty()(15, nullptr));
    EXPECT_THROW(gd.getTexturesProperty()(16, nullptr), std::out_of_range);
    EXPECT_THROW((void)gd.getVertexTexturesProperty()[0], std::out_of_range);
    EXPECT_THROW(gd.getVertexTexturesProperty()(0, nullptr), std::out_of_range);
    EXPECT_THROW((void)gd.getVertexSamplerStatesProperty()[0], std::out_of_range);

    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    EXPECT_NO_THROW(gd.getVertexTexturesProperty()(3, nullptr));
    EXPECT_THROW(gd.getVertexTexturesProperty()(4, nullptr), std::out_of_range);
    EXPECT_NO_THROW((void)gd.getVertexSamplerStatesProperty()[3]);
    EXPECT_THROW((void)gd.getVertexSamplerStatesProperty()[4], std::out_of_range);
}

TEST(TextureCollectionValidationTest, VertexTextureFormatAndValidationOrderMatchXna)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    Texture2D color(gd, 1, 1, false, SurfaceFormat::Color);
    Texture2D single(gd, 1, 1, false, SurfaceFormat::Single);

    EXPECT_THROW(
        gd.getVertexTexturesProperty()(0, &color),
        System::NotSupportedException);
    EXPECT_NO_THROW(gd.getVertexTexturesProperty()(0, &single));
    EXPECT_EQ(gd.getVertexTexturesProperty()[0], &single);

    color.Dispose();
    EXPECT_THROW(
        gd.getTexturesProperty()(TextureCollection::MaxTextures, &color),
        System::ObjectDisposedException);
}

TEST(TextureCollectionValidationTest, OwnedCollectionRejectsUseAfterDeviceDisposal)
{
    GraphicsDevice gd;
    TextureCollection& textures = gd.getTexturesProperty();
    gd.Dispose();

    EXPECT_THROW((void)textures[0], System::ObjectDisposedException);
    EXPECT_THROW(textures(0, nullptr), System::ObjectDisposedException);
}

// =============================================================================
// GraphicsDevice.SetRenderTargets — MAX_RENDERTARGET_BINDINGS=4 cap (Task 881)
//
// Matches FNA's real behavior: GraphicsDevice.MAX_RENDERTARGET_BINDINGS=4, and
// SetRenderTargets's Array.Copy into the fixed-size renderTargetBindings array throws when
// given more than 4 targets.
//
// Real RenderTarget2D instances are used throughout so the cap tests reach only the fixed-size
// binding limit. The explicit RenderTargetBinding constructors reject null, while a default
// binding remains representable and is covered separately by the deterministic null-binding test.
// =============================================================================

TEST(GraphicsDeviceValidationTest, SetRenderTargets_FiveTargets_Throws)
{
    GraphicsDevice gd;
    std::vector<std::unique_ptr<RenderTarget2D>> targets;
    std::vector<RenderTargetBinding> bindings;
    for (int i = 0; i < 5; ++i)
    {
        targets.push_back(std::make_unique<RenderTarget2D>(gd, 4, 4));
        bindings.emplace_back(targets.back().get());
    }
    EXPECT_THROW(gd.SetRenderTargets(bindings), std::invalid_argument);
}

TEST(GraphicsDeviceValidationTest, SetRenderTargets_FourTargets_DoesNotThrow)
{
    GraphicsDevice gd;
    // MRT is a HiDef-only XNA feature. The renderer-specific branches below test the native
    // capability boundary, so do not let the default Reach profile reject the bind first.
    gd.SetGraphicsProfileEXT(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
    std::vector<std::unique_ptr<RenderTarget2D>> targets;
    std::vector<RenderTargetBinding> bindings;
    for (int i = 0; i < 4; ++i)
    {
        targets.push_back(std::make_unique<RenderTarget2D>(gd, 4, 4));
        bindings.emplace_back(targets.back().get());
    }
    if (CNA_RENDERER_IS(SdlRenderer, FreeDirect, DirectX1, DirectX2, DirectX3, DirectX5,
                        DirectX6, DirectX7, DirectX8, Gdi))
    {
    // Task 709 (the SDL_RENDERER family) / DX3-27 (DirectDraw, plans/plan_freedirect.md) / DX1-27 (real DirectDraw v1,
    // plans/plan_dx1.md) / DX2-84 (same DirectDraw v1 2D layer, plans/plan_dx2.md) / plans/plan_dx3.md (same 2D
    // layer, now DirectDraw v2) / plans/plan_dx5.md (same 2D layer, now DirectDraw v4): each supports
    // exactly one active render target at a time -- unlike the other, real-MRT-capable renderers,
    // binding more than one target here must throw clearly rather than silently rendering to only
    // the first. 4 is still within the MAX_RENDERTARGET_BINDINGS cap
    // this test's name/history (Task 881) refers to, so the throw here comes entirely from the
    // renderer's own single-target limitation, not the cap check.
    // SOFTWARE-120 removed Software from this refusal set: it now owns a four-slot CPU binding and
    // finalization path, so the generic positive arm below applies.
    // Sokol left this list at plans/plan_sokol.md SOKOL-26: it is now real-MRT-capable too (a genuine
    // multi-attachment sg_pass, 2-4 RenderTarget2D targets), so 4 real targets bind cleanly here
    // exactly like EasyGL/Vulkan/D3D11/etc. do below.
    // Diligent left this list at plans/plan_diligent.md DILIGENT-24: it is now real-MRT-capable
    // too (up to four attachments), so 4 real targets bind cleanly here as well.
    EXPECT_THROW(gd.SetRenderTargets(bindings), std::runtime_error);
    }
    else if (CNA_RENDERER_IS(Stub, OpenVg, NanoVg))
    {
    // plans/plan_stub.md: Stub supports no render targets AT ALL -- it keeps IGraphicsRenderer's nullptr
    // CreateRenderTarget2D()/CreateRenderTargetCube() defaults -- so this is a different case from
    // the single-target renderers above, which support one. GraphicsDevice rejects the bind before
    // reaching the renderer, because RenderTarget2D::GetRenderTargetRenderer() is null. Rejecting is
    // the correct behaviour and the reason it is asserted here: a no-op renderer must not report
    // false success for a target it cannot honour.
    //
    // OPENVG shares this exact shape for a different reason: ShivaVG has no off-screen
    // VGImage-surface/FBO equivalent to bind as a render target (docs/openvg-renderer.md), so
    // OpenVgRenderer also keeps the nullptr CreateRenderTarget2D() default.
    //
    // NANOVG shares it too: NanoVG's own off-screen-framebuffer helper (nanovg_gl_utils.h's
    // NVGLUframebuffer) was deliberately left out of this renderer's scope (plans/plan_nanovg.md), so
    // NanoVgRenderer also keeps the nullptr CreateRenderTarget2D() default.
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
    }
    else if (CNA_RENDERER_IS(PortableGL))
    {
    // PortableGL owns exactly one framebuffer per context and creates no render targets at all --
    // the same shape as Stub above: GraphicsDevice rejects the bind before reaching the renderer
    // because RenderTarget2D::GetRenderTargetRenderer() is null, and PortableGLRenderer::
    // SetRenderTargets() refuses a non-empty set as well, so neither layer can accept one silently.
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
    }
    else if (CNA_RENDERER_IS(TinyGL))
    {
        // From `next`: TinyGL keeps IGraphicsRenderer's nullptr CreateRenderTarget2D()/
        // CreateRenderTargetCube() defaults -- it renders into exactly one ZBuffer and has no
        // off-screen framebuffer concept -- so GraphicsDevice rejects the bind before reaching the
        // renderer, and TinyGLRenderer::SetRenderTargets() refuses a non-empty set as well
        // (modules/renderers/tinygl/examples/tinygl_rejection_test.cpp).
        EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
    }
    else if (CNA_RENDERER_IS(OpenGLES1))
    {
    // plans/plan_opengles1.md: OpenGL ES 1.1 has no MRT mechanism, and no extension in the CM registry
    // adds one -- a third distinct case from the single-target renderers above (which support one)
    // and from Stub (which supports none). A single RenderTarget2D binds normally via
    // GL_OES_framebuffer_object; more than one is refused rather than binding the first and
    // silently dropping the rest, which is also why SupportsCapability(MultipleRenderTargets)
    // reports false.
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
    }
    else if (CNA_RENDERER_IS(OpenGL1))
    {
    // plans/plan_opengl1.md: the same single-colour-attachment refusal shape as OPENGLES1 above, for the
    // desktop fixed-function pipeline -- a single RenderTarget2D binds normally via the
    // ARB_framebuffer_object/core FBO path; more than one is refused rather than binding the
    // first and silently dropping the rest, which is also why
    // SupportsCapability(MultipleRenderTargets) reports false.
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
    }
    else if (CNA_RENDERER_IS(OpenGLES2))
    {
    // docs/opengles2-renderer.md: single-colour-attachment refusal, like OPENGLES1/OPENGL1 above
    // -- core OpenGL ES 2.0 has no glDrawBuffers, a single RenderTarget2D binds normally through
    // the family's FBO path, and SupportsCapability(MultipleRenderTargets) reports false. The
    // exception TYPE deliberately stays the EasyGL family's own established over-the-ceiling
    // std::runtime_error (the profile pins the ceiling to 1), which the family's
    // lifecycle/diagnostic tests already catch as the recorded MRT boundary -- not
    // OPENGLES1/OPENGL1's System::NotSupportedException.
    EXPECT_THROW(gd.SetRenderTargets(bindings), std::runtime_error);
    }
    else
    {
        EXPECT_NO_THROW(gd.SetRenderTargets(bindings));
    }
}

TEST(GraphicsDeviceValidationTest, SetRenderTargets_OneTarget_DoesNotThrow)
{
    GraphicsDevice gd;
    RenderTarget2D rt(gd, 4, 4);
    std::vector<RenderTargetBinding> bindings{ RenderTargetBinding(&rt) };
    if (CNA_RENDERER_IS(Stub, OpenVg, TinyGL, NanoVg))
    {
    // Same Stub/OpenVG contract as the four-target case above: no render-target support of any
    // kind, so even a single binding is refused deterministically rather than silently accepted.
    // TinyGL joins them -- it renders into exactly one ZBuffer and creates no render target at all.
    // NanoVG joins them for the same reason as the four-target case above (plans/plan_nanovg.md).
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
    }
    else
    {
    EXPECT_NO_THROW(gd.SetRenderTargets(bindings));
    }
}

TEST(GraphicsDeviceValidationTest, SetRenderTarget_SingleOverload_MatchesArrayOverloadRejection)
{
    // REMED-GFX-PGL-AUDIT: SetRenderTarget(RenderTarget2D*) used to call renderer_->SetRenderTarget2D()
    // directly with `renderTarget ? renderTarget->GetRenderTargetRenderer() : nullptr`. On a renderer
    // that keeps IGraphicsRenderer's nullptr CreateRenderTarget2D() default (Stub, PortableGL,
    // OpenGLES1's/OpenGL1's zero-target case does not apply here since they support one),
    // GetRenderTargetRenderer() is itself null, so that ternary collapsed to the exact nullptr the
    // "unbind" call passes -- the renderer accepted it as an ordinary restore-backbuffer request
    // instead of refusing an unsupported binding, while this method still recorded the target as
    // bound and reset Viewport/ScissorRectangle to its size. Every draw after that silently landed
    // in the real backbuffer, not the (never actually created) render target, with no error raised
    // anywhere -- exactly the "no meaningful public renderer operation is silently ignored" contract
    // this pins for both public entry points.
    GraphicsDevice gd;
    RenderTarget2D target(gd, 4, 4);
    if (CNA_RENDERER_IS(Stub, PortableGL, TinyGL, NanoVg))
    {
    EXPECT_THROW(gd.SetRenderTarget(&target), System::NotSupportedException);
    // No partial state: GraphicsDevice must not report the rejected target as bound...
    EXPECT_TRUE(gd.GetRenderTargets().empty());
    // ...and the renderer must be left exactly as before the rejected call -- an ordinary backbuffer
    // draw still works immediately afterward.
    EXPECT_NO_THROW(gd.Clear(Color(0, 0, 0, 255)));
    }
    else
    {
    EXPECT_NO_THROW(gd.SetRenderTarget(&target));
    EXPECT_EQ(gd.GetRenderTargets().size(), 1u);
    gd.SetRenderTarget(nullptr);
    }
}

TEST(GraphicsDeviceValidationTest, SetRenderTargets_Empty_DoesNotThrow)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW(gd.SetRenderTargets({}));
}

TEST(GraphicsDeviceValidationTest, IdenticalRenderTargetBindingsAreNoOps)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice gd;
    RenderTarget2D target(gd, 8, 8);
    gd.SetRenderTarget(&target);

    const Viewport targetViewport(1, 2, 3, 4);
    const Rectangle targetScissor(2, 1, 4, 5);
    gd.setViewportProperty(targetViewport);
    gd.setScissorRectangleProperty(targetScissor);
    gd.SetRenderTarget(&target);
    EXPECT_EQ(gd.getViewportProperty().getXProperty(), 1);
    EXPECT_EQ(gd.getViewportProperty().getYProperty(), 2);
    EXPECT_EQ(gd.getViewportProperty().getWidthProperty(), 3);
    EXPECT_EQ(gd.getViewportProperty().getHeightProperty(), 4);
    EXPECT_EQ(gd.getScissorRectangleProperty(), targetScissor);

    gd.SetRenderTarget(nullptr);
    const Viewport backBufferViewport(2, 3, 4, 5);
    const Rectangle backBufferScissor(3, 2, 5, 4);
    gd.setViewportProperty(backBufferViewport);
    gd.setScissorRectangleProperty(backBufferScissor);
    gd.SetRenderTargets({});
    EXPECT_EQ(gd.getViewportProperty().getXProperty(), 2);
    EXPECT_EQ(gd.getViewportProperty().getYProperty(), 3);
    EXPECT_EQ(gd.getViewportProperty().getWidthProperty(), 4);
    EXPECT_EQ(gd.getViewportProperty().getHeightProperty(), 5);
    EXPECT_EQ(gd.getScissorRectangleProperty(), backBufferScissor);
}

TEST(GraphicsDeviceValidationTest, CubeFaceParticipatesInRenderTargetBindingIdentity)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice gd;
    RenderTargetCube target(
        gd, 8, false, Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color,
        Microsoft::Xna::Framework::Graphics::DepthFormat::None, 0,
        Microsoft::Xna::Framework::Graphics::RenderTargetUsage::PreserveContents);
    gd.SetRenderTarget(&target, CubeMapFace::PositiveX);

    const Viewport custom(1, 2, 3, 4);
    gd.setViewportProperty(custom);
    gd.SetRenderTarget(&target, CubeMapFace::PositiveX);
    EXPECT_EQ(gd.getViewportProperty().getXProperty(), 1);
    EXPECT_EQ(gd.getViewportProperty().getYProperty(), 2);
    EXPECT_EQ(gd.getViewportProperty().getWidthProperty(), 3);
    EXPECT_EQ(gd.getViewportProperty().getHeightProperty(), 4);

    gd.SetRenderTarget(&target, CubeMapFace::NegativeX);
    EXPECT_EQ(gd.getViewportProperty().getXProperty(), 0);
    EXPECT_EQ(gd.getViewportProperty().getYProperty(), 0);
    EXPECT_EQ(gd.getViewportProperty().getWidthProperty(), 8);
    EXPECT_EQ(gd.getViewportProperty().getHeightProperty(), 8);
    ASSERT_EQ(gd.GetRenderTargets().size(), 1u);
    EXPECT_EQ(gd.GetRenderTargets()[0].getCubeMapFaceProperty(), CubeMapFace::NegativeX);
    gd.SetRenderTargets({});
}

TEST(GraphicsDeviceValidationTest, SetRenderTargets_DefaultNullBindingThrows)
{
    GraphicsDevice gd;
    EXPECT_THROW(
        gd.SetRenderTargets({RenderTargetBinding()}),
        std::invalid_argument);
}

// =============================================================================
// GraphicsDevice.SetVertexBuffers — public binding validation and empty unbind
// =============================================================================

TEST(GraphicsDeviceValidationTest, SetVertexBuffers_DefaultNullBindingThrowsAndClearsState)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline";

    VertexBuffer live(gd, 3);
    gd.SetVertexBuffer(&live);
    EXPECT_THROW(gd.SetVertexBuffers({VertexBufferBinding()}), System::ArgumentException);
    EXPECT_TRUE(gd.GetVertexBuffers().empty());
    EXPECT_EQ(gd.GetVertexBuffer(), nullptr);
}

TEST(GraphicsDeviceValidationTest, SetVertexBuffers_SeventeenBindingsThrowNotSupported)
{
    GraphicsDevice gd;
    EXPECT_THROW(
        gd.SetVertexBuffers(std::vector<VertexBufferBinding>(17)),
        System::NotSupportedException);
}

TEST(GraphicsDeviceValidationTest, SetVertexBuffers_FailureKeepsOnlyProcessedPrefix)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline";

    VertexBuffer oldBuffer(gd, 3);
    VertexBuffer newBuffer(gd, 3);
    VertexBuffer disposed(gd, 3);
    disposed.Dispose();
    gd.SetVertexBuffer(&oldBuffer);

    EXPECT_THROW(
        gd.SetVertexBuffers({VertexBufferBinding(&newBuffer, 0, 0),
                             VertexBufferBinding(&disposed, 0, 0)}),
        System::ObjectDisposedException);
    ASSERT_EQ(gd.GetVertexBuffers().size(), 1u);
    EXPECT_EQ(gd.GetVertexBuffers()[0].getVertexBufferProperty(), &newBuffer);
    EXPECT_EQ(gd.GetVertexBuffer(), &newBuffer);

    EXPECT_NO_THROW(gd.SetVertexBuffer(nullptr, -1));
    EXPECT_TRUE(gd.GetVertexBuffers().empty());
    EXPECT_EQ(gd.GetVertexBuffer(), nullptr);
}

TEST(GraphicsDeviceValidationTest, SetVertexBuffers_EmptyClearsSingularBinding)
{
    GraphicsDevice gd;
    // VertexBuffer/DrawPrimitives are inherently 3D concepts -- a permanently 2D-only renderer
    // (OpenVG, and likewise Canvas/native-2D/ASCII/GDI/DirectX1/etc. if this test is ever run
    // against them) has no real vertex-buffer factory to construct one at all, so there is no
    // "empty vertex-buffer state DrawPrimitives should reject" to observe. Found running this file
    // for the first time against a native, CI-runnable 2D-only renderer (OPENVG) -- this test was
    // previously ungated and unconditionally required CreateVertexBuffer() to succeed.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    VertexBuffer vertexBuffer(gd, 3);
    gd.SetVertexBuffer(&vertexBuffer);
    gd.SetVertexBuffers({});

    try
    {
        gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        FAIL() << "DrawPrimitives unexpectedly accepted an empty vertex-buffer state";
    }
    catch (const std::runtime_error& ex)
    {
        EXPECT_NE(
            std::string(ex.what()).find("no vertex buffer"),
            std::string::npos);
    }
}

TEST(GraphicsDeviceValidationTest, ForeignBuffersAreRejectedTransactionally)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice receiving;
    GraphicsDevice owner;
    receiving.SetGraphicsProfileEXT(
        Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
    owner.SetGraphicsProfileEXT(
        Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);

    VertexBuffer localVertex(
        receiving, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
    VertexBuffer foreignVertex(
        owner, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
    receiving.SetVertexBuffer(&localVertex);
    EXPECT_THROW(receiving.SetVertexBuffer(&foreignVertex), System::InvalidOperationException);
    EXPECT_EQ(receiving.GetVertexBuffer(), &localVertex);
    EXPECT_THROW(
        receiving.SetVertexBuffers({VertexBufferBinding(&localVertex),
                                    VertexBufferBinding(&foreignVertex)}),
        System::InvalidOperationException);
    ASSERT_FALSE(receiving.GetVertexBuffers().empty());
    EXPECT_EQ(receiving.GetVertexBuffers().size(), 1u);
    EXPECT_EQ(receiving.GetVertexBuffers()[0].getVertexBufferProperty(), &localVertex);

    IndexBuffer localIndex(receiving, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    IndexBuffer foreignIndex(owner, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    receiving.SetIndexBuffer(&localIndex);
    EXPECT_THROW(receiving.SetIndexBuffer(&foreignIndex), System::InvalidOperationException);
    EXPECT_EQ(receiving.GetIndexBuffer(), &localIndex);
}

TEST(GraphicsDeviceValidationTest, ForeignTexturesAreRejectedTransactionally)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice receiving;
    GraphicsDevice owner;
    receiving.SetGraphicsProfileEXT(
        Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
    owner.SetGraphicsProfileEXT(
        Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
    Texture2D local(receiving, 1, 1, false, SurfaceFormat::Single);
    Texture2D foreign(owner, 1, 1, false, SurfaceFormat::Single);

    receiving.getTexturesProperty()(0, &local);
    EXPECT_THROW(
        receiving.getTexturesProperty()(0, &foreign),
        System::InvalidOperationException);
    EXPECT_EQ(receiving.getTexturesProperty()[0], &local);

    receiving.getVertexTexturesProperty()(0, &local);
    EXPECT_THROW(
        receiving.getVertexTexturesProperty()(0, &foreign),
        System::InvalidOperationException);
    EXPECT_EQ(receiving.getVertexTexturesProperty()[0], &local);
}

// Regression test for a real reported crash (cna-template/missing.md): the single-argument
// Clear(const Color&) overload matches FNA's own semantics by requesting
// Target|DepthBuffer|Stencil together, which used to forward unconditionally to
// ClearColorDepthAndStencil() — a hard throw on the native 2D renderer, since it is entirely
// 2D-only and never has a depth/stencil buffer at all. GraphicsDevice::Clear(ClearOptions, ...)
// now masks DepthBuffer/Stencil out of the request when IGraphicsRenderer::SupportsDepthStencil()
// reports false, degrading to a color-only clear instead of crashing (matching FNA's own
// dsFormat == DepthFormat.None masking behavior in GraphicsDevice.Clear(ClearOptions, ...)).
TEST(GraphicsDeviceValidationTest, Clear_SingleArgumentColorOverload_DoesNotThrow)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW(gd.Clear(Color::CornflowerBlue));
}
