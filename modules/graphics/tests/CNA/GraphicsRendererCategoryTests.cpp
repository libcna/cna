#include <gtest/gtest.h>
#include <optional>

#include "CNA/GraphicsRendererCategory.hpp"
#include "CNA/GraphicsRendererType.hpp"

using CNA::GraphicsRendererCategory;
using CNA::GraphicsRendererType;
using CNA::getCurrentGraphicsRendererCategory;
using CNA::getCurrentGraphicsRendererType;
using CNA::getGraphicsRendererCategory;
using CNA::toStringView;

// Both functions are constexpr -- usable directly in a constant expression, mirroring
// GraphicsRendererType.hpp's own getCurrentGraphicsRendererType()/getCurrentGraphicsRendererName().
static_assert(getCurrentGraphicsRendererCategory() == getCurrentGraphicsRendererCategory());
static_assert(!toStringView(getCurrentGraphicsRendererCategory()).empty());
constexpr GraphicsRendererCategory kCompileTimeCategory = getCurrentGraphicsRendererCategory();
constexpr int kPublicRendererCount = static_cast<int>(GraphicsRendererType::Fna3d) + 1;
static_assert(kPublicRendererCount == 10,
              "GraphicsRendererType must expose all 10 public renderer identities");

TEST(GraphicsRendererCategoryTest, GetCurrentGraphicsRendererCategoryDoesNotThrow)
{
    EXPECT_NO_THROW({ (void)getCurrentGraphicsRendererCategory(); });
}

TEST(GraphicsRendererCategoryTest, ToStringViewIsCorrectForEveryEnumerator)
{
    EXPECT_EQ(toStringView(GraphicsRendererCategory::Native), "Native");
    EXPECT_EQ(toStringView(GraphicsRendererCategory::TranslationLayer), "TranslationLayer");
    EXPECT_EQ(toStringView(GraphicsRendererCategory::Software), "Software");
    EXPECT_EQ(toStringView(GraphicsRendererCategory::Web), "Web");
    EXPECT_EQ(toStringView(GraphicsRendererCategory::Diagnostic), "Diagnostic");
}

namespace
{
    // The one authoritative expected category per public GraphicsRendererType member, kept
    // independent of GraphicsRendererCategory.hpp's own switch so a drift between the two is
    // caught by EveryPublicRendererHasAClassifiedCategory below instead of passing vacuously.
    // Deliberately has NO `default:` label -- see GraphicsRendererTypeTests.cpp for why.
    constexpr std::optional<GraphicsRendererCategory> ExpectedCategoryFor(GraphicsRendererType type)
    {
        switch (type)
        {
            case GraphicsRendererType::OpenGLES3:
            case GraphicsRendererType::OpenGL33:
            case GraphicsRendererType::DirectX9:
                return GraphicsRendererCategory::Native;

            case GraphicsRendererType::WebGPU:
            case GraphicsRendererType::SdlGpu:
            case GraphicsRendererType::Fna3d:
                return GraphicsRendererCategory::TranslationLayer;

            case GraphicsRendererType::Software:
                return GraphicsRendererCategory::Software;

            case GraphicsRendererType::WebGL2:
                return GraphicsRendererCategory::Web;

            case GraphicsRendererType::Headless:
            case GraphicsRendererType::Stub:
                return GraphicsRendererCategory::Diagnostic;
        }
        return std::nullopt;
    }
}

TEST(GraphicsRendererCategoryTest, EveryPublicRendererHasAClassifiedCategory)
{
    for (int i = 0; i < kPublicRendererCount; ++i)
    {
        const auto type = static_cast<GraphicsRendererType>(i);
        const auto expected = ExpectedCategoryFor(type);
        ASSERT_TRUE(expected.has_value()) << "missing category for GraphicsRendererType value " << i;
        EXPECT_EQ(getGraphicsRendererCategory(type), *expected)
            << "GraphicsRendererCategory.hpp disagrees with this test's own table for value " << i;
    }
}

TEST(GraphicsRendererCategoryTest, CategoryMatchesForCurrentRenderer)
{
    const auto type = getCurrentGraphicsRendererType();
    const auto expected = ExpectedCategoryFor(type);
    ASSERT_TRUE(expected.has_value())
        << "no expected-category arm for the active renderer -- add it to ExpectedCategoryFor() in this file";
    EXPECT_EQ(getCurrentGraphicsRendererCategory(), *expected);
}

TEST(GraphicsRendererCategoryTest, RepeatedCallsAreStable)
{
    EXPECT_EQ(getCurrentGraphicsRendererCategory(), getCurrentGraphicsRendererCategory());
}

TEST(GraphicsRendererCategoryTest, RuntimeValueMatchesCompileTimeConstant)
{
    EXPECT_EQ(getCurrentGraphicsRendererCategory(), kCompileTimeCategory);
}
