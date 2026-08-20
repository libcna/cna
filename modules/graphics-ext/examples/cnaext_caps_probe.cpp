// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md Phase 16: what does *this* renderer actually promise?
//
// Every Phase 16 row asks the same question of a different renderer, and answering it by reading
// the renderer's source is how MOD-1699 got answered wrongly three times. This prints the answers
// instead. It is a diagnostic, not a test -- it asserts nothing and always exits 0; what it is for
// is filling in a Phase 16 row with a measurement rather than a reading.
//
// Build it in whichever renderer's build directory you are measuring:
//
//   cmake --build cmake-build-<variant> --target cna_test_cnaext_caps
//   DISPLAY=:99 ./cmake-build-<variant>/cna_test_cnaext_caps   (with the usual x11 driver env)
//
// The pairing matters more than any single line: `CustomEffects` says a renderer *accepts* an
// effect, `ExecutesShaderSourceEXT` says it runs the source this layer writes, and a renderer that
// answers yes/no to those two is the one that renders a wrong frame while reporting success.
// `Instancing` next to `MultiStreamVertexInput` is the same shape -- SDL_GPU answers yes then no,
// and the instanced path needs both because the transforms are a second vertex stream.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include <cstdio>
#include <memory>
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
class Caps : public Game {
    std::unique_ptr<GraphicsDeviceManager> gdm_;
protected:
    void Draw(const GameTime&) override {
        auto& d = getGraphicsDeviceProperty();
        std::printf("renderer=%s\n", std::string(d.GetGraphicsRendererName()).c_str());
        const struct { const char* n; CNA::GraphicsCapability c; } caps[] = {
            {"ThreeD", CNA::GraphicsCapability::ThreeD},
            {"CustomEffects", CNA::GraphicsCapability::CustomEffects},
            {"FloatRenderTargets", CNA::GraphicsCapability::FloatRenderTargets},
            {"HalfFloatRenderTargets", CNA::GraphicsCapability::HalfFloatRenderTargets},
            {"ComputeShaders", CNA::GraphicsCapability::ComputeShaders},
            {"Instancing", CNA::GraphicsCapability::Instancing},
            {"MultiStreamVertexInput", CNA::GraphicsCapability::MultiStreamVertexInput},
        };
        for (const auto& c : caps) std::printf("  %-24s %s\n", c.n, d.SupportsCapability(c.c) ? "yes" : "no");
        std::printf("  %-24s %s\n", "ExecutesShaderSourceEXT", d.ExecutesShaderEffectSourceEXT() ? "yes" : "no");
        std::printf("  %-24s %s\n", "ShadowSamplingEXT", d.SupportsShadowSamplingEXT() ? "yes" : "no");
        std::printf("  %-24s %s\n", "ImageBasedLightingEXT", d.SupportsImageBasedLightingEXT() ? "yes" : "no");
        std::printf("  %-24s %s\n", "ComputeShaders(cap)", d.SupportsCapability(CNA::GraphicsCapability::ComputeShaders) ? "yes" : "no");
        Exit();
    }
public:
    Caps() { gdm_ = std::make_unique<GraphicsDeviceManager>(this);
             gdm_->setPreferredBackBufferWidthProperty(64);
             gdm_->setPreferredBackBufferHeightProperty(64); }
};
int main() { Caps c; c.Run(); return 0; }
