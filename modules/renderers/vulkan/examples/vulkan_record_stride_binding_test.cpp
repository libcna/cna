// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-158 (finding F-26): the declaration decides where each element sits
// INSIDE a record; the record stride decides where the next record starts. Both have to reach the
// pipeline, and until this row three families only got the first.
//
// `VULKAN-146`..`VULKAN-149` moved every stock family's attribute offsets to the caller's
// VertexDeclaration. What they did not check is the `VkVertexInputBindingDescription`, and three
// factories baked a constant there:
//
//     GetOrCreatePipelineEnvMap3D        bind{ 0, kEnvStride /* 32 */, ... }
//     GetOrCreatePipelineSkinned3D       bind{ 0, skinnedStride /* 52 or 56 */, ... }
//     GetOrCreatePipelineSkinned3DVertexLit                    likewise
//
// So a declaration at a padded stride produced correct offsets into a record the fetch never
// reached: vertex 0 lands, vertex 1 is read from the middle of vertex 0, and the quad collapses.
// That is why every leg here probes TWO widely separated pixels -- one vertex being right is
// exactly the symptom.
//
// The pipeline KEY had the mirror of the same gap on every converted family: `MakeExt3DKey`
// buckets an unlisted stride and the layout hash covers offsets only, so two records with the same
// element offsets and different strides shared one pipeline, with the binding stride of whichever
// drew first.
//
//   A  EnvironmentMapEffect at stride 40 -- its canonical 32-byte record with eight bytes appended.
//   B  SkinnedEffect at stride 60 -- its canonical 52 plus eight.
//   C  PbrEffect at stride 64. The CONTROL: that factory already bound the record's own stride, so
//      this leg passed before the fix and must still pass after it.
//   D  The same element offsets at two different strides must leave TWO pipeline entries.
//   E  No validation messages.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
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
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 64;
    const Color kLeft (230, 30, 30, 255);
    const Color kRight(30, 60, 230, 255);

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

class VulkanRecordStrideBindingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D>   strip_;
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

    static bool Near(const Color& got, const Color& want)
    {
        const int tol = 24;
        return std::abs(int(got.getRProperty()) - int(want.getRProperty())) <= tol
            && std::abs(int(got.getGProperty()) - int(want.getGProperty())) <= tol
            && std::abs(int(got.getBProperty()) - int(want.getBProperty())) <= tol;
    }
    static std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

    Color ReadAt(GraphicsDevice& dev, int x, int y)
    {
        Color got(0, 0, 0, 0);
        const Rectangle probe(x, y, 1, 1);
        dev.GetBackBufferData(&probe, &got, 0, 1);
        return got;
    }

    /// A full-screen quad of `stride`-byte records: position at 0, and whichever of normal,
    /// tangent, UV, blend weights and blend indices the caller asks for, at the offsets it names.
    /// Everything else is padding, which is the point.
    static std::vector<std::uint8_t> BuildRecords(int stride, int normalOffset, int tangentOffset,
                                                  int uvOffset, int weightOffset, int indexOffset)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * stride), 0);
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytes.data() + i * stride;
            PutFloat3(v + 0, kQuad[i].x, kQuad[i].y, 0.0f);
            if (normalOffset  >= 0) PutFloat3(v + normalOffset,  0.0f, 0.0f, 1.0f);
            if (tangentOffset >= 0) PutFloat4(v + tangentOffset, 1.0f, 0.0f, 0.0f, 1.0f);
            if (uvOffset      >= 0) PutFloat2(v + uvOffset, 0.25f, 0.5f);   // the LEFT texel
            if (weightOffset  >= 0) PutFloat4(v + weightOffset, 1.0f, 0.0f, 0.0f, 0.0f);
            if (indexOffset   >= 0) { v[indexOffset] = 0; v[indexOffset + 1] = 0;
                                      v[indexOffset + 2] = 0; v[indexOffset + 3] = 0; }
        }
        return bytes;
    }

    /// One draw, probing two widely separated pixels. A collapsed quad lights neither, and a quad
    /// whose first vertex alone survived lights at most one.
    template <typename Configure>
    void RunPadded(GraphicsDevice& dev, const char* name, int stride,
                   const std::vector<VertexElement>& elements, int normalOffset, int tangentOffset,
                   int uvOffset, int weightOffset, int indexOffset, Configure configure)
    {
        VertexDeclaration decl(stride, elements);
        VertexBuffer vb(dev, decl, 6, BufferUsage::None);
        const auto bytes = BuildRecords(stride, normalOffset, tangentOffset, uvOffset,
                                        weightOffset, indexOffset);
        vb.SetDataRaw(bytes.data(), 6, stride);

        Color a(0, 0, 0, 0), b(0, 0, 0, 0);
        std::string refusal;
        try {
            dev.Clear(Color(0, 0, 0, 255));
            configure(vb);
            a = ReadAt(dev, 8, 8);
            b = ReadAt(dev, kSize - 8, kSize - 8);
        } catch (const std::exception& e) {
            refusal = std::string("refused: ") + e.what();
        }
        check(refusal.empty() && Near(a, kLeft) && Near(b, kLeft),
              std::string(name) + " at stride " + std::to_string(stride) +
                  " covers the whole quad, not just its first vertex",
              refusal.empty() ? "(8,8)=" + Show(a) + " (56,56)=" + Show(b) + ", expected "
                                    + Show(kLeft) + " at both"
                              : refusal);
    }

    void DrawWith(GraphicsDevice& dev, Effect& effect, VertexBuffer& vb)
    {
        effect.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
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

        strip_ = std::make_unique<Texture2D>(dev, 2, 1);
        const Color texels[2] = { kLeft, kRight };
        strip_->SetData(texels, 2);

        cube_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face) {
            Color grey(128, 128, 128, 255);
            cube_->SetData(static_cast<CubeMapFace>(face), &grey, 1);
        }

        const auto envElements = [](int stride) {
            (void)stride;
            return std::vector<VertexElement>{
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
            };
        };

        // ---- A: EnvironmentMapEffect at stride 40 ----------------------------------------------
        RunPadded(dev, "A EnvironmentMapEffect", 40, envElements(40), 12, -1, 24, -1, -1,
                  [&](VertexBuffer& vb) {
                      EnvironmentMapEffect fx(dev);
                      fx.setTextureProperty(strip_.get());
                      fx.setEnvironmentMapProperty(cube_.get());
                      fx.setEnvironmentMapAmountProperty(0.0f);
                      fx.setFresnelFactorProperty(0.0f);
                      fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
                      fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                      fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                      fx.DirectionalLight0.setEnabledProperty(false);
                      fx.DirectionalLight1.setEnabledProperty(false);
                      fx.DirectionalLight2.setEnabledProperty(false);
                      fx.setWorldProperty(Matrix::getIdentityProperty());
                      fx.setViewProperty(Matrix::getIdentityProperty());
                      fx.setProjectionProperty(Matrix::getIdentityProperty());
                      DrawWith(dev, fx, vb);
                  });

        // ---- B: SkinnedEffect at stride 60 ------------------------------------------------------
        {
            const std::vector<VertexElement> elements{
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::BlendWeight, 0),
                VertexElement(48, VertexElementFormat::Byte4,
                              VertexElementUsage::BlendIndices, 0),
            };
            RunPadded(dev, "B SkinnedEffect", 60, elements, 12, -1, 24, 32, 48,
                      [&](VertexBuffer& vb) {
                          SkinnedEffect fx(dev);
                          fx.setTextureProperty(strip_.get());
                          fx.setWeightsPerVertexProperty(1);
                          std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
                          fx.SetBoneTransforms(bones);
                          fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                          fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                          fx.DirectionalLight0.setEnabledProperty(false);
                          fx.DirectionalLight1.setEnabledProperty(false);
                          fx.DirectionalLight2.setEnabledProperty(false);
                          fx.setWorldProperty(Matrix::getIdentityProperty());
                          fx.setViewProperty(Matrix::getIdentityProperty());
                          fx.setProjectionProperty(Matrix::getIdentityProperty());
                          DrawWith(dev, fx, vb);
                      });
        }

        // ---- C: the control -- PbrEffect already bound the record's own stride -------------------
        {
            const std::vector<VertexElement> elements{
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(16, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
                VertexElement(52, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
            };
            RunPadded(dev, "C PbrEffect (control: already correct before this row)", 64, elements,
                      16, 32, 52, -1, -1,
                      [&](VertexBuffer& vb) {
                          PbrEffect fx(dev);
                          fx.setTextureProperty(strip_.get());
                          fx.setMetallicFactorProperty(0.0f);
                          fx.setRoughnessFactorProperty(1.0f);
                          fx.setBaseColorTextureIsSrgbEXTProperty(false);
                          fx.setEncodeOutputToSrgbEXTProperty(false);
                          fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                          fx.DirectionalLight0.setEnabledProperty(false);
                          fx.DirectionalLight1.setEnabledProperty(false);
                          fx.DirectionalLight2.setEnabledProperty(false);
                          fx.setWorldProperty(Matrix::getIdentityProperty());
                          fx.setViewProperty(Matrix::getIdentityProperty());
                          fx.setProjectionProperty(Matrix::getIdentityProperty());
                          DrawWith(dev, fx, vb);
                      });
        }

        // ---- D: two strides, one set of offsets, two pipelines -----------------------------------
        {
            const std::size_t before = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
            RunPadded(dev, "D EnvironmentMapEffect (second stride)", 48, envElements(48), 12, -1,
                      24, -1, -1,
                      [&](VertexBuffer& vb) {
                          EnvironmentMapEffect fx(dev);
                          fx.setTextureProperty(strip_.get());
                          fx.setEnvironmentMapProperty(cube_.get());
                          fx.setEnvironmentMapAmountProperty(0.0f);
                          fx.setFresnelFactorProperty(0.0f);
                          fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
                          fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                          fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                          fx.DirectionalLight0.setEnabledProperty(false);
                          fx.DirectionalLight1.setEnabledProperty(false);
                          fx.DirectionalLight2.setEnabledProperty(false);
                          fx.setWorldProperty(Matrix::getIdentityProperty());
                          fx.setViewProperty(Matrix::getIdentityProperty());
                          fx.setProjectionProperty(Matrix::getIdentityProperty());
                          DrawWith(dev, fx, vb);
                      });
            const std::size_t after = Renderer().GetGraphicsPipelineCacheEntryCountEXT();
            check(after >= before + 1,
                  "D a second stride with identical element offsets builds its own pipeline",
                  std::to_string(before) + " -> " + std::to_string(after));
        }

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "E no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanRecordStrideBindingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanRecordStrideBindingTest g;
    g.Run();
    return g.getResult();
}
