// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dEnumMapping.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Graphics/TextureKind.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dVertexLayouts.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Fna3d
{
    namespace
    {
        /** Whether the packed alpha-test vector can ever discard a fragment. */
        bool AlphaTestActive(const GpuDrawParams& params)
        {
            // XNA's shader is clip((abs(a - x) < y) ? z : w); a positive z and w can never be
            // negative, so the default {0,0,1,1} is exactly "never discards".
            return params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f;
        }

        /** Whether only DirectionalLight0 contributes, which selects XNA's one-light variants. */
        bool OneLight(const GpuDrawParams& params)
        {
            const auto isBlack = [](const float (&rgb)[3]) {
                return rgb[0] == 0.0f && rgb[1] == 0.0f && rgb[2] == 0.0f;
            };
            // The shared effect layer zeroes a disabled light's diffuse *and* specular colours
            // (mirroring FNA's DirectionalLight.Enabled setter), so an all-black pair is exactly
            // "this light is disabled" and never a legitimately black enabled light.
            return isBlack(params.light1Diffuse) && isBlack(params.light1Specular) &&
                   isBlack(params.light2Diffuse) && isBlack(params.light2Specular);
        }

        StockEffectKind SelectStockEffect(const GpuDrawParams& params)
        {
            if (params.pbr)
                return params.skinned ? StockEffectKind::SkinnedPbr : StockEffectKind::Pbr;
            // XNA's stock SkinnedEffect binary has no COLOR0 variant. CNA's compiled punctual
            // effect does, and with punctualKind == 0 its extra lamp contributes exactly zero, so
            // it is also the native route for CNA's SkinnedEffect.VertexColorEnabled extension.
            if (params.lightingEnabled &&
                (params.punctualKind != 0 || (params.skinned && params.vertexColorEnabled)))
                return StockEffectKind::Punctual;
            if (params.skinned)      return StockEffectKind::Skinned;
            if (params.envMapping)   return StockEffectKind::EnvironmentMap;
            if (params.dualTexture)  return StockEffectKind::DualTexture;
            if (AlphaTestActive(params)) return StockEffectKind::AlphaTest;
            return StockEffectKind::Basic;
        }

        /** Whether any active stream supplies COLOR0 to a PBR vertex program. */
        bool PbrVertexColorDeclared(const IVertexBufferRenderer& vb,
                                    const GpuDrawParams& params)
        {
            const auto hasColor = [](const Fna3dVertexBufferRenderer& buffer,
                                     int declaredStride) {
                std::span<const FNA3D_VertexElement> elements;
                if (buffer.HasDeclarationEXT())
                {
                    const auto& declared = buffer.GetDeclarationElementsEXT();
                    elements = {declared.data(), declared.size()};
                }
                else
                {
                    const int stride = declaredStride > 0
                        ? declaredStride : buffer.GetStrideEXT();
                    elements = ElementsForStride(stride);
                }
                return std::any_of(elements.begin(), elements.end(), [](const auto& element) {
                    return element.vertexElementUsage == FNA3D_VERTEXELEMENTUSAGE_COLOR &&
                           element.usageIndex == 0;
                });
            };

            if (params.vertexStreamCount > 0)
            {
                for (int i = 0; i < params.vertexStreamCount; ++i)
                {
                    const GpuVertexStreamBinding& stream = params.vertexStreams[i];
                    const auto* buffer =
                        dynamic_cast<const Fna3dVertexBufferRenderer*>(stream.buffer);
                    if (buffer != nullptr && hasColor(*buffer, stream.strideInBytes))
                        return true;
                }
                return false;
            }
            const auto* buffer = dynamic_cast<const Fna3dVertexBufferRenderer*>(&vb);
            return buffer != nullptr && hasColor(*buffer, 0);
        }

        /**
         * plans/plan_apple_m4.md AM4-190: the inputs the selected XNA stock vertex shader reads,
         * from the stock effects' own HLSL (BasicEffect.fx and its siblings), as (usage, index).
         * XNA refuses a draw whose declaration omits one; FNA3D's SDL_GPU and Direct3D 11 drivers
         * fail the pipeline, and its SDL_GPU driver then draws with a null one and crashes.
         */
        void RequireStockVertexInputsDeclared(const GpuDrawParams& params,
                                              const FNA3D_VertexBufferBinding* bindings,
                                              int bindingCount)
        {
            struct Input { FNA3D_VertexElementUsage usage; int index; const char* name; };
            std::array<Input, 8> needed{};
            std::size_t count = 0;
            const auto need = [&](FNA3D_VertexElementUsage usage, int index, const char* name) {
                needed[count++] = Input{usage, index, name};
            };
            need(FNA3D_VERTEXELEMENTUSAGE_POSITION, 0, "Position0");
            const StockEffectKind kind = SelectStockEffect(params);
            const char* effectName = "BasicEffect";
            switch (kind)
            {
                case StockEffectKind::Basic:
                    if (params.lightingEnabled)
                        need(FNA3D_VERTEXELEMENTUSAGE_NORMAL, 0, "Normal0");
                    if (params.textureEnabled)
                        need(FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE, 0, "TextureCoordinate0");
                    if (params.vertexColorEnabled)
                        need(FNA3D_VERTEXELEMENTUSAGE_COLOR, 0, "Color0");
                    break;
                case StockEffectKind::AlphaTest:
                    effectName = "AlphaTestEffect";
                    need(FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE, 0, "TextureCoordinate0");
                    if (params.vertexColorEnabled)
                        need(FNA3D_VERTEXELEMENTUSAGE_COLOR, 0, "Color0");
                    break;
                case StockEffectKind::DualTexture:
                    effectName = "DualTextureEffect";
                    need(FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE, 0, "TextureCoordinate0");
                    need(FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE, 1, "TextureCoordinate1");
                    if (params.vertexColorEnabled)
                        need(FNA3D_VERTEXELEMENTUSAGE_COLOR, 0, "Color0");
                    break;
                case StockEffectKind::EnvironmentMap:
                    effectName = "EnvironmentMapEffect";
                    need(FNA3D_VERTEXELEMENTUSAGE_NORMAL, 0, "Normal0");
                    need(FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE, 0, "TextureCoordinate0");
                    break;
                case StockEffectKind::Skinned:
                    effectName = "SkinnedEffect";
                    need(FNA3D_VERTEXELEMENTUSAGE_NORMAL, 0, "Normal0");
                    need(FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE, 0, "TextureCoordinate0");
                    need(FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES, 0, "BlendIndices0");
                    need(FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT, 0, "BlendWeight0");
                    break;
                case StockEffectKind::Punctual:
                    effectName = "CnaPunctualEffect";
                    need(FNA3D_VERTEXELEMENTUSAGE_NORMAL, 0, "Normal0");
                    if (params.textureEnabled || params.skinned)
                        need(FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE, 0,
                             "TextureCoordinate0");
                    if (params.skinned)
                    {
                        need(FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES, 0, "BlendIndices0");
                        need(FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT, 0, "BlendWeight0");
                    }
                    if (params.vertexColorEnabled)
                        need(FNA3D_VERTEXELEMENTUSAGE_COLOR, 0, "Color0");
                    break;
                case StockEffectKind::Pbr:
                case StockEffectKind::SkinnedPbr:
                    effectName = kind == StockEffectKind::Pbr
                        ? "CnaPbrEffect" : "CnaSkinnedPbrEffect";
                    need(FNA3D_VERTEXELEMENTUSAGE_NORMAL, 0, "Normal0");
                    need(FNA3D_VERTEXELEMENTUSAGE_TANGENT, 0, "Tangent0");
                    need(FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE, 0,
                         "TextureCoordinate0");
                    if (params.pbrTextureCoordinateSetMask != 0)
                        need(FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE, 1,
                             "TextureCoordinate1");
                    if (kind == StockEffectKind::SkinnedPbr)
                    {
                        need(FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES, 0, "BlendIndices0");
                        need(FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT, 0, "BlendWeight0");
                    }
                    break;
                default:
                    return;
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                bool declared = false;
                for (int b = 0; b < bindingCount && !declared; ++b)
                {
                    const FNA3D_VertexDeclaration& declaration = bindings[b].vertexDeclaration;
                    for (int e = 0; e < declaration.elementCount && !declared; ++e)
                        declared = declaration.elements[e].vertexElementUsage == needed[i].usage &&
                                   declaration.elements[e].usageIndex == needed[i].index;
                }
                if (!declared)
                {
                    throw System::NotSupportedException(
                        std::string("FNA3D renderer (FNA3D-MISSING-STOCK-INPUT): the ") + effectName +
                        " variant this draw selects reads " + needed[i].name +
                        ", which no bound VertexDeclaration declares. XNA refuses such a draw, and "
                        "FNA3D's SDL_GPU and Direct3D 11 drivers cannot create a pipeline for it "
                        "(only its OpenGL driver reads a default for a missing input).");
                }
            }
        }

        const Fna3dSampledTexture* GetSampledTexture(Texture* texture)
        {
            using namespace Microsoft::Xna::Framework::Graphics;
            if (texture == nullptr) return nullptr;
            if (auto* texture2D = dynamic_cast<Texture2D*>(texture))
                return dynamic_cast<const Fna3dSampledTexture*>(&texture2D->GetRenderer());
            if (auto* texture3D = CNA::Internal::Graphics::AsTexture3D(texture))
                return dynamic_cast<const Fna3dSampledTexture*>(&texture3D->GetRenderer());
            if (auto* textureCube = CNA::Internal::Graphics::AsTextureCube(texture))
                return dynamic_cast<const Fna3dSampledTexture*>(&textureCube->GetRenderer());
            return nullptr;
        }
    }

    void Fna3dRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world,
                                              const Matrix& view, const Matrix& projection,
                                              PrimitiveType primitive, int primitiveCount)
    {
        GpuDrawParams params{};
        params.vertexColorEnabled = true;
        params.textureEnabled = false;
        params.lightingEnabled = false;
        DrawPrimitivesEx(vb, world, view, projection, primitive, primitiveCount, params);
    }

    void Fna3dRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                     const IIndexBufferRenderer& ib,
                                                     const Matrix& world, const Matrix& view,
                                                     const Matrix& projection,
                                                     PrimitiveType primitive, int primitiveCount)
    {
        GpuDrawParams params{};
        params.vertexColorEnabled = true;
        params.textureEnabled = false;
        params.lightingEnabled = false;
        DrawIndexedPrimitivesEx(vb, ib, world, view, projection, primitive, primitiveCount,
                                params);
    }

    void Fna3dRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world,
                                         const Matrix& view, const Matrix& projection,
                                         PrimitiveType primitive, int primitiveCount,
                                         const GpuDrawParams& params)
    {
        PrepareDrawEXT(vb, world, view, projection, params, /*baseVertex=*/0);
        FNA3D_DrawPrimitives(device_, ToFna3dPrimitiveType(primitive), params.vertexStart,
                             primitiveCount);
    }

    void Fna3dRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                const IIndexBufferRenderer& ib,
                                                const Matrix& world, const Matrix& view,
                                                const Matrix& projection, PrimitiveType primitive,
                                                int primitiveCount, const GpuDrawParams& params)
    {
        const auto* indexBuffer = dynamic_cast<const Fna3dIndexBufferRenderer*>(&ib);
        if (indexBuffer == nullptr || indexBuffer->GetFna3dDeviceStateEXT() != deviceState_)
        {
            throw std::runtime_error(
                "FNA3D renderer: the index buffer was not created by this graphics device.");
        }
        PrepareDrawEXT(vb, world, view, projection, params, params.baseVertex);
        FNA3D_DrawIndexedPrimitives(
            device_, ToFna3dPrimitiveType(primitive), params.baseVertex, params.minVertexIndex,
            params.numVertices > 0 ? params.numVertices : vb.GetVertexCount(), params.startIndex,
            primitiveCount, indexBuffer->GetFna3dBufferEXT(), indexBuffer->GetElementSizeEXT());
    }

    void Fna3dRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                  const IIndexBufferRenderer& ib,
                                                  const Matrix& world, const Matrix& view,
                                                  const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  int instanceCount, const GpuDrawParams& params)
    {
        if (params.compiledEffectRuntime == nullptr)
        {
            throw std::runtime_error(
                "FNA3D renderer: instanced drawing requires a compiled XNA Effect whose vertex "
                "shader consumes the bound per-instance stream; stock effects do not declare "
                "instance semantics.");
        }
        const auto& indexBuffer = static_cast<const Fna3dIndexBufferRenderer&>(ib);
        PrepareDrawEXT(vb, world, view, projection, params, params.baseVertex);
        FNA3D_DrawInstancedPrimitives(
            device_, ToFna3dPrimitiveType(primitive), params.baseVertex, params.minVertexIndex,
            params.numVertices > 0 ? params.numVertices : vb.GetVertexCount(), params.startIndex,
            primitiveCount, instanceCount, indexBuffer.GetFna3dBufferEXT(),
            indexBuffer.GetElementSizeEXT());
    }

    void Fna3dRenderer::PrepareDrawEXT(const IVertexBufferRenderer& vb, const Matrix& world,
                                       const Matrix& view, const Matrix& projection,
                                       const GpuDrawParams& params, int baseVertex)
    {
        // XNA's D3D9 rasterization convention uses integer pixel centres. FNA3D normally exposes
        // the host driver's convention, so ask CNA's pinned FNA3D extension to apply the same
        // correction used by the other CNA 3D renderers. SpriteBatch explicitly disables it.
        FNA3D_SetXnaPixelCenterEXT(device_, 1);
        if (params.compiledEffectRuntime == nullptr)
        {
            ApplyStockEffectEXT(world, view, projection, params,
                                params.pbr && PbrVertexColorDeclared(vb, params));
        }
        else
        {
            // EffectPass.Apply may assign a texture, or a game may replace the public device slot
            // afterward. Resolve the final XNA-visible slot at draw time, matching FNA's device
            // state application instead of retaining the native binding from pass application.
            BindCompiledPixelTexturesEXT(params);
            BindCompiledVertexTexturesEXT(params);
        }
        ApplyVertexBindingsEXT(vb, params, baseVertex);
    }

    void Fna3dRenderer::ApplyVertexBindingsEXT(const IVertexBufferRenderer& vb,
                                               const GpuDrawParams& params, int baseVertex)
    {
        constexpr int kMaxElementsPerDeclaration = 16;
        if (params.vertexStreamCount < 0 || params.vertexStreamCount > kMaxVertexStreams)
        {
            throw std::runtime_error(
                "FNA3D renderer: a draw declared " +
                std::to_string(params.vertexStreamCount) +
                " vertex streams, but XNA and CNA permit at most " +
                std::to_string(kMaxVertexStreams) + ".");
        }

        const auto* primaryBuffer = dynamic_cast<const Fna3dVertexBufferRenderer*>(&vb);
        if (primaryBuffer == nullptr || primaryBuffer->GetFna3dDeviceStateEXT() != deviceState_)
        {
            throw std::runtime_error(
                "FNA3D renderer: the primary vertex buffer was not created by this graphics "
                "device.");
        }

        // FNA3D requires the elements to live through ApplyVertexBufferBindings. CNA's public
        // contract caps a draw at 16 streams; XNA's vertex declaration cap is 16 elements. Keep
        // the complete native payload on the stack, so an ordinary draw does not allocate merely
        // to translate declarations it already owns.
        std::array<FNA3D_VertexBufferBinding, kMaxVertexStreams> bindings{};
        std::array<std::array<FNA3D_VertexElement, kMaxElementsPerDeclaration>,
                   kMaxVertexStreams> elementStorage{};

        const auto describe = [&](const Fna3dVertexBufferRenderer& buffer, int declaredStride,
                                  int bindingIndex, FNA3D_VertexDeclaration& declaration) {
            std::span<const FNA3D_VertexElement> source;
            if (buffer.HasDeclarationEXT())
            {
                const auto& declaredElements = buffer.GetDeclarationElementsEXT();
                source = { declaredElements.data(), declaredElements.size() };
                declaration.vertexStride = buffer.GetDeclarationStrideEXT();
            }
            else
            {
                // Internal staged routes bind no public VertexDeclaration, so their known typed
                // stride selects the canonical static layout table.
                const int stride = declaredStride > 0 ? declaredStride : buffer.GetStrideEXT();
                source = ElementsForStride(stride);
                declaration.vertexStride = stride;
            }

            if (source.empty() || source.size() > kMaxElementsPerDeclaration)
            {
                throw std::runtime_error(
                    "FNA3D renderer: a vertex declaration has " +
                    std::to_string(source.size()) +
                    " elements; this renderer accepts 1 through " +
                    std::to_string(kMaxElementsPerDeclaration) + ".");
            }

            std::copy(source.begin(), source.end(), elementStorage[bindingIndex].begin());
            declaration.elementCount = static_cast<int32_t>(source.size());
            declaration.elements = elementStorage[bindingIndex].data();
        };

        const int bindingCount = params.vertexStreamCount > 0 ? params.vertexStreamCount : 1;
        if (params.vertexStreamCount > 0)
        {
            for (int i = 0; i < bindingCount; ++i)
            {
                const GpuVertexStreamBinding& stream = params.vertexStreams[i];
                const auto* streamBuffer =
                    dynamic_cast<const Fna3dVertexBufferRenderer*>(stream.buffer);
                if (streamBuffer == nullptr ||
                    streamBuffer->GetFna3dDeviceStateEXT() != deviceState_)
                {
                    throw std::runtime_error(
                        "FNA3D renderer: a declared vertex stream was not created by this "
                        "graphics device; the binding list is never silently truncated.");
                }
                FNA3D_VertexBufferBinding& binding = bindings[static_cast<std::size_t>(i)];
                binding.vertexBuffer = streamBuffer->GetFna3dBufferEXT();
                binding.vertexOffset = stream.vertexOffset;
                binding.instanceFrequency = stream.instanceFrequency;
                describe(*streamBuffer, stream.strideInBytes, i, binding.vertexDeclaration);
            }
        }
        else
        {
            FNA3D_VertexBufferBinding& binding = bindings[0];
            binding.vertexBuffer = primaryBuffer->GetFna3dBufferEXT();
            binding.vertexOffset = 0;
            binding.instanceFrequency = 0;
            describe(*primaryBuffer, 0, 0, binding.vertexDeclaration);
        }

        if (params.compiledEffectRuntime == nullptr && !driverDefaultsMissingVertexInputs_)
            RequireStockVertexInputsDeclared(params, bindings.data(), bindingCount);
        FNA3D_ApplyVertexBufferBindings(device_, bindings.data(), bindingCount, 1, baseVertex);
    }

    void Fna3dRenderer::BindEffectTextureEXT(int slot, const ITextureRenderer* texture)
    {
        if (slot < 0)
        {
            return;
        }
        // A texture bound to a slot the driver does not have samples nothing at all, silently.
        // FNA3D reports the driver's real ceiling, so this is the point where such a bind is
        // refused by name. The check precedes the sampler-array bound below because on a driver
        // whose ceiling is XNA's own 16 the two coincide, and a silent return there would make the
        // boundary unobservable.
        if (maxTextureSlots_ > 0 && slot >= maxTextureSlots_)
        {
            throw std::runtime_error(
                "FNA3D renderer: a texture was bound to sampler slot " + std::to_string(slot) +
                ", but the running driver exposes only " + std::to_string(maxTextureSlots_) +
                " fragment texture slots. Binding it would sample nothing.");
        }
        if (slot >= static_cast<int>(samplerStates_.size()))
        {
            return;
        }

        // A render target bound as an effect texture is an IRenderTargetRenderer, not an
        // Fna3dTextureRenderer, so the sampled-texture mix-in is what both have in common. A
        // type match alone is insufficient: two live FNA3D renderers own different devices.
        const auto* sampled = dynamic_cast<const Fna3dSampledTexture*>(texture);
        if (texture != nullptr &&
            (sampled == nullptr || sampled->GetFna3dDeviceStateEXT() != deviceState_))
        {
            throw std::runtime_error(
                "FNA3D renderer: an effect texture was not created by this graphics device.");
        }
        FNA3D_Texture* native = sampled != nullptr ? sampled->GetFna3dTextureEXT() : nullptr;
        FNA3D_VerifySampler(device_, slot, native,
                            &samplerStates_[static_cast<std::size_t>(slot)]);
        boundPixelTextures_[static_cast<std::size_t>(slot)] = native;
    }

    ITextureRenderer* Fna3dRenderer::GetOrCreatePbrWhiteTextureEXT()
    {
        if (pbrDefaultWhiteTexture_ == nullptr)
        {
            CNA::Internal::Graphics::ImageData data{};
            data.width = 1;
            data.height = 1;
            data.pixels = {255, 255, 255, 255};
            pbrDefaultWhiteTexture_ = CreateTexture(data);
        }
        return pbrDefaultWhiteTexture_.get();
    }

    ITextureRenderer* Fna3dRenderer::GetOrCreatePbrFlatNormalTextureEXT()
    {
        if (pbrDefaultFlatNormalTexture_ == nullptr)
        {
            CNA::Internal::Graphics::ImageData data{};
            data.width = 1;
            data.height = 1;
            data.pixels = {128, 128, 255, 255};
            pbrDefaultFlatNormalTexture_ = CreateTexture(data);
        }
        return pbrDefaultFlatNormalTexture_.get();
    }

    void Fna3dRenderer::BindCompiledPixelTexturesEXT(const GpuDrawParams& params,
                                                     FNA3D_Texture* spriteTexture0)
    {
        const auto* textures = params.compiledDeviceTextures;
        if (textures == nullptr && spriteTexture0 == nullptr) return;

        using Microsoft::Xna::Framework::Graphics::TextureCollection;
        const int slotCount = std::min(
            maxTextureSlots_ > 0 ? maxTextureSlots_ : TextureCollection::MaxTextures,
            TextureCollection::MaxTextures);
        for (int slot = 0; slot < slotCount; ++slot)
        {
            Texture* texture = textures != nullptr ? (*textures)[slot] : nullptr;
            const Fna3dSampledTexture* sampled = GetSampledTexture(texture);
            if (texture != nullptr &&
                (sampled == nullptr || sampled->GetFna3dDeviceStateEXT() != deviceState_))
            {
                throw std::runtime_error(
                    "FNA3D renderer: a compiled effect device texture was not created by this "
                    "graphics device.");
            }

            FNA3D_Texture* native = sampled != nullptr ? sampled->GetFna3dTextureEXT() : nullptr;
            if (slot == 0 && spriteTexture0 != nullptr) native = spriteTexture0;
            FNA3D_VerifySampler(device_, slot, native,
                                &samplerStates_[static_cast<std::size_t>(slot)]);
            boundPixelTextures_[static_cast<std::size_t>(slot)] = native;
        }
    }

    void Fna3dRenderer::BindCompiledVertexTexturesEXT(const GpuDrawParams& params)
    {
        const auto* textures = params.compiledDeviceVertexTextures;
        const auto* samplers = params.compiledDeviceVertexSamplerStates;
        if (textures == nullptr || samplers == nullptr) return;

        // The public collection reports four vertex-stage slots in HiDef and none in Reach.
        // Using that count avoids indexing a deliberately empty Reach collection.
        const int publicSlots = samplers->ActiveSamplerCount();
        for (int slot = 0; slot < publicSlots; ++slot)
        {
            Texture* texture = (*textures)[slot];
            if (maxVertexTextureSlots_ <= 0 || slot >= maxVertexTextureSlots_)
            {
                if (texture != nullptr)
                    throw std::runtime_error(
                        "FNA3D renderer: a vertex texture was bound to sampler slot " +
                        std::to_string(slot) + ", but the running driver exposes only " +
                        std::to_string(std::max(maxVertexTextureSlots_, 0)) +
                        " vertex texture slots.");
                continue;
            }

            const Fna3dSampledTexture* sampled = GetSampledTexture(texture);
            if (texture != nullptr &&
                (sampled == nullptr || sampled->GetFna3dDeviceStateEXT() != deviceState_))
            {
                throw std::runtime_error(
                    "FNA3D renderer: a compiled effect vertex texture was not created by this "
                    "graphics device.");
            }

            const auto& state = (*samplers)[slot];
            FNA3D_SamplerState native = vertexSamplerStates_[static_cast<std::size_t>(slot)];
            native.filter = static_cast<FNA3D_TextureFilter>(state.getFilterProperty());
            native.addressU = static_cast<FNA3D_TextureAddressMode>(state.getAddressUProperty());
            native.addressV = static_cast<FNA3D_TextureAddressMode>(state.getAddressVProperty());
            native.addressW = static_cast<FNA3D_TextureAddressMode>(state.getAddressWProperty());
            native.mipMapLevelOfDetailBias = state.getMipMapLevelOfDetailBiasProperty();
            native.maxMipLevel = state.getMaxMipLevelProperty();
            native.maxAnisotropy = state.getMaxAnisotropyProperty();
            FNA3D_Texture* nativeTexture = sampled != nullptr ? sampled->GetFna3dTextureEXT()
                                                              : nullptr;
            FNA3D_VerifyVertexSampler(device_, slot, nativeTexture, &native);
            vertexSamplerStates_[static_cast<std::size_t>(slot)] = native;
            boundVertexTextures_[static_cast<std::size_t>(slot)] = nativeTexture;
        }
    }

    void Fna3dRenderer::ApplyStockEffectEXT(const Matrix& world, const Matrix& view,
                                            const Matrix& projection, const GpuDrawParams& params,
                                            bool pbrVertexColorDeclared)
    {
        const StockEffectKind kind = SelectStockEffect(params);
        Fna3dStockEffect& effect = effects_[static_cast<std::size_t>(kind)];

        const Matrix worldView = Matrix::Multiply(world, view);
        const Matrix worldViewProj = Matrix::Multiply(worldView, projection);
        effect.SetMatrix("WorldViewProj", worldViewProj);

        // Fog: the shared layer already produces FNA's own fogVector (EffectHelpers.SetFogVector),
        // all-zero when fog is disabled -- which is exactly the value FNA writes in that case, so
        // it is forwarded verbatim rather than recomputed here.
        effect.SetFloats("FogVector", params.fogVector, 4);
        effect.SetFloats("FogColor", params.fogColor, 3);

        const bool oneLight = OneLight(params);

        switch (kind)
        {
            case StockEffectKind::Basic:
            {
                effect.SetFloats("DiffuseColor", params.diffuseColor, 4);
                ApplyLightingParamsEXT(effect, world, view, params, /*withSpecular=*/true);
                effect.SetInt("ShaderIndex",
                              StockShaderIndex::Basic(params.fogEnabled, params.vertexColorEnabled,
                                                      params.textureEnabled,
                                                      params.lightingEnabled,
                                                      params.preferPerPixelLighting, oneLight));
                effect.Apply(device_);
                BindEffectTextureEXT(0, params.texture0);
                break;
            }

            case StockEffectKind::AlphaTest:
            {
                effect.SetFloats("DiffuseColor", params.diffuseColor, 4);
                effect.SetFloats("AlphaTest", params.alphaTest, 4);
                effect.SetInt("ShaderIndex",
                              StockShaderIndex::AlphaTest(params.fogEnabled,
                                                          params.vertexColorEnabled,
                                                          params.alphaTest[1] > 0.0f));
                effect.Apply(device_);
                BindEffectTextureEXT(0, params.texture0);
                break;
            }

            case StockEffectKind::DualTexture:
            {
                effect.SetFloats("DiffuseColor", params.diffuseColor, 4);
                effect.SetInt("ShaderIndex",
                              StockShaderIndex::DualTexture(params.fogEnabled,
                                                            params.vertexColorEnabled));
                effect.Apply(device_);
                BindEffectTextureEXT(0, params.texture0);
                BindEffectTextureEXT(1, params.texture1);
                break;
            }

            case StockEffectKind::EnvironmentMap:
            {
                effect.SetFloats("DiffuseColor", params.diffuseColor, 4);
                ApplyLightingParamsEXT(effect, world, view, params, /*withSpecular=*/false);
                effect.SetFloats("EnvironmentMapSpecular", params.envMapSpecular, 3);
                const float amount[4] = { params.envMapAmount, params.envMapAmount,
                                          params.envMapAmount, params.envMapAmount };
                effect.SetFloats("EnvironmentMapAmount", amount, 4);
                const float fresnel[4] = { params.fresnelFactor, params.fresnelFactor,
                                           params.fresnelFactor, params.fresnelFactor };
                effect.SetFloats("FresnelFactor", fresnel, 4);
                effect.SetInt("ShaderIndex",
                              StockShaderIndex::EnvironmentMap(params.fogEnabled,
                                                               params.fresnelEnabled,
                                                               params.specularEnabled, oneLight));
                effect.Apply(device_);
                BindEffectTextureEXT(0, params.texture0);
                const auto* cube = dynamic_cast<const Fna3dSampledTexture*>(params.envMap);
                if (params.envMap != nullptr &&
                    (cube == nullptr || cube->GetFna3dDeviceStateEXT() != deviceState_))
                {
                    throw std::runtime_error(
                        "FNA3D renderer: the environment map was not created by this graphics "
                        "device.");
                }
                FNA3D_Texture* cubeNative =
                    cube != nullptr ? cube->GetFna3dTextureEXT() : nullptr;
                FNA3D_VerifySampler(device_, 1, cubeNative, &samplerStates_[1]);
                boundPixelTextures_[1] = cubeNative;
                break;
            }

            case StockEffectKind::Skinned:
            {
                effect.SetFloats("DiffuseColor", params.diffuseColor, 4);
                ApplyLightingParamsEXT(effect, world, view, params, /*withSpecular=*/true);
                effect.SetMatrix4x3Array("Bones", params.boneTransforms, params.boneCount);
                effect.SetInt("ShaderIndex",
                              StockShaderIndex::Skinned(params.fogEnabled,
                                                        params.weightsPerVertex,
                                                        params.preferPerPixelLighting, oneLight));
                effect.Apply(device_);
                BindEffectTextureEXT(0, params.texture0);
                break;
            }

            case StockEffectKind::Punctual:
            {
                effect.SetFloats("DiffuseColor", params.diffuseColor, 4);
                ApplyLightingParamsEXT(effect, world, view, params, /*withSpecular=*/true);
                if (params.skinned)
                {
                    effect.SetMatrix4x3Array("Bones", params.boneTransforms, params.boneCount);
                    const float skinning[4] = {
                        static_cast<float>(params.weightsPerVertex), 0.0f, 0.0f, 0.0f
                    };
                    effect.SetFloats("SkinningParams", skinning, 4);
                }
                const float positionRange[4] = {
                    params.punctualPosition[0], params.punctualPosition[1],
                    params.punctualPosition[2], params.punctualRange
                };
                const float directionKind[4] = {
                    params.punctualDirection[0], params.punctualDirection[1],
                    params.punctualDirection[2], static_cast<float>(params.punctualKind)
                };
                const float punctualDiffuse[4] = {
                    params.punctualDiffuse[0], params.punctualDiffuse[1],
                    params.punctualDiffuse[2], 1.0f
                };
                const float cone[4] = {
                    params.punctualCosInner, params.punctualCosOuter, 0.0f, 0.0f
                };
                effect.SetFloats("PunctualPositionRange", positionRange, 4);
                effect.SetFloats("PunctualDirectionKind", directionKind, 4);
                effect.SetFloats("PunctualDiffuse", punctualDiffuse, 4);
                effect.SetFloats("PunctualCone", cone, 4);

                const int passIndex = params.skinned
                    ? 4 + (params.vertexColorEnabled ? 1 : 0)
                    : (params.textureEnabled ? 2 : 0) +
                          (params.vertexColorEnabled ? 1 : 0);
                effect.Apply(device_, passIndex);
                if (params.textureEnabled || params.skinned)
                    BindEffectTextureEXT(0, params.texture0);
                break;
            }

            case StockEffectKind::Pbr:
            case StockEffectKind::SkinnedPbr:
            {
                const bool skinned = kind == StockEffectKind::SkinnedPbr;
                const bool useVertexColor =
                    params.vertexColorEnabled && pbrVertexColorDeclared;
                effect.SetMatrix("World", world);
                if (skinned)
                {
                    const float fogParams[4] = {
                        0.0f, 0.0f, 0.0f, static_cast<float>(params.weightsPerVertex)
                    };
                    effect.SetFloats("FogParams", fogParams, 4);
                    effect.SetFloats("FogVector", params.fogVector, 4);
                    effect.SetMatrix4x3Array("Bones", params.boneTransforms, params.boneCount);
                }
                else
                {
                    effect.SetMatrix("NormalMatrix", Matrix::Transpose(Matrix::Invert(world)));
                    effect.SetFloats("FogParams", params.fogVector, 4);
                }

                effect.SetFloats("DiffuseColor", params.diffuseColor, 4);
                const bool useIbl = params.iblEnabled && params.iblIrradiance != nullptr &&
                                    params.iblPrefilteredSpecular != nullptr &&
                                    params.iblBrdfLut != nullptr;
                const float ambient[4] = {
                    useIbl ? 0.0f : params.ambientColor[0],
                    useIbl ? 0.0f : params.ambientColor[1],
                    useIbl ? 0.0f : params.ambientColor[2],
                    params.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f
                };
                const float emissive[4] = {
                    params.emissiveColor[0], params.emissiveColor[1], params.emissiveColor[2],
                    params.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f
                };
                const float metallicRoughness[4] = {
                    params.pbrMetallicFactor, params.pbrRoughnessFactor,
                    params.pbrNormalScale, params.pbrOcclusionStrength
                };
                const float fogColor[4] = {
                    params.fogColor[0], params.fogColor[1], params.fogColor[2],
                    params.pbrEncodeOutputToSrgb ? 1.0f : 0.0f
                };
                const float vertexColorFlags[4] = {
                    useVertexColor ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f
                };
                const float specularFresnel[4] = {
                    params.pbrDielectricF0Unclamped[0],
                    params.pbrDielectricF0Unclamped[1],
                    params.pbrDielectricF0Unclamped[2], params.pbrSpecularFactor
                };
                const float specularMapFlags[4] = {
                    params.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f,
                    0.0f, 0.0f, 0.0f
                };
                const float punctualPositionRange[4] = {
                    params.punctualPosition[0], params.punctualPosition[1],
                    params.punctualPosition[2], params.punctualRange
                };
                const float punctualDirectionKind[4] = {
                    params.punctualDirection[0], params.punctualDirection[1],
                    params.punctualDirection[2], static_cast<float>(params.punctualKind)
                };
                const float punctualDiffuse[4] = {
                    params.punctualDiffuse[0], params.punctualDiffuse[1],
                    params.punctualDiffuse[2], 0.0f
                };
                const float punctualCone[4] = {
                    params.punctualCosInner, params.punctualCosOuter, 0.0f, 0.0f
                };
                const float iblParams[4] = {
                    useIbl ? 1.0f : 0.0f,
                    static_cast<float>(params.iblPrefilteredMipCount > 0
                                           ? params.iblPrefilteredMipCount : 1),
                    params.iblIntensity, 0.0f
                };
                float doubleSidedOrientation = 0.0f;
                if (params.pbrDoubleSided)
                {
                    const float determinant =
                        world.M11 * (world.M22 * world.M33 - world.M23 * world.M32) -
                        world.M12 * (world.M21 * world.M33 - world.M23 * world.M31) +
                        world.M13 * (world.M21 * world.M32 - world.M22 * world.M31);
                    doubleSidedOrientation = determinant < 0.0f ? -1.0f : 1.0f;
                }
                const float doubleSidedParams[4] = {
                    doubleSidedOrientation, 0.0f, 0.0f, 0.0f
                };
                const auto textureCoordinateSet = [&](int slot) {
                    return (params.pbrTextureCoordinateSetMask &
                            (std::uint32_t{1} << slot)) != 0
                        ? 1.0f : 0.0f;
                };
                const float textureCoordinateSets[4] = {
                    textureCoordinateSet(0), textureCoordinateSet(1),
                    textureCoordinateSet(2), textureCoordinateSet(3)
                };
                const float textureCoordinateSets2[4] = {
                    textureCoordinateSet(4), textureCoordinateSet(5),
                    textureCoordinateSet(6), 0.0f
                };

                effect.SetFloats("AmbientColor", ambient, 4);
                effect.SetFloats("EmissiveColor", emissive, 4);
                effect.SetFloats("MetallicRoughnessFactor", metallicRoughness, 4);
                effect.SetFloats("Light0Dir", params.light0Dir, 3);
                effect.SetFloats("Light0Diffuse", params.light0Diffuse, 3);
                effect.SetFloats("Light1Dir", params.light1Dir, 3);
                effect.SetFloats("Light1Diffuse", params.light1Diffuse, 3);
                effect.SetFloats("Light2Dir", params.light2Dir, 3);
                effect.SetFloats("Light2Diffuse", params.light2Diffuse, 3);
                effect.SetFloats("EyePosition", params.eyePositionWorld, 3);
                effect.SetFloats("AlphaTest", params.alphaTest, 4);
                effect.SetFloats("FogColor", fogColor, 4);
                effect.SetFloats("VertexColorFlags", vertexColorFlags, 4);
                effect.SetFloats("TextureTransformRows",
                                 &params.pbrTextureTransformRows[0][0], 40);
                effect.SetFloats("SpecularFresnelInputs", specularFresnel, 4);
                effect.SetFloats("SpecularMapFlags", specularMapFlags, 4);
                effect.SetFloats("SpecularTextureTransformRows",
                                 &params.pbrSpecularTextureTransformRows[0][0], 16);
                effect.SetFloats("PunctualPositionRange", punctualPositionRange, 4);
                effect.SetFloats("PunctualDirectionKind", punctualDirectionKind, 4);
                effect.SetFloats("PunctualDiffuse", punctualDiffuse, 4);
                effect.SetFloats("PunctualCone", punctualCone, 4);
                effect.SetFloats("IblParams", iblParams, 4);
                effect.SetFloats("PbrDoubleSidedParams", doubleSidedParams, 4);
                effect.SetFloats("PbrTextureCoordinateSets", textureCoordinateSets, 4);
                effect.SetFloats("PbrTextureCoordinateSets2", textureCoordinateSets2, 4);

                const bool useDualUv = params.pbrTextureCoordinateSetMask != 0;
                effect.Apply(device_, (useDualUv ? 2 : 0) + (useVertexColor ? 1 : 0));
                const auto bindPbrTexture = [&](const char* samplerName,
                                                const ITextureRenderer* texture) {
                    const int slot = effect.FindPixelSamplerRegister(samplerName);
                    if (slot < 0)
                    {
                        throw std::runtime_error(
                            std::string("FNA3D PBR effect: applied pass has no pixel sampler '") +
                            samplerName + "'.");
                    }
                    BindEffectTextureEXT(slot, texture);
                };
                ITextureRenderer* white = GetOrCreatePbrWhiteTextureEXT();
                // Create both lazy resources before binding either one. FNA3D's OpenGL texture
                // upload temporarily touches the active texture unit; creating the normal fallback
                // between the s0 and s1 binds would replace the first draw's base-colour binding.
                ITextureRenderer* flatNormal = GetOrCreatePbrFlatNormalTextureEXT();
                bindPbrTexture("Texture", params.texture0 != nullptr ? params.texture0 : white);
                bindPbrTexture(
                    "NormalMap", params.pbrNormalMap != nullptr
                        ? params.pbrNormalMap : flatNormal);
                bindPbrTexture(
                    "MetallicRoughnessMap", params.pbrMetallicRoughnessMap != nullptr
                        ? params.pbrMetallicRoughnessMap : white);
                bindPbrTexture(
                    "EmissiveMap", params.pbrEmissiveMap != nullptr
                        ? params.pbrEmissiveMap : white);
                bindPbrTexture(
                    "OcclusionMap", params.pbrOcclusionMap != nullptr
                        ? params.pbrOcclusionMap : white);
                bindPbrTexture(
                    "SpecularMap", params.pbrSpecularMap != nullptr
                        ? params.pbrSpecularMap : white);
                bindPbrTexture(
                    "SpecularColorMap", params.pbrSpecularColorMap != nullptr
                        ? params.pbrSpecularColorMap : white);

                // The two cube interfaces are siblings of ITextureRenderer, so bind their shared
                // native sampled-texture mix-in directly. Resolve registers from the applied pass,
                // exactly as for the seven material maps: MojoShader is free to remap them per
                // profile even though the authored SM3 registers are public slots 10/11/12.
                const auto bindPbrSampled = [&](const char* samplerName,
                                                const Fna3dSampledTexture* sampled) {
                    const int slot = effect.FindPixelSamplerRegister(samplerName);
                    if (slot < 0)
                    {
                        throw std::runtime_error(
                            std::string("FNA3D PBR effect: applied pass has no pixel sampler '") +
                            samplerName + "'.");
                    }
                    if (sampled != nullptr && sampled->GetFna3dDeviceStateEXT() != deviceState_)
                    {
                        throw std::runtime_error(
                            "FNA3D renderer: an IBL texture was not created by this graphics device.");
                    }
                    FNA3D_Texture* native = sampled != nullptr
                        ? sampled->GetFna3dTextureEXT() : nullptr;
                    FNA3D_VerifySampler(device_, slot, native,
                                        &samplerStates_[static_cast<std::size_t>(slot)]);
                    boundPixelTextures_[static_cast<std::size_t>(slot)] = native;
                };
                const auto* irradiance = useIbl
                    ? dynamic_cast<const Fna3dSampledTexture*>(params.iblIrradiance) : nullptr;
                const auto* specular = useIbl
                    ? dynamic_cast<const Fna3dSampledTexture*>(params.iblPrefilteredSpecular)
                    : nullptr;
                if (useIbl && (irradiance == nullptr || specular == nullptr))
                {
                    throw std::runtime_error(
                        "FNA3D renderer: an IBL cube texture was not created by FNA3D.");
                }
                bindPbrSampled("IblIrradiance", irradiance);
                bindPbrSampled("IblSpecular", specular);
                bindPbrTexture("IblBrdfLut", useIbl ? params.iblBrdfLut : nullptr);
                break;
            }

            case StockEffectKind::Sprite:
            case StockEffectKind::Count:
            default:
                throw std::runtime_error(
                    "FNA3D renderer: the sprite effect is not a 3D draw path.");
        }
    }

    void Fna3dRenderer::ApplyLightingParamsEXT(Fna3dStockEffect& effect, const Matrix& world,
                                               const Matrix& view, const GpuDrawParams& params,
                                               bool withSpecular)
    {
        // FNA merges AmbientLightColor into the emissive parameter so the shader needs one fewer
        // add: EmissiveColor = (emissive + ambient * diffuse) * alpha. The shared layer hands over
        // emissive*alpha and diffuse*alpha separately, so the merge is reconstructed here exactly.
        const float emissive[3] = {
            params.emissiveColor[0] + params.ambientColor[0] * params.diffuseColor[0],
            params.emissiveColor[1] + params.ambientColor[1] * params.diffuseColor[1],
            params.emissiveColor[2] + params.ambientColor[2] * params.diffuseColor[2],
        };
        effect.SetFloats("EmissiveColor", emissive, 3);

        effect.SetFloats("DirLight0Direction", params.light0Dir, 3);
        effect.SetFloats("DirLight0DiffuseColor", params.light0Diffuse, 3);
        effect.SetFloats("DirLight1Direction", params.light1Dir, 3);
        effect.SetFloats("DirLight1DiffuseColor", params.light1Diffuse, 3);
        effect.SetFloats("DirLight2Direction", params.light2Dir, 3);
        effect.SetFloats("DirLight2DiffuseColor", params.light2Diffuse, 3);

        if (withSpecular)
        {
            effect.SetFloats("DirLight0SpecularColor", params.light0Specular, 3);
            effect.SetFloats("DirLight1SpecularColor", params.light1Specular, 3);
            effect.SetFloats("DirLight2SpecularColor", params.light2Specular, 3);
            effect.SetFloats("SpecularColor", params.specularColor, 3);
            const float specularPower[4] = { params.specularPower, params.specularPower,
                                             params.specularPower, params.specularPower };
            effect.SetFloats("SpecularPower", specularPower, 4);
        }

        effect.SetFloats("EyePosition", params.eyePositionWorld, 3);
        effect.SetMatrix("World", world);
        effect.SetMatrix("WorldInverseTranspose",
                         Matrix::Transpose(Matrix::Invert(world)));
        (void)view;
    }
}
