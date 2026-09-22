// SPDX-License-Identifier: MS-PL
// plans/plan_street.md STREET-0004: a frame with more stock-effect draws than a per-frame uniform
// ring was sized for must still draw every one of them.
//
// Every stock 3D family takes its per-draw uniforms from a per-frame dynamic-uniform ring: 512
// PbrEffect blocks, 32 SkinnedPbrEffect bone palettes. A ring that ran out skipped the copy and
// bound the out-of-range dynamic offset anyway, so the GPU read past the buffer: the validation
// layer reported VUID-vkCmdBindDescriptorSets-pDescriptorSets-01979 and RADV lost the device.
// cna-street, a real scene, draws about 1200 PbrEffect batches and 150 skinned figures a frame.
//
// One frame here holds 1500 PbrEffect draws and 100 SkinnedPbrEffect draws, each a 4x4 cell of a
// 256x256 back buffer lit by nothing but its own emissive factor -- a colour only its own uniform
// block carries -- so a draw that read another block, or none, shows in its cell.
//
//   A  every PbrEffect cell holds its own colour (past the 512-slot ring)
//   B  every SkinnedPbrEffect cell holds its own colour (past the 32-slot rings)
//   C  the validation layer stayed silent
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize        = 256;
    constexpr int kCell        = 4;
    constexpr int kColumns     = kSize / kCell;
    constexpr int kPbrDraws    = 1500;
    constexpr int kSkinnedDraws = 100;

    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

    struct SkinnedPbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrGpuVertex) == 68, "skinned PBR vertex must be 68 bytes");

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
        return Color(static_cast<std::uint8_t>(16 + (i * 37) % 224),
                     static_cast<std::uint8_t>(16 + (i * 91) % 224),
                     static_cast<std::uint8_t>(family == 0 ? 64 : 192), 255);
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
}

class VulkanUniformRingGrowthTest : public Game
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

    static std::string CountCells(const std::vector<Color>& frame, int first, int count,
                                  int family, int& wrong)
    {
        wrong = 0;
        int firstWrong = -1;
        Color seen;
        for (int i = 0; i < count; ++i) {
            const int cell = first + i;
            const int x = (cell % kColumns) * kCell + kCell / 2;
            const int y = (cell / kColumns) * kCell + kCell / 2;
            const Color got  = frame[static_cast<std::size_t>(y) * kSize + x];
            const Color want = ColourFor(i, family);
            if (std::abs(got.getRProperty() - want.getRProperty()) > 2
                || std::abs(got.getGProperty() - want.getGProperty()) > 2
                || std::abs(got.getBProperty() - want.getBProperty()) > 2) {
                if (firstWrong < 0) { firstWrong = i; seen = got; }
                ++wrong;
            }
        }
        std::string detail = std::to_string(count - wrong) + "/" + std::to_string(count)
                           + " cells correct";
        if (firstWrong >= 0)
            detail += "; first wrong draw " + std::to_string(firstWrong) + " = ("
                    + std::to_string(seen.getRProperty()) + "," + std::to_string(seen.getGProperty())
                    + "," + std::to_string(seen.getBProperty()) + ")";
        return detail;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        Texture2D white(dev, 1, 1);
        const Color whitePixel(255, 255, 255, 255);
        white.SetData(&whitePixel, 1);

        const auto pbrVerts = MakeUnitQuad<PbrGpuVertex>();
        VertexBuffer pbrVb(dev, static_cast<int>(pbrVerts.size()));
        pbrVb.SetDataRaw(pbrVerts.data(), static_cast<int>(pbrVerts.size()),
                         static_cast<int>(sizeof(PbrGpuVertex)));
        auto skinnedVerts = MakeUnitQuad<SkinnedPbrGpuVertex>();
        for (auto& v : skinnedVerts) {
            v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;
            v.i0 = v.i1 = v.i2 = v.i3 = 0;
        }
        VertexBuffer skinnedVb(dev, static_cast<int>(skinnedVerts.size()));
        skinnedVb.SetDataRaw(skinnedVerts.data(), static_cast<int>(skinnedVerts.size()),
                             static_cast<int>(sizeof(SkinnedPbrGpuVertex)));

        PbrEffect pbr(dev);
        Unlit(pbr, white);
        SkinnedPbrEffect skinned(dev);
        Unlit(skinned, white);
        skinned.SetBoneTransforms({ Matrix::getIdentityProperty() });
        skinned.setWeightsPerVertexProperty(1);

        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        dev.SetVertexBuffer(&pbrVb);
        for (int i = 0; i < kPbrDraws; ++i) {
            pbr.setWorldProperty(CellWorld(i));
            pbr.setEmissiveFactorProperty(Linear(ColourFor(i, 0)));
            pbr.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        dev.SetVertexBuffer(&skinnedVb);
        for (int i = 0; i < kSkinnedDraws; ++i) {
            skinned.setWorldProperty(CellWorld(kPbrDraws + i));
            skinned.setEmissiveFactorProperty(Linear(ColourFor(i, 1)));
            skinned.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }

        std::vector<Color> frame(static_cast<std::size_t>(kSize) * kSize);
        dev.GetBackBufferData(frame.data(), static_cast<int>(frame.size()));

        int wrong = 0;
        std::string detail = CountCells(frame, 0, kPbrDraws, 0, wrong);
        check(wrong == 0, "A every PbrEffect draw past the 512-block ring drew its own uniforms",
              detail);
        detail = CountCells(frame, kPbrDraws, kSkinnedDraws, 1, wrong);
        check(wrong == 0,
              "B every SkinnedPbrEffect draw past the 32-block rings drew its own uniforms", detail);

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "C no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VulkanUniformRingGrowthTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // GetBackBufferData is HiDef-only (SOFTWARE-213 enforces XNA's rule).
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanUniformRingGrowthTest g;
    g.Run();
    return g.getResult();
}
