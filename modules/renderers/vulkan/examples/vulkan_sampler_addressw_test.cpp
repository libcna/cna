// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-094: `SamplerState.AddressW` on Vulkan -- and what the audit found
// underneath it.
//
// The row proposed sampling a `Texture3D` with W wrap versus W clamp at a coordinate outside
// [0,1] and asserting two different pixels. That test cannot be written on this renderer, and the
// reason is the finding: **Vulkan has no Texture3D sampling path at all.**
// `IGraphicsRenderer::BindTexture3D` is a virtual whose default body is `{}`; EasyGL overrides it,
// this renderer does not, and `VulkanTexture3DRenderer` has no sampling interface or descriptor
// path. `Texture3D` here is upload/readback storage, which is exactly what the capability profile
// already calls it -- `RendererFeature::Texture3DStorage`.
//
// So `AddressW` is plumbed and inert, and the public API that would make it observable accepts a
// binding and silently does nothing with it. This file pins both, because "the row could not be
// written" is not evidence and a gap nobody measured is a gap nobody fixes.
//
//   A  `AddressW` reaches the renderer: two sampler states differing ONLY in AddressW produce two
//      distinct VkSamplers. So it is not being dropped on the way down.
//   B  ...and it changes no pixel, because 2D sampling never consults the W axis. Same draw, same
//      colour, both address modes.
//   C  `ShaderEffect::SetTexture(unit, Texture3D&)` -- a public CNAEXT API EasyGL implements -- is
//      accepted here and does nothing. This is a CHARACTERISATION of today's behaviour, named as
//      such: `VULKAN-163` turns it into a refusal, and this leg becomes that assertion.
//   D  No validation messages.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 32;
    const Color kLeft(230, 30, 30, 255);
    const Color kRight(30, 60, 230, 255);
}

class VulkanSamplerAddressWTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D>   strip_;
    std::unique_ptr<VertexBuffer> quad_;
    int  pass_ = 0;
    int  fail_ = 0;
    bool done_ = false;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        std::fflush(stdout);
        if (ok) ++pass_; else ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    static std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

    Color DrawWith(GraphicsDevice& dev, const SamplerState& state)
    {
        dev.getSamplerStatesProperty()[0] = state;
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(strip_.get());
        fx.setLightingEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        dev.Clear(Color(0, 0, 0, 255));
        fx.Apply();
        dev.SetVertexBuffer(quad_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        Color got(0, 0, 0, 0);
        const Rectangle at(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&at, &got, 0, 1);
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetDepthTestEnabled(false);

        const Color texels[2] = { kLeft, kRight };
        strip_ = std::make_unique<Texture2D>(dev, 2, 1);
        strip_->SetData(texels, 2);

        std::uint8_t bytes[6 * 20]{};
        constexpr float kX[6] = { -1.f, -1.f,  1.f, -1.f,  1.f,  1.f };
        constexpr float kY[6] = {  1.f, -1.f, -1.f,  1.f, -1.f,  1.f };
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytes + i * 20;
            const float pos[3] = { kX[i], kY[i], 0.0f };
            const float uv[2]  = { 0.25f, 0.5f };
            std::memcpy(v + 0,  pos, sizeof(pos));
            std::memcpy(v + 12, uv,  sizeof(uv));
        }
        VertexDeclaration decl(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0)});
        quad_ = std::make_unique<VertexBuffer>(dev, decl, 6, BufferUsage::None);
        quad_->SetDataRaw(bytes, 6, 20);

        // ---- A: AddressW reaches the renderer -----------------------------------------------------
        // Neither W value equals the state's U/V (Clamp), and that is load-bearing.
        // `ApplySamplerState` sets `addressW = addressU` and REBUILDS before
        // `ApplySamplerAddressW` overwrites it, so the (Clamp,Clamp,Clamp) sampler is created and
        // cached as a side effect of every apply. A leg that used W=Clamp as its second value
        // would find that sampler already present and read "AddressW does nothing" -- which is what
        // the first version of this file concluded, wrongly.
        SamplerState mirrorW = SamplerState::PointClamp;
        mirrorW.setAddressWProperty(TextureAddressMode::Mirror);
        SamplerState wrapW = SamplerState::PointClamp;
        wrapW.setAddressWProperty(TextureAddressMode::Wrap);

        // Measured from BEFORE the first draw. Comparing only the two draws to each other would
        // have been wrong in a way that looks right: whichever state the device already had cached
        // adds nothing, so the pair could differ while the count did not move. (It did exactly
        // that on the first run of this file.)
        const std::size_t base = Renderer().GetSamplerCacheSizeEXT();
        const Color first  = DrawWith(dev, mirrorW);
        const std::size_t afterMirror = Renderer().GetSamplerCacheSizeEXT();
        const Color second = DrawWith(dev, wrapW);
        const std::size_t afterWrap = Renderer().GetSamplerCacheSizeEXT();
        (void)DrawWith(dev, wrapW);
        const std::size_t afterRepeat = Renderer().GetSamplerCacheSizeEXT();
        // The control this leg cannot do without: AddressU is unquestionably part of the sampler
        // key, so if changing IT does not add an entry either, the harness is not applying sampler
        // states at all and nothing below means anything.
        SamplerState wrapU = SamplerState::PointClamp;
        wrapU.setAddressUProperty(TextureAddressMode::Wrap);
        const std::size_t beforeU = Renderer().GetSamplerCacheSizeEXT();
        (void)DrawWith(dev, wrapU);
        const std::size_t afterU = Renderer().GetSamplerCacheSizeEXT();
        check(afterU > beforeU,
              "A0 CONTROL: changing AddressU adds a sampler, so this harness really does apply "
              "sampler states",
              std::to_string(beforeU) + " -> " + std::to_string(afterU));

        // A1: the same question asked one layer down, so a negative result upstairs can be
        // attributed. If the renderer's own entry point does build a distinct sampler, the mode is
        // being lost ABOVE it; if it does not, the renderer is where it goes.
        {
            // Through the INTERFACE, where the entry point is public -- the override is private on
            // the concrete class, which is itself worth noticing: only the shared layer calls it.
            const std::size_t beforeDirect = Renderer().GetSamplerCacheSizeEXT();
            dev.GetRenderer().ApplySamplerAddressW(
                0, static_cast<int>(TextureAddressMode::Mirror));
            const std::size_t afterDirect = Renderer().GetSamplerCacheSizeEXT();
            check(afterDirect > beforeDirect,
                  "A1 the renderer's own ApplySamplerAddressW does build a distinct sampler",
                  std::to_string(beforeDirect) + " -> " + std::to_string(afterDirect));
        }

        check(afterWrap > afterMirror && afterRepeat == afterWrap,
              "A two sampler states differing ONLY in AddressW build two distinct VkSamplers, and "
              "repeating one builds no further -- so the mode reaches the key intact",
              "base=" + std::to_string(base) + " W=Mirror:" + std::to_string(afterMirror)
                  + " W=Wrap:" + std::to_string(afterWrap)
                  + " W=Wrap again:" + std::to_string(afterRepeat));

        // ---- B: and it changes no pixel ------------------------------------------------------------
        check(Show(first) == Show(second),
              "B and it changes no pixel: 2D sampling never consults the W axis",
              "W=Mirror " + Show(first) + " vs W=Wrap " + Show(second));

        // ---- C: the API that would make it observable ----------------------------------------------
        // Characterisation, not approval. VULKAN-163 turns this into a refusal and rewrites the leg.
        {
            Texture3D volume(dev, 2, 2, 2, false, SurfaceFormat::Color);
            std::vector<Color> voxels(8, kLeft);
            volume.SetData(voxels.data(), static_cast<int>(voxels.size()));

            // A ShaderEffect is the only route that could carry a sampler3D. The SPIR-V here is
            // irrelevant to the leg -- what is measured is whether the BINDING is acknowledged.
            std::string vert, frag;
            ShaderEffect effect(dev, vert, frag);
            std::string how = "accepted and ignored";
            try {
                effect.SetTexture(0, volume);
            } catch (const std::exception& e) {
                how = std::string("refused: ") + e.what();
            }
            check(how == "accepted and ignored",
                  "C TODAY: binding a Texture3D to a ShaderEffect is accepted and does nothing -- "
                  "IGraphicsRenderer::BindTexture3D is a `{}` default this renderer never overrode "
                  "(finding F-31, owned by VULKAN-163)",
                  how);
        }

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "D no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanSamplerAddressWTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanSamplerAddressWTest g;
    g.Run();
    return g.getResult();
}
