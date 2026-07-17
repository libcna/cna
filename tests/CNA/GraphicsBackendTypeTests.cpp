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

TEST(GraphicsBackendTypeTest, NameMatchesTypeForEveryBackend)
{
    // Every call in this build returns the SAME compile-time-selected backend; this checks
    // that the (type, name) pair returned is internally consistent (the name matches what this
    // specific type is documented to map to), not that a specific backend is active.
    const auto type = getCurrentGraphicsBackendType();
    const auto name = getCurrentGraphicsBackendName();

    switch (type)
    {
        case GraphicsBackendType::SdlRenderer: EXPECT_EQ(name, "SDL_RENDERER"); break;
        case GraphicsBackendType::EasyGL:      EXPECT_EQ(name, "EASYGL");       break;
        case GraphicsBackendType::Bgfx:        EXPECT_EQ(name, "BGFX");         break;
        case GraphicsBackendType::Vulkan:      EXPECT_EQ(name, "VULKAN");       break;
        case GraphicsBackendType::WebGPU:      EXPECT_EQ(name, "WEBGPU");       break;
        case GraphicsBackendType::Headless:    EXPECT_EQ(name, "HEADLESS");     break;
        case GraphicsBackendType::Software:    EXPECT_EQ(name, "SOFTWARE");     break;
        case GraphicsBackendType::D3D11:       EXPECT_EQ(name, "D3D11");        break;
        case GraphicsBackendType::D3D12:       EXPECT_EQ(name, "D3D12");        break;
        case GraphicsBackendType::Canvas:      EXPECT_EQ(name, "CANVAS");       break;
        case GraphicsBackendType::Ascii:       EXPECT_EQ(name, "ASCII");        break;
        case GraphicsBackendType::Dx3:         EXPECT_EQ(name, "DX3");          break;
        case GraphicsBackendType::D3D9:        EXPECT_EQ(name, "D3D9");         break;
        case GraphicsBackendType::SdlGpu:      EXPECT_EQ(name, "SDL_GPU");      break;
    }
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
