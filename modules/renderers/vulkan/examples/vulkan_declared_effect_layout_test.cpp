// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-147: the stock EFFECT pipelines take their vertex attribute offsets
// from the caller's VertexDeclaration too, not from the byte stride.
//
// VULKAN-146 did this for the BasicEffect bundle. These three families had the same defect, and
// alpha test had it in its most explicit form: one vertex shader serves strides 20 and 32 with the
// UV remapped to location 1, and the factory guessed the offset as
//
//     uint32_t uvOffset = (stride == 32) ? 24 : 12;   // past a float3 normal, else stride 20
//
// which is a guess about what a 32-byte record contains. A declaration says.
//
// Every leg draws the SAME geometry twice at the SAME stride, with the UV element at a different
// byte offset, over a texture whose left and right halves are different colours and UVs chosen so
// that the two offsets sample opposite halves. A renderer that took the offset from the stride
// gives both draws the same pixel; one that takes it from the declaration gives two.
//
//   A  AlphaTestEffect, stride 32, UV at 24 (canonical) versus at 12.
//   B  EnvironmentMapEffect, stride 32, the same pair. Its shader consumes the UV as well, so the
//      same discriminator works without a second cube map.
//   C  SkinnedEffect, stride 52, UV at 24 (canonical) versus at 12, with one identity bone -- a
//      mathematically neutral skin, so the pixel is decided by the sampled texel alone.
//   D  Each pair leaves TWO pipeline entries behind, not one. This is the half that fails if the
//      declaration reaches the pipeline but not its key, which would otherwise show up only as
//      whichever declaration drew first winning for the rest of the process.
//   D2 VULKAN-148: a PbrEffect draw at a stride the renderer's own list does not contain --
//      RequirePbrStrideEXT names 48 and 60 -- renders, because the declaration says where each of
//      pbr3d.vert.glsl's inputs lives. The same record without a declaration is still refused BY
//      NAME, which is the half that must not be lost: the stride list is what a buffer with
//      nothing to go on is judged by.
//   E  No validation messages.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
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

    // A 2x1 texture: left texel red, right texel blue. Point-sampled, so the pixel says which
    // half the UV addressed and nothing else.
    const Color kLeft (230, 30, 30, 255);
    const Color kRight(30, 60, 230, 255);

    // Full-screen quad. `uLeft` puts every vertex on the left texel, `uRight` on the right one, so
    // the two UV OFFSETS in a record select different halves of the texture.
    struct Corner { float x, y; };
    constexpr Corner kQuad[6] = {
        { -1.f,  1.f }, { -1.f, -1.f }, {  1.f, -1.f },
        { -1.f,  1.f }, {  1.f, -1.f }, {  1.f,  1.f },
    };

    void PutFloat3(std::uint8_t* at, float a, float b, float c)
    { const float v[3] = { a, b, c }; std::memcpy(at, v, sizeof(v)); }
    void PutFloat4(std::uint8_t* at, float a, float b, float c, float d)
    { const float v[4] = { a, b, c, d }; std::memcpy(at, v, sizeof(v)); }
    void PutFloat2(std::uint8_t* at, float a, float b)
    { const float v[2] = { a, b }; std::memcpy(at, v, sizeof(v)); }
}

class VulkanDeclaredEffectLayoutTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D>  strip_;
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

    static bool Matches(const Color& got, const Color& want)
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

    Color ReadCentre(GraphicsDevice& dev)
    {
        Color got(0, 0, 0, 0);
        const Rectangle probe(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&probe, &got, 0, 1);
        return got;
    }

    /// Builds a `stride`-byte record set with position at 0, an optional normal, the UV at
    /// `uvOffset`, and optional skinning data -- i.e. whatever the caller's declaration says.
    static std::vector<std::uint8_t> BuildRecords(int stride, int normalOffset, int uvOffset,
                                                  float u, int weightOffset, int indexOffset)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * stride), 0);
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytes.data() + i * stride;
            PutFloat3(v + 0, kQuad[i].x, kQuad[i].y, 0.0f);
            if (normalOffset >= 0) PutFloat3(v + normalOffset, 0.0f, 0.0f, 1.0f);
            PutFloat2(v + uvOffset, u, 0.5f);
            if (weightOffset >= 0) PutFloat4(v + weightOffset, 1.0f, 0.0f, 0.0f, 0.0f);
            if (indexOffset >= 0) { v[indexOffset] = 0; v[indexOffset + 1] = 0;
                                    v[indexOffset + 2] = 0; v[indexOffset + 3] = 0; }
        }
        return bytes;
    }

    /// One draw with `effect` already configured; returns the centre pixel.
    Color DrawWith(GraphicsDevice& dev, Effect& effect, VertexBuffer& vb)
    {
        dev.Clear(Color(0, 0, 0, 255));
        effect.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        return ReadCentre(dev);
    }

    /// The shared body of all three legs: same stride, UV moved, opposite halves sampled.
    template <typename Configure>
    void RunFamily(GraphicsDevice& dev, const char* name, int stride, int normalOffset,
                   int canonicalUv, int movedUv, int weightOffset, int indexOffset,
                   Configure configure)
    {
        auto declarationFor = [&](int uvOffset) {
            std::vector<VertexElement> elements;
            elements.emplace_back(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
            if (normalOffset >= 0)
                elements.emplace_back(normalOffset, VertexElementFormat::Vector3,
                                      VertexElementUsage::Normal, 0);
            elements.emplace_back(uvOffset, VertexElementFormat::Vector2,
                                  VertexElementUsage::TextureCoordinate, 0);
            if (weightOffset >= 0)
                elements.emplace_back(weightOffset, VertexElementFormat::Vector4,
                                      VertexElementUsage::BlendWeight, 0);
            if (indexOffset >= 0)
                elements.emplace_back(indexOffset, VertexElementFormat::Byte4,
                                      VertexElementUsage::BlendIndices, 0);
            return VertexDeclaration(stride, std::move(elements));
        };

        // Canonical layout addressing the LEFT texel; moved layout addressing the RIGHT one.
        const std::size_t pipesBefore = Renderer().GetGraphicsPipelineCacheEntryCountEXT();

        VertexDeclaration declA = declarationFor(canonicalUv);
        VertexBuffer vbA(dev, declA, 6, BufferUsage::None);
        const auto bytesA = BuildRecords(stride, normalOffset, canonicalUv, 0.25f,
                                         weightOffset, indexOffset);
        vbA.SetDataRaw(bytesA.data(), 6, stride);

        VertexDeclaration declB = declarationFor(movedUv);
        VertexBuffer vbB(dev, declB, 6, BufferUsage::None);
        const auto bytesB = BuildRecords(stride, normalOffset, movedUv, 0.75f,
                                         weightOffset, indexOffset);
        vbB.SetDataRaw(bytesB.data(), 6, stride);

        Color gotA(0, 0, 0, 0), gotB(0, 0, 0, 0);
        std::string failure;
        try {
            gotA = configure(vbA);
            gotB = configure(vbB);
        } catch (const std::exception& e) {
            failure = std::string("refused: ") + e.what();
        }
        const std::size_t pipesAfter = Renderer().GetGraphicsPipelineCacheEntryCountEXT();

        if (!failure.empty()) {
            check(false, std::string(name) + " both declarations draw", failure);
            check(false, std::string(name) + " D two declarations, two pipelines", failure);
            return;
        }
        check(Matches(gotA, kLeft) && Matches(gotB, kRight),
              std::string(name) + " each UV offset samples its own half of the texture",
              "canonical=" + Show(gotA) + " (expected " + Show(kLeft) + "), moved=" + Show(gotB)
                  + " (expected " + Show(kRight) + ")");
        check(pipesAfter >= pipesBefore + 2,
              std::string(name) + " D two declarations of one stride left two pipeline entries",
              std::to_string(pipesBefore) + " -> " + std::to_string(pipesAfter));
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

        // ---- A: AlphaTestEffect, stride 32 -----------------------------------------------------
        RunFamily(dev, "A AlphaTestEffect", 32, 12, 24, 12, -1, -1,
                  [&](VertexBuffer& vb) {
                      AlphaTestEffect fx(dev);
                      fx.setTextureProperty(strip_.get());
                      fx.setWorldProperty(Matrix::getIdentityProperty());
                      fx.setViewProperty(Matrix::getIdentityProperty());
                      fx.setProjectionProperty(Matrix::getIdentityProperty());
                      return DrawWith(dev, fx, vb);
                  });

        // ---- B: EnvironmentMapEffect, stride 32 ------------------------------------------------
        // EnvironmentMapAmount 0 leaves the base texture alone, so the pixel is the sampled texel.
        cube_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face) {
            Color grey(128, 128, 128, 255);
            cube_->SetData(static_cast<CubeMapFace>(face), &grey, 1);
        }
        RunFamily(dev, "B EnvironmentMapEffect", 32, 12, 24, 12, -1, -1,
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
                      return DrawWith(dev, fx, vb);
                  });

        // ---- C: SkinnedEffect, stride 52, one identity bone ------------------------------------
        RunFamily(dev, "C SkinnedEffect", 52, 12, 24, 12, 32, 48,
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
                      return DrawWith(dev, fx, vb);
                  });

        // ---- C2: VULKAN-151, both spellings of BLENDINDICES draw the same picture ---------------
        // XNA lets a content processor write BLENDINDICES as Byte4 or as Vector4, and this
        // renderer's skinned shader used to declare `uvec4`, so only the integer spelling bound.
        // It takes `vec4` now: Vector4 binds natively and Byte4 binds through
        // VK_FORMAT_R8G8B8A8_USCALED. The claim is that the SPELLING makes no difference to the
        // picture, so it is asserted as equality of the two readbacks -- not as each matching a
        // constant, which two blank frames would also satisfy.
        //
        // The bone index used is 1, not 0, and bone 0 is a transform that puts the quad far off
        // screen. An index read as anything but 1 therefore renders nothing, so this cannot pass
        // by ignoring the attribute -- which is exactly what a wrong format conversion would do.
        {
            const auto drawSkinned = [&](VertexBuffer& vb) {
                SkinnedEffect fx(dev);
                fx.setTextureProperty(strip_.get());
                fx.setWeightsPerVertexProperty(1);
                std::vector<Matrix> bones = { Matrix::CreateTranslation(1000.f, 1000.f, 0.f),
                                              Matrix::getIdentityProperty() };
                fx.SetBoneTransforms(bones);
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.DirectionalLight0.setEnabledProperty(false);
                fx.DirectionalLight1.setEnabledProperty(false);
                fx.DirectionalLight2.setEnabledProperty(false);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                return DrawWith(dev, fx, vb);
            };

            // Byte4 spelling: the canonical stride-52 record, bone index 1.
            VertexDeclaration declByte(52, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::BlendWeight, 0),
                VertexElement(48, VertexElementFormat::Byte4,
                              VertexElementUsage::BlendIndices, 0),
            });
            std::vector<std::uint8_t> byteBytes(static_cast<std::size_t>(6 * 52), 0);
            for (int i = 0; i < 6; ++i) {
                std::uint8_t* v = byteBytes.data() + i * 52;
                PutFloat3(v + 0,  kQuad[i].x, kQuad[i].y, 0.0f);
                PutFloat3(v + 12, 0.0f, 0.0f, 1.0f);
                PutFloat2(v + 24, 0.25f, 0.5f);          // the LEFT texel
                PutFloat4(v + 32, 1.0f, 0.0f, 0.0f, 0.0f);
                v[48] = 1; v[49] = 0; v[50] = 0; v[51] = 0;
            }
            VertexBuffer vbByte(dev, declByte, 6, BufferUsage::None);
            vbByte.SetDataRaw(byteBytes.data(), 6, 52);

            // Vector4 spelling: the same record with the indices as four floats, so stride 64.
            VertexDeclaration declFloat(64, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::BlendWeight, 0),
                VertexElement(48, VertexElementFormat::Vector4,
                              VertexElementUsage::BlendIndices, 0),
            });
            std::vector<std::uint8_t> floatBytes(static_cast<std::size_t>(6 * 64), 0);
            for (int i = 0; i < 6; ++i) {
                std::uint8_t* v = floatBytes.data() + i * 64;
                PutFloat3(v + 0,  kQuad[i].x, kQuad[i].y, 0.0f);
                PutFloat3(v + 12, 0.0f, 0.0f, 1.0f);
                PutFloat2(v + 24, 0.25f, 0.5f);
                PutFloat4(v + 32, 1.0f, 0.0f, 0.0f, 0.0f);
                PutFloat4(v + 48, 1.0f, 0.0f, 0.0f, 0.0f);
            }
            VertexBuffer vbFloat(dev, declFloat, 6, BufferUsage::None);
            vbFloat.SetDataRaw(floatBytes.data(), 6, 64);

            Color gotByte(0, 0, 0, 0), gotFloat(0, 0, 0, 0);
            std::string how;
            try {
                gotByte  = drawSkinned(vbByte);
                gotFloat = drawSkinned(vbFloat);
            } catch (const std::exception& e) {
                how = std::string("refused: ") + e.what();
            }
            check(how.empty() && Matches(gotByte, gotFloat),
                  "C2 a Vector4-spelled BlendIndices renders the SAME picture as the Byte4 one",
                  how.empty() ? "Byte4=" + Show(gotByte) + " Vector4=" + Show(gotFloat) : how);
            check(how.empty() && Matches(gotByte, kLeft),
                  "C2 and that picture is the sampled texel, so bone 1 really was selected "
                  "(bone 0 puts the quad off screen)",
                  how.empty() ? Show(gotByte) + " (expected " + Show(kLeft) + ")" : how);
        }

        // ---- D2: PbrEffect at stride 64, which is on no list -----------------------------------
        // pbr3d.vert.glsl takes {position, normal, tangent, uv}. Canonically that is 48 bytes;
        // here it is 64, with four bytes of padding after each of the last three elements -- the
        // shape a content pipeline produces when it aligns elements. Nothing about it is
        // expressible from the stride.
        {
            const int kPad = 64;
            VertexDeclaration declPbr(kPad, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(16, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
                VertexElement(52, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            });
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * kPad), 0);
            for (int i = 0; i < 6; ++i) {
                std::uint8_t* v = bytes.data() + i * kPad;
                PutFloat3(v + 0,  kQuad[i].x, kQuad[i].y, 0.0f);
                PutFloat3(v + 16, 0.0f, 0.0f, 1.0f);
                PutFloat4(v + 32, 1.0f, 0.0f, 0.0f, 1.0f);
                PutFloat2(v + 52, 0.25f, 0.5f);      // the LEFT texel
            }
            VertexBuffer vbPbr(dev, declPbr, 6, BufferUsage::None);
            vbPbr.SetDataRaw(bytes.data(), 6, kPad);

            auto drawPbr = [&](VertexBuffer& vb) {
                PbrEffect fx(dev);
                fx.setTextureProperty(strip_.get());
                fx.setNormalMapProperty(nullptr);
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
                return DrawWith(dev, fx, vb);
            };

            std::string how;
            Color gotPbr(0, 0, 0, 0);
            try { gotPbr = drawPbr(vbPbr); }
            catch (const std::exception& e) { how = std::string("refused: ") + e.what(); }
            check(how.empty() && gotPbr.getRProperty() > gotPbr.getBProperty(),
                  "D2 PbrEffect at stride 64 renders from its declaration, off the stride list",
                  how.empty() ? Show(gotPbr) + " (the left texel is the red one)" : how);

            // And the same record with NO declaration is still refused by name. The stride list is
            // what a buffer with nothing to go on is judged by, and losing that would turn an
            // honest refusal into a draw from undefined bytes.
            VertexBuffer bare(dev, 6);
            bare.SetDataRaw(bytes.data(), 6, kPad);
            std::string bareHow = "accepted";
            try { (void)drawPbr(bare); }
            catch (const std::exception& e) { bareHow = e.what(); }
            check(bareHow.find("requires vertex stride 48 or 60") != std::string::npos,
                  "D2 the same record with no declaration is still refused by name", bareHow);
        }

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "E no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanDeclaredEffectLayoutTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanDeclaredEffectLayoutTest g;
    g.Run();
    return g.getResult();
}
