// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::PrimitiveType;
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        [[nodiscard]] int PrimitiveElementCount(
            const PrimitiveType primitive, const int primitiveCount)
        {
            if (primitiveCount <= 0)
                throw std::out_of_range("RLGL: primitiveCount must be positive");
            std::int64_t count = 0;
            switch (primitive)
            {
            case PrimitiveType::TriangleList:
                count = static_cast<std::int64_t>(primitiveCount) * 3; break;
            case PrimitiveType::TriangleStrip:
                count = static_cast<std::int64_t>(primitiveCount) + 2; break;
            case PrimitiveType::LineList:
                count = static_cast<std::int64_t>(primitiveCount) * 2; break;
            case PrimitiveType::LineStrip:
                count = static_cast<std::int64_t>(primitiveCount) + 1; break;
            case PrimitiveType::PointListEXT:
                count = primitiveCount; break;
            default:
                throw std::invalid_argument("RLGL: invalid PrimitiveType ordinal");
            }
            if (count > std::numeric_limits<int>::max())
                throw std::overflow_error("RLGL: primitive element count exceeds Int32");
            return static_cast<int>(count);
        }

        [[nodiscard]] const VertexElement* FindElement(
            const std::vector<VertexElement>& declaration,
            const VertexElementUsage usage, const int usageIndex)
        {
            const auto found = std::find_if(
                declaration.begin(), declaration.end(),
                [usage, usageIndex](const VertexElement& element)
                {
                    return element.getVertexElementUsageProperty() == usage &&
                        element.getUsageIndexProperty() == usageIndex;
                });
            return found == declaration.end() ? nullptr : &*found;
        }

        [[nodiscard]] std::vector<VertexAttributeBinding> BuildStockAttributes(
            const IVertexBufferRenderer& vertexBuffer,
            const bool vertexColorEnabled, const bool textureEnabled,
            const bool lightingEnabled, const bool dualTexture, const bool skinned)
        {
            const std::size_t nativeStride = GetVertexStride(vertexBuffer);
            if (nativeStride == 0 || nativeStride >
                static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::runtime_error("RLGL: vertex buffer has no usable uploaded stride");
            const int stride = static_cast<int>(nativeStride);
            const std::vector<VertexElement>& declaration =
                GetVertexDeclaration(vertexBuffer);

            VertexElement inferredPosition(
                0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
            VertexElement inferredColor(
                skinned ? 52 : 12,
                VertexElementFormat::Color, VertexElementUsage::Color, 0);
            VertexElement inferredTextureCoordinate(
                skinned ? 24 : (vertexColorEnabled ? 16 : 12),
                VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0);
            VertexElement inferredNormal(
                12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
            VertexElement inferredBlendWeight(
                32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0);
            VertexElement inferredByteBlendIndices(
                48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0);
            VertexElement inferredVectorBlendIndices(
                48, VertexElementFormat::Vector4, VertexElementUsage::BlendIndices, 0);
            const VertexElement* position = FindElement(
                declaration, VertexElementUsage::Position, 0);
            const VertexElement* color = FindElement(
                declaration, VertexElementUsage::Color, 0);
            const VertexElement* textureCoordinate = FindElement(
                declaration, VertexElementUsage::TextureCoordinate, 0);
            const VertexElement* textureCoordinate1 = FindElement(
                declaration, VertexElementUsage::TextureCoordinate, 1);
            const VertexElement* normal = FindElement(
                declaration, VertexElementUsage::Normal, 0);
            const VertexElement* blendWeight = FindElement(
                declaration, VertexElementUsage::BlendWeight, 0);
            const VertexElement* blendIndices = FindElement(
                declaration, VertexElementUsage::BlendIndices, 0);
            if (declaration.empty() && stride >= 12) position = &inferredPosition;
            if (declaration.empty() && skinned &&
                (stride == 52 || stride == 56 || stride == 64))
            {
                textureCoordinate = &inferredTextureCoordinate;
                normal = &inferredNormal;
                blendWeight = &inferredBlendWeight;
                blendIndices = stride == 64
                    ? &inferredVectorBlendIndices : &inferredByteBlendIndices;
                if (stride == 56) color = &inferredColor;
            }
            else if (declaration.empty())
            {
                if (stride >= 16) color = &inferredColor;
                if (stride >= inferredTextureCoordinate.getOffsetProperty() + 8)
                    textureCoordinate = &inferredTextureCoordinate;
            }
            if (position == nullptr ||
                position->getVertexElementFormatProperty() != VertexElementFormat::Vector3)
            {
                throw System::NotSupportedException(
                    "RLGL: the baseline primitive shader requires Position0 as Vector3 "
                    "(plans/plan_rlgl.md RLGL-012)");
            }
            if (vertexColorEnabled &&
                (color == nullptr ||
                 color->getVertexElementFormatProperty() != VertexElementFormat::Color))
            {
                throw System::NotSupportedException(
                    "RLGL: vertex-color drawing requires Color0 as packed Color "
                    "(plans/plan_rlgl.md RLGL-012)");
            }
            if (textureEnabled &&
                (textureCoordinate == nullptr ||
                 textureCoordinate->getVertexElementFormatProperty() !=
                     VertexElementFormat::Vector2))
            {
                throw System::NotSupportedException(
                    "RLGL: texture drawing requires TextureCoordinate0 as Vector2 "
                    "(plans/plan_rlgl.md RLGL-033)");
            }
            if (lightingEnabled &&
                (normal == nullptr ||
                 normal->getVertexElementFormatProperty() != VertexElementFormat::Vector3))
            {
                throw System::NotSupportedException(
                    "RLGL: BasicEffect lighting requires Normal0 as Vector3 "
                    "(plans/plan_rlgl.md RLGL-034)");
            }
            if (dualTexture &&
                (textureCoordinate1 == nullptr ||
                 textureCoordinate1->getVertexElementFormatProperty() !=
                     VertexElementFormat::Vector2))
            {
                throw System::NotSupportedException(
                    "RLGL: DualTextureEffect requires TextureCoordinate1 as Vector2 "
                    "(plans/plan_rlgl.md RLGL-035)");
            }
            if (skinned &&
                (blendWeight == nullptr ||
                 blendWeight->getVertexElementFormatProperty() !=
                     VertexElementFormat::Vector4))
            {
                throw System::NotSupportedException(
                    "RLGL: SkinnedEffect requires BlendWeight0 as Vector4 "
                    "(plans/plan_rlgl.md RLGL-037)");
            }
            if (skinned &&
                (blendIndices == nullptr ||
                 (blendIndices->getVertexElementFormatProperty() !=
                      VertexElementFormat::Byte4 &&
                  blendIndices->getVertexElementFormatProperty() !=
                      VertexElementFormat::Vector4)))
            {
                throw System::NotSupportedException(
                    "RLGL: SkinnedEffect requires BlendIndices0 as Byte4 or Vector4 "
                    "(plans/plan_rlgl.md RLGL-037)");
            }

            std::vector<VertexAttributeBinding> result;
            result.reserve(
                1u + (vertexColorEnabled ? 1u : 0u) + (textureEnabled ? 1u : 0u) +
                (lightingEnabled ? 1u : 0u) + (dualTexture ? 1u : 0u) +
                (skinned ? 2u : 0u));
            result.push_back(DescribeVertexAttribute(*position, 0, stride));
            if (vertexColorEnabled)
                result.push_back(DescribeVertexAttribute(*color, 1, stride));
            if (textureEnabled)
                result.push_back(DescribeVertexAttribute(*textureCoordinate, 2, stride));
            if (lightingEnabled)
                result.push_back(DescribeVertexAttribute(*normal, 3, stride));
            if (dualTexture)
                result.push_back(DescribeVertexAttribute(*textureCoordinate1, 4, stride));
            if (skinned)
            {
                result.push_back(DescribeVertexAttribute(*blendWeight, 5, stride));
                result.push_back(DescribeVertexAttribute(*blendIndices, 6, stride));
            }
            return result;
        }

        void RequireBaselineEffect(const GpuDrawParams& params)
        {
            bool hasInstanceStream = false;
            for (int stream = 0; stream < params.vertexStreamCount; ++stream)
                hasInstanceStream = hasInstanceStream ||
                    params.vertexStreams[stream].instanceFrequency != 0;
            if (params.customEffectRequested || params.customEffectRenderer != nullptr ||
                params.compiledEffectRuntime != nullptr || params.envMapping ||
                params.pbr ||
                params.instanceCount != 1 || params.vertexStreamCount > 1 || hasInstanceStream)
            {
                throw System::NotSupportedException(
                    "RLGL: this effect or vertex-stream shape requires the stock/custom shader "
                    "campaign (plans/plan_rlgl.md RLGL-012)");
            }
        }

        void Submit(
            const Bridge::PrimitivePipeline& pipeline,
            const IVertexBufferRenderer& vertexBuffer,
            const IIndexBufferRenderer* const indexBuffer,
            const Matrix& world, const Matrix& view, const Matrix& projection,
            const PrimitiveType primitive, const int primitiveCount,
            const int firstVertex, const int startIndex, const int baseVertex,
            const GpuDrawParams& params,
            const bool applyXnaPixelCenter,
            const int viewportWidth, const int viewportHeight,
            const int multiSampleCount)
        {
            const int elementCount = PrimitiveElementCount(primitive, primitiveCount);
            if (firstVertex < 0 || startIndex < 0 || baseVertex < 0)
                throw std::out_of_range("RLGL: primitive offsets must be non-negative");
            if (indexBuffer == nullptr)
            {
                const int capacity = GetBufferCapacity(vertexBuffer);
                if (firstVertex > capacity || elementCount > capacity - firstVertex)
                    throw std::out_of_range("RLGL: primitive draw exceeds vertex capacity");
            }
            else
            {
                const int capacity = GetBufferCapacity(*indexBuffer);
                if (startIndex > capacity || elementCount > capacity - startIndex)
                    throw std::out_of_range("RLGL: indexed draw exceeds index capacity");
            }

            const std::vector<VertexAttributeBinding> attributes = BuildStockAttributes(
                vertexBuffer, params.vertexColorEnabled, params.textureEnabled,
                params.lightingEnabled, params.dualTexture, params.skinned);
            Matrix worldViewProjection = world * view * projection;
            if (applyXnaPixelCenter && viewportWidth > 0 && viewportHeight > 0 &&
                multiSampleCount <= 1)
            {
                constexpr float xnaPixelCenterScale = 63.0f / 64.0f;
                worldViewProjection *= Matrix::CreateTranslation(
                    xnaPixelCenterScale / static_cast<float>(viewportWidth),
                    -xnaPixelCenterScale / static_cast<float>(viewportHeight), 0.0f);
            }
            float columnMajor[16]{};
            worldViewProjection.ToColumnMajor(columnMajor);
            const unsigned int texture = params.textureEnabled && params.texture0 != nullptr
                ? GetNativeTextureId(*params.texture0) : 0u;
            const unsigned int texture1 = params.dualTexture && params.texture1 != nullptr
                ? GetNativeTextureId(*params.texture1) : 0u;
            Bridge::DrawPrimitiveGeometry(
                pipeline, GetNativeBufferId(vertexBuffer),
                indexBuffer == nullptr ? 0u : GetNativeBufferId(*indexBuffer),
                attributes.data(), static_cast<int>(attributes.size()),
                columnMajor, texture, texture1, params,
                static_cast<int>(primitive), elementCount,
                firstVertex, startIndex, baseVertex,
                indexBuffer != nullptr && indexBuffer->IsThirtyTwoBit());
        }
    }

    Bridge::PrimitivePipeline& RlglRenderer::GetPrimitivePipeline()
    {
        if (!primitivePipeline_)
        {
            primitivePipeline_ = std::make_unique<Bridge::PrimitivePipeline>(
                Bridge::CreatePrimitivePipeline());
        }
        return *primitivePipeline_;
    }

    void RlglRenderer::DrawColoredPrimitives(
        const IVertexBufferRenderer& vertexBuffer,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        const PrimitiveType primitive, const int primitiveCount)
    {
        GpuDrawParams params;
        params.vertexColorEnabled = true;
        Submit(
            GetPrimitivePipeline(), vertexBuffer, nullptr,
            world, view, projection, primitive, primitiveCount,
            0, 0, 0, params, false,
            currentViewportWidth_, currentViewportHeight_,
            GetCurrentSampleCount());
    }

    void RlglRenderer::DrawIndexedColoredPrimitives(
        const IVertexBufferRenderer& vertexBuffer,
        const IIndexBufferRenderer& indexBuffer,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        const PrimitiveType primitive, const int primitiveCount)
    {
        GpuDrawParams params;
        params.vertexColorEnabled = true;
        Submit(
            GetPrimitivePipeline(), vertexBuffer, &indexBuffer,
            world, view, projection, primitive, primitiveCount,
            0, 0, 0, params, false,
            currentViewportWidth_, currentViewportHeight_,
            GetCurrentSampleCount());
    }

    void RlglRenderer::DrawPrimitivesEx(
        const IVertexBufferRenderer& vertexBuffer,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        const PrimitiveType primitive, const int primitiveCount,
        const GpuDrawParams& params)
    {
        RequireBaselineEffect(params);
        Submit(
            GetPrimitivePipeline(), vertexBuffer, nullptr,
            world, view, projection, primitive, primitiveCount,
            params.vertexStart, 0, 0, params, true,
            currentViewportWidth_, currentViewportHeight_,
            GetCurrentSampleCount());
    }

    void RlglRenderer::DrawIndexedPrimitivesEx(
        const IVertexBufferRenderer& vertexBuffer,
        const IIndexBufferRenderer& indexBuffer,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        const PrimitiveType primitive, const int primitiveCount,
        const GpuDrawParams& params)
    {
        RequireBaselineEffect(params);
        Submit(
            GetPrimitivePipeline(), vertexBuffer, &indexBuffer,
            world, view, projection, primitive, primitiveCount,
            0, params.startIndex, params.baseVertex, params, true,
            currentViewportWidth_, currentViewportHeight_,
            GetCurrentSampleCount());
    }
}
