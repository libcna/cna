// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
using Microsoft::Xna::Framework::Graphics::TextureCollection;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;

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
    std::vector<std::unique_ptr<RenderTarget2D>> targets;
    std::vector<RenderTargetBinding> bindings;
    for (int i = 0; i < 4; ++i)
    {
        targets.push_back(std::make_unique<RenderTarget2D>(gd, 4, 4));
        bindings.emplace_back(targets.back().get());
    }
#if defined(CNA_RENDERER_SDL_RENDERER) || defined(CNA_RENDERER_FREEDIRECT) || defined(CNA_RENDERER_DIRECTX1) || defined(CNA_RENDERER_DIRECTX2) || defined(CNA_RENDERER_DIRECTX3) || defined(CNA_RENDERER_DIRECTX5) || defined(CNA_RENDERER_DIRECTX6) || defined(CNA_RENDERER_DIRECTX7) || defined(CNA_RENDERER_DIRECTX8) || defined(CNA_RENDERER_GDI)
    // Task 709 (native 2D renderer) / DX3-27 (DirectDraw, plan_freedirect.md) / DX1-27 (real DirectDraw v1,
    // plan_dx1.md) / DX2-84 (same DirectDraw v1 2D layer, plan_dx2.md) / plan_dx3.md (same 2D
    // layer, now DirectDraw v2) / plan_dx5.md (same 2D layer, now DirectDraw v4): each supports
    // exactly one active render target at a time -- unlike the other, real-MRT-capable renderers,
    // binding more than one target here must throw clearly rather than silently rendering to only
    // the first. 4 is still within the MAX_RENDERTARGET_BINDINGS cap
    // this test's name/history (Task 881) refers to, so the throw here comes entirely from the
    // renderer's own single-target limitation, not the cap check.
    // Sokol left this list at plan_sokol.md SOKOL-26: it is now real-MRT-capable too (a genuine
    // multi-attachment sg_pass, 2-4 RenderTarget2D targets), so 4 real targets bind cleanly here
    // exactly like EasyGL/Vulkan/D3D11/etc. do below.
    // Diligent left this list at plan_diligent.md DILIGENT-24: it is now real-MRT-capable
    // too (up to four attachments), so 4 real targets bind cleanly here as well.
    EXPECT_THROW(gd.SetRenderTargets(bindings), std::runtime_error);
#elif defined(CNA_RENDERER_STUB) || defined(CNA_RENDERER_OPENVG)
    // plan_stub.md: Stub supports no render targets AT ALL -- it keeps IGraphicsRenderer's nullptr
    // CreateRenderTarget2D()/CreateRenderTargetCube() defaults -- so this is a different case from
    // the single-target renderers above, which support one. GraphicsDevice rejects the bind before
    // reaching the renderer, because RenderTarget2D::GetRenderTargetRenderer() is null. Rejecting is
    // the correct behaviour and the reason it is asserted here: a no-op renderer must not report
    // false success for a target it cannot honour.
    //
    // OPENVG shares this exact shape for a different reason: ShivaVG has no off-screen
    // VGImage-surface/FBO equivalent to bind as a render target (docs/openvg-renderer.md), so
    // OpenVgRenderer also keeps the nullptr CreateRenderTarget2D() default.
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
#elif defined(CNA_RENDERER_PORTABLEGL)
    // PortableGL owns exactly one framebuffer per context and creates no render targets at all --
    // the same shape as Stub above: GraphicsDevice rejects the bind before reaching the renderer
    // because RenderTarget2D::GetRenderTargetRenderer() is null, and PortableGLRenderer::
    // SetRenderTargets() refuses a non-empty set as well, so neither layer can accept one silently.
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
#elif defined(CNA_RENDERER_TINYGL)
    // TinyGL has the same shape for its own reason: it keeps IGraphicsRenderer's nullptr
    // CreateRenderTarget2D()/CreateRenderTargetCube() defaults -- TinyGL renders into exactly one
    // ZBuffer and has no off-screen framebuffer concept -- so GraphicsDevice rejects the bind
    // before reaching the renderer, and TinyGLRenderer::SetRenderTargets() refuses a non-empty set
    // as well (modules/renderers/tinygl/examples/tinygl_rejection_test.cpp).
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
#elif defined(CNA_RENDERER_OPENGLES1)
    // plan_opengles1.md: OpenGL ES 1.1 has no MRT mechanism, and no extension in the CM registry
    // adds one -- a third distinct case from the single-target renderers above (which support one)
    // and from Stub (which supports none). A single RenderTarget2D binds normally via
    // GL_OES_framebuffer_object; more than one is refused rather than binding the first and
    // silently dropping the rest, which is also why SupportsCapability(MultipleRenderTargets)
    // reports false.
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
#elif defined(CNA_RENDERER_OPENGL1)
    // plan_opengl1.md: the same single-colour-attachment refusal shape as OPENGLES1 above, for the
    // desktop fixed-function pipeline -- a single RenderTarget2D binds normally via the
    // ARB_framebuffer_object/core FBO path; more than one is refused rather than binding the
    // first and silently dropping the rest, which is also why
    // SupportsCapability(MultipleRenderTargets) reports false.
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
#elif defined(CNA_RENDERER_EASYGL) && defined(CNA_GL_PROFILE_OPENGLES2)
    // docs/opengles2-renderer.md: single-colour-attachment refusal, like OPENGLES1/OPENGL1 above
    // -- core OpenGL ES 2.0 has no glDrawBuffers, a single RenderTarget2D binds normally through
    // the family's FBO path, and SupportsCapability(MultipleRenderTargets) reports false. The
    // exception TYPE deliberately stays the EasyGL family's own established over-the-ceiling
    // std::runtime_error (the profile pins the ceiling to 1), which the family's
    // lifecycle/diagnostic tests already catch as the recorded MRT boundary -- not
    // OPENGLES1/OPENGL1's System::NotSupportedException.
    EXPECT_THROW(gd.SetRenderTargets(bindings), std::runtime_error);
#else
    EXPECT_NO_THROW(gd.SetRenderTargets(bindings));
#endif
}

TEST(GraphicsDeviceValidationTest, SetRenderTargets_OneTarget_DoesNotThrow)
{
    GraphicsDevice gd;
    RenderTarget2D rt(gd, 4, 4);
    std::vector<RenderTargetBinding> bindings{ RenderTargetBinding(&rt) };
#if defined(CNA_RENDERER_STUB) || defined(CNA_RENDERER_OPENVG) || defined(CNA_RENDERER_TINYGL)
    // Same Stub/OpenVG contract as the four-target case above: no render-target support of any
    // kind, so even a single binding is refused deterministically rather than silently accepted.
    // TinyGL joins them -- it renders into exactly one ZBuffer and creates no render target at all.
    EXPECT_THROW(gd.SetRenderTargets(bindings), System::NotSupportedException);
#else
    EXPECT_NO_THROW(gd.SetRenderTargets(bindings));
#endif
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
#if defined(CNA_RENDERER_STUB) || defined(CNA_RENDERER_PORTABLEGL) || defined(CNA_RENDERER_TINYGL)
    EXPECT_THROW(gd.SetRenderTarget(&target), System::NotSupportedException);
    // No partial state: GraphicsDevice must not report the rejected target as bound...
    EXPECT_TRUE(gd.GetRenderTargets().empty());
    // ...and the renderer must be left exactly as before the rejected call -- an ordinary backbuffer
    // draw still works immediately afterward.
    EXPECT_NO_THROW(gd.Clear(Color(0, 0, 0, 255)));
#else
    EXPECT_NO_THROW(gd.SetRenderTarget(&target));
    EXPECT_EQ(gd.GetRenderTargets().size(), 1u);
    gd.SetRenderTarget(nullptr);
#endif
}

TEST(GraphicsDeviceValidationTest, SetRenderTargets_Empty_DoesNotThrow)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW(gd.SetRenderTargets({}));
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

TEST(GraphicsDeviceValidationTest, SetVertexBuffers_DefaultNullBindingIsAccepted)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW(gd.SetVertexBuffers({VertexBufferBinding()}));
    ASSERT_EQ(gd.GetVertexBuffers().size(), 1u);
    EXPECT_EQ(gd.GetVertexBuffers()[0].getVertexBufferProperty(), nullptr);
}

TEST(GraphicsDeviceValidationTest, SetVertexBuffers_SeventeenBindingsThrow)
{
    GraphicsDevice gd;
    EXPECT_THROW(
        gd.SetVertexBuffers(std::vector<VertexBufferBinding>(17)),
        System::ArgumentOutOfRangeException);
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
