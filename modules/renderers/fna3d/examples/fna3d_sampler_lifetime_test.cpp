// SPDX-License-Identifier: MS-PL
// MUSEUM-FNA3D-001: observe the real native API boundaries without replacing GPU work.
// ELF interposition makes a released binding fail deterministically before a driver UAF.
// The draw case covers a hole in the punctual-light shader's sampler registers; the
// release cases cover each owned texture kind, both stages, and compiled cache reuse.
#include "CNA/Internal/Renderers/Fna3d/Fna3dCompiledEffect.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include <SDL3/SDL_gpu.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
std::unordered_set<SDL_GPUTexture*> released;
unsigned bindings = 0, releases = 0;
std::array<FNA3D_Texture*, 16> pixelBindings{};
std::array<FNA3D_Texture*, 4> vertexBindings{};
FNA3D_Texture* watched = nullptr;
bool watchedReleased = false;
template <class T> T Next(const char* name)
{
    auto p = reinterpret_cast<T>(dlsym(RTLD_NEXT, name));
    if (!p)
    {
        std::fprintf(stderr, "missing symbol %s\n", name);
        std::_Exit(91);
    }
    return p;
}
extern "C" void FNA3D_VerifySampler(FNA3D_Device* device, int32_t slot, FNA3D_Texture* texture,
                                    FNA3D_SamplerState* sampler)
{
    pixelBindings.at(slot) = texture;
    Next<decltype(&FNA3D_VerifySampler)>("FNA3D_VerifySampler")(device, slot, texture, sampler);
}
extern "C" void FNA3D_VerifyVertexSampler(FNA3D_Device* device, int32_t slot,
                                          FNA3D_Texture* texture, FNA3D_SamplerState* sampler)
{
    vertexBindings.at(slot) = texture;
    Next<decltype(&FNA3D_VerifyVertexSampler)>("FNA3D_VerifyVertexSampler")(device, slot, texture,
                                                                            sampler);
}
extern "C" void FNA3D_AddDisposeTexture(FNA3D_Device* device, FNA3D_Texture* texture)
{
    if (texture == watched)
    {
        for (auto* bound : pixelBindings)
            if (bound == texture)
            {
                std::fprintf(stderr, "FAIL disposing a texture still in a fragment sampler\n");
                std::_Exit(93);
            }
        for (auto* bound : vertexBindings)
            if (bound == texture)
            {
                std::fprintf(stderr, "FAIL disposing a texture still in a vertex sampler\n");
                std::_Exit(94);
            }
        watchedReleased = true;
    }
    Next<decltype(&FNA3D_AddDisposeTexture)>("FNA3D_AddDisposeTexture")(device, texture);
}
} // namespace
extern "C" SDL_GPUTexture* SDL_CreateGPUTexture(SDL_GPUDevice* d,
                                                const SDL_GPUTextureCreateInfo* info)
{
    auto* t = Next<decltype(&SDL_CreateGPUTexture)>("SDL_CreateGPUTexture")(d, info);
    released.erase(t);
    return t;
}
extern "C" void SDL_ReleaseGPUTexture(SDL_GPUDevice* d, SDL_GPUTexture* t)
{
    released.insert(t);
    ++releases;
    Next<decltype(&SDL_ReleaseGPUTexture)>("SDL_ReleaseGPUTexture")(d, t);
}
extern "C" void SDL_BindGPUFragmentSamplers(SDL_GPURenderPass* pass, Uint32 first,
                                            const SDL_GPUTextureSamplerBinding* pairs, Uint32 count)
{
    ++bindings;
    for (Uint32 i = 0; i < count; ++i)
    {
        if (released.contains(pairs[i].texture))
        {
            std::fprintf(stderr,
                         "FAIL released texture rebound at fragment slot %u; "
                         "bindings=%u releases=%u\n",
                         first + i, bindings, releases);
            std::_Exit(90);
        }
    }
    Next<decltype(&SDL_BindGPUFragmentSamplers)>("SDL_BindGPUFragmentSamplers")(pass, first, pairs,
                                                                                count);
}
int main(int argc, char** argv)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                     PresentationParameters::CreateWindowedEXT(800, 480));
    if (argc == 2 && std::string(argv[1]) == "--device-only")
    {
        std::puts("PASS device-only control: no user textures or lifetime repair exercised");
        std::fflush(stdout);
        return 0;
    }
    if (argc == 3)
    {
        using namespace CNA::Internal::Renderers;
        using namespace CNA::Internal::Renderers::Fna3d;
        using namespace CNA::TestSupport;
        auto& renderer = dynamic_cast<Fna3dRenderer&>(d.GetRenderer());
        const std::string kind = argv[1];
        const bool vertex = std::string(argv[2]) == "vertex";
        std::unique_ptr<Texture> texture;
        auto samplerKind = SyntheticSamplerKind::Sampler2D;
        if (kind == "2d")
            texture = std::make_unique<Texture2D>(d, 2, 2);
        else if (kind == "rt2d")
            texture = std::make_unique<RenderTarget2D>(d, 2, 2, false, SurfaceFormat::Color,
                                                       DepthFormat::None);
        else if (kind == "cube")
        {
            texture = std::make_unique<TextureCube>(d, 2, false, SurfaceFormat::Color);
            samplerKind = SyntheticSamplerKind::SamplerCube;
        }
        else if (kind == "rtcube")
        {
            texture = std::make_unique<RenderTargetCube>(d, 2, false, SurfaceFormat::Color,
                                                         DepthFormat::None);
            samplerKind = SyntheticSamplerKind::SamplerCube;
        }
        else if (kind == "3d")
        {
            texture = std::make_unique<Texture3D>(d, 2, 2, 2, false, SurfaceFormat::Color);
            samplerKind = SyntheticSamplerKind::Sampler3D;
        }
        else
            return 95;
        SyntheticEffectOptions options;
        options.includeSampler = true;
        options.includeDrawableProgram = true;
        options.vertexShaderSamplesTexture = vertex;
        options.samplerKind = samplerKind;
        options.samplerRegister = 2;
        const auto bytes = BuildSyntheticEffect(options);
        Fna3dCompiledEffect effect(renderer, bytes.data(), bytes.size());
        std::uint32_t parameter = UINT32_MAX;
        for (const auto& p : effect.GetDescription().parameters)
            if (p.name == "FxTexture")
                parameter = p.runtimeIndex;
        if (parameter == UINT32_MAX)
            return 96;
        effect.SetParameterTexture(parameter, texture.get());
        CompiledEffectPassStateChanges changes;
        effect.ApplyPass(1, {}, changes);
        watched = vertex ? vertexBindings[2] : pixelBindings[2];
        if (!watched)
            return 97;
        // A separate live binding must survive retirement of the watched resource.
        Texture2D survivor(d, 2, 2);
        renderer.BindEffectTextureEXT(7, &survivor.GetRenderer());
        auto* const survivorNative = pixelBindings[7];
        effect.SetParameterTexture(parameter, nullptr);
        texture.reset();
        if (!watchedReleased || pixelBindings[7] != survivorNative)
            return 98;
        // Null texture assignments reuse the renderer cache. It too must have been
        // cleared.
        changes = {};
        effect.ApplyPass(1, {}, changes);
        if ((vertex ? vertexBindings[2] : pixelBindings[2]) != nullptr)
            return 99;
        std::printf("PASS %s %s: unbound before release, cache cleared, live slot "
                    "preserved\n",
                    argv[1], argv[2]);
        std::fflush(stdout);
        watched = nullptr;
        return 0;
    }
    RenderTarget2D target(d, 16, 16, false, SurfaceFormat::Color, DepthFormat::None);
    d.SetRenderTarget(&target);
    d.setBlendStateProperty(BlendState::Opaque);
    d.setDepthStencilStateProperty(DepthStencilState::None);
    d.setRasterizerStateProperty(RasterizerState::CullNone);
    const VertexPositionNormalTexture tri[] = {
        {Vector3(-1, -1, 0), Vector3(0, 0, 1), Vector2(0, 1)},
        {Vector3(1, -1, 0), Vector3(0, 0, 1), Vector2(1, 1)},
        {Vector3(0, 1, 0), Vector3(0, 0, 1), Vector2(0.5f, 0)}};
    {
        Texture2D texture =
            Texture2D::CreateFromPixels(d, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
        BasicEffect textured(d);
        textured.setTextureEnabledProperty(true);
        textured.setTextureProperty(&texture);
        textured.Apply();
        d.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);
    }
    // Museum's punctual probe follows a room which draws and destroys textures.
    for (int i = 0; i < 4; ++i)
        d.getTexturesProperty()(i, nullptr);
    BasicEffect punctual(d);
    punctual.setLightingEnabledProperty(true);
    punctual.setTextureEnabledProperty(false);
    punctual.setPreferPerPixelLightingProperty(true);
    PunctualLightEXT light;
    light.Kind = PunctualLightKindEXT::Point;
    light.Position = Vector3(0, 0, 1);
    light.Range = 10;
    light.DiffuseColor = Vector3(6, 6, 6);
    punctual.setPunctualLightEXT(light);
    punctual.Apply();
    d.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);
    d.SetRenderTarget(nullptr);
    std::vector<Color> pixels(256);
    target.GetData(pixels.data(), 256);
    std::printf("PASS bindings=%u releases=%u\n", bindings, releases);
    std::fflush(stdout);
    return bindings >= 2 && releases > 0 ? 0 : 92;
}
