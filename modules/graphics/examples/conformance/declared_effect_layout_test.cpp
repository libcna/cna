// SPDX-License-Identifier: MS-PL
// The stock effects read each vertex element from the offset the caller's VertexDeclaration
// gives, never from a guess based on the stride. Moved from the retired VULKAN suite (VULKAN-147,
// VULKAN-148, VULKAN-151); the BasicEffect case already lives in parity_vertex_semantics.
//
// Every leg draws the same full-frame quad twice at the SAME stride, once with the canonical
// layout and once with the TextureCoordinate element moved (swapped with the normal, so no two
// elements overlap -- XNA refuses overlapping declarations). The texture is 2x1, left texel red
// and right texel blue, point sampled, and each layout's UV addresses the opposite texel. A
// renderer that inferred the offset from the stride shows the same texel twice.
//
//   A  AlphaTestEffect,      stride 32, UV at 24 (canonical) and at 12;
//   B  EnvironmentMapEffect, stride 32, the same pair, EnvironmentMapAmount 0;
//   C  SkinnedEffect,        stride 52, the same pair, one identity bone;
//   C2 SkinnedEffect's BlendIndices spelled Byte4 and Vector4 render the same picture, and that
//      picture really used bone 1 (bone 0 moves the quad off screen);
//   D  PbrEffect, a CNA extension with no XNA reference, at its canonical 48-byte stride with
//      TextureCoordinate and Tangent swapped: a declaration the renderer accepts is honoured, and
//      one it cannot map is refused with NotSupportedException (reported as SKIP) -- never drawn
//      from the wrong bytes. (The padded-stride PbrEffect case is stock_effect_padded_stride.)
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
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
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kLeft(230, 30, 30, 255);
    const Color kRight(30, 60, 230, 255);

    struct Corner { float x, y; };
    constexpr Corner kQuad[6] = {
        {-1.f, 1.f}, {-1.f, -1.f}, {1.f, -1.f},
        {-1.f, 1.f}, {1.f, -1.f}, {1.f, 1.f},
    };

    /// One vertex record of exactly @p N bytes, so SetData<T> uploads the declared stride.
    template <int N>
    struct Record
    {
        std::uint8_t bytes[N];
    };

    void PutFloats(std::uint8_t* at, std::initializer_list<float> values)
    {
        std::size_t i = 0;
        for (float v : values)
        {
            std::memcpy(at + i * sizeof(float), &v, sizeof(float));
            ++i;
        }
    }

    bool Matches(const Color& got, const Color& want)
    {
        const int tol = 24;
        return std::abs(got.getRProperty() - want.getRProperty()) <= tol &&
               std::abs(got.getGProperty() - want.getGProperty()) <= tol &&
               std::abs(got.getBProperty() - want.getBProperty()) <= tol;
    }

    std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    template <int N>
    std::vector<Record<N>> BuildRecords(int normalOffset, int uvOffset, float u,
                                        int weightOffset, int indexOffset)
    {
        std::vector<Record<N>> records(6);
        for (int i = 0; i < 6; ++i)
        {
            std::uint8_t* v = records[static_cast<std::size_t>(i)].bytes;
            std::memset(v, 0, N);
            PutFloats(v, {kQuad[i].x, kQuad[i].y, 0.0f});
            if (normalOffset >= 0) PutFloats(v + normalOffset, {0.0f, 0.0f, 1.0f});
            PutFloats(v + uvOffset, {u, 0.5f});
            if (weightOffset >= 0) PutFloats(v + weightOffset, {1.0f, 0.0f, 0.0f, 0.0f});
            if (indexOffset >= 0) v[indexOffset] = 0;
        }
        return records;
    }
}

class DeclaredEffectLayoutTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> strip_;
    std::unique_ptr<TextureCube> cube_;
    bool done_ = false;
    int result_ = 0;

    void Check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        std::fflush(stdout);
        if (!ok) result_ = 1;
    }

    Color DrawWith(GraphicsDevice& dev, Effect& effect, VertexBuffer& vb)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        effect.Apply();
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        Color got(0, 0, 0, 0);
        const Rectangle probe(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&probe, &got, 0, 1);
        return got;
    }

    /// Same stride, UV moved, opposite texels sampled.
    template <int Stride, typename Configure>
    void RunFamily(GraphicsDevice& dev, const char* name, int normalOffset, int canonicalUv,
                   int movedUv, int weightOffset, int indexOffset, Configure configure)
    {
        const auto declarationFor = [&](int uvOffset, int normalAt) {
            std::vector<VertexElement> elements;
            elements.emplace_back(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
            if (normalAt >= 0)
                elements.emplace_back(normalAt, VertexElementFormat::Vector3,
                                      VertexElementUsage::Normal, 0);
            elements.emplace_back(uvOffset, VertexElementFormat::Vector2,
                                  VertexElementUsage::TextureCoordinate, 0);
            if (weightOffset >= 0)
                elements.emplace_back(weightOffset, VertexElementFormat::Vector4,
                                      VertexElementUsage::BlendWeight, 0);
            if (indexOffset >= 0)
                elements.emplace_back(indexOffset, VertexElementFormat::Byte4,
                                      VertexElementUsage::BlendIndices, 0);
            return VertexDeclaration(Stride, std::move(elements));
        };

        // The moved record SWAPS the UV and the normal, keeping the stride.
        const int movedNormal = normalOffset >= 0 ? movedUv + 8 : -1;

        VertexDeclaration declA = declarationFor(canonicalUv, normalOffset);
        VertexBuffer vbA(dev, declA, 6, BufferUsage::None);
        const auto recordsA =
            BuildRecords<Stride>(normalOffset, canonicalUv, 0.25f, weightOffset, indexOffset);
        vbA.SetData(recordsA.data(), 6);

        VertexDeclaration declB = declarationFor(movedUv, movedNormal);
        VertexBuffer vbB(dev, declB, 6, BufferUsage::None);
        const auto recordsB =
            BuildRecords<Stride>(movedNormal, movedUv, 0.75f, weightOffset, indexOffset);
        vbB.SetData(recordsB.data(), 6);

        Color gotA(0, 0, 0, 0), gotB(0, 0, 0, 0);
        try
        {
            gotA = configure(vbA);
            gotB = configure(vbB);
        }
        catch (const std::exception& e)
        {
            Check(false, std::string(name) + " both declarations draw",
                  std::string("refused: ") + e.what());
            return;
        }
        Check(Matches(gotA, kLeft) && Matches(gotB, kRight),
              std::string(name) + " each UV offset samples its own half of the texture",
              "canonical=" + Show(gotA) + " (expected " + Show(kLeft) + "), moved=" + Show(gotB) +
                  " (expected " + Show(kRight) + ")");
    }

    static void Identity(IEffectMatrices& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
    }

    static void Unlit(IEffectLights& fx)
    {
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.getDirectionalLight0Property().setEnabledProperty(false);
        fx.getDirectionalLight1Property().setEnabledProperty(false);
        fx.getDirectionalLight2Property().setEnabledProperty(false);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        strip_ = std::make_unique<Texture2D>(dev, 2, 1);
        const Color texels[2] = {kLeft, kRight};
        strip_->SetData(texels, 2);

        // ---- A: AlphaTestEffect, stride 32 -------------------------------------------------------
        RunFamily<32>(dev, "A AlphaTestEffect", 12, 24, 12, -1, -1, [&](VertexBuffer& vb) {
            AlphaTestEffect fx(dev);
            fx.setTextureProperty(strip_.get());
            Identity(fx);
            return DrawWith(dev, fx, vb);
        });

        // ---- B: EnvironmentMapEffect, stride 32 --------------------------------------------------
        cube_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face)
        {
            const Color grey(128, 128, 128, 255);
            cube_->SetData(static_cast<CubeMapFace>(face), &grey, 1);
        }
        RunFamily<32>(dev, "B EnvironmentMapEffect", 12, 24, 12, -1, -1, [&](VertexBuffer& vb) {
            EnvironmentMapEffect fx(dev);
            fx.setTextureProperty(strip_.get());
            fx.setEnvironmentMapProperty(cube_.get());
            fx.setEnvironmentMapAmountProperty(0.0f);
            fx.setFresnelFactorProperty(0.0f);
            fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            Unlit(fx);
            Identity(fx);
            return DrawWith(dev, fx, vb);
        });

        // ---- C: SkinnedEffect, stride 52, one identity bone ---------------------------------------
        RunFamily<52>(dev, "C SkinnedEffect", 12, 24, 12, 32, 48, [&](VertexBuffer& vb) {
            SkinnedEffect fx(dev);
            fx.setTextureProperty(strip_.get());
            fx.setWeightsPerVertexProperty(1);
            fx.SetBoneTransforms({Matrix::getIdentityProperty()});
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            Unlit(fx);
            Identity(fx);
            return DrawWith(dev, fx, vb);
        });

        // ---- C2: BlendIndices as Byte4 and as Vector4 -------------------------------------------
        {
            const auto drawSkinned = [&](VertexBuffer& vb) {
                SkinnedEffect fx(dev);
                fx.setTextureProperty(strip_.get());
                fx.setWeightsPerVertexProperty(1);
                fx.SetBoneTransforms({Matrix::CreateTranslation(1000.f, 1000.f, 0.f),
                                      Matrix::getIdentityProperty()});
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                Unlit(fx);
                Identity(fx);
                return DrawWith(dev, fx, vb);
            };

            const VertexDeclaration declByte(52, {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
                VertexElement(48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
            });
            std::vector<Record<52>> byteRecords(6);
            for (int i = 0; i < 6; ++i)
            {
                std::uint8_t* v = byteRecords[static_cast<std::size_t>(i)].bytes;
                std::memset(v, 0, 52);
                PutFloats(v + 0, {kQuad[i].x, kQuad[i].y, 0.0f});
                PutFloats(v + 12, {0.0f, 0.0f, 1.0f});
                PutFloats(v + 24, {0.25f, 0.5f});
                PutFloats(v + 32, {1.0f, 0.0f, 0.0f, 0.0f});
                v[48] = 1;
            }
            VertexBuffer vbByte(dev, declByte, 6, BufferUsage::None);
            vbByte.SetData(byteRecords.data(), 6);

            const VertexDeclaration declFloat(64, {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
                VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendIndices, 0),
            });
            std::vector<Record<64>> floatRecords(6);
            for (int i = 0; i < 6; ++i)
            {
                std::uint8_t* v = floatRecords[static_cast<std::size_t>(i)].bytes;
                std::memset(v, 0, 64);
                PutFloats(v + 0, {kQuad[i].x, kQuad[i].y, 0.0f});
                PutFloats(v + 12, {0.0f, 0.0f, 1.0f});
                PutFloats(v + 24, {0.25f, 0.5f});
                PutFloats(v + 32, {1.0f, 0.0f, 0.0f, 0.0f});
                PutFloats(v + 48, {1.0f, 0.0f, 0.0f, 0.0f});
            }
            VertexBuffer vbFloat(dev, declFloat, 6, BufferUsage::None);
            vbFloat.SetData(floatRecords.data(), 6);

            Color gotByte(0, 0, 0, 0), gotFloat(0, 0, 0, 0);
            std::string how;
            try
            {
                gotByte = drawSkinned(vbByte);
                gotFloat = drawSkinned(vbFloat);
            }
            catch (const std::exception& e)
            {
                how = std::string("refused: ") + e.what();
            }
            Check(how.empty() && Matches(gotByte, gotFloat),
                  "C2 a Vector4 BlendIndices renders the same picture as the Byte4 one",
                  how.empty() ? "Byte4=" + Show(gotByte) + " Vector4=" + Show(gotFloat) : how);
            Check(how.empty() && Matches(gotByte, kLeft),
                  "C2 and that picture is the sampled texel, so bone 1 was selected",
                  how.empty() ? Show(gotByte) + " (expected " + Show(kLeft) + ")" : how);
        }

        // ---- D: PbrEffect (CNA extension) ------------------------------------------------------------
        // There is no XNA reference for PbrEffect, so the contract asserted is the one CNA keeps for
        // every capability: a declaration the renderer accepts is honoured, and one it cannot map is
        // refused with NotSupportedException -- never drawn from the wrong bytes.
        // The record is the canonical size, 48 bytes, with TextureCoordinate and Tangent swapped.
        {
            const auto pbrRecord = [](std::uint8_t* v, const Corner& c, int normalAt, int tangentAt,
                                      int uvAt, float u) {
                PutFloats(v + 0, {c.x, c.y, 0.0f});
                PutFloats(v + normalAt, {0.0f, 0.0f, 1.0f});
                PutFloats(v + tangentAt, {1.0f, 0.0f, 0.0f, 1.0f});
                PutFloats(v + uvAt, {u, 0.5f});
            };
            const auto pbrDeclaration = [](int stride, int normalAt, int tangentAt, int uvAt) {
                return VertexDeclaration(stride, {
                    VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                    VertexElement(normalAt, VertexElementFormat::Vector3,
                                  VertexElementUsage::Normal, 0),
                    VertexElement(tangentAt, VertexElementFormat::Vector4,
                                  VertexElementUsage::Tangent, 0),
                    VertexElement(uvAt, VertexElementFormat::Vector2,
                                  VertexElementUsage::TextureCoordinate, 0),
                });
            };
            // A stride-48 reader that assumed the canonical layout would fetch the UV from byte 40,
            // where the moved record keeps Tangent.zw = (0,1): u = 0, the LEFT texel. The moved
            // record's real UV addresses the RIGHT one.
            std::vector<Record<48>> moved(6);
            for (int i = 0; i < 6; ++i)
            {
                std::memset(moved[static_cast<std::size_t>(i)].bytes, 0, 48);
                pbrRecord(moved[static_cast<std::size_t>(i)].bytes, kQuad[i], 12, 32, 24, 0.75f);
            }
            VertexBuffer vbMoved(dev, pbrDeclaration(48, 12, 32, 24), 6, BufferUsage::None);
            vbMoved.SetData(moved.data(), 6);

            std::unique_ptr<PbrEffect> fx;
            try
            {
                fx = std::make_unique<PbrEffect>(dev);
            }
            catch (const System::NotSupportedException& e)
            {
                std::printf("[SKIP] D PbrEffect is not offered by this renderer: %s\n", e.what());
            }
            if (fx)
            {
                fx->setTextureProperty(strip_.get());
                fx->setNormalMapProperty(nullptr);
                fx->setMetallicFactorProperty(0.0f);
                fx->setRoughnessFactorProperty(1.0f);
                fx->setBaseColorTextureIsSrgbEXTProperty(false);
                fx->setEncodeOutputToSrgbEXTProperty(false);
                Unlit(*fx);
                Identity(*fx);
                const auto leg = [&](const char* label, VertexBuffer& vb, bool wantRed) {
                    Color got(0, 0, 0, 0);
                    try
                    {
                        got = DrawWith(dev, *fx, vb);
                    }
                    catch (const System::NotSupportedException& e)
                    {
                        std::printf("[SKIP] %s: refused by name (%s)\n", label, e.what());
                        return;
                    }
                    catch (const std::exception& e)
                    {
                        Check(false, label, std::string("threw something other than "
                                                        "NotSupportedException: ") + e.what());
                        return;
                    }
                    const bool red = got.getRProperty() > got.getBProperty() + 40;
                    const bool blue = got.getBProperty() > got.getRProperty() + 40;
                    Check(wantRed ? red : blue, label,
                          Show(got) + (wantRed ? " (expected the red left texel)"
                                               : " (expected the blue right texel)"));
                };
                leg("D PbrEffect at stride 48 with UV and Tangent swapped reads the declared UV",
                    vbMoved, false);
            }
        }

        Exit();
    }

public:
    DeclaredEffectLayoutTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    DeclaredEffectLayoutTest game;
    game.Run();
    return game.getResult();
}
