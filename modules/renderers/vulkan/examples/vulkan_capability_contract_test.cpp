// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-020 (finding F-07): VulkanRenderer::SupportsCapability must answer
// for every CNA::GraphicsCapability member, not fall through a catch-all.
//
// The old switch handled three members and ended `default: return true`. Two things followed, and
// this test is shaped around both rather than around the source change:
//
//   A  **The renderer and GraphicsDevice disagreed.** Six members are answered above the renderer,
//      by a virtual whose default is false (CompiledEffects, Float/HalfFloat render targets,
//      HalfFloatTextureLinearFiltering, ComputeShaders, IndirectDraw). GraphicsDevice therefore
//      said false while IGraphicsRenderer::SupportsCapability -- the answer a C-ABI caller or a
//      renderer-level test gets -- said true through the catch-all. This leg asks BOTH seams the
//      same 19 questions and requires the same 19 answers. It fails six times on the old code and
//      needs no mutation to do it.
//   B  **A value that is not an enumerator was claimed too.** Casting an out-of-range int returned
//      true. It must be refused.
//
// And the clause the whole exercise exists for (plan_vulkan.md section 6.2): a capability reported
// FALSE must correspond to a refusal, not to a working path. Leg C drives each false answer into
// the public API and requires a named exception:
//
//   * MultiStreamVertexInput -> two per-vertex bindings, then a draw -> NotSupportedException
//   * FloatRenderTargets     -> RenderTarget2D(SurfaceFormat::Vector4)      -> refused
//   * HalfFloatRenderTargets -> RenderTarget2D(SurfaceFormat::HdrBlendable) -> refused
//
// Leg E belongs to VULKAN-021: MultiSampleAntiAliasing is read from the device's own
// framebufferColorSampleCounts & framebufferDepthSampleCounts, so it is required to agree, in both
// directions, with the sample count GraphicsDeviceManager.ApplyChanges() actually reaches.
//
// The TRUE answers are not re-proved here. Each already has a dedicated Vulkan CTest that observes
// the behaviour, and the map from member to CTest lives in VULKAN-020's plan row rather than being
// duplicated into a second, weaker copy of those tests.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    struct Member { GraphicsCapability value; const char* name; };

    // Every member of CNA::GraphicsCapability, in declaration order. This list is maintained by
    // hand on purpose: the compile-time guard that a NEW member cannot be ignored lives in the
    // renderer itself (-Werror=switch on cna_renderer_vulkan, VULKAN-020), which is where it can
    // actually stop a wrong answer being given. Here a stale list would only under-test.
    constexpr Member kMembers[] = {
        { GraphicsCapability::ThreeD,                          "ThreeD" },
        { GraphicsCapability::DepthStencilBuffer,              "DepthStencilBuffer" },
        { GraphicsCapability::MultiSampleAntiAliasing,         "MultiSampleAntiAliasing" },
        { GraphicsCapability::MultipleRenderTargets,           "MultipleRenderTargets" },
        { GraphicsCapability::AnisotropicFiltering,            "AnisotropicFiltering" },
        { GraphicsCapability::WireFrame,                       "WireFrame" },
        { GraphicsCapability::OcclusionQuery,                  "OcclusionQuery" },
        { GraphicsCapability::CustomEffects,                   "CustomEffects" },
        { GraphicsCapability::Texture3D,                       "Texture3D" },
        { GraphicsCapability::MultiStreamVertexInput,          "MultiStreamVertexInput" },
        { GraphicsCapability::Instancing,                      "Instancing" },
        { GraphicsCapability::StencilBuffer,                   "StencilBuffer" },
        { GraphicsCapability::AdditiveBlending,                "AdditiveBlending" },
        { GraphicsCapability::CompiledEffects,                 "CompiledEffects" },
        { GraphicsCapability::FloatRenderTargets,              "FloatRenderTargets" },
        { GraphicsCapability::HalfFloatRenderTargets,          "HalfFloatRenderTargets" },
        { GraphicsCapability::HalfFloatTextureLinearFiltering, "HalfFloatTextureLinearFiltering" },
        { GraphicsCapability::ComputeShaders,                  "ComputeShaders" },
        { GraphicsCapability::IndirectDraw,                    "IndirectDraw" },
    };

    constexpr int kSize = 64;
}

class VulkanCapabilityContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        if (ok) ++pass_; else ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    // ── Leg A: the two seams must give the same answer ───────────────────────

    void testSeamAgreement(GraphicsDevice& dev)
    {
        const VulkanRenderer& r = Renderer();
        for (const Member& m : kMembers)
        {
            const bool viaRenderer = r.SupportsCapability(m.value);
            const bool viaDevice   = dev.SupportsCapability(m.value);
            check(viaRenderer == viaDevice,
                  std::string("A ") + m.name + ": renderer and GraphicsDevice agree",
                  std::string("renderer=") + (viaRenderer ? "true" : "false")
                      + " device=" + (viaDevice ? "true" : "false"));
        }
    }

    // ── Leg B: a value that is not an enumerator is refused ──────────────────

    void testOutOfRange()
    {
        // Only reachable by casting; the point is that the answer is "no", not "yes by default".
        const bool claimed =
            Renderer().SupportsCapability(static_cast<GraphicsCapability>(1000));
        check(!claimed, "B an out-of-range capability is refused, not claimed",
              claimed ? "returned true" : "returned false");
    }

    // ── Leg C: every FALSE answer has a real refusal behind it ───────────────

    void testFalseAnswersRefuse(GraphicsDevice& dev)
    {
        const VulkanRenderer& r = Renderer();

        if (!r.SupportsCapability(GraphicsCapability::MultiStreamVertexInput))
        {
            // The two declarations must be DISJOINT by semantic. XNA composes bound declarations
            // by (usage, usageIndex), and CNA's shared layer reproduces FNA3D's rule that a later
            // stream repeating an already-claimed pair is "not in use" and contributes nothing --
            // so two copies of VertexPositionColor are one stream, not two, and would prove
            // nothing here. Position in slot 0, Colour in slot 1 is a genuine split vertex.
            const float positions[9] = { -1.f, 1.f, 0.f,  -1.f, -1.f, 0.f,   1.f, -1.f, 0.f };
            const std::uint32_t colours[3] = { 0xFF0000FFu, 0xFF0000FFu, 0xFF0000FFu };

            VertexDeclaration positionOnly(12, {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            });
            VertexDeclaration colourOnly(4, {
                VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
            VertexBuffer a(dev, positionOnly, 3, BufferUsage::None);
            VertexBuffer b(dev, colourOnly,   3, BufferUsage::None);
            a.SetDataRaw(positions, 3, 12);
            b.SetDataRaw(colours,   3, 4);

            std::vector<VertexBufferBinding> bindings;
            bindings.emplace_back(&a, 0, 0);
            bindings.emplace_back(&b, 0, 0);   // a second stream of the SAME input rate

            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.Apply();
            dev.SetVertexBuffers(bindings);

            bool refused = false;
            std::string how = "the draw was accepted";
            try {
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            } catch (const System::NotSupportedException& e) {
                refused = true; how = std::string("NotSupportedException: ") + e.what();
            } catch (const std::exception& e) {
                how = std::string("wrong type: ") + e.what();
            }
            dev.SetVertexBuffer(nullptr);
            check(refused, "C MultiStreamVertexInput=false is a refusal, not a working path", how);
        }

        struct FloatCase { GraphicsCapability cap; SurfaceFormat format; const char* name; };
        const FloatCase floatCases[] = {
            { GraphicsCapability::FloatRenderTargets,     SurfaceFormat::Vector4,      "FloatRenderTargets" },
            { GraphicsCapability::HalfFloatRenderTargets, SurfaceFormat::HdrBlendable, "HalfFloatRenderTargets" },
        };
        for (const FloatCase& c : floatCases)
        {
            const bool supported = r.SupportsCapability(c.cap);
            bool created = false;
            std::string how;
            try {
                RenderTarget2D rt(dev, 8, 8, false, c.format, DepthFormat::None);
                created = true;
                how = "created";
            } catch (const std::exception& e) {
                how = std::string("refused: ") + e.what();
            }
            // The capability and the constructor must agree in BOTH directions -- a false report
            // with a working constructor is exactly the "silent substitution" section 6.3 forbids.
            check(created == supported,
                  std::string("C ") + c.name + " matches what RenderTarget2D actually does",
                  std::string("capability=") + (supported ? "true" : "false") + ", " + how);
        }

        // VULKAN-470. HalfFloatTextureLinearFiltering is about SAMPLING a half-float texture, not
        // about rendering to one, so the leg above does not cover it. The reason it is false here
        // is one step earlier than filtering: the format cannot be given to a Texture2D at all, so
        // there is no surface for a sampler to filter. Asserting that is what makes "false" an
        // observation rather than a claim.
        {
            const bool supported =
                r.SupportsCapability(GraphicsCapability::HalfFloatTextureLinearFiltering);
            bool created = false;
            std::string how;
            try {
                Texture2D half(dev, 8, 8, false, SurfaceFormat::HalfVector4);
                created = true;
                how = "created";
            } catch (const std::exception& e) {
                how = std::string("refused: ") + e.what();
            }
            check(!supported && !created,
                  "C HalfFloatTextureLinearFiltering=false has no surface to filter: a half-float "
                  "Texture2D is refused outright",
                  std::string("capability=") + (supported ? "true" : "false") + ", " + how);
        }

        // VULKAN-470. IndirectDraw=false, observed one step before the draw: an indirect draw needs
        // its arguments in a storage buffer, and this renderer cannot make one. `CreateStorageBuffer`
        // returns null, so `DrawPrimitivesIndirectEXT` has no argument buffer that could be passed
        // to it -- the capability is false because the route does not exist, not because a check
        // turns it away. That is a stronger observation than a refusal message would be, and it is
        // the only one available: the parameter is a reference, so there is nothing to hand it.
        //
        // ComputeShaders is false for the same reason and is checked the same way -- storage
        // buffers are the compute path's own memory. CompiledEffects is different: it is false
        // because `CNA_VULKAN_COMPILED_EFFECTS` is OFF in this build, and only a build that turns
        // it on can observe the other answer. plan_vulkan.md's VULKAN-470 row records that.
        {
            const bool indirect = r.SupportsCapability(GraphicsCapability::IndirectDraw);
            const bool compute  = r.SupportsCapability(GraphicsCapability::ComputeShaders);
            const auto buffer   = const_cast<VulkanRenderer&>(r).CreateStorageBuffer(64);
            check(!indirect && !compute && buffer == nullptr,
                  "C IndirectDraw=false and ComputeShaders=false have no route: this renderer "
                  "cannot create the storage buffer both would need",
                  std::string("indirect=") + (indirect ? "true" : "false")
                      + " compute=" + (compute ? "true" : "false")
                      + " storageBuffer=" + (buffer == nullptr ? "null" : "created"));
        }
    }

    // ── Leg E: the MSAA answer matches what the device actually applies ──────

    void testMultiSampleAgreement()
    {
        // VULKAN-021. The capability is now read from the device's own
        // framebufferColorSampleCounts & framebufferDepthSampleCounts, so the observable it must
        // agree with is what ApplyMultiSampleCount can actually reach on that same device. Run
        // last: it recreates renderer-owned MSAA state, and the renderer reference must be
        // re-taken afterwards.
        const bool capability = Renderer().SupportsCapability(
            GraphicsCapability::MultiSampleAntiAliasing);

        gdm_->setPreferMultiSamplingProperty(true);
        gdm_->ApplyChanges();

        const int applied = Renderer().GetMultiSampleCount();
        // Both directions. A true report with a device that cannot exceed one sample is a promise
        // nothing can keep; a false report on a device that just applied 4x is a capability the
        // renderer is hiding.
        check(capability == (applied > 1),
              "E MultiSampleAntiAliasing matches the count the device applies",
              std::string("capability=") + (capability ? "true" : "false")
                  + ", applied MultiSampleCount=" + std::to_string(applied));
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        testSeamAgreement(dev);
        testOutOfRange();
        testFalseAnswersRefuse(dev);
        testMultiSampleAgreement();

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "D no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VulkanCapabilityContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanCapabilityContractTest g;
    g.Run();
    return g.getResult();
}
