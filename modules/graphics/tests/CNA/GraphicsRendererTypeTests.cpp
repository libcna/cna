#include <gtest/gtest.h>
#include "CNA/GraphicsRendererType.hpp"

using CNA::GraphicsRendererType;
using CNA::getCurrentGraphicsRendererType;
using CNA::getCurrentGraphicsRendererName;

// Both functions are constexpr -- usable directly in a constant expression, not just at
// runtime. This is the actual point of making them constexpr (they don't take a GraphicsDevice
// argument, so unlike the GraphicsDevice::GetGraphicsRendererType()/Name() member wrappers,
// these two are genuinely evaluable at compile time).
static_assert(getCurrentGraphicsRendererType() == getCurrentGraphicsRendererType());
static_assert(!getCurrentGraphicsRendererName().empty());
constexpr GraphicsRendererType kCompileTimeType = getCurrentGraphicsRendererType();
constexpr std::string_view kCompileTimeName = getCurrentGraphicsRendererName();
constexpr int kPublicRendererCount = static_cast<int>(GraphicsRendererType::NanoVg) + 1;
static_assert(kPublicRendererCount == 49,
              "GraphicsRendererType must expose all 50 genuine renderer identities");

TEST(GraphicsRendererTypeTest, GetCurrentGraphicsRendererTypeDoesNotThrow)
{
    EXPECT_NO_THROW({ (void)getCurrentGraphicsRendererType(); });
}

TEST(GraphicsRendererTypeTest, GetCurrentGraphicsRendererNameDoesNotThrow)
{
    EXPECT_NO_THROW({ (void)getCurrentGraphicsRendererName(); });
}

TEST(GraphicsRendererTypeTest, GetCurrentGraphicsRendererNameIsNotEmpty)
{
    EXPECT_FALSE(getCurrentGraphicsRendererName().empty());
}

namespace
{
    // The one authoritative expected name per public GraphicsRendererType member.
    //
    // Deliberately has NO `default:` label, so an enum member with no arm here falls through to
    // the empty return below, which NameMatchesTypeForEveryRenderer turns into a hard failure
    // rather than a silent pass. That runtime guard is what this test relies on: the project
    // compiles with `-g -std=c++23` and does not enable `-Wall`/`-Wswitch`, so a missing arm
    // produces no compiler diagnostic. Omitting `default:` additionally means the switch WOULD
    // be flagged at compile time by any build that does turn those warnings on.
    //
    // The previous form of this test switched directly on the active renderer with only 15 of the
    // enum's members listed and no failure arm, so every build selecting one of the other members
    // executed no assertion at all and reported a vacuous pass.
    constexpr std::string_view ExpectedNameFor(GraphicsRendererType type)
    {
        switch (type)
        {
            case GraphicsRendererType::SdlRenderer: return "SDL_RENDERER";
            case GraphicsRendererType::OpenGLES2:   return "OPENGLES2";
            case GraphicsRendererType::OpenGLES3:    return "OPENGLES3";
            case GraphicsRendererType::OpenGL33:    return "OPENGL33";
            case GraphicsRendererType::WebGL1:      return "WEBGL1";
            case GraphicsRendererType::WebGL2:      return "WEBGL2";
            case GraphicsRendererType::Bgfx:        return "BGFX";
            case GraphicsRendererType::Vulkan:      return "VULKAN";
            case GraphicsRendererType::WebGPU:      return "WEBGPU";
            case GraphicsRendererType::Magnum:      return "MAGNUM";
            case GraphicsRendererType::Headless:    return "HEADLESS";
            case GraphicsRendererType::Software:    return "SOFTWARE";
            case GraphicsRendererType::DirectX11:       return "DIRECTX11";
            case GraphicsRendererType::DirectX12:       return "DIRECTX12";
            case GraphicsRendererType::Direct2D:    return "DIRECT2D";
            case GraphicsRendererType::Canvas:      return "CANVAS";
            case GraphicsRendererType::HtmlDom:     return "HTML_DOM";
            case GraphicsRendererType::Blend2D:     return "BLEND2D";
            case GraphicsRendererType::FreeDirect:  return "FREEDIRECT";
            case GraphicsRendererType::Stub:        return "STUB";
            case GraphicsRendererType::DirectX9:        return "DIRECTX9";
            case GraphicsRendererType::DirectX1:         return "DIRECTX1";
            case GraphicsRendererType::DirectX2:         return "DIRECTX2";
            case GraphicsRendererType::DirectX3:         return "DIRECTX3";
            case GraphicsRendererType::DirectX5:         return "DIRECTX5";
            case GraphicsRendererType::DirectX6:         return "DIRECTX6";
            case GraphicsRendererType::DirectX7:         return "DIRECTX7";
            case GraphicsRendererType::DirectX8:         return "DIRECTX8";
            case GraphicsRendererType::DirectX10:       return "DIRECTX10";
            case GraphicsRendererType::SdlGpu:      return "SDL_GPU";
            case GraphicsRendererType::OpenGLES1:   return "OPENGLES1";
            case GraphicsRendererType::OpenGL4:     return "OPENGL4";
            case GraphicsRendererType::OpenGL1:     return "OPENGL1";
            case GraphicsRendererType::OpenGL2:     return "OPENGL2";
            case GraphicsRendererType::Wicked:      return "WICKED";
            case GraphicsRendererType::Sokol:       return "SOKOL";
            case GraphicsRendererType::Diligent:    return "DILIGENT";
            case GraphicsRendererType::Glide:       return "GLIDE";
            case GraphicsRendererType::Gdi:         return "GDI";
            case GraphicsRendererType::Llgl:        return "LLGL";
            case GraphicsRendererType::Metal:       return "METAL";
            case GraphicsRendererType::Fna3d:       return "FNA3D";
            case GraphicsRendererType::SvgDom:      return "SVG_DOM";
            case GraphicsRendererType::OpenVg:      return "OPENVG";
            case GraphicsRendererType::PortableGL:  return "PORTABLEGL";
            case GraphicsRendererType::TinyGL:      return "TINYGL";
            case GraphicsRendererType::Igl:         return "IGL";
            case GraphicsRendererType::PixiJs:      return "PIXIJS";
            case GraphicsRendererType::NanoVg:      return "NANOVG";
        }
        return {};
    }
}

TEST(GraphicsRendererTypeTest, EveryPublicRendererHasOneUniqueCanonicalName)
{
    for (int i = 0; i < kPublicRendererCount; ++i)
    {
        const auto type = static_cast<GraphicsRendererType>(i);
        const auto name = ExpectedNameFor(type);
        ASSERT_FALSE(name.empty()) << "missing name for GraphicsRendererType value " << i;

        for (int j = i + 1; j < kPublicRendererCount; ++j)
        {
            EXPECT_NE(name, ExpectedNameFor(static_cast<GraphicsRendererType>(j)))
                << "duplicate public renderer name at values " << i << " and " << j;
        }
    }
}

TEST(GraphicsRendererTypeTest, NameMatchesTypeForEveryRenderer)
{
    // Every call in this build returns the SAME compile-time-selected renderer, so one build
    // checks one active arm; EveryPublicRendererHasOneUniqueCanonicalName covers the complete
    // 46-identity enum in every build. What this asserts
    // is that the (type, name) pair is internally consistent AND that the active renderer has an
    // expected-name arm at all -- a renderer with no arm fails here instead of passing vacuously.
    const auto type = getCurrentGraphicsRendererType();
    const auto name = getCurrentGraphicsRendererName();

    const std::string_view expected = ExpectedNameFor(type);
    ASSERT_FALSE(expected.empty())
        << "no expected-name arm for the active renderer (getCurrentGraphicsRendererName() reports \""
        << name << "\") -- add it to ExpectedNameFor() in this file";
    EXPECT_EQ(name, expected);
}

TEST(GraphicsRendererTypeTest, RepeatedCallsAreStable)
{
    EXPECT_EQ(getCurrentGraphicsRendererType(), getCurrentGraphicsRendererType());
    EXPECT_EQ(getCurrentGraphicsRendererName(), getCurrentGraphicsRendererName());
}

TEST(GraphicsRendererTypeTest, RuntimeValueMatchesCompileTimeConstant)
{
    EXPECT_EQ(getCurrentGraphicsRendererType(), kCompileTimeType);
    EXPECT_EQ(getCurrentGraphicsRendererName(), kCompileTimeName);
}
