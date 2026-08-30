#include <gtest/gtest.h>
#include <optional>

#include "CNA/GraphicsBackendMaturity.hpp"
#include "CNA/GraphicsRendererType.hpp"

using CNA::GraphicsBackendMaturity;
using CNA::GraphicsRendererType;
using CNA::getCurrentGraphicsBackendMaturity;
using CNA::getCurrentGraphicsRendererType;
using CNA::getGraphicsBackendMaturity;
using CNA::toStringView;

// Both functions are constexpr -- usable directly in a constant expression, mirroring
// GraphicsRendererType.hpp's own getCurrentGraphicsRendererType()/getCurrentGraphicsRendererName().
static_assert(getCurrentGraphicsBackendMaturity() == getCurrentGraphicsBackendMaturity());
static_assert(!toStringView(getCurrentGraphicsBackendMaturity()).empty());
constexpr GraphicsBackendMaturity kCompileTimeMaturity = getCurrentGraphicsBackendMaturity();
constexpr int kPublicRendererCount = static_cast<int>(GraphicsRendererType::PixiJs) + 1;
static_assert(kPublicRendererCount == 39,
              "GraphicsRendererType must expose all 48 genuine renderer identities");

TEST(GraphicsBackendMaturityTest, GetCurrentGraphicsBackendMaturityDoesNotThrow)
{
    EXPECT_NO_THROW({ (void)getCurrentGraphicsBackendMaturity(); });
}

TEST(GraphicsBackendMaturityTest, ToStringViewIsCorrectForEveryEnumerator)
{
    EXPECT_EQ(toStringView(GraphicsBackendMaturity::Production), "Production");
    EXPECT_EQ(toStringView(GraphicsBackendMaturity::Supported), "Supported");
    EXPECT_EQ(toStringView(GraphicsBackendMaturity::Experimental), "Experimental");
    EXPECT_EQ(toStringView(GraphicsBackendMaturity::Historical), "Historical");
    EXPECT_EQ(toStringView(GraphicsBackendMaturity::Deprecated), "Deprecated");
}

namespace
{
    // The one authoritative expected maturity per public GraphicsRendererType member, kept
    // independent of GraphicsBackendMaturity.hpp's own switch so a drift between the two is
    // caught by EveryPublicRendererHasAClassifiedMaturity below instead of passing vacuously.
    // Deliberately has NO `default:` label -- see GraphicsRendererTypeTests.cpp for why: this
    // project compiles without -Wswitch, so a missing arm needs a runtime guard instead of relying
    // on a compiler diagnostic. std::optional's empty state is that guard (there is no natural
    // "invalid" GraphicsBackendMaturity enumerator to fall back to, unlike a std::string_view).
    constexpr std::optional<GraphicsBackendMaturity> ExpectedMaturityFor(GraphicsRendererType type)
    {
        switch (type)
        {
            case GraphicsRendererType::SdlRenderer:
            case GraphicsRendererType::OpenGLES2:
            case GraphicsRendererType::OpenGLES3:
            case GraphicsRendererType::OpenGL33:
            case GraphicsRendererType::Bgfx:
            case GraphicsRendererType::Vulkan:
            case GraphicsRendererType::DirectX9:
            case GraphicsRendererType::DirectX11:
            case GraphicsRendererType::DirectX12:
                return GraphicsBackendMaturity::Production;

            case GraphicsRendererType::WebGL1:
            case GraphicsRendererType::WebGL2:
            case GraphicsRendererType::Headless:
            case GraphicsRendererType::Stub:
            case GraphicsRendererType::Direct2D:
            case GraphicsRendererType::Canvas:
            case GraphicsRendererType::HtmlDom:
            case GraphicsRendererType::SdlGpu:
            case GraphicsRendererType::OpenGL4:
            case GraphicsRendererType::OpenGL2:
            case GraphicsRendererType::Gdi:
            case GraphicsRendererType::Metal:
                return GraphicsBackendMaturity::Supported;

            case GraphicsRendererType::WebGPU:
            case GraphicsRendererType::Software:
            case GraphicsRendererType::FreeDirect:
            case GraphicsRendererType::Fna3d:
            case GraphicsRendererType::SvgDom:
            case GraphicsRendererType::PortableGL:
                return GraphicsBackendMaturity::Experimental;

            case GraphicsRendererType::DirectX1:
            case GraphicsRendererType::DirectX2:
            case GraphicsRendererType::DirectX3:
            case GraphicsRendererType::DirectX5:
            case GraphicsRendererType::DirectX6:
            case GraphicsRendererType::DirectX7:
            case GraphicsRendererType::DirectX8:
            case GraphicsRendererType::DirectX10:
            case GraphicsRendererType::OpenGLES1:
            case GraphicsRendererType::OpenGL1:
            case GraphicsRendererType::Glide:
                return GraphicsBackendMaturity::Historical;
        }
        return std::nullopt;
    }
}

TEST(GraphicsBackendMaturityTest, EveryPublicRendererHasAClassifiedMaturity)
{
    for (int i = 0; i < kPublicRendererCount; ++i)
    {
        const auto type = static_cast<GraphicsRendererType>(i);
        const auto expected = ExpectedMaturityFor(type);
        ASSERT_TRUE(expected.has_value()) << "missing maturity for GraphicsRendererType value " << i;
        EXPECT_EQ(getGraphicsBackendMaturity(type), *expected)
            << "GraphicsBackendMaturity.hpp disagrees with this test's own table for value " << i;
    }
}

TEST(GraphicsBackendMaturityTest, MaturityMatchesForCurrentRenderer)
{
    const auto type = getCurrentGraphicsRendererType();
    const auto expected = ExpectedMaturityFor(type);
    ASSERT_TRUE(expected.has_value())
        << "no expected-maturity arm for the active renderer -- add it to ExpectedMaturityFor() in this file";
    EXPECT_EQ(getCurrentGraphicsBackendMaturity(), *expected);
}

TEST(GraphicsBackendMaturityTest, RepeatedCallsAreStable)
{
    EXPECT_EQ(getCurrentGraphicsBackendMaturity(), getCurrentGraphicsBackendMaturity());
}

TEST(GraphicsBackendMaturityTest, RuntimeValueMatchesCompileTimeConstant)
{
    EXPECT_EQ(getCurrentGraphicsBackendMaturity(), kCompileTimeMaturity);
}
