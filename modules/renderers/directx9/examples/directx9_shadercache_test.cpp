// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-7 (D9-74): D3D9ShaderCache creates all 66 embedded stock-effect shaders
// through a real, live D3D9 device -- the first task in this plan that needs a D3D9 device
// actually running under DXVK AND the real Microsoft d3dcompiler_47.dll's own output (embedded
// as bytecode by D9-71, no longer needing the compiler DLL itself at this step) at once. Per
// this row's own plan note, DXVK was installed into ~/.wine-cna-d3d9-spike (the prefix that
// already has the real compiler) rather than standing up a 4th prefix.
//
// Check A -- CreateAllEXT() creates all 66 shaders (42 vertex + 24 pixel, the real split D9-71's
//   own manifest reports) through a live device with zero failures.
// Check B -- GetVertexShader()/GetPixelShader() genuinely cache: a second call for the same name
//   returns the identical pointer (no recreation), and the cached counts don't grow further.
// Check C -- an unknown shader name throws a real, named error rather than returning null or
//   silently creating garbage.
// Check D -- a real vs-name-requested-as-pixel-shader (and vice versa) throws too -- proves the
//   lookup is stage-aware, not just name-aware (D3D9's vertex/pixel shader register files and
//   object types are entirely separate; a name existing as a VS says nothing about the PS side).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ShaderCache.hpp"
#include "shaders/d3d9_shaders.hpp"

#include <cstdio>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX9;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }
}

class D3D9ShaderCacheTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX9Renderer&>(dev.GetRenderer());

        D3D9ShaderCache cache(renderer.GetDeviceEXT());

        // Check A: CreateAllEXT() creates all 66 shaders with zero failures.
        cache.CreateAllEXT();
        check(cache.GetCachedVertexShaderCountEXT() == 42 && cache.GetCachedPixelShaderCountEXT() == 24,
              "D9-74: CreateAllEXT() creates all 66 embedded shaders through a live device "
              "(42 vertex + 24 pixel, matching D9-71's own manifest split)");

        // Check B: a second lookup for the same name returns the identical cached object, not a
        // freshly-recreated one, and the cached counts stay exactly 42/24 (no silent duplicates).
        IDirect3DVertexShader9* vs1 = cache.GetVertexShader("BasicEffect_VSBasic");
        IDirect3DVertexShader9* vs2 = cache.GetVertexShader("BasicEffect_VSBasic");
        IDirect3DPixelShader9* ps1 = cache.GetPixelShader("SpriteEffect_SpritePixelShader");
        IDirect3DPixelShader9* ps2 = cache.GetPixelShader("SpriteEffect_SpritePixelShader");
        check(vs1 != nullptr && vs1 == vs2 && ps1 != nullptr && ps1 == ps2 &&
              cache.GetCachedVertexShaderCountEXT() == 42 && cache.GetCachedPixelShaderCountEXT() == 24,
              "D9-74: GetVertexShader()/GetPixelShader() genuinely cache -- a second lookup returns "
              "the identical object, not a new one, and the cached counts do not grow");

        // Check C: an unknown name throws a real, named error.
        bool threwOnUnknownVs = false;
        try { (void)cache.GetVertexShader("NoSuchEffect_NoSuchEntry"); }
        catch (const std::runtime_error&) { threwOnUnknownVs = true; }
        check(threwOnUnknownVs, "D9-74: GetVertexShader() with an unknown name throws std::runtime_error");

        bool threwOnUnknownPs = false;
        try { (void)cache.GetPixelShader("NoSuchEffect_NoSuchEntry"); }
        catch (const std::runtime_error&) { threwOnUnknownPs = true; }
        check(threwOnUnknownPs, "D9-74: GetPixelShader() with an unknown name throws std::runtime_error");

        // Check D: the lookup is stage-aware -- a real vertex-shader name requested via
        // GetPixelShader() (and vice versa) must throw too, not silently return the wrong object
        // or fall through to a coincidental same-named entry (D3D9's vs/ps register files and
        // shader object types are entirely separate address spaces).
        bool threwOnWrongStageVs = false;
        try { (void)cache.GetPixelShader("BasicEffect_VSBasic"); } // real VS name, asked as PS
        catch (const std::runtime_error&) { threwOnWrongStageVs = true; }
        check(threwOnWrongStageVs,
              "D9-74: a real vertex-shader name requested via GetPixelShader() throws (stage-aware lookup)");

        bool threwOnWrongStagePs = false;
        try { (void)cache.GetVertexShader("SpriteEffect_SpritePixelShader"); } // real PS name, asked as VS
        catch (const std::runtime_error&) { threwOnWrongStagePs = true; }
        check(threwOnWrongStagePs,
              "D9-74: a real pixel-shader name requested via GetVertexShader() throws (stage-aware lookup)");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9ShaderCacheTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }
};

int main()
{
    D3D9ShaderCacheTest game;
    game.Run();
    return (passCount == totalCount) ? 0 : 1;
}
