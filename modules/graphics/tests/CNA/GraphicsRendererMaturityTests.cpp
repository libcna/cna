#include <gtest/gtest.h>
#include <optional>

#include "CNA/GraphicsRendererMaturity.hpp"
#include "CNA/GraphicsRendererType.hpp"

using CNA::GraphicsRendererMaturity;
using CNA::GraphicsRendererType;
using CNA::getCurrentGraphicsRendererMaturity;
using CNA::getCurrentGraphicsRendererType;
using CNA::getGraphicsRendererMaturity;
using CNA::toStringView;

// Both functions are constexpr -- usable directly in a constant expression, mirroring
// GraphicsRendererType.hpp's own getCurrentGraphicsRendererType()/getCurrentGraphicsRendererName().
static_assert(getCurrentGraphicsRendererMaturity() == getCurrentGraphicsRendererMaturity());
static_assert(!toStringView(getCurrentGraphicsRendererMaturity()).empty());
constexpr GraphicsRendererMaturity kCompileTimeMaturity = getCurrentGraphicsRendererMaturity();
constexpr int kPublicRendererCount = static_cast<int>(GraphicsRendererType::Fna3d) + 1;
static_assert(kPublicRendererCount == 10,
              "GraphicsRendererType must expose all 10 public renderer identities");

TEST(GraphicsRendererMaturityTest, GetCurrentGraphicsRendererMaturityDoesNotThrow)
{
    EXPECT_NO_THROW({ (void)getCurrentGraphicsRendererMaturity(); });
}

TEST(GraphicsRendererMaturityTest, ToStringViewIsCorrectForEveryEnumerator)
{
    EXPECT_EQ(toStringView(GraphicsRendererMaturity::Production), "Production");
    EXPECT_EQ(toStringView(GraphicsRendererMaturity::Supported), "Supported");
    EXPECT_EQ(toStringView(GraphicsRendererMaturity::Experimental), "Experimental");
    EXPECT_EQ(toStringView(GraphicsRendererMaturity::Historical), "Historical");
    EXPECT_EQ(toStringView(GraphicsRendererMaturity::Deprecated), "Deprecated");
}

namespace
{
    // The one authoritative expected maturity per public GraphicsRendererType member, kept
    // independent of GraphicsRendererMaturity.hpp's own switch so a drift between the two is
    // caught by EveryPublicRendererHasAClassifiedMaturity below instead of passing vacuously.
    // Deliberately has NO `default:` label -- see GraphicsRendererTypeTests.cpp for why: this
    // project compiles without -Wswitch, so a missing arm needs a runtime guard instead of relying
    // on a compiler diagnostic. std::optional's empty state is that guard (there is no natural
    // "invalid" GraphicsRendererMaturity enumerator to fall back to, unlike a std::string_view).
    constexpr std::optional<GraphicsRendererMaturity> ExpectedMaturityFor(GraphicsRendererType type)
    {
        switch (type)
        {
            case GraphicsRendererType::OpenGLES3:
            case GraphicsRendererType::OpenGL33:
            case GraphicsRendererType::DirectX9:
                return GraphicsRendererMaturity::Production;

            case GraphicsRendererType::WebGL2:
            case GraphicsRendererType::WebGPU:
            case GraphicsRendererType::Headless:
            case GraphicsRendererType::Stub:
            case GraphicsRendererType::SdlGpu:
                return GraphicsRendererMaturity::Supported;

            case GraphicsRendererType::Software:
            case GraphicsRendererType::Fna3d:
                return GraphicsRendererMaturity::Experimental;

                return GraphicsRendererMaturity::Historical;
        }
        return std::nullopt;
    }
}

TEST(GraphicsRendererMaturityTest, EveryPublicRendererHasAClassifiedMaturity)
{
    for (int i = 0; i < kPublicRendererCount; ++i)
    {
        const auto type = static_cast<GraphicsRendererType>(i);
        const auto expected = ExpectedMaturityFor(type);
        ASSERT_TRUE(expected.has_value()) << "missing maturity for GraphicsRendererType value " << i;
        EXPECT_EQ(getGraphicsRendererMaturity(type), *expected)
            << "GraphicsRendererMaturity.hpp disagrees with this test's own table for value " << i;
    }
}

TEST(GraphicsRendererMaturityTest, MaturityMatchesForCurrentRenderer)
{
    const auto type = getCurrentGraphicsRendererType();
    const auto expected = ExpectedMaturityFor(type);
    ASSERT_TRUE(expected.has_value())
        << "no expected-maturity arm for the active renderer -- add it to ExpectedMaturityFor() in this file";
    EXPECT_EQ(getCurrentGraphicsRendererMaturity(), *expected);
}

TEST(GraphicsRendererMaturityTest, RepeatedCallsAreStable)
{
    EXPECT_EQ(getCurrentGraphicsRendererMaturity(), getCurrentGraphicsRendererMaturity());
}

TEST(GraphicsRendererMaturityTest, RuntimeValueMatchesCompileTimeConstant)
{
    EXPECT_EQ(getCurrentGraphicsRendererMaturity(), kCompileTimeMaturity);
}
