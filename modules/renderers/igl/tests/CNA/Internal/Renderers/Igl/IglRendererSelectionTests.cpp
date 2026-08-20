// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

// plans/plan_runtimerenderer.md RTR-P9-9: PRESENT_, not only the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running whenever
// that renderer is COMPILED IN -- in a multi-renderer build it need not be the selected one.
#if defined(CNA_RENDERER_IGL) || defined(CNA_RENDERER_PRESENT_IGL)
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"
#include "CNA/Internal/Renderers/Igl/IglRendererSelection.hpp"
#include "CNA/Internal/Renderers/Igl/IglShaderLibrary.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    namespace Detail = CNA::Internal::Renderers::Igl::Detail;
    using Detail::RendererBackend;
    using CNA::Internal::Renderers::RendererGlFramebufferRequest;
    using CNA::Internal::Renderers::RendererWindowKind;
    using CNA::Internal::Renderers::Igl::BuildEffectShaderSources;
    using CNA::Internal::Renderers::Igl::GetSamplerUniformName;
    using CNA::Internal::Renderers::Igl::GetUniformBlockName;
    using CNA::Internal::Renderers::Igl::IglShaderSources;
    namespace TextureUnit = CNA::Internal::Renderers::Igl::TextureUnit;
    namespace UniformBufferBinding = CNA::Internal::Renderers::Igl::UniformBufferBinding;
    using CNA::Internal::Renderers::Igl::VertexAttributeBit;
    namespace VertexAttributeSlot = CNA::Internal::Renderers::Igl::VertexAttributeSlot;

    [[nodiscard]] bool Contains(const std::string& haystack, const char* needle)
    {
        return haystack.find(needle) != std::string::npos;
    }
}

TEST(IglRendererSelection, EveryBackendHasAStableName)
{
    EXPECT_STREQ("OpenGL", Detail::GetRendererBackendName(RendererBackend::OpenGL));
    EXPECT_STREQ("Vulkan", Detail::GetRendererBackendName(RendererBackend::Vulkan));
}

TEST(IglRendererSelection, DefaultPreferenceOnlyContainsCompiledInBackends)
{
    const std::vector<RendererBackend> preference = Detail::GetDefaultRendererPreference();
    for (const RendererBackend backend : preference)
        EXPECT_TRUE(Detail::IsRendererBackendCompiledIn(backend));
}

TEST(IglRendererSelection, AutoAndEmptyResolveToTheDefaultPreference)
{
    const std::vector<RendererBackend> expected = Detail::GetDefaultRendererPreference();
    EXPECT_EQ(expected, Detail::ParseRendererBackendOverride(nullptr));
    EXPECT_EQ(expected, Detail::ParseRendererBackendOverride(""));
    EXPECT_EQ(expected, Detail::ParseRendererBackendOverride("auto"));
    EXPECT_EQ(expected, Detail::ParseRendererBackendOverride("AUTO"));
    EXPECT_EQ(expected, Detail::ParseRendererBackendOverride("default"));
}

TEST(IglRendererSelection, AnExplicitBackendIsHonouredExactlyWithNoFallback)
{
    if (Detail::IsRendererBackendCompiledIn(RendererBackend::OpenGL))
    {
        const std::vector<RendererBackend> resolved =
            Detail::ParseRendererBackendOverride("opengl");
        ASSERT_EQ(1u, resolved.size());
        EXPECT_EQ(RendererBackend::OpenGL, resolved.front());

        // Case and punctuation are normalised away, so a value from a shell script cannot fail on
        // formatting alone.
        EXPECT_EQ(resolved, Detail::ParseRendererBackendOverride("Open-GL"));
        EXPECT_EQ(resolved, Detail::ParseRendererBackendOverride("  GL  "));
    }

    if (Detail::IsRendererBackendCompiledIn(RendererBackend::Vulkan))
    {
        const std::vector<RendererBackend> resolved =
            Detail::ParseRendererBackendOverride("vulkan");
        ASSERT_EQ(1u, resolved.size());
        EXPECT_EQ(RendererBackend::Vulkan, resolved.front());
        EXPECT_EQ(resolved, Detail::ParseRendererBackendOverride("VK"));
    }
}

TEST(IglRendererSelection, AnUnknownBackendNameIsRejected)
{
    EXPECT_THROW((void)Detail::ParseRendererBackendOverride("metal"), std::runtime_error);
    EXPECT_THROW((void)Detail::ParseRendererBackendOverride("d3d12"), std::runtime_error);
    EXPECT_THROW((void)Detail::ParseRendererBackendOverride("nonsense"), std::runtime_error);
}

TEST(IglRendererSelection, ABackendMissingFromThisBuildIsRejectedRatherThanSubstituted)
{
    if (!Detail::IsRendererBackendCompiledIn(RendererBackend::Vulkan))
        EXPECT_THROW((void)Detail::ParseRendererBackendOverride("vulkan"), std::runtime_error);
    if (!Detail::IsRendererBackendCompiledIn(RendererBackend::OpenGL))
        EXPECT_THROW((void)Detail::ParseRendererBackendOverride("opengl"), std::runtime_error);
}

TEST(IglRendererSelection, OnlyTheOpenGlBackendNeedsAnOpenGlWindow)
{
    EXPECT_TRUE(Detail::RendererBackendNeedsOpenGLWindow(RendererBackend::OpenGL));
    EXPECT_FALSE(Detail::RendererBackendNeedsOpenGLWindow(RendererBackend::Vulkan));
    EXPECT_FALSE(Detail::RendererBackendNeedsVulkanWindow(RendererBackend::OpenGL));
    EXPECT_TRUE(Detail::RendererBackendNeedsVulkanWindow(RendererBackend::Vulkan));
}

TEST(IglRendererSelection, TheResolvedBackendIsStableWithinTheProcess)
{
    // The window's render intent is chosen from this answer before the renderer exists, so a
    // second call that disagreed would leave the window configured for the wrong API.
    const RendererBackend first = Detail::ResolveRendererBackend();
    EXPECT_EQ(first, Detail::ResolveRendererBackend());
    EXPECT_TRUE(Detail::IsRendererBackendCompiledIn(first));
}

// ---------------------------------------------------------------------------
// Window kind and pre-window framebuffer request
// ---------------------------------------------------------------------------

TEST(IglRendererSelection, EachBackendMapsToItsOwnWindowKind)
{
    // The regression this pins: the renderer descriptor recorded RendererWindowKind::OpenGL
    // unconditionally, so CNA_IGL_BACKEND=vulkan built a Vulkan device against a window created
    // with an OpenGL render intent -- and the fallback rule (design decision 8) compared a window
    // kind the window did not have.
    EXPECT_EQ(RendererWindowKind::OpenGL,
              Detail::GetRendererBackendWindowKind(RendererBackend::OpenGL));
    EXPECT_EQ(RendererWindowKind::Vulkan,
              Detail::GetRendererBackendWindowKind(RendererBackend::Vulkan));
}

TEST(IglRendererSelection, WindowKindAgreesWithTheBooleanFormOfTheSameQuestion)
{
    // One source of truth: the kind is derived from these two, not stated a second time.
    for (const RendererBackend backend : {RendererBackend::OpenGL, RendererBackend::Vulkan})
    {
        const RendererWindowKind kind = Detail::GetRendererBackendWindowKind(backend);
        EXPECT_EQ(Detail::RendererBackendNeedsOpenGLWindow(backend),
                  kind == RendererWindowKind::OpenGL);
        EXPECT_EQ(Detail::RendererBackendNeedsVulkanWindow(backend),
                  kind == RendererWindowKind::Vulkan);
    }
}

TEST(IglRendererSelection, AVulkanWindowIsNotCompatibleWithAnOpenGlOne)
{
    using CNA::Internal::Renderers::AreWindowKindsCompatible;

    // What makes the kind above matter: a fallback across these two must recreate the window
    // rather than hand a Vulkan device a window created for OpenGL.
    EXPECT_FALSE(AreWindowKindsCompatible(
        Detail::GetRendererBackendWindowKind(RendererBackend::OpenGL),
        Detail::GetRendererBackendWindowKind(RendererBackend::Vulkan)));
    EXPECT_FALSE(AreWindowKindsCompatible(
        Detail::GetRendererBackendWindowKind(RendererBackend::Vulkan),
        Detail::GetRendererBackendWindowKind(RendererBackend::OpenGL)));
    EXPECT_TRUE(AreWindowKindsCompatible(
        Detail::GetRendererBackendWindowKind(RendererBackend::Vulkan),
        Detail::GetRendererBackendWindowKind(RendererBackend::Vulkan)));
}

TEST(IglRendererSelection, TheOpenGlBackendStatesItsFramebufferBeforeTheWindowExists)
{
    // GLX fixes a window's visual when the window is created. Without this request the window got
    // the default visual -- in practice 0 stencil bits, which silently turns every
    // DepthStencilState::StencilEnable into a no-op no matter what the renderer asks for later.
    const RendererGlFramebufferRequest request =
        Detail::GetRendererBackendGlFramebufferRequest(RendererBackend::OpenGL);

    EXPECT_EQ(24, request.depthBits);
    EXPECT_EQ(8, request.stencilBits);
    EXPECT_TRUE(request.doubleBuffered);
    // Back-buffer MSAA on this backend is the visual's own multisample buffer; there is no resolve
    // pass here that could add it after the window exists.
    EXPECT_TRUE(request.wantsMultiSample);
}

TEST(IglRendererSelection, TheVulkanBackendMakesNoOpenGlFramebufferRequest)
{
    // A Vulkan-intent window carries no GL visual at all, so asking for depth/stencil bits on it
    // would be a statement about something that does not exist.
    const RendererGlFramebufferRequest request =
        Detail::GetRendererBackendGlFramebufferRequest(RendererBackend::Vulkan);

    EXPECT_EQ(0, request.depthBits);
    EXPECT_EQ(0, request.stencilBits);
    EXPECT_FALSE(request.doubleBuffered);
    EXPECT_FALSE(request.wantsMultiSample);
}

TEST(IglRendererSelection, ThePreWindowResolutionMatchesTheRendererIsOwnAndNeverThrows)
{
    // The descriptor carrying the window kind is built from a static initializer (the generated
    // registry publishes the compiled-in set before main()), where a throw would terminate the
    // process. It must still agree with the answer the device is later built from.
    RendererBackend forWindow = RendererBackend::OpenGL;
    EXPECT_NO_THROW(forWindow = Detail::ResolveRendererBackendForWindow());
    EXPECT_EQ(Detail::ResolveRendererBackend(), forWindow);
    EXPECT_TRUE(Detail::IsRendererBackendCompiledIn(forWindow));
}

TEST(IglRendererSelection, TheRegisteredDescriptorFollowsTheResolvedBackend)
{
    namespace Registry = CNA::Internal::Renderers::GraphicsRendererRegistry;

    const auto* descriptor = Registry::Find(CNA::GraphicsRendererType::Igl);
    if (descriptor == nullptr)
        GTEST_SKIP() << "the Igl family is compiled in but not registered in this build";

    const RendererBackend backend = Detail::ResolveRendererBackendForWindow();

    EXPECT_EQ(Detail::GetRendererBackendWindowKind(backend), descriptor->windowKind);
    EXPECT_EQ(Detail::GetRendererBackendGlFramebufferRequest(backend).depthBits,
              descriptor->glFramebuffer.depthBits);
    EXPECT_EQ(Detail::GetRendererBackendGlFramebufferRequest(backend).stencilBits,
              descriptor->glFramebuffer.stencilBits);
    EXPECT_EQ(Detail::GetRendererBackendGlFramebufferRequest(backend).doubleBuffered,
              descriptor->glFramebuffer.doubleBuffered);
    EXPECT_EQ(Detail::GetRendererBackendGlFramebufferRequest(backend).wantsMultiSample,
              descriptor->glFramebuffer.wantsMultiSample);
    // Only the OpenGL backend adopts the platform's GL context; the Vulkan one builds its surface
    // from the native window handle and never reads that service.
    EXPECT_EQ(Detail::RendererBackendNeedsOpenGLWindow(backend), descriptor->needsGlContext);
    EXPECT_FALSE(descriptor->needsVulkanSurface);
    EXPECT_TRUE(descriptor->needsWindow);
    EXPECT_TRUE(descriptor->needsVideoSubsystem);
}

TEST(IglShaderLibrary, EveryDeclaredSlotAndBindingHasAName)
{
    for (std::uint32_t unit = 0; unit < TextureUnit::Count; ++unit)
        EXPECT_STRNE("", GetSamplerUniformName(unit)) << "texture unit " << unit;

    EXPECT_STREQ("CnaEffect", GetUniformBlockName(UniformBufferBinding::Effect));
    EXPECT_STREQ("CnaBones", GetUniformBlockName(UniformBufferBinding::Bones));
}

TEST(IglShaderLibrary, OnlyRequestedAttributesAreDeclared)
{
    // A Vulkan pipeline rejects a shader input with no matching vertex-input attribute, so the
    // generated vertex shader must declare exactly the layout it was asked for.
    const std::uint32_t mask = VertexAttributeBit(VertexAttributeSlot::Position) |
                               VertexAttributeBit(VertexAttributeSlot::Color);
    const IglShaderSources sources = BuildEffectShaderSources(mask, /*vulkan=*/false, 1);

    EXPECT_TRUE(Contains(sources.vertex, "in vec3 aPosition;"));
    EXPECT_TRUE(Contains(sources.vertex, "in vec4 aColor;"));
    EXPECT_FALSE(Contains(sources.vertex, "in vec3 aNormal;"));
    EXPECT_FALSE(Contains(sources.vertex, "in vec2 aTexCoord0;"));
    EXPECT_FALSE(Contains(sources.vertex, "in vec4 aBlendWeights;"));
}

TEST(IglShaderLibrary, TheFragmentStageDeclaresOneOutputPerColourAttachment)
{
    const std::uint32_t mask = VertexAttributeBit(VertexAttributeSlot::Position);

    const IglShaderSources single = BuildEffectShaderSources(mask, /*vulkan=*/false, 1);
    EXPECT_TRUE(Contains(single.fragment, "out vec4 outColor0;"));
    EXPECT_FALSE(Contains(single.fragment, "out vec4 outColor1;"));

    const IglShaderSources multi = BuildEffectShaderSources(mask, /*vulkan=*/false, 3);
    EXPECT_TRUE(Contains(multi.fragment, "out vec4 outColor0;"));
    EXPECT_TRUE(Contains(multi.fragment, "out vec4 outColor2;"));
    EXPECT_FALSE(Contains(multi.fragment, "out vec4 outColor3;"));
}

TEST(IglShaderLibrary, VulkanSourcesCarryTheDescriptorSetsIglBindsInto)
{
    const std::uint32_t mask = VertexAttributeBit(VertexAttributeSlot::Position);
    const IglShaderSources vulkan = BuildEffectShaderSources(mask, /*vulkan=*/true, 1);

    // IGL binds combined image samplers into set 0 and uniform buffers into set 1
    // (VulkanContext's kBindPoint_CombinedImageSamplers / kBindPoint_Buffers).
    EXPECT_TRUE(Contains(vulkan.fragment, "layout(set = 0, binding = 0) uniform sampler2D uTexture0;"));
    EXPECT_TRUE(Contains(vulkan.fragment, "layout(set = 1, binding = 0, std140) uniform CnaEffect"));
    EXPECT_TRUE(Contains(vulkan.vertex, "layout(set = 1, binding = 1, std140) uniform CnaBones"));
    EXPECT_TRUE(Contains(vulkan.vertex, "#version 460"));
}

TEST(IglShaderLibrary, OpenGlSourcesBindByNameRatherThanByLayoutQualifier)
{
    const std::uint32_t mask = VertexAttributeBit(VertexAttributeSlot::Position);
    const IglShaderSources opengl = BuildEffectShaderSources(mask, /*vulkan=*/false, 1);

    // The generated GLSL targets 4.1 core, where an explicit binding qualifier on a uniform block
    // is not yet available; the pipeline's own maps resolve them instead.
    EXPECT_TRUE(Contains(opengl.fragment, "layout(std140) uniform CnaEffect"));
    EXPECT_TRUE(Contains(opengl.fragment, "uniform sampler2D uTexture0;"));
    EXPECT_FALSE(Contains(opengl.fragment, "set = 0"));
    EXPECT_TRUE(Contains(opengl.vertex, "#version 410 core"));
}

TEST(IglShaderLibrary, CustomShaderSourcesKeepTheirOwnVersionDirective)
{
    using CNA::Internal::Renderers::Igl::AdaptCustomShaderSources;

    const IglShaderSources adapted =
        AdaptCustomShaderSources("#version 300 es\nvoid main(){}", "void main(){}", false);
    EXPECT_TRUE(Contains(adapted.vertex, "#version 300 es"));
    EXPECT_FALSE(Contains(adapted.vertex, "#version 410 core"));
    EXPECT_TRUE(Contains(adapted.fragment, "#version 410 core"));
}
#endif // CNA_RENDERER_IGL
