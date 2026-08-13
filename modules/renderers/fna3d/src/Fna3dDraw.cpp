// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Fna3d/Fna3dEnumMapping.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dVertexLayouts.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

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
            if (params.skinned)      return StockEffectKind::Skinned;
            if (params.envMapping)   return StockEffectKind::EnvironmentMap;
            if (params.dualTexture)  return StockEffectKind::DualTexture;
            if (AlphaTestActive(params)) return StockEffectKind::AlphaTest;
            return StockEffectKind::Basic;
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
        const auto& indexBuffer = static_cast<const Fna3dIndexBufferRenderer&>(ib);
        PrepareDrawEXT(vb, world, view, projection, params, params.baseVertex);
        FNA3D_DrawIndexedPrimitives(
            device_, ToFna3dPrimitiveType(primitive), params.baseVertex, params.minVertexIndex,
            params.numVertices > 0 ? params.numVertices : vb.GetVertexCount(), params.startIndex,
            primitiveCount, indexBuffer.GetFna3dBufferEXT(), indexBuffer.GetElementSizeEXT());
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
        if (params.compiledEffectRuntime == nullptr)
        {
            ApplyStockEffectEXT(world, view, projection, params);
        }
        ApplyVertexBindingsEXT(vb, params, baseVertex);
    }

    void Fna3dRenderer::ApplyVertexBindingsEXT(const IVertexBufferRenderer& vb,
                                               const GpuDrawParams& params, int baseVertex)
    {
        const auto& primaryBuffer = static_cast<const Fna3dVertexBufferRenderer&>(vb);

        std::vector<FNA3D_VertexBufferBinding> bindings;
        // Element storage must outlive the FNA3D_ApplyVertexBufferBindings call: the declarations
        // inside the bindings point into it.
        std::vector<std::vector<FNA3D_VertexElement>> elementStorage;

        const auto describe = [&](const Fna3dVertexBufferRenderer& buffer, int declaredStride,
                                  FNA3D_VertexDeclaration& declaration) {
            if (buffer.HasDeclarationEXT())
            {
                elementStorage.push_back(buffer.GetDeclarationElementsEXT());
                declaration.vertexStride = buffer.GetDeclarationStrideEXT();
                declaration.elementCount =
                    static_cast<int32_t>(elementStorage.back().size());
                declaration.elements = elementStorage.back().data();
                return;
            }

            // The internal routes (SpriteBatch, DrawUser*, DrawColoredPrimitives) and
            // REMED-GFX-233's legacy empty-declaration buffer bind no public VertexDeclaration at
            // all, so the canonical stride-keyed layout stands in -- the same table every other
            // CNA renderer dispatches on.
            const int stride = declaredStride > 0 ? declaredStride : buffer.GetStrideEXT();
            elementStorage.push_back(ElementsForStride(stride));
            declaration.vertexStride = stride;
            declaration.elementCount = static_cast<int32_t>(elementStorage.back().size());
            declaration.elements = elementStorage.back().data();
        };

        if (params.vertexStreamCount > 0)
        {
            bindings.resize(static_cast<std::size_t>(params.vertexStreamCount));
            elementStorage.reserve(static_cast<std::size_t>(params.vertexStreamCount));
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const GpuVertexStreamBinding& stream = params.vertexStreams[i];
                const auto* streamBuffer =
                    static_cast<const Fna3dVertexBufferRenderer*>(stream.buffer);
                if (streamBuffer == nullptr)
                {
                    throw std::runtime_error(
                        "FNA3D renderer: a declared vertex stream carried no buffer; the binding "
                        "list is never silently truncated.");
                }
                FNA3D_VertexBufferBinding& binding = bindings[static_cast<std::size_t>(i)];
                std::memset(&binding, 0, sizeof(binding));
                binding.vertexBuffer = streamBuffer->GetFna3dBufferEXT();
                binding.vertexOffset = stream.vertexOffset;
                binding.instanceFrequency = stream.instanceFrequency;
                describe(*streamBuffer, stream.strideInBytes, binding.vertexDeclaration);
            }
        }
        else
        {
            bindings.resize(1);
            elementStorage.reserve(1);
            FNA3D_VertexBufferBinding& binding = bindings[0];
            std::memset(&binding, 0, sizeof(binding));
            binding.vertexBuffer = primaryBuffer.GetFna3dBufferEXT();
            binding.vertexOffset = 0;
            binding.instanceFrequency = 0;
            describe(primaryBuffer, 0, binding.vertexDeclaration);
        }

        FNA3D_ApplyVertexBufferBindings(device_, bindings.data(),
                                        static_cast<int32_t>(bindings.size()), 1, baseVertex);
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
        // Fna3dTextureRenderer, so the sampled-texture mix-in is what both have in common.
        const auto* sampled = dynamic_cast<const Fna3dSampledTexture*>(texture);
        FNA3D_Texture* native = sampled != nullptr ? sampled->GetFna3dTextureEXT() : nullptr;
        FNA3D_VerifySampler(device_, slot, native,
                            &samplerStates_[static_cast<std::size_t>(slot)]);
        boundPixelTextures_[static_cast<std::size_t>(slot)] = native;
    }

    void Fna3dRenderer::ApplyStockEffectEXT(const Matrix& world, const Matrix& view,
                                            const Matrix& projection, const GpuDrawParams& params)
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
                if (const auto* cube =
                        dynamic_cast<const Fna3dSampledTexture*>(params.envMap))
                {
                    FNA3D_VerifySampler(device_, 1, cube->GetFna3dTextureEXT(),
                                        &samplerStates_[1]);
                    boundPixelTextures_[1] = cube->GetFna3dTextureEXT();
                }
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
