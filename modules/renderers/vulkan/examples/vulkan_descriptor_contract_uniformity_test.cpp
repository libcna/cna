// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-391: every descriptor-set allocation in this renderer answers the
// same way when the device refuses it.
//
// F-06 counted eleven `vkAllocateDescriptorSets` sites and found three contracts among them:
// three threw, seven returned `VK_NULL_HANDLE`, and one substituted a white texture. VULKAN-390
// fixed the substituting one and measured what the null-returning shape actually costs -- with the
// old fallback restored, the renderer bound `VK_NULL_HANDLE` and **segfaulted**, with the layer
// reporting `pDescriptorSets[0] (VK_NULL_HANDLE)`. A null is not a value a caller can act on; the
// only thing to do with a descriptor set is bind it.
//
// So the contract is: **a caller never receives a null, and a refusal is by name, at the draw.**
// This file holds every family to it.
//
// Each family is exercised the same way, and the shape matters:
//
//   1. draw it once so its pool, its cache and the shared default-white set all exist;
//   2. arm ONE injected allocation failure and draw the same family with a DIFFERENT texture, so
//      the cache misses and the allocation is really attempted;
//   3. assert the draw threw, and that the message names that family;
//   4. disarm and repeat the same draw, asserting it now succeeds -- otherwise a test could pass
//      by leaving the renderer permanently broken.
//
// Step 1 is what makes step 2 honest: without the warm-up the injected failure could be consumed
// by the default white texture's own set, and the test would assert the wrong refusal.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 64;

    struct Corner { float x, y; };
    constexpr Corner kQuad[6] = {
        { -1.f,  1.f }, { -1.f, -1.f }, {  1.f, -1.f },
        { -1.f,  1.f }, {  1.f, -1.f }, {  1.f,  1.f },
    };

    void PutFloat2(std::uint8_t* at, float a, float b)
    { const float v[2] = { a, b }; std::memcpy(at, v, sizeof(v)); }
    void PutFloat3(std::uint8_t* at, float a, float b, float c)
    { const float v[3] = { a, b, c }; std::memcpy(at, v, sizeof(v)); }
    void PutFloat4(std::uint8_t* at, float a, float b, float c, float d)
    { const float v[4] = { a, b, c, d }; std::memcpy(at, v, sizeof(v)); }
}

class VulkanDescriptorContractUniformityTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D>   texA_;
    std::unique_ptr<Texture2D>   texB_;
    std::unique_ptr<TextureCube> cube_;
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

    /// Records of `stride` bytes with whichever elements the family needs.
    static std::vector<std::uint8_t> Records(int stride, int normalOffset, int tangentOffset,
                                             int uvOffset, int uv1Offset, int weightOffset,
                                             int indexOffset)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * stride), 0);
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytes.data() + i * stride;
            PutFloat3(v + 0, kQuad[i].x, kQuad[i].y, 0.0f);
            if (normalOffset  >= 0) PutFloat3(v + normalOffset,  0.0f, 0.0f, 1.0f);
            if (tangentOffset >= 0) PutFloat4(v + tangentOffset, 1.0f, 0.0f, 0.0f, 1.0f);
            if (uvOffset      >= 0) PutFloat2(v + uvOffset,  0.25f, 0.5f);
            if (uv1Offset     >= 0) PutFloat2(v + uv1Offset, 0.25f, 0.5f);
            if (weightOffset  >= 0) PutFloat4(v + weightOffset, 1.0f, 0.0f, 0.0f, 0.0f);
            if (indexOffset   >= 0) { v[indexOffset] = 0; v[indexOffset + 1] = 0;
                                      v[indexOffset + 2] = 0; v[indexOffset + 3] = 0; }
        }
        return bytes;
    }

    /// Builds the family's effect around `texture`, draws the quad once, and returns the refusal
    /// text -- or "" when it drew. The factory returns the effect by value so it OUTLIVES the
    /// draw: an effect destroyed before `DrawPrimitives` leaves nothing applied, which is a
    /// mistake this file made on its first run and one the harness cannot detect for itself.
    using EffectFactory = std::function<std::unique_ptr<Effect>(Texture2D*)>;

    std::string DrawOnce(GraphicsDevice& dev, VertexBuffer& vb, const EffectFactory& make,
                         Texture2D* texture)
    {
        try {
            dev.Clear(Color(0, 0, 0, 255));
            std::unique_ptr<Effect> fx = make(texture);
            fx->Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            Color probe(0, 0, 0, 0);
            const Rectangle at(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&at, &probe, 0, 1);
        } catch (const std::exception& e) {
            return e.what();
        }
        return {};
    }

    /// The four-step body every family goes through. `expected` is the substring the refusal must
    /// name, so a message pointing at the wrong family fails rather than passing.
    /// @param skipFirst how many allocations to let through before the injected failure. Not
    ///        zero for every family: the textured BasicEffect route takes a set from the SHARED
    ///        combined-image-sampler pool before it takes one from its own, and that pool chains
    ///        rather than refusing (VULKAN-390), so a failure injected with no skip is absorbed
    ///        there and this family's arm never runs. That is not a defect in either place -- it
    ///        is two different pools with two different recovery strategies, and the skip is how
    ///        the test reaches past the first to the second.
    void RunFamily(GraphicsDevice& dev, const char* label, VertexBuffer& vb,
                   const EffectFactory& make, const char* expected, std::uint32_t skipFirst = 0)
    {
        const std::string warm = DrawOnce(dev, vb, make, texA_.get());
        if (!warm.empty()) {
            check(false, std::string(label) + " warm-up draw succeeds", "refused: " + warm);
            return;
        }

        VulkanRenderer::SetDescriptorAllocationFailuresForTestEXT(1, skipFirst);
        const std::string refused = DrawOnce(dev, vb, make, texB_.get());
        VulkanRenderer::SetDescriptorAllocationFailuresForTestEXT(0);

        const bool named = refused.find(expected) != std::string::npos;
        const bool contract =
            refused.find("Refused rather than binding a null descriptor set") != std::string::npos;
        check(!refused.empty() && named && contract,
              std::string(label) + " refuses by name instead of handing back a null set",
              refused.empty() ? "the draw SUCCEEDED with an allocation failure injected"
                              : refused);

        const std::string recovered = DrawOnce(dev, vb, make, texB_.get());
        check(recovered.empty(),
              std::string(label) + " and the refusal left the renderer usable",
              recovered.empty() ? "the same draw succeeds once the injection is disarmed"
                                : "still refused: " + recovered);
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
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        dev.getSamplerStatesProperty()[1] = SamplerState::PointClamp;

        const Color a[2] = { Color(230, 30, 30, 255), Color(30, 60, 230, 255) };
        const Color b[2] = { Color(30, 230, 30, 255), Color(230, 230, 30, 255) };
        texA_ = std::make_unique<Texture2D>(dev, 2, 1);  texA_->SetData(a, 2);
        texB_ = std::make_unique<Texture2D>(dev, 2, 1);  texB_->SetData(b, 2);
        cube_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face) {
            Color grey(128, 128, 128, 255);
            cube_->SetData(static_cast<CubeMapFace>(face), &grey, 1);
        }

        const auto neutral = [](auto& fx) {
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
        };

        // ---- the textured BasicEffect (FogTex3D pool), stride 20 --------------------------------
        {
            VertexDeclaration decl(20, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0)});
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            const auto bytes = Records(20, -1, -1, 12, -1, -1, -1);
            vb.SetDataRaw(bytes.data(), 6, 20);
            RunFamily(dev, "A textured BasicEffect", vb, [&](Texture2D* t) -> std::unique_ptr<Effect> {
                auto fx = std::make_unique<BasicEffect>(dev);
                fx->setTextureEnabledProperty(true);
                fx->setTextureProperty(t);
                fx->setLightingEnabledProperty(false);
                fx->setFogEnabledProperty(false);
                neutral(*fx);
                return fx;
            }, "the textured BasicEffect", /*skipFirst=*/1);
        }

        // ---- the lit-textured BasicEffect, stride 32 --------------------------------------------
        {
            VertexDeclaration decl(32, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0)});
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            const auto bytes = Records(32, 12, -1, 24, -1, -1, -1);
            vb.SetDataRaw(bytes.data(), 6, 32);
            RunFamily(dev, "B lit-textured BasicEffect", vb, [&](Texture2D* t) -> std::unique_ptr<Effect> {
                auto fx = std::make_unique<BasicEffect>(dev);
                fx->setTextureEnabledProperty(true);
                fx->setTextureProperty(t);
                fx->setLightingEnabledProperty(true);
                fx->setFogEnabledProperty(false);
                neutral(*fx);
                return fx;
            }, "the lit-textured BasicEffect");
        }

        // ---- DualTextureEffect, stride 20 --------------------------------------------------------
        {
            VertexDeclaration decl(20, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0)});
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            const auto bytes = Records(20, -1, -1, 12, -1, -1, -1);
            vb.SetDataRaw(bytes.data(), 6, 20);
            RunFamily(dev, "C DualTextureEffect", vb, [&](Texture2D* t) -> std::unique_ptr<Effect> {
                auto fx = std::make_unique<DualTextureEffect>(dev);
                fx->setTextureProperty(t);
                fx->setTexture2Property(texA_.get());
                neutral(*fx);
                return fx;
            }, "DualTextureEffect");
        }

        // ---- EnvironmentMapEffect, stride 32 -----------------------------------------------------
        {
            VertexDeclaration decl(32, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0)});
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            const auto bytes = Records(32, 12, -1, 24, -1, -1, -1);
            vb.SetDataRaw(bytes.data(), 6, 32);
            RunFamily(dev, "D EnvironmentMapEffect", vb, [&](Texture2D* t) -> std::unique_ptr<Effect> {
                auto fx = std::make_unique<EnvironmentMapEffect>(dev);
                fx->setTextureProperty(t);
                fx->setEnvironmentMapProperty(cube_.get());
                fx->setEnvironmentMapAmountProperty(0.0f);
                fx->setFresnelFactorProperty(0.0f);
                fx->setEnvironmentMapSpecularProperty(Vector3::Zero);
                fx->DirectionalLight0.setEnabledProperty(false);
                fx->DirectionalLight1.setEnabledProperty(false);
                fx->DirectionalLight2.setEnabledProperty(false);
                neutral(*fx);
                return fx;
            }, "EnvironmentMapEffect");
        }

        // ---- SkinnedEffect, stride 52 -------------------------------------------------------------
        {
            VertexDeclaration decl(52, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::BlendWeight, 0),
                VertexElement(48, VertexElementFormat::Byte4,
                              VertexElementUsage::BlendIndices, 0)});
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            const auto bytes = Records(52, 12, -1, 24, -1, 32, 48);
            vb.SetDataRaw(bytes.data(), 6, 52);
            RunFamily(dev, "E SkinnedEffect", vb, [&](Texture2D* t) -> std::unique_ptr<Effect> {
                auto fx = std::make_unique<SkinnedEffect>(dev);
                fx->setTextureProperty(t);
                fx->setWeightsPerVertexProperty(1);
                std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
                fx->SetBoneTransforms(bones);
                fx->DirectionalLight0.setEnabledProperty(false);
                fx->DirectionalLight1.setEnabledProperty(false);
                fx->DirectionalLight2.setEnabledProperty(false);
                neutral(*fx);
                return fx;
            }, "SkinnedEffect");
        }

        // ---- PbrEffect, stride 48 ------------------------------------------------------------------
        {
            VertexDeclaration decl(48, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
                VertexElement(40, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0)});
            VertexBuffer vb(dev, decl, 6, BufferUsage::None);
            const auto bytes = Records(48, 12, 24, 40, -1, -1, -1);
            vb.SetDataRaw(bytes.data(), 6, 48);
            RunFamily(dev, "F PbrEffect", vb, [&](Texture2D* t) -> std::unique_ptr<Effect> {
                auto fx = std::make_unique<PbrEffect>(dev);
                fx->setTextureProperty(t);
                fx->setMetallicFactorProperty(0.0f);
                fx->setRoughnessFactorProperty(1.0f);
                fx->setBaseColorTextureIsSrgbEXTProperty(false);
                fx->setEncodeOutputToSrgbEXTProperty(false);
                fx->setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx->DirectionalLight0.setEnabledProperty(false);
                fx->DirectionalLight1.setEnabledProperty(false);
                fx->DirectionalLight2.setEnabledProperty(false);
                neutral(*fx);
                return fx;
            }, "PbrEffect");
        }

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "G no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanDescriptorContractUniformityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanDescriptorContractUniformityTest g;
    g.Run();
    return g.getResult();
}
