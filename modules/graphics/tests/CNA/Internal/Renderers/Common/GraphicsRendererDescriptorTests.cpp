// plan_runtimerenderer.md RTR-P0-7: contract tests for the runtime-dispatch value types.
//
// These types carry no behaviour of their own beyond the window-kind compatibility rule, so what
// is worth pinning is exactly that rule, the trivial-copyability the generated registry table
// depends on, and the completeness of the name table every diagnostic message is built from.

#include <gtest/gtest.h>

#include "CNA/GraphicsRendererFallbackRecord.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"

#include <array>
#include <string_view>
#include <type_traits>

using CNA::GraphicsRendererFallbackReason;
using CNA::GraphicsRendererFallbackRecord;
using CNA::GraphicsRendererType;
using CNA::Internal::Renderers::AreWindowKindsCompatible;
using CNA::Internal::Renderers::GraphicsRendererDescriptor;
using CNA::Internal::Renderers::RendererPreWindowRequest;
using CNA::Internal::Renderers::RendererWindowKind;

namespace
{
    /// Every public identity, in enum order. Kept in one place so the name-table test below cannot
    /// silently pass by testing fewer identities than exist; scripts/check_renderer_identities.py
    /// is what pins the count itself at 46.
    constexpr std::array<GraphicsRendererType, 46> AllRendererTypes{
        GraphicsRendererType::SdlRenderer, GraphicsRendererType::OpenGLES2,
        GraphicsRendererType::OpenGLES3,   GraphicsRendererType::OpenGL33,
        GraphicsRendererType::WebGL1,      GraphicsRendererType::WebGL2,
        GraphicsRendererType::Bgfx,        GraphicsRendererType::Vulkan,
        GraphicsRendererType::WebGPU,      GraphicsRendererType::Magnum,
        GraphicsRendererType::Headless,    GraphicsRendererType::Software,
        GraphicsRendererType::Stub,        GraphicsRendererType::DirectX11,
        GraphicsRendererType::DirectX12,   GraphicsRendererType::Direct2D,
        GraphicsRendererType::Canvas,      GraphicsRendererType::HtmlDom,
        GraphicsRendererType::Skia,        GraphicsRendererType::Blend2D,
        GraphicsRendererType::FreeDirect,  GraphicsRendererType::DirectX9,
        GraphicsRendererType::DirectX1,    GraphicsRendererType::DirectX2,
        GraphicsRendererType::DirectX3,    GraphicsRendererType::DirectX5,
        GraphicsRendererType::DirectX6,    GraphicsRendererType::DirectX7,
        GraphicsRendererType::DirectX8,    GraphicsRendererType::DirectX10,
        GraphicsRendererType::SdlGpu,      GraphicsRendererType::OpenGLES1,
        GraphicsRendererType::OpenGL4,     GraphicsRendererType::OpenGL1,
        GraphicsRendererType::OpenGL2,     GraphicsRendererType::Wicked,
        GraphicsRendererType::Sokol,       GraphicsRendererType::Diligent,
        GraphicsRendererType::Glide,       GraphicsRendererType::Gdi,
        GraphicsRendererType::Llgl,        GraphicsRendererType::Metal,
        GraphicsRendererType::Fna3d,       GraphicsRendererType::SvgDom,
        GraphicsRendererType::OpenVg,      GraphicsRendererType::PortableGL,
    };

    constexpr std::array<RendererWindowKind, 5> AllWindowKinds{
        RendererWindowKind::None,   RendererWindowKind::Plain, RendererWindowKind::OpenGL,
        RendererWindowKind::Vulkan, RendererWindowKind::Metal,
    };
}

// ---------------------------------------------------------------------------
// GraphicsRendererDescriptor
// ---------------------------------------------------------------------------

TEST(GraphicsRendererDescriptorTest, IsTriviallyCopyable)
{
    // The generated registry (design decision 3) is a plain static array of these. Anything that
    // needed a non-trivial constructor would reintroduce exactly the static-initialization order
    // problem that design chose to avoid.
    static_assert(std::is_trivially_copyable_v<GraphicsRendererDescriptor>);
    static_assert(std::is_trivially_copyable_v<RendererPreWindowRequest>);
    SUCCEED();
}

TEST(GraphicsRendererDescriptorTest, DefaultsAreInertRatherThanPlausible)
{
    // A default-constructed descriptor must not look like a usable renderer: every hook is null
    // and it claims neither a window nor the video subsystem, so a registry entry someone forgot
    // to populate fails loudly at its first use instead of half-working.
    constexpr GraphicsRendererDescriptor descriptor{};
    EXPECT_FALSE(descriptor.needsWindow);
    EXPECT_FALSE(descriptor.needsVideoSubsystem);
    EXPECT_EQ(descriptor.windowKind, RendererWindowKind::None);
    // MERGE (plan_platform.md PLAT-8): the two pre-window hooks became data. An inert default now
    // means "asks the platform for nothing", which is the same guarantee one step earlier: a
    // forgotten registry entry cannot silently acquire a high-density backing or a GL visual.
    EXPECT_FALSE(descriptor.wantsHighDpi);
    EXPECT_EQ(descriptor.glFramebuffer.depthBits, 0);
    EXPECT_EQ(descriptor.glFramebuffer.stencilBits, 0);
    EXPECT_FALSE(descriptor.glFramebuffer.doubleBuffered);
    EXPECT_FALSE(descriptor.glFramebuffer.wantsMultiSample);
    EXPECT_FALSE(descriptor.needsSurfacePresenter);
    EXPECT_FALSE(descriptor.needsGlContext);
    EXPECT_FALSE(descriptor.needsVulkanSurface);
    EXPECT_EQ(descriptor.isAvailable, nullptr);
    EXPECT_EQ(descriptor.create, nullptr);
}

TEST(GraphicsRendererDescriptorTest, PreWindowRequestDefaultsMatchPresentationDefaults)
{
    constexpr RendererPreWindowRequest request{};
    EXPECT_EQ(request.backBufferWidth, 0);
    EXPECT_EQ(request.backBufferHeight, 0);
    EXPECT_EQ(request.multiSampleCount, 1);
    EXPECT_EQ(request.backBufferFormat, 0);
    EXPECT_EQ(request.depthStencilFormat, 0);
    EXPECT_FALSE(request.isFullScreen);
}

// ---------------------------------------------------------------------------
// Window-kind compatibility (plan_runtimerenderer.md design decision 8)
// ---------------------------------------------------------------------------

TEST(RendererWindowKindTest, IdenticalKindsAreAlwaysCompatible)
{
    for (const RendererWindowKind kind : AllWindowKinds)
    {
        EXPECT_TRUE(AreWindowKindsCompatible(kind, kind))
            << "kind ordinal " << static_cast<int>(kind);
    }
}

TEST(RendererWindowKindTest, CandidateNeedingNoWindowAcceptsAnyExistingWindow)
{
    // A window-free renderer never looks at the window it was handed, so it can always follow any
    // other renderer without the window being recreated.
    for (const RendererWindowKind existing : AllWindowKinds)
    {
        EXPECT_TRUE(AreWindowKindsCompatible(existing, RendererWindowKind::None))
            << "existing ordinal " << static_cast<int>(existing);
    }
}

TEST(RendererWindowKindTest, CandidateNeedingAWindowCannotAdoptAWindowThatWasNeverCreated)
{
    for (const RendererWindowKind candidate : AllWindowKinds)
    {
        if (candidate == RendererWindowKind::None)
            continue;
        EXPECT_FALSE(AreWindowKindsCompatible(RendererWindowKind::None, candidate))
            << "candidate ordinal " << static_cast<int>(candidate);
    }
}

TEST(RendererWindowKindTest, OpenGLAndVulkanNeverShareAWindow)
{
    // The concrete case design decision 8 exists for: SDL3 rejects a window created with both
    // an OpenGL and a Vulkan intent, so this crossing always costs a window recreation.
    EXPECT_FALSE(AreWindowKindsCompatible(RendererWindowKind::OpenGL, RendererWindowKind::Vulkan));
    EXPECT_FALSE(AreWindowKindsCompatible(RendererWindowKind::Vulkan, RendererWindowKind::OpenGL));
}

TEST(RendererWindowKindTest, PlainWindowIsNotInterchangeableWithAnApiWindow)
{
    EXPECT_FALSE(AreWindowKindsCompatible(RendererWindowKind::Plain, RendererWindowKind::OpenGL));
    EXPECT_FALSE(AreWindowKindsCompatible(RendererWindowKind::OpenGL, RendererWindowKind::Plain));
    EXPECT_FALSE(AreWindowKindsCompatible(RendererWindowKind::Plain, RendererWindowKind::Metal));
}

TEST(RendererWindowKindTest, CompatibilityIsUsableInAConstantExpression)
{
    static_assert(AreWindowKindsCompatible(RendererWindowKind::OpenGL, RendererWindowKind::OpenGL));
    static_assert(!AreWindowKindsCompatible(RendererWindowKind::OpenGL, RendererWindowKind::Vulkan));
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Fallback record
// ---------------------------------------------------------------------------

TEST(GraphicsRendererFallbackRecordTest, CarriesTypeReasonAndMessage)
{
    const GraphicsRendererFallbackRecord record{
        GraphicsRendererType::Vulkan,
        GraphicsRendererFallbackReason::InitializationFailed,
        "vkCreateInstance failed"};

    EXPECT_EQ(record.type, GraphicsRendererType::Vulkan);
    EXPECT_EQ(record.reason, GraphicsRendererFallbackReason::InitializationFailed);
    EXPECT_EQ(record.message, "vkCreateInstance failed");
}

TEST(GraphicsRendererFallbackRecordTest, EveryReasonHasItsOwnName)
{
    constexpr std::array<GraphicsRendererFallbackReason, 4> reasons{
        GraphicsRendererFallbackReason::NotCompiledIn,
        GraphicsRendererFallbackReason::ProbeUnavailable,
        GraphicsRendererFallbackReason::InitializationFailed,
        GraphicsRendererFallbackReason::WindowKindConflict,
    };

    for (std::size_t i = 0; i < reasons.size(); ++i)
    {
        const std::string_view name = CNA::getGraphicsRendererFallbackReasonName(reasons[i]);
        EXPECT_FALSE(name.empty());
        EXPECT_NE(name, "UNKNOWN");
        for (std::size_t j = i + 1; j < reasons.size(); ++j)
        {
            EXPECT_NE(name, CNA::getGraphicsRendererFallbackReasonName(reasons[j]));
        }
    }
}

// ---------------------------------------------------------------------------
// Name table (RTR-P7-5: the 46 names must exist exactly once, and be complete)
// ---------------------------------------------------------------------------

TEST(GraphicsRendererNameTest, EveryIdentityHasItsOwnNonPlaceholderName)
{
    for (std::size_t i = 0; i < AllRendererTypes.size(); ++i)
    {
        const std::string_view name = CNA::getGraphicsRendererName(AllRendererTypes[i]);
        EXPECT_FALSE(name.empty()) << "ordinal " << i;
        EXPECT_NE(name, "UNKNOWN") << "ordinal " << i;
        for (std::size_t j = i + 1; j < AllRendererTypes.size(); ++j)
        {
            EXPECT_NE(name, CNA::getGraphicsRendererName(AllRendererTypes[j]))
                << "ordinals " << i << " and " << j << " share the name " << name;
        }
    }
}

TEST(GraphicsRendererNameTest, CurrentNameDelegatesToTheSharedTable)
{
    // getCurrentGraphicsRendererName() must not carry a second copy of the table -- it is the
    // by-identity accessor applied to the compile-time identity, and stays a constant expression.
    static_assert(CNA::getCurrentGraphicsRendererName()
                  == CNA::getGraphicsRendererName(CNA::getCurrentGraphicsRendererType()));
    SUCCEED();
}
