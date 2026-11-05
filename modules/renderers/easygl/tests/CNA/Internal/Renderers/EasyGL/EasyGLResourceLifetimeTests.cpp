// SPDX-License-Identifier: MS-PL
// TQ-073: destructor GL calls belong to the context that owns the objects. Observe the
// renderer's native loader so a stale call fails deterministically even on forgiving drivers.
#include <gtest/gtest.h>
#include <metagl/metagl.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <tuple>
#include <stdexcept>
#include <string>
#include <vector>
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
#include "CNA/TestSupport/TestPaths.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include <fstream>
#include <iterator>
#endif

namespace
{
using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::EasyGL;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Testing::Renderers;

struct NativeLifetimeObserver
{
    using DeleteMany = void (GL_APIENTRY *)(GLsizei, const GLuint*);
    using DeleteOne = void (GL_APIENTRY *)(GLuint);
    using EndQuery = void (GL_APIENTRY *)(GLenum);
    using DetachShader = void (GL_APIENTRY *)(GLuint, GLuint);
    using IsTexture = GLboolean (GL_APIENTRY *)(GLuint);
    static inline NativeLifetimeObserver* active = nullptr;
    CNA::Platform::IPlatformGlContext& native;
    CNA::Platform::GlProcAddressLoader loader = nullptr;
    CNA::Platform::GlContextHandle context = nullptr;
    CNA::Platform::WindowId window = 0;
    int destroyedContexts = 0;
    int invalidCalls = 0;
    int validCalls = 0;
    int deletedTextures = 0;
    bool alive = false;
    bool intercept = true;

    explicit NativeLifetimeObserver(CNA::Platform::IPlatformGlContext& service) : native(service)
    {
        loader = native.GetProcAddressLoader();
        active = this;
    }
    ~NativeLifetimeObserver() { active = nullptr; }

    bool MayCall()
    {
        // After a failure, do not jump through an entry point from an unloaded driver.
        if (!alive || native.GetCurrentBinding().context != context)
        {
            ++invalidCalls;
            return false;
        }
        ++validCalls;
        return true;
    }

#define TQ_OBSERVE_MANY(Name) \
    static inline DeleteMany real##Name = nullptr; \
    static void GL_APIENTRY Name(GLsizei count, const GLuint* names) \
    { if (active->MayCall()) real##Name(count, names); }
    TQ_OBSERVE_MANY(glDeleteBuffers)
    TQ_OBSERVE_MANY(glDeleteVertexArrays)
    TQ_OBSERVE_MANY(glDeleteFramebuffers)
    TQ_OBSERVE_MANY(glDeleteRenderbuffers)
    TQ_OBSERVE_MANY(glDeleteQueries)
    TQ_OBSERVE_MANY(glDeleteSamplers)
#undef TQ_OBSERVE_MANY
    static inline DeleteMany realglDeleteTextures = nullptr;
    static void GL_APIENTRY glDeleteTextures(GLsizei count, const GLuint* names)
    {
        if (active->MayCall())
        {
            active->deletedTextures += count;
            realglDeleteTextures(count, names);
        }
    }
#define TQ_OBSERVE_ONE(Name) \
    static inline DeleteOne real##Name = nullptr; \
    static void GL_APIENTRY Name(GLuint name) \
    { if (active->MayCall()) real##Name(name); }
    TQ_OBSERVE_ONE(glDeleteProgram)
    TQ_OBSERVE_ONE(glDeleteShader)
    TQ_OBSERVE_ONE(glUseProgram)
#undef TQ_OBSERVE_ONE
    static inline EndQuery realglEndQuery = nullptr;
    static void GL_APIENTRY glEndQuery(GLenum target)
    {
        if (active->MayCall()) realglEndQuery(target);
    }
    static inline DetachShader realglDetachShader = nullptr;
    static void GL_APIENTRY glDetachShader(GLuint program, GLuint shader)
    {
        if (active->MayCall()) realglDetachShader(program, shader);
    }
    static void* Load(const char* name)
    {
        void* real = active->loader(name);
        if (!active->intercept || real == nullptr) return real;
#define TQ_LOAD(Name, Type) \
        if (std::strcmp(name, #Name) == 0) \
        { real##Name = reinterpret_cast<Type>(real); return reinterpret_cast<void*>(&Name); }
        TQ_LOAD(glDeleteTextures, DeleteMany)
        TQ_LOAD(glDeleteBuffers, DeleteMany)
        TQ_LOAD(glDeleteVertexArrays, DeleteMany)
        TQ_LOAD(glDeleteFramebuffers, DeleteMany)
        TQ_LOAD(glDeleteRenderbuffers, DeleteMany)
        TQ_LOAD(glDeleteQueries, DeleteMany)
        TQ_LOAD(glDeleteSamplers, DeleteMany)
        TQ_LOAD(glDeleteProgram, DeleteOne)
        TQ_LOAD(glDeleteShader, DeleteOne)
        TQ_LOAD(glUseProgram, DeleteOne)
        TQ_LOAD(glEndQuery, EndQuery)
        TQ_LOAD(glDetachShader, DetachShader)
#undef TQ_LOAD
        return real;
    }
};

class ObservedGlContext final : public CNA::Platform::IPlatformGlContext
{
public:
    NativeLifetimeObserver observer;
    explicit ObservedGlContext(IPlatformGlContext& inner) : observer(inner) {}
    CNA::Platform::GlContextHandle CreateContext(CNA::Platform::WindowId window,
        const CNA::Platform::GlContextDescription& description) override
    {
        observer.window = window;
        observer.context = observer.native.CreateContext(window, description);
        observer.alive = true;
        return observer.context;
    }
    void DestroyContext(CNA::Platform::GlContextHandle context) override
    {
        observer.native.DestroyContext(context);
        observer.alive = false;
        ++observer.destroyedContexts;
    }
    void MakeCurrent(CNA::Platform::WindowId window, CNA::Platform::GlContextHandle context) override
    { observer.native.MakeCurrent(window, context); }
    CNA::Platform::GlContextBinding GetCurrentBinding() const override
    { return observer.native.GetCurrentBinding(); }
    void SwapBuffers(CNA::Platform::WindowId window) override { observer.native.SwapBuffers(window); }
    bool SetSwapInterval(int interval) override { return observer.native.SetSwapInterval(interval); }
    void* GetProcAddress(const std::string& name) const override
    { return NativeLifetimeObserver::Load(name.c_str()); }
    CNA::Platform::GlProcAddressLoader GetProcAddressLoader() const override
    {
        NativeLifetimeObserver::active->loader = observer.native.GetProcAddressLoader();
        return &NativeLifetimeObserver::Load;
    }
    CNA::Platform::GlContextDescription GetContextAttributes(CNA::Platform::GlContextHandle context) const override
    { return observer.native.GetContextAttributes(context); }
};

class ObservedGlPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
{
public:
    std::unique_ptr<ObservedGlContext> context;
    ObservedGlPlatform()
    {
        if (auto* service = GetInner().GetGlContext())
            context = std::make_unique<ObservedGlContext>(*service);
    }
    CNA::Platform::IPlatformGlContext* GetGlContext() override { return context.get(); }
};

class EasyGLResourceLifetimeTest : public ::testing::Test
{
protected:
    // Declare the platform before its installation, and the device last: the observer and
    // real platform service remain alive until every resource and the device have gone.
    std::unique_ptr<ObservedGlPlatform> platform;
    std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;
    std::unique_ptr<GraphicsDevice> device;
    EasyGLRenderer* renderer = nullptr;
    void SetUp() override
    {
        CNA_SKIP_IF_RENDERER_IS_NONE_OF(OpenGL33, OpenGLES3);
        platform = std::make_unique<ObservedGlPlatform>();
        ASSERT_NE(platform->context, nullptr);
        installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(*platform);
        device = std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
            GraphicsProfile::HiDef, PresentationParameters::CreateWindowedEXT(64, 64));
        renderer = dynamic_cast<EasyGLRenderer*>(&device->GetRenderer());
        ASSERT_NE(renderer, nullptr);
    }
    NativeLifetimeObserver& Observe() { return platform->context->observer; }
};

enum class ResourceKind
{
    Texture2D, Texture3D, TextureCube, Target2D, Target2DMSAA, TargetCube, TargetCubeMSAA,
    VertexBuffer, Index16, Index32, ShaderEffect, Query, ActiveQuery, SpriteBatch
};
const char* Name(ResourceKind kind)
{
    static constexpr const char* names[] = {"Texture2D", "Texture3D", "TextureCube", "Target2D",
        "Target2DMSAA", "TargetCube", "TargetCubeMSAA", "VertexBuffer", "Index16", "Index32",
        "ShaderEffect", "Query", "ActiveQuery", "SpriteBatch"};
    return names[static_cast<int>(kind)];
}

std::shared_ptr<void> CreateResource(EasyGLRenderer& renderer, ResourceKind kind)
{
    switch (kind)
    {
    case ResourceKind::Texture2D:
        return renderer.CreateTexture({2, 2, std::vector<std::uint8_t>(16, 255)});
    case ResourceKind::Texture3D: return renderer.CreateTexture3D(2, 2, 2, false, 0);
    case ResourceKind::TextureCube: return renderer.CreateTextureCube(2, false, 0);
    case ResourceKind::Target2D:
    case ResourceKind::Target2DMSAA:
    {
        auto target = renderer.CreateRenderTarget2D(4, 4, 3, false, true,
            kind == ResourceKind::Target2DMSAA ? 4 : 0);
        if (kind == ResourceKind::Target2DMSAA && target->GetMultiSampleCount() <= 0)
            throw std::runtime_error("MSAA lifetime fixture requires applied samples");
        renderer.SetRenderTarget2D(target.get()); // Leave a bound target alive across teardown.
        return target;
    }
    case ResourceKind::TargetCube:
    case ResourceKind::TargetCubeMSAA:
    {
        auto target = renderer.CreateRenderTargetCube(4, 3, false, true,
            kind == ResourceKind::TargetCubeMSAA ? 4 : 0);
        if (kind == ResourceKind::TargetCubeMSAA && target->GetMultiSampleCount() <= 0)
            throw std::runtime_error("cube MSAA lifetime fixture requires applied samples");
        renderer.SetRenderTargetCubeFace(target.get(), 5);
        return target;
    }
    case ResourceKind::VertexBuffer: return renderer.CreateVertexBuffer(3);
    case ResourceKind::Index16: return renderer.CreateIndexBuffer16(3);
    case ResourceKind::Index32: return renderer.CreateIndexBuffer32(3);
    case ResourceKind::ShaderEffect:
    {
        auto effect = renderer.CreateEffectRenderer(
            "#version 300 es\nprecision highp float;\nlayout(location=0) in vec2 p;\n"
            "void main(){gl_Position=vec4(p,0.0,1.0);}",
            "#version 300 es\nprecision mediump float;\nout vec4 c;\n"
            "void main(){c=vec4(1.0);}");
        if (!effect->IsValid()) throw std::runtime_error("lifetime fixture shader did not link");
        return effect;
    }
    case ResourceKind::Query: return renderer.CreateOcclusionQuery();
    case ResourceKind::ActiveQuery:
    {
        auto query = renderer.CreateOcclusionQuery();
        query->Begin(); // Also exercises the destructor's glEndQuery path.
        return query;
    }
    case ResourceKind::SpriteBatch: return renderer.CreateSpriteBatch();
    }
    throw std::runtime_error("unknown lifetime resource kind");
}

class EasyGLEscapedResourceTest : public EasyGLResourceLifetimeTest,
    public ::testing::WithParamInterface<std::tuple<ResourceKind, bool>> {};
}

TEST_P(EasyGLEscapedResourceTest, LastReferenceReleasedAfterDeviceMakesNoGlCall)
{
    const auto [kind, recovery] = GetParam();
    device->SetContextRecoveryEnabled(recovery);
    std::shared_ptr<void> escaped = CreateResource(*renderer, kind);
    ASSERT_NE(escaped, nullptr);
    std::weak_ptr<void> lifetime = escaped;
    device.reset();
    ASSERT_EQ(Observe().destroyedContexts, 1);
    ASSERT_FALSE(Observe().alive);
    ASSERT_FALSE(lifetime.expired());
    EXPECT_EQ(Observe().invalidCalls, 0) << "teardown must bind the owning context";
    const int callsBeforeRelease = Observe().validCalls;
    escaped.reset();
    // Oracle: no GL operation is valid after its owning context was destroyed. Intercepting
    // stale deletion/end calls makes this fail even if a driver would otherwise tolerate them.
    EXPECT_TRUE(lifetime.expired());
    EXPECT_EQ(Observe().invalidCalls, 0) << Name(kind);
    EXPECT_EQ(Observe().validCalls, callsBeforeRelease);
}

TEST_P(EasyGLEscapedResourceTest, LastReferenceReleasedBeforeDeviceDeletesGlObjects)
{
    const auto [kind, recovery] = GetParam();
    device->SetContextRecoveryEnabled(recovery);
    auto resource = CreateResource(*renderer, kind);
    ASSERT_NE(resource, nullptr);
    const int before = Observe().validCalls;
    resource.reset();
    // Oracle: an ordinary live-context resource destruction must issue native deletion,
    // so an overbroad fix that always abandons handles cannot satisfy the late-release tests.
    EXPECT_TRUE(Observe().alive);
    EXPECT_GT(Observe().validCalls, before) << Name(kind);
    EXPECT_EQ(Observe().invalidCalls, 0);
}

INSTANTIATE_TEST_SUITE_P(EveryEasyGLKind, EasyGLEscapedResourceTest,
    ::testing::Combine(::testing::Values(ResourceKind::Texture2D, ResourceKind::Texture3D,
        ResourceKind::TextureCube, ResourceKind::Target2D, ResourceKind::Target2DMSAA,
        ResourceKind::TargetCube, ResourceKind::TargetCubeMSAA, ResourceKind::VertexBuffer,
        ResourceKind::Index16, ResourceKind::Index32, ResourceKind::ShaderEffect,
        ResourceKind::Query, ResourceKind::ActiveQuery, ResourceKind::SpriteBatch),
        ::testing::Bool()),
    [](const ::testing::TestParamInfo<EasyGLEscapedResourceTest::ParamType>& info)
    {
        return std::string(Name(std::get<0>(info.param))) +
            (std::get<1>(info.param) ? "RecoveryOn" : "RecoveryOff");
    });

TEST_F(EasyGLResourceLifetimeTest, ResourceDestroyedBeforeDeviceDeletesItsNativeTexture)
{
    auto resource = renderer->CreateTexture({2, 2, std::vector<std::uint8_t>(16, 255)});
    auto* texture = dynamic_cast<EasyGLTextureRenderer*>(resource.get());
    ASSERT_NE(texture, nullptr);
    const GLuint name = texture->texture.native_handle();
    auto isTexture = reinterpret_cast<NativeLifetimeObserver::IsTexture>(Observe().loader("glIsTexture"));
    ASSERT_NE(isTexture, nullptr);
    ASSERT_NE(name, 0u);
    ASSERT_EQ(isTexture(name), GL_TRUE);
    const int deletions = Observe().deletedTextures;
    resource.reset();
    // Oracle: deleting this one texture while its context is alive sends one deletion and
    // removes that native name. Merely forgetting every handle would leak until device teardown.
    EXPECT_TRUE(Observe().alive);
    EXPECT_EQ(Observe().deletedTextures, deletions + 1);
    EXPECT_EQ(isTexture(name), GL_FALSE);
    EXPECT_EQ(Observe().invalidCalls, 0);
}

TEST_F(EasyGLResourceLifetimeTest, TeardownRebindsTheContextBeforeDeletingOwnerObjects)
{
    // GraphicsDevice already leases the context during Dispose. Exercise the internal owner
    // directly so that its own guarantee is tested independently of that caller's lease.
    device.reset();
    struct VideoScope
    {
        CNA::Platform::IPlatform& platform;
        explicit VideoScope(CNA::Platform::IPlatform& value) : platform(value)
        { platform.AcquireSubsystem(CNA::Platform::PlatformSubsystem::Video); }
        ~VideoScope() { platform.ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Video); }
    } video(*platform);
    CNA::Platform::WindowDescription description;
    description.width = description.height = 64;
    description.visible = false;
    description.renderIntent = CNA::Platform::WindowRenderIntent::OpenGl;
    auto window = platform->CreateWindow(description);
    ASSERT_NE(window, nullptr);
    RendererSurfaceInfo surface;
    surface.windowId = window->GetId();
    surface.drawableSize = window->GetPixelSize();
    auto owner = std::make_unique<EasyGLRenderer>(surface, *platform->context);
    platform->context->MakeCurrent(window->GetId(), nullptr);
    const int before = Observe().validCalls;
    owner.reset();
    EXPECT_EQ(Observe().destroyedContexts, 2); // Fixture device, then the direct owner.
    EXPECT_EQ(Observe().invalidCalls, 0);
    EXPECT_GT(Observe().validCalls, before); // Owner backbuffer FBO/renderbuffers are real objects.
}

TEST_F(EasyGLResourceLifetimeTest, UninterposedLateCubeReleaseCompletesOffscreen)
{
    // Keep the original offscreen crash as an additional end-to-end witness, independently
    // of the observer. Run this test as its own process so a regression cannot hide other cases.
    Observe().intercept = false;
    platform->context->MakeCurrent(Observe().window, nullptr);
    // Reload through the owner after disabling interception: its existing dispatch is private.
    renderer->DebugSimulateContextLoss();
    auto escaped = renderer->CreateRenderTargetCube(2, 0, false, false, 0);
    ASSERT_NE(escaped->GetGLHandle(), 0u);
    device.reset();
    ASSERT_EQ(Observe().destroyedContexts, 2); // recreation, then permanent teardown
    EXPECT_EQ(escaped->GetGLHandle(), 0u); // Teardown forgot the old native name.
    escaped.reset(); // The process must also complete without the original native fault.
}

#if defined(CNA_EASYGL_COMPILED_EFFECTS)
namespace
{
std::unique_ptr<ICompiledEffectRuntime> RuntimeForLifetime(EasyGLRenderer& renderer)
{
    std::ifstream input(CNA::TestSupport::CompiledEffectDirectory() / "CnaConformanceEffect.fxb",
        std::ios::binary);
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.empty()) throw std::runtime_error("missing committed compiled-effect lifetime fixture");
    return renderer.CreateCompiledEffect(bytes.data(), bytes.size());
}
}

TEST_F(EasyGLResourceLifetimeTest, CompiledEffectRetainsATexturePastDeviceAndThenReleasesIt)
{
    auto runtime = RuntimeForLifetime(*renderer);
    ASSERT_NE(runtime, nullptr);
    std::weak_ptr<ITextureRenderer> retained;
    {
        Texture2D texture(*device, 2, 2);
        retained = texture.GetRenderer().weak_from_this();
        const auto& parameters = runtime->GetDescription().parameters;
        const auto parameter = std::find_if(parameters.begin(), parameters.end(),
            [](const auto& p) { return p.name == "FxTexture"; });
        ASSERT_NE(parameter, parameters.end());
        runtime->SetParameterTexture(parameter->runtimeIndex, &texture);
        CompiledEffectDeviceState state;
        CompiledEffectPassStateChanges changes;
        runtime->ApplyPass(0, state, changes);
    }
    ASSERT_FALSE(retained.expired());
    device.reset();
    ASSERT_EQ(Observe().destroyedContexts, 1);
    ASSERT_FALSE(retained.expired());
    runtime.reset();
    EXPECT_TRUE(retained.expired());
    EXPECT_EQ(Observe().invalidCalls, 0);
    // An ASan run also witnesses that runtime destruction never unregisters through a dead owner.
}

TEST_F(EasyGLResourceLifetimeTest, CompiledCloneAndSpriteBatchReleaseAfterDevice)
{
    auto source = RuntimeForLifetime(*renderer);
    auto clone = source->Clone();
    auto sprite = renderer->CreateSpriteBatch();
    source.reset();
    device.reset();
    ASSERT_EQ(Observe().destroyedContexts, 1);
    clone.reset();
    sprite.reset();
    EXPECT_EQ(Observe().invalidCalls, 0);
}

TEST_F(EasyGLResourceLifetimeTest, CompiledEffectDestroyedAfterContextRecoveryUnregistersBeforeNextRecovery)
{
    auto runtime = RuntimeForLifetime(*renderer);
    renderer->DebugSimulateContextLoss();
    runtime.reset();
    // Teardown-only detachment must not be applied on ordinary recovery: otherwise this
    // second recovery visits a freed effect still listed in compiledEffects_.
    renderer->DebugSimulateContextLoss();
    EXPECT_EQ(Observe().destroyedContexts, 2);
    EXPECT_EQ(Observe().invalidCalls, 0);
    device.reset();
    EXPECT_EQ(Observe().destroyedContexts, 3);
}
#endif
