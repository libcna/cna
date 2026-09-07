// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-167 (EasyGL half): does a SpriteBatch's `SamplerState.AddressW`
// reach a `sampler3D` a custom effect bound, on this renderer?
//
// The hook and the half that was missing
// --------------------------------------
// `VULKAN-164` established the XNA rule and added the shared-layer hook: XNA assigns the *whole*
// `SamplerState` to `GraphicsDevice.SamplerStates[0]`, W included, and CNA's `SpriteBatch` was
// forwarding the filter and the U and V axes only. `ISpriteBatchRenderer::SetSamplerAddressModeWEXT`
// carries the third axis, additive with a no-op default so the other renderers were unchanged, and
// Vulkan was the only family that took it (`Vulkan_Texture3DAddressW`).
//
// This is the same proof on the maturity reference. `EasyGLRenderer::ApplySamplerState` derives
// `WrapR` from `addressU` -- correct for every XNA 4.0 preset, since `PointClamp`, `LinearWrap` and
// the rest set all three axes alike, and wrong for a state that set W on its own. The one place it
// shows is a volume texture, which a 2D sprite never has.
//
// Why unit 0 is the right unit here
// ---------------------------------
// On this renderer `ShaderEffect::SetTexture(0, volume)` binds `GL_TEXTURE_3D` on GL texture unit 0
// while the sprite's own texture occupies `GL_TEXTURE_2D` on the same unit -- GL allows one texture
// per target per unit -- and a single GL **sampler object** bound at that unit governs both. So the
// batch's slot-0 sampler is exactly what addresses the volume, which is what makes one override
// enough. (Vulkan numbers this differently: there a bound effect texture lives in descriptor set 1.
// `ShaderEffect` takes renderer-specific source by contract, `VULKAN-250`, so the two tests are
// siblings rather than one shared file.)
//
// How it is measured
// -----------------
// A 1x1x2 volume -- slice 0 red, slice 1 blue -- sampled at `W = 1.25` with point filtering:
//
//     Wrap  -> frac(1.25) = 0.25 -> slice 0 -> RED
//     Clamp -> 1.0              -> slice 1 -> BLUE
//
// One voxel on X and on Y, so neither axis can contribute to the answer. `W = 1.5` would prove
// nothing: `Wrap(1.5) = 0.5` and `Clamp(1.5) = 1.0` both select slice 1.
//
// Two in-range control legs (`W = 0.25` -> red, `W = 0.75` -> blue) pin the binding, the uniform and
// the point filter, so a wrong answer at 1.25 cannot be blamed on those; and a leg asserts that the
// two `SamplerState`s differ in W and in nothing else, so the difference is attributable rather
// than assumed. A final leg repeats the wrap draw through the **same** effect, because a sampler
// cached per effect rather than per batch is the failure mode the Vulkan sibling had to fix.
//
// Exit code 0 = every leg PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <array>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {

constexpr int kN = 64;
const Color kRed  (255, 0,   0,   255);
const Color kBlue (0,   0,   255, 255);
const Color kGreen(0,   255, 0,   255);
const Color kWhite(255, 255, 255, 255);

const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); }
)";

// The sprite's own 2D texture is deliberately never read: every pixel comes from the volume, so a
// leg that reads white would mean the sampler3D produced nothing rather than the wrong slice.
const char* kFragSrc = R"(#version 300 es
precision highp float;
precision highp sampler3D;
out vec4 FragColor;
uniform sampler3D VolumeSampler;
uniform vec3 coord;
void main() { FragColor = vec4(texture(VolumeSampler, coord).rgb, 1.0); }
)";

} // namespace

class EasyGLTexture3DAddressWTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    static bool Is(const Color& got, const Color& want)
    {
        return got.getRProperty() == want.getRProperty() &&
               got.getGProperty() == want.getGProperty() &&
               got.getBProperty() == want.getBProperty();
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    /// A SamplerState that differs from its sibling in the W axis and in nothing else.
    static SamplerState WAxis(const TextureAddressMode w)
    {
        SamplerState s;
        s.setFilterProperty(TextureFilter::Point);
        s.setAddressUProperty(TextureAddressMode::Clamp);
        s.setAddressVProperty(TextureAddressMode::Clamp);
        s.setAddressWProperty(w);
        return s;
    }

    /// Draws one sprite through `effect` with `sampler`, sampling the volume at W = `w`.
    Color DrawWith(GraphicsDevice& dev, ShaderEffect& effect, Texture2D& sprite,
                   SamplerState sampler, float w)
    {
        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(&rt);
        dev.Clear(kGreen);
        effect.Apply();
        effect.SetUniformVec3("coord", 0.5f, 0.5f, w);
        {
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr,
                     &effect);
            sb.Draw(sprite, Rectangle(0, 0, kN, kN), Rectangle(0, 0, 2, 2), kWhite);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        rt.GetData(p.data(), 0, kN * kN);
        return p[kN * kN / 2];
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        // ONE effect for every leg. Two would let each leg get its own sampler binding and hide a
        // sampler cached per effect and reused under another batch's state.
        ShaderEffect effect(dev, kVertSrc, kFragSrc);
        if (!effect.IsEffectValid())
        {
            std::printf("[FAIL] GLSL compile error -- no leg below can mean anything\n");
            Exit();
            return;
        }

        Texture2D sprite(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<std::uint8_t, 16> white{255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255, 255, 255, 255, 255, 255, 255};
        sprite.SetDataRGBA(white.data(), 4);

        // 1x1x2: one voxel per W slice, so X and Y cannot contribute to the answer.
        Texture3D volume(dev, 1, 1, 2, false, SurfaceFormat::Color);
        const std::array<Color, 2> voxels{kRed, kBlue};
        volume.SetData(voxels.data(), static_cast<int>(voxels.size()));
        effect.SetTexture(0, volume);

        const SamplerState wrap  = WAxis(TextureAddressMode::Wrap);
        const SamplerState clamp = WAxis(TextureAddressMode::Clamp);

        // D. The setup itself. A and B mean nothing if the two states differ in more than W.
        check(wrap.getFilterProperty() == clamp.getFilterProperty() &&
                  wrap.getAddressUProperty() == clamp.getAddressUProperty() &&
                  wrap.getAddressVProperty() == clamp.getAddressVProperty() &&
                  wrap.getAddressWProperty() != clamp.getAddressWProperty(),
              "D the two SamplerStates differ in the W axis and in nothing else");

        // Controls, in range, where Wrap and Clamp must agree: these pin the binding, the uniform
        // and the point filter rather than the address mode.
        const Color c025 = DrawWith(dev, effect, sprite, clamp, 0.25f);
        const Color c075 = DrawWith(dev, effect, sprite, clamp, 0.75f);
        check(Is(c025, kRed),
              "control W=0.25 reads slice 0: " + Text(c025) + " (want " + Text(kRed) + ")");
        check(Is(c075, kBlue),
              "control W=0.75 reads slice 1: " + Text(c075) + " (want " + Text(kBlue) + ")");

        const Color gotWrap  = DrawWith(dev, effect, sprite, wrap,  1.25f);
        const Color gotClamp = DrawWith(dev, effect, sprite, clamp, 1.25f);
        const Color gotWrap2 = DrawWith(dev, effect, sprite, wrap,  1.25f);

        check(Is(gotWrap, kRed),
              "A AddressW=Wrap at W=1.25 wraps to 0.25 and reads slice 0: " + Text(gotWrap) +
                  " (want " + Text(kRed) +
                  "; the clamp answer here is the W-follows-U default this row removes)");
        check(Is(gotClamp, kBlue),
              "B AddressW=Clamp at the same W clamps to 1.0 and reads slice 1: " + Text(gotClamp) +
                  " (want " + Text(kBlue) + ")");
        check(!Is(gotWrap, gotClamp),
              "B' wrap and clamp give two different pixels: " + Text(gotWrap) + " vs " +
                  Text(gotClamp));
        check(Is(gotWrap2, kRed),
              "C wrap again through the same effect reads slice 0 again: " + Text(gotWrap2) +
                  " (want " + Text(kRed) +
                  "; the clamp answer here means a sampler outlived the batch that set it)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    EasyGLTexture3DAddressWTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kN);
        gdm_->setPreferredBackBufferHeightProperty(kN);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EasyGLTexture3DAddressWTest game;
    game.Run();
    return game.getResult();
}
