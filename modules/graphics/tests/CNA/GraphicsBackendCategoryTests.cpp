#include <gtest/gtest.h>
#include <optional>

#include "CNA/GraphicsBackendCategory.hpp"
#include "CNA/GraphicsRendererType.hpp"

using CNA::GraphicsBackendCategory;
using CNA::GraphicsRendererType;
using CNA::getCurrentGraphicsBackendCategory;
using CNA::getCurrentGraphicsRendererType;
using CNA::getGraphicsBackendCategory;
using CNA::toStringView;

// Both functions are constexpr -- usable directly in a constant expression, mirroring
// GraphicsRendererType.hpp's own getCurrentGraphicsRendererType()/getCurrentGraphicsRendererName().
static_assert(getCurrentGraphicsBackendCategory() == getCurrentGraphicsBackendCategory());
static_assert(!toStringView(getCurrentGraphicsBackendCategory()).empty());
constexpr GraphicsBackendCategory kCompileTimeCategory = getCurrentGraphicsBackendCategory();
constexpr int kPublicRendererCount = static_cast<int>(GraphicsRendererType::Igl) + 1;
static_assert(kPublicRendererCount == 48,
              "GraphicsRendererType must expose all 48 genuine renderer identities");

TEST(GraphicsBackendCategoryTest, GetCurrentGraphicsBackendCategoryDoesNotThrow)
{
    EXPECT_NO_THROW({ (void)getCurrentGraphicsBackendCategory(); });
}

TEST(GraphicsBackendCategoryTest, ToStringViewIsCorrectForEveryEnumerator)
{
    EXPECT_EQ(toStringView(GraphicsBackendCategory::Native), "Native");
    EXPECT_EQ(toStringView(GraphicsBackendCategory::TranslationLayer), "TranslationLayer");
    EXPECT_EQ(toStringView(GraphicsBackendCategory::Software), "Software");
    EXPECT_EQ(toStringView(GraphicsBackendCategory::Web), "Web");
    EXPECT_EQ(toStringView(GraphicsBackendCategory::Diagnostic), "Diagnostic");
}

namespace
{
    // The one authoritative expected category per public GraphicsRendererType member, kept
    // independent of GraphicsBackendCategory.hpp's own switch so a drift between the two is
    // caught by EveryPublicRendererHasAClassifiedCategory below instead of passing vacuously.
    // Deliberately has NO `default:` label -- see GraphicsRendererTypeTests.cpp for why.
    constexpr std::optional<GraphicsBackendCategory> ExpectedCategoryFor(GraphicsRendererType type)
    {
        switch (type)
        {
            case GraphicsRendererType::OpenGLES2:
            case GraphicsRendererType::OpenGLES3:
            case GraphicsRendererType::OpenGL33:
            case GraphicsRendererType::Vulkan:
            case GraphicsRendererType::Magnum:
            case GraphicsRendererType::DirectX11:
            case GraphicsRendererType::DirectX12:
            case GraphicsRendererType::Direct2D:
            case GraphicsRendererType::DirectX9:
            case GraphicsRendererType::DirectX1:
            case GraphicsRendererType::DirectX2:
            case GraphicsRendererType::DirectX3:
            case GraphicsRendererType::DirectX5:
            case GraphicsRendererType::DirectX6:
            case GraphicsRendererType::DirectX7:
            case GraphicsRendererType::DirectX8:
            case GraphicsRendererType::DirectX10:
            case GraphicsRendererType::OpenGLES1:
            case GraphicsRendererType::OpenGL4:
            case GraphicsRendererType::OpenGL1:
            case GraphicsRendererType::OpenGL2:
            case GraphicsRendererType::Glide:
            case GraphicsRendererType::Gdi:
            case GraphicsRendererType::Metal:
                return GraphicsBackendCategory::Native;

            case GraphicsRendererType::SdlRenderer:
            case GraphicsRendererType::Bgfx:
            case GraphicsRendererType::WebGPU:
            case GraphicsRendererType::FreeDirect:
            case GraphicsRendererType::SdlGpu:
            case GraphicsRendererType::Wicked:
            case GraphicsRendererType::Sokol:
            case GraphicsRendererType::Diligent:
            case GraphicsRendererType::Llgl:
            case GraphicsRendererType::Igl:
            case GraphicsRendererType::Fna3d:
            case GraphicsRendererType::OpenVg:
                return GraphicsBackendCategory::TranslationLayer;

            case GraphicsRendererType::Software:
            case GraphicsRendererType::Skia:
            case GraphicsRendererType::Blend2D:
            case GraphicsRendererType::PortableGL:
            case GraphicsRendererType::TinyGL:
                return GraphicsBackendCategory::Software;

            case GraphicsRendererType::WebGL1:
            case GraphicsRendererType::WebGL2:
            case GraphicsRendererType::Canvas:
            case GraphicsRendererType::HtmlDom:
            case GraphicsRendererType::SvgDom:
                return GraphicsBackendCategory::Web;

            case GraphicsRendererType::Headless:
            case GraphicsRendererType::Stub:
                return GraphicsBackendCategory::Diagnostic;
        }
        return std::nullopt;
    }
}

TEST(GraphicsBackendCategoryTest, EveryPublicRendererHasAClassifiedCategory)
{
    for (int i = 0; i < kPublicRendererCount; ++i)
    {
        const auto type = static_cast<GraphicsRendererType>(i);
        const auto expected = ExpectedCategoryFor(type);
        ASSERT_TRUE(expected.has_value()) << "missing category for GraphicsRendererType value " << i;
        EXPECT_EQ(getGraphicsBackendCategory(type), *expected)
            << "GraphicsBackendCategory.hpp disagrees with this test's own table for value " << i;
    }
}

TEST(GraphicsBackendCategoryTest, CategoryMatchesForCurrentRenderer)
{
    const auto type = getCurrentGraphicsRendererType();
    const auto expected = ExpectedCategoryFor(type);
    ASSERT_TRUE(expected.has_value())
        << "no expected-category arm for the active renderer -- add it to ExpectedCategoryFor() in this file";
    EXPECT_EQ(getCurrentGraphicsBackendCategory(), *expected);
}

TEST(GraphicsBackendCategoryTest, RepeatedCallsAreStable)
{
    EXPECT_EQ(getCurrentGraphicsBackendCategory(), getCurrentGraphicsBackendCategory());
}

TEST(GraphicsBackendCategoryTest, RuntimeValueMatchesCompileTimeConstant)
{
    EXPECT_EQ(getCurrentGraphicsBackendCategory(), kCompileTimeCategory);
}
