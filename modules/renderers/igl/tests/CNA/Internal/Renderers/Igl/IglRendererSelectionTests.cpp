// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#if defined(CNA_RENDERER_IGL)
#include "CNA/Internal/Renderers/Igl/IglRendererSelection.hpp"
#include "CNA/Internal/Renderers/Igl/IglShaderLibrary.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    namespace Detail = CNA::Internal::Renderers::Igl::Detail;
    using Detail::RendererBackend;
    using CNA::Internal::Renderers::Igl::BuildEffectShaderSources;
    using CNA::Internal::Renderers::Igl::GetSamplerUniformName;
    using CNA::Internal::Renderers::Igl::GetUniformBlockName;
    using CNA::Internal::Renderers::Igl::IglShaderSources;
    using CNA::Internal::Renderers::Igl::TextureUnit;
    using CNA::Internal::Renderers::Igl::UniformBufferBinding;
    using CNA::Internal::Renderers::Igl::VertexAttributeBit;
    using CNA::Internal::Renderers::Igl::VertexAttributeSlot;

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
