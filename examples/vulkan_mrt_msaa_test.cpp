// SPDX-License-Identifier: MS-PL
// REMED-GFX-095: Vulkan Multiple Render Targets + MSAA correctness.
//
// The pre-fix Vulkan MRT proxy always built:
//   [single-sample RT0 texture view, single-sample RT1 texture view, 1x depth]
// against a 1x render pass with pResolveAttachments == nullptr. Every 3D MRT
// pipeline independently forced rasterizationSamples=1. The per-target transient
// MSAA images existed, but the MRT path never selected them.
//
// This regression first inspects the live proxy's retained framebuffer-creation
// vector and executes a public partial-sample-mask discriminator. With only bit 0
// enabled, a full-coverage draw resolves to 1/N of its source value on a genuine
// N-sample target, but remains fully opaque on the silent 1x path.
//
// Once that architecture gate passes, test-only precompiled SPIR-V shaders issue
// one fragment invocation with genuinely distinct outputs at locations 0/1 and
// 0/1/2/3. Pixel checks cover 1x and MSAA MRT, mask 0/all/partial,
// ColorWriteChannels slots 0..3, depth-backed MRT, same-submission sampling,
// GetData, A->B->A target sets, single->MRT->single, target retirement, and
// deterministic rejection of incompatible dimensions/sample counts.
//
// Shader sources used for vulkan_mrt_msaa_test_spv.hpp (Android NDK 30 glslc):
//   vertex: Sprite2DVertex pixel coordinates -> NDC; pass UV/color.
//   MRT2 fragment:
//     location0 = texture * vertexColor * pushColor
//     location1 = location0.gbra (RGB channel rotation)
//   MRT4 fragment:
//     location0 = rgba
//     location1 = gbra, location2 = brag, location3 = (1-rgb,a)

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp"
#include "vulkan_mrt_msaa_test_spv.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::Vulkan::VulkanGraphicsBackend;
using CNA::Internal::Backends::Vulkan::VulkanMRTProxy;
using CNA::Internal::Backends::Vulkan::VulkanRenderTargetBackend;

namespace
{
    constexpr int kSize = 32;
    const Color kClear(10, 20, 30, 40);
    const Color kSource(200, 100, 50, 220);

    int SampleCount(VkSampleCountFlagBits value)
    {
        return static_cast<int>(value);
    }

    bool Near(int got, int want, int tolerance = 4)
    {
        return std::abs(got - want) <= tolerance;
    }

    bool Matches(const Color& got, const Color& want, int tolerance = 4)
    {
        return Near(got.getRProperty(), want.getRProperty(), tolerance)
            && Near(got.getGProperty(), want.getGProperty(), tolerance)
            && Near(got.getBProperty(), want.getBProperty(), tolerance)
            && Near(got.getAProperty(), want.getAProperty(), tolerance);
    }

    std::string Describe(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + ","
            + std::to_string(c.getGProperty()) + ","
            + std::to_string(c.getBProperty()) + ","
            + std::to_string(c.getAProperty()) + ")";
    }

    Color ResolveOneSample(const Color& clear, const Color& source, int samples)
    {
        auto channel = [samples](int d, int s) {
            return static_cast<int>(std::lround(
                (static_cast<double>(s) + (samples - 1) * static_cast<double>(d)) / samples));
        };
        return Color(channel(clear.getRProperty(), source.getRProperty()),
                     channel(clear.getGProperty(), source.getGProperty()),
                     channel(clear.getBProperty(), source.getBProperty()),
                     channel(clear.getAProperty(), source.getAProperty()));
    }
}

class VulkanMrtMsaaTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D white_;
    bool done_ = false;
    int pass_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (ok) ++pass_;
    }

    VulkanGraphicsBackend& Backend()
    {
        return static_cast<VulkanGraphicsBackend&>(getGraphicsDeviceProperty().GetBackend());
    }

    static VulkanRenderTargetBackend& BackendOf(RenderTarget2D& target)
    {
        return static_cast<VulkanRenderTargetBackend&>(*target.GetRenderTargetBackend());
    }

    std::unique_ptr<RenderTarget2D> MakeTarget(
        int samples, DepthFormat depth = DepthFormat::None, int size = kSize)
    {
        return std::make_unique<RenderTarget2D>(
            getGraphicsDeviceProperty(), size, size, false, SurfaceFormat::Color,
            depth, samples, RenderTargetUsage::DiscardContents);
    }

    static std::vector<RenderTargetBinding> Bindings(
        std::initializer_list<RenderTarget2D*> targets)
    {
        std::vector<RenderTargetBinding> result;
        result.reserve(targets.size());
        for (RenderTarget2D* target : targets)
            result.emplace_back(target);
        return result;
    }

    Color ReadCenter(RenderTarget2D& target)
    {
        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        target.GetData(0, &center, &pixel, 0, 1);
        return pixel;
    }

    std::unique_ptr<ShaderEffect> MakeEffect(bool fourOutputs)
    {
        std::string vert(reinterpret_cast<const char*>(kGfx095VertSpv),
                         kGfx095VertSpvSize);
        const std::uint32_t* fragWords =
            fourOutputs ? kGfx095Mrt4FragSpv : kGfx095Mrt2FragSpv;
        const std::size_t fragSize =
            fourOutputs ? kGfx095Mrt4FragSpvSize : kGfx095Mrt2FragSpvSize;
        std::string frag(reinterpret_cast<const char*>(fragWords), fragSize);
        return std::make_unique<ShaderEffect>(
            getGraphicsDeviceProperty(), vert, frag);
    }

    void QueueMrtDraw(const std::vector<RenderTargetBinding>& bindings,
                      ShaderEffect& effect, const BlendState& blend,
                      const Color& source, const Color& clear = kClear)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTargets(bindings);
        device.Clear(clear);
        device.setBlendStateProperty(blend);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        effect.SetUniformVec4("uColor",
            source.getRProperty() / 255.0f,
            source.getGProperty() / 255.0f,
            source.getBProperty() / 255.0f,
            source.getAProperty() / 255.0f);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, blend, &point,
                    &noDepth, &noCull, &effect);
        batch.Draw(white_, Rectangle(0, 0, kSize, kSize),
                   Rectangle(0, 0, 1, 1), Color::White);
        batch.End();
        device.SetRenderTargets({});
    }

    Color RunPreFixDiscriminator(RenderTarget2D& rt0, RenderTarget2D& rt1,
                                 int appliedSamples)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTargets(Bindings({&rt0, &rt1}));
        device.Clear(Color::Black);

        BlendState oneSample = BlendState::Opaque;
        oneSample.setMultiSampleMaskProperty(1);
        device.setBlendStateProperty(oneSample);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        const VertexPositionColor quad[6] = {
            {Vector3(-1,  1, 0), Color::White}, {Vector3(-1, -1, 0), Color::White},
            {Vector3( 1, -1, 0), Color::White}, {Vector3(-1,  1, 0), Color::White},
            {Vector3( 1, -1, 0), Color::White}, {Vector3( 1,  1, 0), Color::White},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        device.SetRenderTargets({});

        // Same-submission producer -> consumer. GetBackBufferData records both
        // passes, so this discriminator does not depend on MRT GetData support.
        device.Clear(Color::Black);
        SpriteBatch consumer(device);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        consumer.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                       &noDepth, &noCull);
        consumer.Draw(rt0, Rectangle(0, 0, kSize, kSize),
                      Rectangle(0, 0, kSize, kSize), Color::White);
        consumer.End();

        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        Color got(0, 0, 0, 0);
        device.GetBackBufferData(&center, &got, 0, 1);
        const int expected = static_cast<int>(std::lround(255.0 / appliedSamples));
        Check(Near(got.getRProperty(), expected, 5),
              "partial mask bit0 resolves at 1/" + std::to_string(appliedSamples)
              + " coverage: expected R~" + std::to_string(expected)
              + ", got " + Describe(got));
        return got;
    }

    void RunTwoTargetCases(ShaderEffect& effect, int appliedSamples)
    {
        auto ms0 = MakeTarget(8);
        auto ms1 = MakeTarget(8);
        const auto bindings = Bindings({ms0.get(), ms1.get()});

        BlendState all = BlendState::Opaque;
        QueueMrtDraw(bindings, effect, all, kSource);
        const Color got0 = ReadCenter(*ms0);
        const Color got1 = ReadCenter(*ms1);
        const Color want1(100, 50, 200, 220);
        Check(Matches(got0, kSource),
              "2-target MSAA RT0 resolves distinct location-0 output: " + Describe(got0));
        Check(Matches(got1, want1),
              "2-target MSAA RT1 resolves distinct location-1 output: " + Describe(got1));

        BlendState none = BlendState::Opaque;
        none.setMultiSampleMaskProperty(0);
        QueueMrtDraw(bindings, effect, none, kSource);
        const Color zero0 = ReadCenter(*ms0);
        const Color zero1 = ReadCenter(*ms1);
        Check(Matches(zero0, kClear) && Matches(zero1, kClear),
              "MRT MSAA MultiSampleMask=0 preserves both clear values: "
              + Describe(zero0) + " / " + Describe(zero1));

        BlendState one = BlendState::Opaque;
        one.setMultiSampleMaskProperty(1);
        QueueMrtDraw(bindings, effect, one, kSource);
        const Color part0 = ReadCenter(*ms0);
        const Color part1 = ReadCenter(*ms1);
        Check(Matches(part0, ResolveOneSample(kClear, kSource, appliedSamples), 6)
              && Matches(part1, ResolveOneSample(kClear, want1, appliedSamples), 6),
              "MRT MSAA partial sample mask resolves both outputs at 1/"
              + std::to_string(appliedSamples) + ": "
              + Describe(part0) + " / " + Describe(part1));

        auto one0 = MakeTarget(0);
        auto one1 = MakeTarget(0);
        QueueMrtDraw(Bindings({one0.get(), one1.get()}), effect, all, kSource);
        const Color non0 = ReadCenter(*one0);
        const Color non1 = ReadCenter(*one1);
        Check(Matches(non0, kSource) && Matches(non1, want1),
              "non-MSAA MRT remains correct: " + Describe(non0) + " / " + Describe(non1));
    }

    void RunFourTargetColorWriteCase(ShaderEffect& effect)
    {
        std::array<std::unique_ptr<RenderTarget2D>, 4> targets;
        for (auto& target : targets) target = MakeTarget(8);

        BlendState masks = BlendState::Opaque;
        masks.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        masks.setColorWriteChannels1Property(ColorWriteChannels::Green);
        masks.setColorWriteChannels2Property(ColorWriteChannels::Blue);
        masks.setColorWriteChannels3Property(ColorWriteChannels::Alpha);
        QueueMrtDraw(Bindings({targets[0].get(), targets[1].get(),
                               targets[2].get(), targets[3].get()}),
                     effect, masks, kSource);

        const std::array<Color, 4> expected = {
            Color(200, 20, 30, 40),
            Color(10, 50, 30, 40),
            Color(10, 20, 100, 40),
            Color(10, 20, 30, 220),
        };
        bool all = true;
        std::string values;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const Color got = ReadCenter(*targets[i]);
            all = all && Matches(got, expected[i]);
            values += (i == 0 ? "" : " / ") + Describe(got);
        }
        Check(all, "4-target MSAA ColorWriteChannels0/1/2/3 map to R/G/B/A: " + values);
    }

    void RunDepthCase(ShaderEffect& effect, int appliedSamples)
    {
        auto rt0 = MakeTarget(8, DepthFormat::Depth24Stencil8);
        auto rt1 = MakeTarget(8, DepthFormat::Depth24Stencil8);
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTargets(Bindings({rt0.get(), rt1.get()}));
        const VulkanMRTProxy* proxy = Backend().GetCurrentMRTProxyEXT();
        const bool structure = proxy
            && SampleCount(proxy->GetColorSampleCountEXT()) == appliedSamples
            && proxy->GetDepthFormat() != VK_FORMAT_UNDEFINED
            && proxy->GetFramebufferAttachmentCountEXT() == 5;
        device.SetRenderTargets({});
        QueueMrtDraw(Bindings({rt0.get(), rt1.get()}),
                     effect, BlendState::Opaque, kSource);
        Check(structure && Matches(ReadCenter(*rt0), kSource)
              && Matches(ReadCenter(*rt1), Color(100, 50, 200, 220)),
              "depth-backed 2-target MSAA uses matching color/depth sample structure and resolves");
    }

    void RunProducerConsumerCase(ShaderEffect& effect)
    {
        auto rt0 = MakeTarget(8);
        auto rt1 = MakeTarget(8);
        QueueMrtDraw(Bindings({rt0.get(), rt1.get()}),
                     effect, BlendState::Opaque, kSource);

        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color::Black);
        SpriteBatch consumer(device);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        consumer.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                       &noDepth, &noCull);
        consumer.Draw(*rt0, Rectangle(0, 0, kSize, kSize),
                      Rectangle(0, 0, kSize, kSize), Color::White);
        consumer.Draw(*rt1, Rectangle(kSize, 0, kSize, kSize),
                      Rectangle(0, 0, kSize, kSize), Color::White);
        consumer.End();

        Color left(0, 0, 0, 0), right(0, 0, 0, 0);
        const Rectangle leftRect(kSize / 2, kSize / 2, 1, 1);
        const Rectangle rightRect(kSize + kSize / 2, kSize / 2, 1, 1);
        device.GetBackBufferData(&leftRect, &left, 0, 1);
        device.GetBackBufferData(&rightRect, &right, 0, 1);
        const Color read0 = ReadCenter(*rt0);
        const Color read1 = ReadCenter(*rt1);
        Check(Matches(left, kSource) && Matches(right, Color(100, 50, 200, 220))
              && Matches(read0, left) && Matches(read1, right),
              "same-submission MRT producer -> backbuffer consumer agrees with GetData: "
              + Describe(left) + " / " + Describe(right));
    }

    void RunTargetSetAndLifetimeCases(ShaderEffect& effect)
    {
        auto a0 = MakeTarget(8); auto a1 = MakeTarget(8);
        auto b0 = MakeTarget(8); auto b1 = MakeTarget(8);
        QueueMrtDraw(Bindings({a0.get(), a1.get()}), effect,
                     BlendState::Opaque, Color(200, 40, 20, 255));
        QueueMrtDraw(Bindings({b0.get(), b1.get()}), effect,
                     BlendState::Opaque, Color(30, 180, 60, 255));
        QueueMrtDraw(Bindings({a0.get(), a1.get()}), effect,
                     BlendState::Opaque, Color(20, 60, 210, 255));
        const bool aba = Matches(ReadCenter(*a0), Color(20, 60, 210, 255))
            && Matches(ReadCenter(*a1), Color(60, 210, 20, 255))
            && Matches(ReadCenter(*b0), Color(30, 180, 60, 255))
            && Matches(ReadCenter(*b1), Color(180, 60, 30, 255));
        Check(aba, "MRT set A -> set B -> set A preserves B and A's final resolved outputs");

        auto survivor = MakeTarget(8);
        {
            auto destroyed = MakeTarget(8);
            QueueMrtDraw(Bindings({destroyed.get(), survivor.get()}), effect,
                         BlendState::Opaque, Color(90, 170, 40, 255));
            destroyed.reset();
        }
        Check(Matches(ReadCenter(*survivor), Color(170, 40, 90, 255)),
              "destroying one unbound MRT target keeps deferred GPU work for the surviving target safe");
    }

    void RunSingleMrtSingleCase(ShaderEffect& effect)
    {
        auto single = MakeTarget(8);
        auto m0 = MakeTarget(8);
        auto m1 = MakeTarget(8);

        auto drawSingle = [&](const Color& color) {
            auto& device = getGraphicsDeviceProperty();
            device.SetRenderTarget(single.get());
            device.Clear(kClear);
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr,
                        &noDepth, &noCull);
            batch.Draw(white_, Rectangle(0, 0, kSize, kSize),
                       Rectangle(0, 0, 1, 1), color);
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        };

        drawSingle(Color(180, 20, 30, 255));
        QueueMrtDraw(Bindings({m0.get(), m1.get()}), effect,
                     BlendState::Opaque, Color(30, 190, 80, 255));
        drawSingle(Color(40, 70, 210, 255));
        Check(Matches(ReadCenter(*single), Color(40, 70, 210, 255))
              && Matches(ReadCenter(*m0), Color(30, 190, 80, 255))
              && Matches(ReadCenter(*m1), Color(190, 80, 30, 255)),
              "single RT MSAA -> MRT MSAA -> single RT MSAA keeps both resolve paths correct");
    }

    void RunCompatibilityCases()
    {
        auto& device = getGraphicsDeviceProperty();
        auto msaa = MakeTarget(8);
        auto plain = MakeTarget(0);
        bool sampleRejected = false;
        try {
            device.SetRenderTargets(Bindings({msaa.get(), plain.get()}));
        } catch (const std::runtime_error&) {
            sampleRejected = true;
        }
        device.SetRenderTargets({});
        Check(sampleRejected, "incompatible MRT applied sample counts are rejected deterministically");

        auto full = MakeTarget(8, DepthFormat::None, kSize);
        auto small = MakeTarget(8, DepthFormat::None, kSize / 2);
        bool extentRejected = false;
        try {
            device.SetRenderTargets(Bindings({full.get(), small.get()}));
        } catch (const std::runtime_error&) {
            extentRejected = true;
        }
        device.SetRenderTargets({});
        Check(extentRejected, "incompatible MRT dimensions are rejected deterministically");
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        // Engage the highest normally selected count up to 8 before creating GPU resources.
        getGraphicsDeviceProperty().RecreateBackendForMultiSampleCount(8);
    }

    void LoadContent() override
    {
        white_ = Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 1, 1,
            std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto rt0 = MakeTarget(8);
        auto rt1 = MakeTarget(8);
        auto& vk0 = BackendOf(*rt0);
        auto& vk1 = BackendOf(*rt1);
        const int applied = SampleCount(vk0.GetColorSampleCountEXT());
        Check(applied > 1 && SampleCount(vk1.GetColorSampleCountEXT()) == applied,
              "requested 8x produced matching per-target applied sample count "
              + std::to_string(applied) + "x");

        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTargets(Bindings({rt0.get(), rt1.get()}));
        const VulkanMRTProxy* proxy = Backend().GetCurrentMRTProxyEXT();
        const bool structure = proxy
            && SampleCount(proxy->GetColorSampleCountEXT()) == applied
            && proxy->GetFramebufferAttachmentCountEXT() == 4
            && proxy->GetResolveAttachmentCountEXT() == 2
            && proxy->GetColorAttachmentViewEXT(0) == vk0.GetMsaaColorViewEXT()
            && proxy->GetColorAttachmentViewEXT(1) == vk1.GetMsaaColorViewEXT()
            && proxy->GetResolveAttachmentViewEXT(0) == vk0.GetResolveColorViewEXT()
            && proxy->GetResolveAttachmentViewEXT(1) == vk1.GetResolveColorViewEXT()
            && proxy->GetFramebufferAttachmentViewEXT(0) == vk0.GetMsaaColorViewEXT()
            && proxy->GetFramebufferAttachmentViewEXT(1) == vk1.GetMsaaColorViewEXT()
            && proxy->GetFramebufferAttachmentViewEXT(2) == vk0.GetResolveColorViewEXT()
            && proxy->GetFramebufferAttachmentViewEXT(3) == vk1.GetResolveColorViewEXT();
        std::printf("[INFO] requested=8 applied-target=%d proxy-samples=%d framebuffer-views=%u "
                    "resolve-refs=%u\n",
                    applied, proxy ? SampleCount(proxy->GetColorSampleCountEXT()) : 0,
                    proxy ? proxy->GetFramebufferAttachmentCountEXT() : 0,
                    proxy ? proxy->GetResolveAttachmentCountEXT() : 0);
        Check(structure,
              "live 2-target MRT framebuffer maps [MSAA0,MSAA1,resolve0,resolve1]");
        device.SetRenderTargets({});

        RunPreFixDiscriminator(*rt0, *rt1, applied);
        Check(SampleCount(Backend().GetLastMRTPipelineSampleCountEXT()) == applied
              && Backend().GetLastMRTPipelineColorCountEXT() == 2,
              "MRT graphics pipeline rasterizationSamples="
              + std::to_string(SampleCount(Backend().GetLastMRTPipelineSampleCountEXT()))
              + " with two color outputs");

        // On the known pre-fix code, stop after the clean, validation-safe structural and
        // public partial-mask evidence. The remaining cases use the genuine multi-output
        // pipeline and intentionally run only once the pass/framebuffer architecture is valid.
        if (!structure) {
            std::printf("[INFO] pre-fix architecture observed; downstream multi-output checks "
                        "skipped to avoid binding a known-incompatible custom pipeline.\n");
            std::printf("=== %d/%d PASS ===\n", pass_, total_);
            result_ = 1;
            Exit();
            return;
        }

        auto mrt2 = MakeEffect(false);
        auto mrt4 = MakeEffect(true);
        Check(mrt2->IsEffectValid() && mrt4->IsEffectValid(),
              "dedicated SPIR-V effects compile with genuine 2-output and 4-output fragments");
        if (mrt2->IsEffectValid() && mrt4->IsEffectValid()) {
            RunTwoTargetCases(*mrt2, applied);
            RunFourTargetColorWriteCase(*mrt4);
            RunDepthCase(*mrt2, applied);
            RunProducerConsumerCase(*mrt2);
            RunTargetSetAndLifetimeCases(*mrt2);
            RunSingleMrtSingleCase(*mrt2);
            RunCompatibilityCases();
        }

        std::printf("=== %d/%d PASS ===\n", pass_, total_);
        result_ = (pass_ == total_) ? 0 : 1;
        Exit();
    }

public:
    VulkanMrtMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(96);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int GetResult() const { return result_; }
};

int main()
{
    VulkanMrtMsaaTest game;
    game.Run();
    return game.GetResult();
}
