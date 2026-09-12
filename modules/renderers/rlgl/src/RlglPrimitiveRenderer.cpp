// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
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

        void ValidateDrawRange(
            const IVertexBufferRenderer& vertexBuffer,
            const IIndexBufferRenderer* const indexBuffer,
            const int elementCount, const int firstVertex,
            const int startIndex, const int baseVertex)
        {
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

        [[nodiscard]] std::vector<VertexAttributeBinding> BuildSingleStreamStockAttributes(
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
                (skinned || lightingEnabled) ? 24 : (vertexColorEnabled ? 16 : 12),
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
                if (lightingEnabled && stride == 32)
                    normal = &inferredNormal;
                else if (stride >= 16)
                    color = &inferredColor;
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
                    "RLGL: lit stock effects require Normal0 as Vector3 "
                    "(plans/plan_rlgl.md RLGL-034/RLGL-036)");
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

        struct LocatedVertexElement
        {
            const VertexElement* element = nullptr;
            int streamIndex = -1;
            int combinedByteOffset = 0;
        };

        void ValidateStreamShape(
            const IVertexBufferRenderer& primaryVertexBuffer,
            const GpuDrawParams& params, const bool allowInstanceStreams)
        {
            if (params.vertexStreamCount < 2 ||
                params.vertexStreamCount > static_cast<int>(params.vertexStreams.size()))
            {
                throw std::invalid_argument("RLGL: invalid multi-stream binding count");
            }
            if (params.vertexStreams[0].buffer != &primaryVertexBuffer ||
                params.vertexStreams[0].instanceFrequency != 0)
                throw std::invalid_argument("RLGL: stream zero does not match the primary buffer");

            int expectedCombinedByteBase = 0;
            std::array<bool, kMaxVertexStreams> occupiedSlots{};
            std::vector<std::pair<VertexElementUsage, int>> declaredSemantics;
            for (int streamIndex = 0; streamIndex < params.vertexStreamCount; ++streamIndex)
            {
                const GpuVertexStreamBinding& stream = params.vertexStreams[streamIndex];
                if (stream.buffer == nullptr || stream.instanceFrequency < 0 ||
                    (!allowInstanceStreams && stream.instanceFrequency != 0) ||
                    stream.slot < 0 || stream.slot >= kMaxVertexStreams ||
                    occupiedSlots[static_cast<std::size_t>(stream.slot)] ||
                    stream.strideInBytes <= 0 || stream.vertexOffset < 0 ||
                    stream.vertexCount < 0 ||
                    stream.vertexCount > GetBufferCapacity(*stream.buffer) ||
                    GetNativeBufferId(*stream.buffer) == 0)
                {
                    throw std::invalid_argument("RLGL: invalid vertex stream binding");
                }
                occupiedSlots[static_cast<std::size_t>(stream.slot)] = true;

                const std::size_t nativeStride = GetVertexStride(*stream.buffer);
                const auto& declaration = GetVertexDeclaration(*stream.buffer);
                if (declaration.empty() ||
                    nativeStride != static_cast<std::size_t>(stream.strideInBytes))
                {
                    throw System::NotSupportedException(
                        "RLGL: every multi-stream binding requires its own non-empty, matching "
                        "VertexDeclaration (plans/plan_rlgl.md RLGL-016/RLGL-032)");
                }

                for (const VertexElement& element : declaration)
                {
                    const auto semantic = std::pair{
                        element.getVertexElementUsageProperty(),
                        element.getUsageIndexProperty()};
                    if (std::find(
                            declaredSemantics.begin(), declaredSemantics.end(), semantic) !=
                        declaredSemantics.end())
                    {
                        throw System::NotSupportedException(
                            "RLGL: a multi-stream declaration set cannot bind the same "
                            "vertex semantic more than once "
                            "(plans/plan_rlgl.md RLGL-016/RLGL-032)");
                    }
                    declaredSemantics.push_back(semantic);
                }

                if (stream.instanceFrequency != 0)
                {
                    if (stream.combinedByteBase != 0)
                        throw std::invalid_argument(
                            "RLGL: an instance stream cannot contribute to vertex stride");
                    continue;
                }
                if (stream.combinedByteBase != expectedCombinedByteBase)
                    throw std::invalid_argument("RLGL: invalid combined vertex stream offset");
                if (stream.strideInBytes >
                    std::numeric_limits<int>::max() - expectedCombinedByteBase)
                    throw std::overflow_error("RLGL: combined vertex stride exceeds Int32");
                expectedCombinedByteBase += stream.strideInBytes;
            }
            if (params.combinedVertexStride != expectedCombinedByteBase)
                throw std::invalid_argument("RLGL: invalid combined multi-stream stride");
        }

        [[nodiscard]] LocatedVertexElement FindMultiStreamElement(
            const GpuDrawParams& params,
            const VertexElementUsage usage, const int usageIndex)
        {
            for (int streamIndex = 0; streamIndex < params.vertexStreamCount; ++streamIndex)
            {
                const GpuVertexStreamBinding& stream = params.vertexStreams[streamIndex];
                if (stream.instanceFrequency != 0)
                    continue;
                const VertexElement* const element = FindElement(
                    GetVertexDeclaration(*stream.buffer), usage, usageIndex);
                if (element == nullptr)
                    continue;
                return LocatedVertexElement{
                    element,
                    streamIndex,
                    stream.combinedByteBase + element->getOffsetProperty()};
            }
            return {};
        }

        [[nodiscard]] std::vector<VertexAttributeBinding> BuildStockAttributes(
            const IVertexBufferRenderer& vertexBuffer, const GpuDrawParams& params)
        {
            if (params.vertexStreamCount < 2)
            {
                std::vector<VertexAttributeBinding> result =
                    BuildSingleStreamStockAttributes(
                        vertexBuffer, params.vertexColorEnabled, params.textureEnabled,
                        params.lightingEnabled, params.dualTexture, params.skinned);
                if (params.vertexStreamCount == 1)
                {
                    const GpuVertexStreamBinding& stream = params.vertexStreams[0];
                    if (stream.buffer != &vertexBuffer || stream.instanceFrequency != 0 ||
                        stream.vertexOffset != 0)
                    {
                        throw std::invalid_argument("RLGL: invalid single-stream binding");
                    }
                    const unsigned int nativeBuffer = GetNativeBufferId(*stream.buffer);
                    for (VertexAttributeBinding& attribute : result)
                        attribute.vertexBuffer = nativeBuffer;
                }
                return result;
            }

            const bool instanced = FirstInstanceStream(params) != nullptr;
            ValidateStreamShape(vertexBuffer, params, instanced);
            const LocatedVertexElement position = FindMultiStreamElement(
                params, VertexElementUsage::Position, 0);
            const LocatedVertexElement color = FindMultiStreamElement(
                params, VertexElementUsage::Color, 0);
            const LocatedVertexElement textureCoordinate = FindMultiStreamElement(
                params, VertexElementUsage::TextureCoordinate, 0);
            const LocatedVertexElement textureCoordinate1 = FindMultiStreamElement(
                params, VertexElementUsage::TextureCoordinate, 1);
            const LocatedVertexElement normal = FindMultiStreamElement(
                params, VertexElementUsage::Normal, 0);
            const LocatedVertexElement blendWeight = FindMultiStreamElement(
                params, VertexElementUsage::BlendWeight, 0);
            const LocatedVertexElement blendIndices = FindMultiStreamElement(
                params, VertexElementUsage::BlendIndices, 0);

            if (position.element == nullptr ||
                position.element->getVertexElementFormatProperty() !=
                    VertexElementFormat::Vector3)
            {
                throw System::NotSupportedException(
                    "RLGL: the baseline primitive shader requires Position0 as Vector3 "
                    "(plans/plan_rlgl.md RLGL-032)");
            }
            if (params.vertexColorEnabled &&
                (color.element == nullptr ||
                 color.element->getVertexElementFormatProperty() != VertexElementFormat::Color))
            {
                throw System::NotSupportedException(
                    "RLGL: vertex-color drawing requires Color0 as packed Color "
                    "(plans/plan_rlgl.md RLGL-032)");
            }
            if (params.textureEnabled &&
                (textureCoordinate.element == nullptr ||
                 textureCoordinate.element->getVertexElementFormatProperty() !=
                     VertexElementFormat::Vector2))
            {
                throw System::NotSupportedException(
                    "RLGL: texture drawing requires TextureCoordinate0 as Vector2 "
                    "(plans/plan_rlgl.md RLGL-032)");
            }
            if (params.lightingEnabled &&
                (normal.element == nullptr ||
                 normal.element->getVertexElementFormatProperty() !=
                     VertexElementFormat::Vector3))
            {
                throw System::NotSupportedException(
                    "RLGL: lit stock effects require Normal0 as Vector3 "
                    "(plans/plan_rlgl.md RLGL-032)");
            }
            if (params.dualTexture &&
                (textureCoordinate1.element == nullptr ||
                 textureCoordinate1.element->getVertexElementFormatProperty() !=
                     VertexElementFormat::Vector2))
            {
                throw System::NotSupportedException(
                    "RLGL: DualTextureEffect requires TextureCoordinate1 as Vector2 "
                    "(plans/plan_rlgl.md RLGL-032)");
            }
            if (params.skinned &&
                (blendWeight.element == nullptr ||
                 blendWeight.element->getVertexElementFormatProperty() !=
                     VertexElementFormat::Vector4))
            {
                throw System::NotSupportedException(
                    "RLGL: SkinnedEffect requires BlendWeight0 as Vector4 "
                    "(plans/plan_rlgl.md RLGL-032)");
            }
            if (params.skinned &&
                (blendIndices.element == nullptr ||
                 (blendIndices.element->getVertexElementFormatProperty() !=
                      VertexElementFormat::Byte4 &&
                  blendIndices.element->getVertexElementFormatProperty() !=
                      VertexElementFormat::Vector4)))
            {
                throw System::NotSupportedException(
                    "RLGL: SkinnedEffect requires BlendIndices0 as Byte4 or Vector4 "
                    "(plans/plan_rlgl.md RLGL-032)");
            }

            std::vector<VertexAttributeBinding> result;
            result.reserve(instanced ? 11u : 7u);
            const auto append = [&params, &result](
                const LocatedVertexElement& located, const unsigned int location)
            {
                const GpuVertexStreamSlot slot = MapCombinedOffsetToStream(
                    params, located.combinedByteOffset);
                if (slot.streamIndex != located.streamIndex ||
                    slot.byteOffsetInStream != located.element->getOffsetProperty())
                {
                    throw std::invalid_argument("RLGL: inconsistent combined vertex layout");
                }
                const GpuVertexStreamBinding& stream = params.vertexStreams[slot.streamIndex];
                if (stream.vertexOffset >
                    std::numeric_limits<int>::max() / stream.strideInBytes)
                {
                    throw std::overflow_error("RLGL: vertex stream byte offset exceeds Int32");
                }
                VertexAttributeBinding binding = DescribeVertexAttribute(
                    *located.element, location, stream.strideInBytes,
                    stream.vertexOffset * stream.strideInBytes);
                binding.vertexBuffer = GetNativeBufferId(*stream.buffer);
                result.push_back(binding);
            };

            append(position, 0);
            if (params.vertexColorEnabled) append(color, 1);
            if (params.textureEnabled) append(textureCoordinate, 2);
            if (params.lightingEnabled) append(normal, 3);
            if (params.dualTexture) append(textureCoordinate1, 4);
            if (params.skinned)
            {
                append(blendWeight, 5);
                append(blendIndices, 6);
            }
            if (instanced)
            {
                unsigned int location = 12;
                for (int streamIndex = 0;
                     streamIndex < params.vertexStreamCount; ++streamIndex)
                {
                    const GpuVertexStreamBinding& stream =
                        params.vertexStreams[streamIndex];
                    if (stream.instanceFrequency == 0)
                        continue;
                    if (location >= 16)
                    {
                        throw System::NotSupportedException(
                            "RLGL: every per-instance stream must contribute to the four-column "
                            "instance transform (plans/plan_rlgl.md RLGL-016)");
                    }
                    if (stream.vertexOffset >
                        std::numeric_limits<int>::max() / stream.strideInBytes)
                    {
                        throw std::overflow_error(
                            "RLGL: instance stream byte offset exceeds Int32");
                    }
                    const int baseOffset = stream.vertexOffset * stream.strideInBytes;
                    const auto& declaration = GetVertexDeclaration(*stream.buffer);
                    for (const VertexElement& element : declaration)
                    {
                        if (location >= 16)
                            break;
                        if (element.getVertexElementFormatProperty() !=
                            VertexElementFormat::Vector4)
                        {
                            throw System::NotSupportedException(
                                "RLGL: the stock instanced path requires four positional "
                                "Vector4 matrix columns (plans/plan_rlgl.md RLGL-016)");
                        }
                        VertexAttributeBinding binding = DescribeVertexAttribute(
                            element, location++, stream.strideInBytes, baseOffset);
                        binding.vertexBuffer = GetNativeBufferId(*stream.buffer);
                        binding.divisor = stream.instanceFrequency;
                        result.push_back(binding);
                    }
                }
                if (location != 16)
                {
                    throw System::NotSupportedException(
                        "RLGL: the stock instanced path requires four positional Vector4 "
                        "matrix columns (plans/plan_rlgl.md RLGL-016)");
                }
            }
            return result;
        }

        void RequireBaselineEffect(
            const GpuDrawParams& params, const bool allowInstancing = false)
        {
            bool hasInstanceStream = false;
            for (int stream = 0; stream < params.vertexStreamCount; ++stream)
                hasInstanceStream = hasInstanceStream ||
                    params.vertexStreams[stream].instanceFrequency != 0;
            if (params.customEffectRequested || params.customEffectRenderer != nullptr ||
                params.compiledEffectRuntime != nullptr || params.pbr ||
                (!allowInstancing && (params.instanceCount != 1 || hasInstanceStream)))
            {
                throw System::NotSupportedException(
                    "RLGL: this effect or vertex-stream shape requires the stock/custom shader "
                    "campaign (plans/plan_rlgl.md RLGL-012)");
            }
            if (allowInstancing &&
                (params.instanceCount <= 0 || !hasInstanceStream || params.firstInstance != 0))
            {
                throw System::NotSupportedException(
                    "RLGL: classic instancing requires a per-instance stream and firstInstance "
                    "zero (plans/plan_rlgl.md RLGL-016)");
            }
            if (params.envMapping && params.envMap == nullptr)
            {
                throw std::invalid_argument(
                    "RLGL: EnvironmentMapEffect requires a non-null TextureCube");
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
            ValidateDrawRange(
                vertexBuffer, indexBuffer, elementCount,
                firstVertex, startIndex, baseVertex);

            const std::vector<VertexAttributeBinding> attributes =
                BuildStockAttributes(vertexBuffer, params);
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
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        if (params.compiledEffectRuntime != nullptr)
        {
            const int elementCount = PrimitiveElementCount(primitive, primitiveCount);
            ValidateDrawRange(
                vertexBuffer, nullptr, elementCount,
                params.vertexStart, 0, 0);
            DrawCompiledEffectGeometry(
                vertexBuffer, nullptr, primitive, elementCount,
                params.vertexStart, 0, 0, params);
            return;
        }
#endif
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
#if defined(CNA_RLGL_COMPILED_EFFECTS)
        if (params.compiledEffectRuntime != nullptr)
        {
            const int elementCount = PrimitiveElementCount(primitive, primitiveCount);
            ValidateDrawRange(
                vertexBuffer, &indexBuffer, elementCount,
                0, params.startIndex, params.baseVertex);
            DrawCompiledEffectGeometry(
                vertexBuffer, &indexBuffer, primitive, elementCount,
                0, params.startIndex, params.baseVertex, params);
            return;
        }
#endif
        RequireBaselineEffect(params);
        Submit(
            GetPrimitivePipeline(), vertexBuffer, &indexBuffer,
            world, view, projection, primitive, primitiveCount,
            0, params.startIndex, params.baseVertex, params, true,
            currentViewportWidth_, currentViewportHeight_,
            GetCurrentSampleCount());
    }

    void RlglRenderer::DrawInstancedPrimitivesEx(
        const IVertexBufferRenderer& vertexBuffer,
        const IIndexBufferRenderer& indexBuffer,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        const PrimitiveType primitive, const int primitiveCount,
        const int instanceCount, const GpuDrawParams& params)
    {
        if (instanceCount != params.instanceCount)
            throw std::invalid_argument("RLGL: inconsistent instance count");
        RequireBaselineEffect(params, true);
        Submit(
            GetPrimitivePipeline(), vertexBuffer, &indexBuffer,
            world, view, projection, primitive, primitiveCount,
            0, params.startIndex, params.baseVertex, params, true,
            currentViewportWidth_, currentViewportHeight_,
            GetCurrentSampleCount());
    }
}
