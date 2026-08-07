#include <gtest/gtest.h>
#include "CNA/GraphicsBackendType.hpp"

using CNA::GraphicsBackendType;
using CNA::getCurrentGraphicsBackendType;
using CNA::getCurrentGraphicsBackendName;

// Both functions are constexpr -- usable directly in a constant expression, not just at
// runtime. This is the actual point of making them constexpr (they don't take a GraphicsDevice
// argument, so unlike the GraphicsDevice::GetGraphicsBackendType()/Name() member wrappers,
// these two are genuinely evaluable at compile time).
static_assert(getCurrentGraphicsBackendType() == getCurrentGraphicsBackendType());
static_assert(!getCurrentGraphicsBackendName().empty());
constexpr GraphicsBackendType kCompileTimeType = getCurrentGraphicsBackendType();
constexpr std::string_view kCompileTimeName = getCurrentGraphicsBackendName();

TEST(GraphicsBackendTypeTest, GetCurrentGraphicsBackendTypeDoesNotThrow)
{
    EXPECT_NO_THROW({ (void)getCurrentGraphicsBackendType(); });
}

TEST(GraphicsBackendTypeTest, GetCurrentGraphicsBackendNameDoesNotThrow)
{
    EXPECT_NO_THROW({ (void)getCurrentGraphicsBackendName(); });
}

TEST(GraphicsBackendTypeTest, GetCurrentGraphicsBackendNameIsNotEmpty)
{
    EXPECT_FALSE(getCurrentGraphicsBackendName().empty());
}

namespace
{
    // The one authoritative expected name per public GraphicsBackendType member.
    //
    // Deliberately has NO `default:` label, so an enum member with no arm here falls through to
    // the empty return below, which NameMatchesTypeForEveryBackend turns into a hard failure
    // rather than a silent pass. That runtime guard is what this test relies on: the project
    // compiles with `-g -std=c++23` and does not enable `-Wall`/`-Wswitch`, so a missing arm
    // produces no compiler diagnostic. Omitting `default:` additionally means the switch WOULD
    // be flagged at compile time by any build that does turn those warnings on.
    //
    // The previous form of this test switched directly on the active backend with only 15 of the
    // enum's members listed and no failure arm, so every build selecting one of the other members
    // executed no assertion at all and reported a vacuous pass.
    constexpr std::string_view ExpectedNameFor(GraphicsBackendType type)
    {
        switch (type)
        {
            case GraphicsBackendType::SdlRenderer: return "SDL_RENDERER";
            case GraphicsBackendType::OpenGLES:    return "OPENGLES";
            case GraphicsBackendType::OpenGL33:    return "OPENGL33";
            case GraphicsBackendType::WebGL1:      return "WEBGL1";
            case GraphicsBackendType::WebGL2:      return "WEBGL2";
            case GraphicsBackendType::Bgfx:        return "BGFX";
            case GraphicsBackendType::Vulkan:      return "VULKAN";
            case GraphicsBackendType::WebGPU:      return "WEBGPU";
            case GraphicsBackendType::Magnum:      return "MAGNUM";
            case GraphicsBackendType::Headless:    return "HEADLESS";
            case GraphicsBackendType::Software:    return "SOFTWARE";
            case GraphicsBackendType::D3D11:       return "D3D11";
            case GraphicsBackendType::D3D12:       return "D3D12";
            case GraphicsBackendType::Canvas:      return "CANVAS";
            case GraphicsBackendType::Skia:        return "SKIA";
            case GraphicsBackendType::Ascii:       return "ASCII";
            case GraphicsBackendType::FreeDirect:  return "FREEDIRECT";
            case GraphicsBackendType::Stub:        return "STUB";
            case GraphicsBackendType::D3D9:        return "D3D9";
            case GraphicsBackendType::Dx1:         return "DX1";
            case GraphicsBackendType::Dx2:         return "DX2";
            case GraphicsBackendType::Dx3:         return "DX3";
            case GraphicsBackendType::Dx5:         return "DX5";
            case GraphicsBackendType::Dx6:         return "DX6";
            case GraphicsBackendType::Dx7:         return "DX7";
            case GraphicsBackendType::Dx8:         return "DX8";
            case GraphicsBackendType::D3D10:       return "D3D10";
            case GraphicsBackendType::SdlGpu:      return "SDL_GPU";
            case GraphicsBackendType::OpenGLES1:   return "OPENGLES1";
            case GraphicsBackendType::OpenGL4:     return "OPENGL4";
            case GraphicsBackendType::OpenGL1:     return "OPENGL1";
            case GraphicsBackendType::OpenGL2:     return "OPENGL2";
            case GraphicsBackendType::Wicked:      return "WICKED";
            case GraphicsBackendType::Sokol:       return "SOKOL";
            case GraphicsBackendType::Diligent:    return "DILIGENT";
        }
        return {};
    }
}

TEST(GraphicsBackendTypeTest, NameMatchesTypeForEveryBackend)
{
    // Every call in this build returns the SAME compile-time-selected backend, so one build
    // checks one arm; the 31 backend builds between them cover the whole enum. What this asserts
    // is that the (type, name) pair is internally consistent AND that the active backend has an
    // expected-name arm at all -- a backend with no arm fails here instead of passing vacuously.
    const auto type = getCurrentGraphicsBackendType();
    const auto name = getCurrentGraphicsBackendName();

    const std::string_view expected = ExpectedNameFor(type);
    ASSERT_FALSE(expected.empty())
        << "no expected-name arm for the active backend (getCurrentGraphicsBackendName() reports \""
        << name << "\") -- add it to ExpectedNameFor() in this file";
    EXPECT_EQ(name, expected);
}

TEST(GraphicsBackendTypeTest, RepeatedCallsAreStable)
{
    EXPECT_EQ(getCurrentGraphicsBackendType(), getCurrentGraphicsBackendType());
    EXPECT_EQ(getCurrentGraphicsBackendName(), getCurrentGraphicsBackendName());
}

TEST(GraphicsBackendTypeTest, RuntimeValueMatchesCompileTimeConstant)
{
    EXPECT_EQ(getCurrentGraphicsBackendType(), kCompileTimeType);
    EXPECT_EQ(getCurrentGraphicsBackendName(), kCompileTimeName);
}
