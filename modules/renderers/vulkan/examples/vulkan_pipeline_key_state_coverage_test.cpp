// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-096 -- every public field of BlendState, DepthStencilState and
// RasterizerState, measured against the pipeline key that is supposed to carry it.
//
// The row's own method note says why a pixel test cannot answer this: a key that omits a field is
// invisible to a test that only ever sets one value of that field, and it is equally invisible to
// one that sets two values but never looks at whether a second pipeline was built. So the
// instrument here is cardinality. `GetGraphicsPipelineCacheEntryCountEXT()` counts the entries of
// every PipelineKey-keyed cache, and each field is put through the same three-step protocol:
//
//   1. draw with the baseline state, so its pipeline exists;
//   2. change exactly ONE field, draw, and see whether the count went up;
//   3. draw the changed state AGAIN, and require the count to hold still.
//
// Step 3 is what makes step 2 mean something: a count that keeps climbing on repetition is
// measuring frame count, not the key. Exactly one field changes per case and the state returns to
// the baseline afterwards, so no two cases can collide on the same combination.
//
// Three verdicts, and all three are assertions rather than observations:
//
//   IN KEY   -- the field reaches the pipeline. Vulkan bakes it into the VkPipeline, so it MUST
//               fragment the cache; a field that does not is a field the renderer cannot change.
//   DYNAMIC  -- the field is real Vulkan dynamic state (vkCmdSetBlendConstants, vkCmdSetDepthBias,
//               vkCmdSetStencil{CompareMask,WriteMask,Reference}, vkCmdSetScissor). It MUST NOT
//               fragment the cache: one pipeline serves every value, and a key that carried it
//               would multiply pipelines for nothing. Absence from the key is correctness here.
//   DROPPED  -- neither. The renderer receives the field and does nothing with it, or never
//               receives it at all. Recorded with the reason, and each such field owns a row.
//
// The one DROPPED field found by this audit is RasterizerState.MultiSampleAntiAlias:
// `IGraphicsRenderer::ApplyRasterizerState(cullMode, fillMode, scissorTestEnable, depthBias,
// slopeScaleDepthBias)` has no parameter for it, so it reaches NO renderer -- EasyGL included, and
// the D3D11 state cache says so in its own comment. It is asserted here as DROPPED rather than
// silently omitted, so that the day it starts fragmenting the cache this test says so.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
constexpr int kSize = 64;
const Color kVertexColor(230, 30, 30, 255);

/// What the audit claims about a field, and therefore what the cardinality must show.
enum class Verdict
{
    InKey,    ///< baked into the VkPipeline; changing it MUST build a second pipeline
    Dynamic,  ///< real Vulkan dynamic state; changing it MUST NOT build a second pipeline
    Dropped   ///< reaches no VkPipeline at all; changing it cannot build one, and that is the bug
};

/// Compile-time half of the DROPPED verdict, and the half the cardinality cannot supply.
///
/// A DYNAMIC field and a DROPPED one produce the SAME cardinality signal -- neither fragments the
/// key -- so the run-time measurement below can confirm a claim but never tell those two apart. The
/// separation comes from the interface itself: a dynamic field is carried to the renderer and
/// issued with a `vkCmdSet*`, and `RasterizerState.MultiSampleAntiAlias` is not carried at all.
/// That is asserted here rather than asserted in prose, so that adding the parameter breaks this
/// file instead of quietly invalidating the row.
template <typename T, typename = void>
struct TakesSixRasterizerArgs : std::false_type {};
template <typename T>
struct TakesSixRasterizerArgs<
    T, std::void_t<decltype(std::declval<T&>().ApplyRasterizerState(0, 0, false, 0.0f, 0.0f, false))>>
    : std::true_type {};

template <typename T, typename = void>
struct TakesFiveRasterizerArgs : std::false_type {};
template <typename T>
struct TakesFiveRasterizerArgs<
    T, std::void_t<decltype(std::declval<T&>().ApplyRasterizerState(0, 0, false, 0.0f, 0.0f))>>
    : std::true_type {};

using RendererBase = CNA::Internal::Renderers::IGraphicsRenderer;

// The positive control. Without it the negative below could pass vacuously -- a renamed or
// relocated method would make BOTH detectors false and the audit would report a boundary it never
// actually looked at.
static_assert(TakesFiveRasterizerArgs<RendererBase>::value,
              "ApplyRasterizerState(cullMode, fillMode, scissorTestEnable, depthBias, "
              "slopeScaleDepthBias) must exist -- the detector below means nothing otherwise");
static_assert(!TakesSixRasterizerArgs<RendererBase>::value,
              "ApplyRasterizerState now accepts a sixth argument. If that is "
              "RasterizerState.MultiSampleAntiAlias, plan_vulkan.md VULKAN-096's DROPPED verdict "
              "and the row it opened are out of date and must be re-measured.");

const char* Name(Verdict v)
{
    switch (v) {
        case Verdict::InKey:   return "IN KEY";
        case Verdict::Dynamic: return "DYNAMIC";
        case Verdict::Dropped: return "DROPPED";
    }
    return "?";
}
}  // namespace

class VulkanPipelineKeyStateCoverageTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<VertexBuffer> vb_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    std::size_t Count() { return Renderer().GetGraphicsPipelineCacheEntryCountEXT(); }

    static std::vector<std::uint8_t> Records()
    {
        constexpr int kStride = 16;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * kStride), 0);
        constexpr float kX[6] = { -1.f, -1.f,  1.f, -1.f,  1.f,  1.f };
        constexpr float kY[6] = {  1.f, -1.f, -1.f,  1.f, -1.f,  1.f };
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytes.data() + i * kStride;
            const float pos[3] = { kX[i], kY[i], 0.0f };
            std::memcpy(v + 0, pos, sizeof(pos));
            v[12] = kVertexColor.getRProperty();
            v[13] = kVertexColor.getGProperty();
            v[14] = kVertexColor.getBProperty();
            v[15] = kVertexColor.getAProperty();
        }
        return bytes;
    }

    void DrawQuad()
    {
        auto& dev = getGraphicsDeviceProperty();
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        dev.Clear(Color(0, 0, 0, 255));
        fx.Apply();
        dev.SetVertexBuffer(vb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        Color probe(0, 0, 0, 0);
        const Rectangle at(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&at, &probe, 0, 1);
    }

    /// The baseline every case starts and ends at: one explicit value for all three state objects,
    /// so a case's "changed" combination is reached only by that case.
    void ApplyBaseline()
    {
        auto& dev = getGraphicsDeviceProperty();
        BlendState bs;
        bs.setColorSourceBlendProperty(Blend::SourceAlpha);
        bs.setColorDestinationBlendProperty(Blend::InverseSourceAlpha);
        bs.setAlphaSourceBlendProperty(Blend::One);
        bs.setAlphaDestinationBlendProperty(Blend::Zero);
        bs.setColorBlendFunctionProperty(BlendFunction::Add);
        bs.setAlphaBlendFunctionProperty(BlendFunction::Add);
        bs.setColorWriteChannelsProperty(ColorWriteChannels::All);
        bs.setColorWriteChannels1Property(ColorWriteChannels::All);
        bs.setColorWriteChannels2Property(ColorWriteChannels::All);
        bs.setColorWriteChannels3Property(ColorWriteChannels::All);
        bs.setBlendFactorProperty(Color(255, 255, 255, 255));
        bs.setMultiSampleMaskProperty(static_cast<int>(0xFFFFFFFFu));
        dev.setBlendStateProperty(bs);

        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(true);
        ds.setDepthBufferWriteEnableProperty(true);
        ds.setDepthBufferFunctionProperty(CompareFunction::LessEqual);
        ds.setStencilEnableProperty(false);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setStencilFailProperty(StencilOperation::Keep);
        ds.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        ds.setTwoSidedStencilModeProperty(false);
        ds.setCounterClockwiseStencilFunctionProperty(CompareFunction::Always);
        ds.setCounterClockwiseStencilPassProperty(StencilOperation::Keep);
        ds.setCounterClockwiseStencilFailProperty(StencilOperation::Keep);
        ds.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::Keep);
        ds.setStencilMaskProperty(static_cast<int>(0xFFFFFFFF));
        ds.setStencilWriteMaskProperty(static_cast<int>(0xFFFFFFFF));
        ds.setReferenceStencilProperty(0);
        dev.setDepthStencilStateProperty(ds);

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        rs.setFillModeProperty(FillMode::Solid);
        rs.setScissorTestEnableProperty(false);
        rs.setDepthBiasProperty(0.0f);
        rs.setSlopeScaleDepthBiasProperty(0.0f);
        rs.setMultiSampleAntiAliasProperty(true);
        dev.setRasterizerStateProperty(rs);
    }

    /// Measures one field and asserts the audit's claim about it.
    ///
    /// `mutate` receives the baseline state objects and changes exactly one field; the caller
    /// re-applies whichever object it touched.
    void Measure(const char* field, Verdict claim, const std::function<void()>& applyChanged)
    {
        ApplyBaseline();
        DrawQuad();                       // the baseline pipeline exists from here on
        const std::size_t before = Count();

        applyChanged();
        DrawQuad();
        const std::size_t after = Count();

        applyChanged();
        DrawQuad();
        const std::size_t settled = Count();

        ApplyBaseline();

        const bool grew   = after > before;
        const bool stable = settled == after;
        const bool ok     = stable && (grew == (claim == Verdict::InKey));

        char buf[320];
        std::snprintf(buf, sizeof(buf),
                      "%-42s claimed %-7s -- cache %zu -> %zu on the changed value, %zu on a "
                      "repeat (%s)",
                      field, Name(claim), before, after, settled,
                      grew ? "fragments the key" : "does not fragment the key");
        check(ok, buf);
    }

    void MeasureBlend(const char* field, Verdict claim,
                      const std::function<void(BlendState&)>& mutate)
    {
        Measure(field, claim, [this, &mutate] {
            auto& dev = getGraphicsDeviceProperty();
            BlendState bs = dev.getBlendStateProperty();
            mutate(bs);
            dev.setBlendStateProperty(bs);
        });
    }

    void MeasureDepth(const char* field, Verdict claim,
                      const std::function<void(DepthStencilState&)>& mutate)
    {
        Measure(field, claim, [this, &mutate] {
            auto& dev = getGraphicsDeviceProperty();
            DepthStencilState ds = dev.getDepthStencilStateProperty();
            mutate(ds);
            dev.setDepthStencilStateProperty(ds);
        });
    }

    void MeasureRaster(const char* field, Verdict claim,
                       const std::function<void(RasterizerState&)>& mutate)
    {
        Measure(field, claim, [this, &mutate] {
            auto& dev = getGraphicsDeviceProperty();
            RasterizerState rs = dev.getRasterizerStateProperty();
            mutate(rs);
            dev.setRasterizerStateProperty(rs);
        });
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        const VertexDeclaration decl16(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)});
        vb_ = std::make_unique<VertexBuffer>(dev, decl16, 6, BufferUsage::None);
        const auto bytes = Records();
        vb_->SetDataRaw(bytes.data(), 6, 16);

        // ---- BlendState: 12 public fields ----------------------------------------------------
        MeasureBlend("BlendState.ColorSourceBlend", Verdict::InKey,
                     [](BlendState& b) { b.setColorSourceBlendProperty(Blend::DestinationColor); });
        MeasureBlend("BlendState.ColorDestinationBlend", Verdict::InKey,
                     [](BlendState& b) { b.setColorDestinationBlendProperty(Blend::SourceColor); });
        MeasureBlend("BlendState.AlphaSourceBlend", Verdict::InKey,
                     [](BlendState& b) { b.setAlphaSourceBlendProperty(Blend::DestinationAlpha); });
        MeasureBlend("BlendState.AlphaDestinationBlend", Verdict::InKey,
                     [](BlendState& b) { b.setAlphaDestinationBlendProperty(Blend::SourceAlpha); });
        MeasureBlend("BlendState.ColorBlendFunction", Verdict::InKey,
                     [](BlendState& b) { b.setColorBlendFunctionProperty(BlendFunction::ReverseSubtract); });
        MeasureBlend("BlendState.AlphaBlendFunction", Verdict::InKey,
                     [](BlendState& b) { b.setAlphaBlendFunctionProperty(BlendFunction::Max); });
        MeasureBlend("BlendState.ColorWriteChannels", Verdict::InKey,
                     [](BlendState& b) { b.setColorWriteChannelsProperty(ColorWriteChannels::Red); });
        MeasureBlend("BlendState.ColorWriteChannels1", Verdict::InKey,
                     [](BlendState& b) { b.setColorWriteChannels1Property(ColorWriteChannels::Green); });
        MeasureBlend("BlendState.ColorWriteChannels2", Verdict::InKey,
                     [](BlendState& b) { b.setColorWriteChannels2Property(ColorWriteChannels::Blue); });
        MeasureBlend("BlendState.ColorWriteChannels3", Verdict::InKey,
                     [](BlendState& b) { b.setColorWriteChannels3Property(ColorWriteChannels::Alpha); });
        MeasureBlend("BlendState.MultiSampleMask", Verdict::InKey,
                     [](BlendState& b) { b.setMultiSampleMaskProperty(static_cast<int>(0xFFFFFFFEu)); });
        // VK_DYNAMIC_STATE_BLEND_CONSTANTS, appended only when the equation consumes it.
        MeasureBlend("BlendState.BlendFactor", Verdict::Dynamic,
                     [](BlendState& b) { b.setBlendFactorProperty(Color(10, 20, 30, 40)); });

        // ---- DepthStencilState: 16 public fields ---------------------------------------------
        MeasureDepth("DepthStencilState.DepthBufferEnable", Verdict::InKey,
                     [](DepthStencilState& d) { d.setDepthBufferEnableProperty(false); });
        MeasureDepth("DepthStencilState.DepthBufferWriteEnable", Verdict::InKey,
                     [](DepthStencilState& d) { d.setDepthBufferWriteEnableProperty(false); });
        MeasureDepth("DepthStencilState.DepthBufferFunction", Verdict::InKey,
                     [](DepthStencilState& d) { d.setDepthBufferFunctionProperty(CompareFunction::Greater); });
        MeasureDepth("DepthStencilState.StencilEnable", Verdict::InKey,
                     [](DepthStencilState& d) { d.setStencilEnableProperty(true); });
        MeasureDepth("DepthStencilState.StencilFunction", Verdict::InKey,
                     [](DepthStencilState& d) { d.setStencilFunctionProperty(CompareFunction::Equal); });
        MeasureDepth("DepthStencilState.StencilPass", Verdict::InKey,
                     [](DepthStencilState& d) { d.setStencilPassProperty(StencilOperation::Replace); });
        MeasureDepth("DepthStencilState.StencilFail", Verdict::InKey,
                     [](DepthStencilState& d) { d.setStencilFailProperty(StencilOperation::Zero); });
        MeasureDepth("DepthStencilState.StencilDepthBufferFail", Verdict::InKey,
                     [](DepthStencilState& d) { d.setStencilDepthBufferFailProperty(StencilOperation::Invert); });
        MeasureDepth("DepthStencilState.TwoSidedStencilMode", Verdict::InKey,
                     [](DepthStencilState& d) { d.setTwoSidedStencilModeProperty(true); });
        MeasureDepth("DepthStencilState.CounterClockwiseStencilFunction", Verdict::InKey,
                     [](DepthStencilState& d) { d.setCounterClockwiseStencilFunctionProperty(CompareFunction::Never); });
        MeasureDepth("DepthStencilState.CounterClockwiseStencilPass", Verdict::InKey,
                     [](DepthStencilState& d) { d.setCounterClockwiseStencilPassProperty(StencilOperation::Increment); });
        MeasureDepth("DepthStencilState.CounterClockwiseStencilFail", Verdict::InKey,
                     [](DepthStencilState& d) { d.setCounterClockwiseStencilFailProperty(StencilOperation::Decrement); });
        MeasureDepth("DepthStencilState.CounterClockwiseStencilDepthBufferFail", Verdict::InKey,
                     [](DepthStencilState& d) { d.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::IncrementSaturation); });
        // The three genuinely dynamic ones: vkCmdSetStencilCompareMask / WriteMask / Reference,
        // issued per draw. One pipeline serves every value, so the key must NOT carry them.
        MeasureDepth("DepthStencilState.StencilMask", Verdict::Dynamic,
                     [](DepthStencilState& d) { d.setStencilMaskProperty(0x0F); });
        MeasureDepth("DepthStencilState.StencilWriteMask", Verdict::Dynamic,
                     [](DepthStencilState& d) { d.setStencilWriteMaskProperty(0x0F); });
        MeasureDepth("DepthStencilState.ReferenceStencil", Verdict::Dynamic,
                     [](DepthStencilState& d) { d.setReferenceStencilProperty(3); });

        // ---- RasterizerState: 6 public fields -------------------------------------------------
        MeasureRaster("RasterizerState.CullMode", Verdict::InKey,
                      [](RasterizerState& r) { r.setCullModeProperty(CullMode::CullClockwiseFace); });
        MeasureRaster("RasterizerState.FillMode", Verdict::InKey,
                      [](RasterizerState& r) { r.setFillModeProperty(FillMode::WireFrame); });
        MeasureRaster("RasterizerState.DepthBias", Verdict::Dynamic,
                      [](RasterizerState& r) { r.setDepthBiasProperty(0.25f); });
        MeasureRaster("RasterizerState.SlopeScaleDepthBias", Verdict::Dynamic,
                      [](RasterizerState& r) { r.setSlopeScaleDepthBiasProperty(0.5f); });
        MeasureRaster("RasterizerState.ScissorTestEnable", Verdict::Dynamic,
                      [](RasterizerState& r) { r.setScissorTestEnableProperty(true); });
        // The audit's one DROPPED field. IGraphicsRenderer::ApplyRasterizerState carries no
        // parameter for it, so no renderer -- Vulkan, EasyGL or any other -- can see it. The
        // cardinality below shows it does not reach the key; the static_asserts at the top of this
        // file are what show it does not reach the renderer either, which is the part that makes
        // this DROPPED rather than DYNAMIC.
        MeasureRaster("RasterizerState.MultiSampleAntiAlias", Verdict::Dropped,
                      [](RasterizerState& r) { r.setMultiSampleAntiAliasProperty(false); });
        check(TakesFiveRasterizerArgs<RendererBase>::value &&
                  !TakesSixRasterizerArgs<RendererBase>::value,
              "RasterizerState.MultiSampleAntiAlias                DROPPED at the interface -- "
              "IGraphicsRenderer::ApplyRasterizerState takes five arguments and none of them is "
              "this flag, so no renderer receives it (detector positive control included)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        vb_.reset();
        Exit();
    }

public:
    VulkanPipelineKeyStateCoverageTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanPipelineKeyStateCoverageTest game;
    game.Run();
    return game.getResult();
}
