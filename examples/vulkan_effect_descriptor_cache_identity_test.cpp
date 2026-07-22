// SPDX-License-Identifier: MS-PL
// REMED-GFX-076: the Vulkan backend caches one descriptor set per unique sampled-texture combination
// for each 3D effect (BasicEffect-textured -> litTextured/fogTex3D, DualTextureEffect, Environment-
// MapEffect, SkinnedEffect, PbrEffect/SkinnedPbrEffect). Those seven per-frame caches key on a HASH
// of raw VkImageView handle *values* and persist across frames with no per-view free path. GFX-075's
// retirement queue keeps a dying view alive through the deferred-consume window (fixing the UAF), but
// once that view is finally freed its VkImageView handle value can be reused by a later texture. If a
// stale hash-keyed entry survives, the later texture's key collides with it and it is handed a
// descriptor set that samples the DESTROYED image -- a silent wrong-texture correctness defect, not a
// UAF.
//
// This regression proves the structural fix DETERMINISTICALLY (independent of whether the driver
// happens to recycle a handle in a given run): a sampled resource's effect-cache entries are evicted
// the moment the resource is destroyed, so no entry keyed on a recyclable VkImageView value can ever
// outlive its resource. It reads the backend's read-only cache introspection
// (EffectDescSetEntriesForViewInTests / TotalEffectDescSetEntriesForTests) to observe this directly.
// It ALSO runs a best-effort real-handle-reuse scene (destroy A, free its view, create B, redraw) and
// asserts B renders its own colour, reporting whether the raw handle was actually recycled.
//
// Pre-fix: after the source texture/cube is destroyed its entry SURVIVES (count stays 1) and the
// reuse scene can sample the stale image. Post-fix: the entry is gone (count 0) and reuse is safe.
// Exit 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

// REMED-GFX-076: read-only cache introspection lives on the concrete Vulkan backend (reached via the
// NOXNA GraphicsDevice::GetBackend()), and the raw sampled VkImageView via IVulkanSamplable -- both
// backend-internal, exactly as the other backend lifetime/diag tests reach their backends.
#include "CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::Vulkan::VulkanGraphicsBackend;
using CNA::Internal::Backends::Vulkan::IVulkanSamplable;
using CNA::Internal::Backends::Vulkan::IVulkanCubeSamplable;

namespace {
    constexpr int kW = 96, kH = 72;
    bool IsRed(const Color& c)   { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    bool IsGreen(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
}

class EffectDescriptorCacheIdentityTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_ = false;
    int  pass_ = 0;
    int  total_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (ok) ++pass_;
    }

    VulkanGraphicsBackend& backend()
    {
        return static_cast<VulkanGraphicsBackend&>(getGraphicsDeviceProperty().GetBackend());
    }

    // Raw VkImageView handle value (as uint64_t) a texture/cube is sampled through -- the exact value
    // the effect caches hash into their key.
    std::uint64_t viewHandleOf(const Texture2D& t)
    {
        const auto* s = dynamic_cast<const IVulkanSamplable*>(&t.GetBackend());
        return s ? reinterpret_cast<std::uint64_t>(s->GetVkImageView()) : 0;
    }
    std::uint64_t viewHandleOf(const TextureCube& c)
    {
        // A cube is sampled through its own IVulkanCubeSamplable::GetVkCubeImageView().
        const auto* s = dynamic_cast<const IVulkanCubeSamplable*>(&c.GetBackend());
        return s ? reinterpret_cast<std::uint64_t>(s->GetVkCubeImageView()) : 0;
    }

    std::unique_ptr<RenderTarget2D> makeRt()
    {
        auto& dev = getGraphicsDeviceProperty();
        return std::make_unique<RenderTarget2D>(dev, kW, kH, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    }

    std::unique_ptr<Texture2D> makeColorTex(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        auto& dev = getGraphicsDeviceProperty();
        return std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{ r, g, b, 255 }));
    }

    std::vector<Color> readRt(RenderTarget2D& rt)
    {
        std::vector<Color> pix(static_cast<std::size_t>(kW) * kH, Color(0, 0, 0, 0));
        rt.GetData(0, nullptr, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    template <typename Pred>
    int countIf(const std::vector<Color>& pix, Pred pred)
    {
        int n = 0;
        for (const auto& c : pix) if (pred(c)) ++n;
        return n;
    }

    // A full-viewport BasicEffect textured draw (VertexPositionNormalTexture, stride 32 -> the Vulkan
    // litTextured effect cache). Lighting off so the output is the texture's own colour.
    void drawTexturedBasic(Texture2D& tex, RenderTarget2D* rt)
    {
        auto& dev = getGraphicsDeviceProperty();
        if (rt) dev.SetRenderTarget(rt);
        dev.Clear(Color::Black);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        const Vector3 n(0.f, 0.f, 1.f);
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-1.f,  1.f, 0.f), n, Vector2(0.f, 1.f) },
            { Vector3(-1.f, -1.f, 0.f), n, Vector2(0.f, 0.f) },
            { Vector3( 1.f, -1.f, 0.f), n, Vector2(1.f, 0.f) },
            { Vector3(-1.f,  1.f, 0.f), n, Vector2(0.f, 1.f) },
            { Vector3( 1.f, -1.f, 0.f), n, Vector2(1.f, 0.f) },
            { Vector3( 1.f,  1.f, 0.f), n, Vector2(1.f, 1.f) },
        };
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
        if (rt) dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    // A DualTextureEffect draw (two samplers -> the dualTex effect cache).
    void drawDualTex(Texture2D& t0, Texture2D& t1, RenderTarget2D* rt)
    {
        auto& dev = getGraphicsDeviceProperty();
        if (rt) dev.SetRenderTarget(rt);
        dev.Clear(Color::Black);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        DualTextureEffect fx(dev);
        fx.setTextureProperty(&t0);
        fx.setTexture2Property(&t1);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        const Vector3 n(0.f, 0.f, 1.f);
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-1.f,  1.f, 0.f), n, Vector2(0.f, 1.f) },
            { Vector3(-1.f, -1.f, 0.f), n, Vector2(0.f, 0.f) },
            { Vector3( 1.f, -1.f, 0.f), n, Vector2(1.f, 0.f) },
            { Vector3(-1.f,  1.f, 0.f), n, Vector2(0.f, 1.f) },
            { Vector3( 1.f, -1.f, 0.f), n, Vector2(1.f, 0.f) },
            { Vector3( 1.f,  1.f, 0.f), n, Vector2(1.f, 1.f) },
        };
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
        if (rt) dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    // An EnvironmentMapEffect draw sampling a cube (2D tex + cube -> the envMap effect cache; the cube
    // view is the newly-covered eviction path).
    void drawEnvMap(TextureCube& cube, RenderTarget2D* rt)
    {
        auto& dev = getGraphicsDeviceProperty();
        if (rt) dev.SetRenderTarget(rt);
        dev.Clear(Color::Black);
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        EnvironmentMapEffect fx(dev);
        fx.setDiffuseColorProperty(Vector3(1.f, 1.f, 1.f));
        fx.setEnvironmentMapAmountProperty(1.f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.f, 0.f, 0.f));
        fx.setTextureProperty(&whiteTex_);
        fx.setEnvironmentMapProperty(&cube);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        const Vector3 n(0.f, 0.f, 1.f);
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-1.f,  1.f, 0.f), n, Vector2(0.f, 1.f) },
            { Vector3(-1.f, -1.f, 0.f), n, Vector2(0.f, 0.f) },
            { Vector3( 1.f, -1.f, 0.f), n, Vector2(1.f, 0.f) },
            { Vector3(-1.f,  1.f, 0.f), n, Vector2(0.f, 1.f) },
            { Vector3( 1.f, -1.f, 0.f), n, Vector2(1.f, 0.f) },
            { Vector3( 1.f,  1.f, 0.f), n, Vector2(1.f, 1.f) },
        };
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
        if (rt) dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    std::unique_ptr<TextureCube> makeColorCube(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        auto& dev = getGraphicsDeviceProperty();
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        const Color face(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b), 255);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace f : faces) cube->SetData(f, &face, 1);
        return cube;
    }

    // Advance retirement generations so a just-destroyed view is actually vkDestroy'd (not merely
    // queued), maximising the chance the driver recycles its handle value for the next allocation.
    void advanceFrames(int n)
    {
        auto& dev = getGraphicsDeviceProperty();
        for (int i = 0; i < n; ++i) { dev.Clear(Color::Black); dev.Present(); }
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        // === T1: litTextured cache entry is EVICTED the instant its source texture is destroyed. ===
        // The deterministic core proof: no entry keyed on a recyclable VkImageView value outlives its
        // resource. Pre-fix the entry survives (count stays 1); post-fix it is gone (count 0).
        {
            auto rt  = makeRt();
            auto tex = makeColorTex(255, 0, 0);
            const std::uint64_t h = viewHandleOf(*tex);
            check(h != 0, "T1a source texture exposes a VkImageView handle");
            check(backend().EffectDescSetEntriesForViewInTests(h) == 0,
                  "T1b litTextured cache has no entry for the view before drawing");
            drawTexturedBasic(*tex, rt.get());
            (void)readRt(*rt);                                  // record the deferred draw
            check(backend().EffectDescSetEntriesForViewInTests(h) >= 1,
                  "T1c litTextured cache holds an entry for the view after drawing");
            tex.reset();                                        // destroy the source texture
            check(backend().EffectDescSetEntriesForViewInTests(h) == 0,
                  "T1d litTextured cache entry EVICTED when the source texture is destroyed");
        }

        // === T2: real-handle-reuse safety (litTextured). ===
        // Destroy texA (RED), advance frames so its VkImageView is actually freed, create texB (GREEN)
        // -- which may recycle texA's handle value -- then draw texB through the SAME effect path. B
        // must render its OWN colour; a surviving stale entry keyed on the reused handle would sample
        // texA's destroyed RED image instead. Repeated to maximise the chance of a real recycle.
        {
            bool allGreen = true, reuseSeen = false, staleForA = false;
            for (int iter = 0; iter < 6 && allGreen; ++iter) {
                auto rtA = makeRt();
                auto texA = makeColorTex(255, 0, 0);
                const std::uint64_t hA = viewHandleOf(*texA);
                drawTexturedBasic(*texA, rtA.get());
                (void)readRt(*rtA);
                texA.reset();
                staleForA = staleForA || (backend().EffectDescSetEntriesForViewInTests(hA) != 0);
                advanceFrames(4);                               // fence-safe free of texA's view

                auto rtB = makeRt();
                auto texB = makeColorTex(0, 255, 0);
                const std::uint64_t hB = viewHandleOf(*texB);
                if (hB == hA) reuseSeen = true;                 // the driver recycled the handle value
                drawTexturedBasic(*texB, rtB.get());
                const std::vector<Color> pix = readRt(*rtB);
                if (countIf(pix, IsGreen) < kW * kH / 2 || countIf(pix, IsRed) > 0) allGreen = false;
                texB.reset();
            }
            std::printf("    (T2 raw VkImageView handle reuse observed: %s)\n", reuseSeen ? "yes" : "no");
            check(!staleForA, "T2a destroyed texA left no stale litTextured entry keyed on its handle");
            check(allGreen,   "T2b texB renders its own GREEN after texA destroyed (no stale-cache RED)");
        }

        // === T3: EnvironmentMapEffect cube-view eviction (the path GFX-075 did not cover). ===
        {
            auto rt = makeRt();
            auto cube = makeColorCube(0, 0, 255);
            const std::uint64_t h = viewHandleOf(*cube);
            check(h != 0, "T3a cube exposes a VkImageView handle");
            drawEnvMap(*cube, rt.get());
            (void)readRt(*rt);
            check(backend().EffectDescSetEntriesForViewInTests(h) >= 1,
                  "T3b envMap cache holds an entry for the cube view after drawing");
            cube.reset();
            check(backend().EffectDescSetEntriesForViewInTests(h) == 0,
                  "T3c envMap cache entry EVICTED when the source cube is destroyed");
        }

        // === T4: DualTextureEffect -- both sampled views drive independent eviction. ===
        {
            auto rt = makeRt();
            auto t0 = makeColorTex(255, 0, 0);
            auto t1 = makeColorTex(0, 255, 0);
            const std::uint64_t h0 = viewHandleOf(*t0);
            const std::uint64_t h1 = viewHandleOf(*t1);
            drawDualTex(*t0, *t1, rt.get());
            (void)readRt(*rt);
            check(backend().EffectDescSetEntriesForViewInTests(h0) >= 1 &&
                  backend().EffectDescSetEntriesForViewInTests(h1) >= 1,
                  "T4a dualTex cache holds an entry referencing both views after drawing");
            t0.reset();
            check(backend().EffectDescSetEntriesForViewInTests(h0) == 0,
                  "T4b destroying texture0 evicts the shared dualTex entry (via view0)");
            check(backend().EffectDescSetEntriesForViewInTests(h1) == 0,
                  "T4c the same entry is gone for view1 too (one entry, two views)");
            t1.reset();
        }

        // === T5: cache is memory-bounded -- entries do not accumulate across many temp textures. ===
        // Without a per-view free path the litTextured cache (and its pool) would grow by one every
        // iteration; with eviction it returns to its baseline after each destroyed texture.
        {
            const std::size_t base = backend().TotalEffectDescSetEntriesForTests();
            std::size_t peak = base;
            for (int i = 0; i < 64; ++i) {
                auto rt  = makeRt();
                auto tex = makeColorTex(static_cast<std::uint8_t>(i * 3), 128, 200);
                drawTexturedBasic(*tex, rt.get());
                (void)readRt(*rt);
                const std::size_t during = backend().TotalEffectDescSetEntriesForTests();
                if (during > peak) peak = during;
                tex.reset();
            }
            const std::size_t after = backend().TotalEffectDescSetEntriesForTests();
            std::printf("    (T5 effect-cache entries: base=%zu peak=%zu after=%zu over 64 temp textures)\n",
                        base, peak, after);
            check(peak <= base + 3,  "T5a cache stays bounded while cycling 64 temporary textures");
            check(after <= base + 1, "T5b cache returns to baseline after the temporaries are destroyed");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, total_);
        result_ = (pass_ == total_) ? 0 : 1;
        Exit();
    }

public:
    EffectDescriptorCacheIdentityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(96);
        gdm_->setPreferredBackBufferHeightProperty(72);
    }

    int getResult() const { return result_; }
};

int main()
{
    EffectDescriptorCacheIdentityTest game;
    game.Run();
    return game.getResult();
}
