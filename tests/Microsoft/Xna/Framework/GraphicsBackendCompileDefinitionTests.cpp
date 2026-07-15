// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#if defined(CNA_BACKEND_BGFX) && __has_include(<bgfx/bgfx.h>)
#define CNA_TEST_BGFX_AVAILABLE 1
#include <bgfx/bgfx.h>
#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"
#endif

TEST(GraphicsBackendCompileDefinitionsTest, ExactlyOneGraphicsBackendIsSelected)
{
    int enabled = 0;

#ifdef CNA_BACKEND_SDL_RENDERER
    ++enabled;
#endif
#ifdef CNA_BACKEND_EASYGL
    ++enabled;
#endif
#ifdef CNA_BACKEND_BGFX
    ++enabled;
#endif
#ifdef CNA_BACKEND_VULKAN
    ++enabled;
#endif
#ifdef CNA_BACKEND_WEBGPU
    ++enabled;
#endif
#ifdef CNA_BACKEND_HEADLESS
    ++enabled;
#endif
#ifdef CNA_BACKEND_SOFTWARE
    ++enabled;
#endif
#ifdef CNA_BACKEND_ASCII
    ++enabled;
#endif

    EXPECT_EQ(enabled, 1);
}

#ifdef CNA_TEST_BGFX_AVAILABLE
TEST(GraphicsBackendCompileDefinitionsTest, BgfxApiIsLinkedForBgfxBackend)
{
    const bgfx::TextureHandle invalidTexture = BGFX_INVALID_HANDLE;
    EXPECT_FALSE(bgfx::isValid(invalidTexture));
}

TEST(GraphicsBackendCompileDefinitionsTest, BgfxRendererTypeDefaultIsSafeForPlatform)
{
    const auto renderer = CNA::Internal::Backends::Bgfx::Detail::GetDefaultRendererType();
#if defined(__linux__)
EXPECT_EQ(renderer, bgfx::RendererType::OpenGL);
#else
EXPECT_EQ(renderer, bgfx::RendererType::Count);
#endif
}

TEST(GraphicsBackendCompileDefinitionsTest, BgfxRendererTypeOverrideParsingWorks)
{
    EXPECT_EQ(
        CNA::Internal::Backends::Bgfx::Detail::ParseRendererTypeOverride("vulkan"),
        bgfx::RendererType::Vulkan
    );
    EXPECT_EQ(
        CNA::Internal::Backends::Bgfx::Detail::ParseRendererTypeOverride("OpenGL"),
        bgfx::RendererType::OpenGL
    );
    EXPECT_EQ(
        CNA::Internal::Backends::Bgfx::Detail::ParseRendererTypeOverride("auto"),
        bgfx::RendererType::Count
    );
}

TEST(GraphicsBackendCompileDefinitionsTest, BgfxRendererTypeOverrideRejectsInvalidValue)
{
    EXPECT_THROW(
        CNA::Internal::Backends::Bgfx::Detail::ParseRendererTypeOverride("invalid-renderer"),
        std::runtime_error
    );
}
#endif
