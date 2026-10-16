// SPDX-License-Identifier: MS-PL
// One frame with more stock-effect draws than a renderer's per-frame uniform storage was first
// sized for still draws every one of them with its own parameters.
//
// Moved from the retired VULKAN suite (STREET-0004). A renderer that records draws and replays
// them later must give each draw its own copy of the effect's parameters; a ring that runs out
// and reuses, skips or overruns a slot shows up as a draw with a neighbour's colour or none.
// cna-street, a real scene, draws about 1200 PbrEffect batches and 150 skinned figures a frame.
//
// One frame holds 1500 PbrEffect draws and then 100 SkinnedPbrEffect draws, each a 4x4 cell of a
// 256x256 back buffer lit only by its own EmissiveFactor -- a colour no other draw carries -- and
// all through ONE effect instance whose parameters change between draws, as a game's loop does.
//
//   A  every PbrEffect cell holds its own colour
//   B  every SkinnedPbrEffect cell holds its own colour
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no display or no 3D rendering).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "CNA/GraphicsCapability.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 256;
    constexpr int kCell = 4;
    constexpr int kColumns = kSize / kCell;
    constexpr int kPbrDraws = 1500;
    constexpr int kSkinnedDraws = 100;

    struct PbrVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrVertex) == 48);

    struct SkinnedPbrVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrVertex) == 68);

    std::vector<VertexElement> PbrElements()
    {
        return {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
            VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        };
    }

    std::vector<VertexElement> SkinnedPbrElements()
    {
        auto elements = PbrElements();
        elements.emplace_back(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0);
        elements.emplace_back(64, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0);
        return elements;
    }

    // A unit square from (0,0) to (1,1), facing +Z.
    template <typename V>
    std::vector<V> MakeUnitQuad()
    {
        V a{}, b{}, c{}, d{};
        auto fill = [](V& v, float x, float y) {
            v.px = x; v.py = y; v.pz = 0.0f;
            v.nx = 0.0f; v.ny = 0.0f; v.nz = 1.0f;
            v.tx = 1.0f; v.ty = 0.0f; v.tz = 0.0f; v.tw = 1.0f;
            v.u = x; v.v = y;
        };
        fill(a, 0.0f, 0.0f); fill(b, 0.0f, 1.0f); fill(c, 1.0f, 1.0f); fill(d, 1.0f, 0.0f);
        return { a, b, c, a, c, d };
    }

    // Index -> an 8-bit colour no neighbour shares; blue marks the family.
    Color ColourFor(int i, int family)
    {
        return Color(16 + (i * 37) % 224, 16 + (i * 91) % 224, family == 0 ? 64 : 192, 255);
    }

    Vector3 Linear(const Color& c)
    {
        return Vector3(c.getRProperty() / 255.0f, c.getGProperty() / 255.0f,
                       c.getBProperty() / 255.0f);
    }

    Matrix CellWorld(int cell)
    {
        const float x = static_cast<float>((cell % kColumns) * kCell);
        const float y = static_cast<float>((cell / kColumns) * kCell);
        return Matrix::CreateScale(static_cast<float>(kCell)) * Matrix::CreateTranslation(x, y, 0.0f);
    }

    // Black base colour, no lights, no ambient: the emissive factor is the whole pixel.
    template <typename Effect>
    void Unlit(Effect& fx, Texture2D& white)
    {
        fx.setBaseColorTextureIsSrgbEXTProperty(false);
        fx.setEmissiveTextureIsSrgbEXTProperty(false);
        fx.setEncodeOutputToSrgbEXTProperty(false);
        fx.setTextureProperty(&white);
        fx.setNormalMapProperty(nullptr);
        fx.setDiffuseColorProperty(Vector3::Zero);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, -1.0f, 1.0f));
    }

    std::string CountCells(const std::vector<Color>& frame, int first, int count, int family,
                           int& wrong)
    {
        wrong = 0;
        int firstWrong = -1;
        Color seen;
        for (int i = 0; i < count; ++i)
        {
            const int cell = first + i;
            const int x = (cell % kColumns) * kCell + kCell / 2;
            const int y = (cell / kColumns) * kCell + kCell / 2;
            const Color got = frame[static_cast<std::size_t>(y) * kSize + x];
            const Color want = ColourFor(i, family);
            if (std::abs(got.getRProperty() - want.getRProperty()) > 2 ||
                std::abs(got.getGProperty() - want.getGProperty()) > 2 ||
                std::abs(got.getBProperty() - want.getBProperty()) > 2)
            {
                if (firstWrong < 0) { firstWrong = i; seen = got; }
                ++wrong;
            }
        }
        std::string detail = std::to_string(count - wrong) + "/" + std::to_string(count) +
                             " cells correct";
        if (firstWrong >= 0)
            detail += "; first wrong draw " + std::to_string(firstWrong) + " = (" +
                      std::to_string(seen.getRProperty()) + "," + std::to_string(seen.getGProperty()) +
                      "," + std::to_string(seen.getBProperty()) + ")";
        return detail;
    }
}

class StockEffectUniformRingTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool skipped_ = false;

public:
    StockEffectUniformRingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int ExitCode() const
    {
        return skipped_ ? CNA::Examples::kSkipExitCode : getResultProperty();
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        if (!dev.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        {
            std::printf("[SKIP] this renderer does not rasterize 3D stock effects\n");
            skipped_ = true;
            return;
        }

        Texture2D white(dev, 1, 1);
        const Color whitePixel(255, 255, 255, 255);
        white.SetData(&whitePixel, 1);

        const auto pbrVerts = MakeUnitQuad<PbrVertex>();
        VertexBuffer pbrVb(dev, VertexDeclaration(48, PbrElements()),
                           static_cast<int>(pbrVerts.size()), BufferUsage::None);
        pbrVb.SetDataRaw(pbrVerts.data(), static_cast<int>(pbrVerts.size()), sizeof(PbrVertex));
        auto skinnedVerts = MakeUnitQuad<SkinnedPbrVertex>();
        for (auto& v : skinnedVerts)
        {
            v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;
            v.i0 = v.i1 = v.i2 = v.i3 = 0;
        }
        VertexBuffer skinnedVb(dev, VertexDeclaration(68, SkinnedPbrElements()),
                               static_cast<int>(skinnedVerts.size()), BufferUsage::None);
        skinnedVb.SetDataRaw(skinnedVerts.data(), static_cast<int>(skinnedVerts.size()),
                             sizeof(SkinnedPbrVertex));

        PbrEffect pbr(dev);
        Unlit(pbr, white);
        SkinnedPbrEffect skinned(dev);
        Unlit(skinned, white);
        skinned.SetBoneTransforms({ Matrix::getIdentityProperty() });
        skinned.setWeightsPerVertexProperty(1);

        dev.Clear(Color(0, 0, 0, 255));
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        dev.SetVertexBuffer(&pbrVb);
        for (int i = 0; i < kPbrDraws; ++i)
        {
            pbr.setWorldProperty(CellWorld(i));
            pbr.setEmissiveFactorProperty(Linear(ColourFor(i, 0)));
            pbr.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        dev.SetVertexBuffer(&skinnedVb);
        for (int i = 0; i < kSkinnedDraws; ++i)
        {
            skinned.setWorldProperty(CellWorld(kPbrDraws + i));
            skinned.setEmissiveFactorProperty(Linear(ColourFor(i, 1)));
            skinned.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        dev.SetVertexBuffer(nullptr);

        std::vector<Color> frame(static_cast<std::size_t>(kSize) * kSize);
        dev.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));

        int wrong = 0;
        std::string detail = CountCells(frame, 0, kPbrDraws, 0, wrong);
        Check(wrong == 0, ("A every one of 1500 PbrEffect draws in one frame drew its own "
                           "parameters: " + detail).c_str());
        detail = CountCells(frame, kPbrDraws, kSkinnedDraws, 1, wrong);
        Check(wrong == 0, ("B every one of 100 SkinnedPbrEffect draws after them drew its own "
                           "parameters: " + detail).c_str());
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("stock_effect_uniform_ring"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::Conformance::ProbePbrLightingEvaluated("stock_effect_uniform_ring"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    StockEffectUniformRingTest game;
    try
    {
        game.Run();
    }
    catch (const std::exception& e)
    {
        // Every draw here is a PbrEffect or SkinnedPbrEffect one, so a renderer with no
        // metallic-roughness model may refuse the whole program by name.
        if (!CNA::Examples::Conformance::IsNamedPbrRefusal(e))
            throw;
        std::printf("[SKIP] stock_effect_uniform_ring: %s\n", e.what());
        return CNA::Examples::Conformance::kSkipExitCode;
    }
    return game.ExitCode();
}
