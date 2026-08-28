// SPDX-License-Identifier: MS-PL
// WEBGPU-28: every WGSL shader source is extracted into webgpu_shaders.hpp and compilable through
// one validation entry point, WebGPURenderer::ValidateAllShadersEXT(). This test drives that entry
// point on a real device and asserts every shader -- including the ones a normal scene never draws
// (the lazy mipBlit shader and the Pbr/SkinnedPbr expanded variants) -- compiles cleanly. Before
// this, a WGSL error in an UNUSED effect surfaced only at that effect's first pipeline creation;
// now it is a single deterministic gate.
//
// Check A -- ValidateAllShadersEXT() reports zero failures over the whole set.
// Check B -- the direct-shader registry still covers the expected number of sources (guards against
//            an entry silently dropping out of webgpu_shaders.hpp).
//
// Exit code 0 = both checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "CNA/Internal/Renderers/WebGPU/webgpu_shaders.hpp"

#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

class WebGpuShaderValidationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& renderer = static_cast<WebGPURenderer&>(getGraphicsDeviceProperty().GetRenderer());

        std::vector<std::string> failed;
        const int failures = renderer.ValidateAllShadersEXT(&failed);
        std::string joined;
        for (const std::string& label : failed) { joined += ' '; joined += label; }
        check(failures == 0,
              "Check A: every WGSL shader compiles cleanly (failures=" + std::to_string(failures) +
              (failed.empty() ? std::string() : ", failed:" + joined) + ")");

        namespace shaders = CNA::Internal::Renderers::WebGPU::webgpu_shaders;
        const std::size_t directCount = std::size(shaders::kDirectShaders);
        // 18 directly-compiled sources + Pbr + SkinnedPbr (each a template, validated as 2 variants).
        check(directCount == 18,
              "Check B: the direct-shader registry covers all 18 non-template sources (got " +
              std::to_string(directCount) + ")");

        std::printf("[INFO] validated %zu direct shaders + 4 Pbr/SkinnedPbr variants = %zu total\n",
                    directCount, directCount + 4);
        std::printf("=== %d/2 PASS ===\n", passCount_);
        result_ = (passCount_ == 2) ? 0 : 1;
        Exit();
    }

public:
    WebGpuShaderValidationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuShaderValidationTest game;
    game.Run();
    return game.getResult();
}
