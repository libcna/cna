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

        class VertexAttributeList
        {
        public:
            VertexAttributeList()
            {
                Bridge::RecordAttributeScratchBuild();
            }

            void reserve(std::size_t) noexcept {}

            void push_back(const VertexAttributeBinding& value)
            {
                if (size_ >= values_.size())
                    throw System::NotSupportedException(
                        "RLGL: vertex input exceeds the 16-location GL 3.3 limit");
                values_[size_++] = value;
            }

            [[nodiscard]] VertexAttributeBinding* begin() noexcept { return values_.data(); }
            [[nodiscard]] VertexAttributeBinding* end() noexcept
            {
                return values_.data() + size_;
            }
            [[nodiscard]] const VertexAttributeBinding* data() const noexcept
            {
                return values_.data();
            }
            [[nodiscard]] std::size_t size() const noexcept { return size_; }
            [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

        private:
            std::array<VertexAttributeBinding, 16> values_{};
            std::size_t size_ = 0;
        };

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
            Bridge::RecordSemanticLookup();
            const auto found = std::find_if(
                declaration.begin(), declaration.end(),
                [usage, usageIndex](const VertexElement& element)
                {
                    return element.getVertexElementUsageProperty() == usage &&
                        element.getUsageIndexProperty() == usageIndex;
                });
            return found == declaration.end() ? nullptr : &*found;
        }

        [[nodiscard]] VertexAttributeList BuildSingleStreamStockAttributes(
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
            if (dualTexture && textureCoordinate1 != nullptr &&
                textureCoordinate1->getVertexElementFormatProperty() !=
                    VertexElementFormat::Vector2)
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

            VertexAttributeList result;
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
            if (dualTexture && textureCoordinate1 != nullptr)
                result.push_back(DescribeVertexAttribute(*textureCoordinate1, 4, stride));
            if (skinned)
            {
                result.push_back(DescribeVertexAttribute(*blendWeight, 5, stride));
                result.push_back(DescribeVertexAttribute(*blendIndices, 6, stride));
            }
            const unsigned int nativeBuffer = GetNativeBufferId(vertexBuffer);
            const std::uint64_t bufferIdentity = GetVertexBufferIdentity(vertexBuffer);
            for (VertexAttributeBinding& attribute : result)
            {
                attribute.vertexBuffer = nativeBuffer;
                attribute.vertexBufferIdentity = bufferIdentity;
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
            std::array<std::pair<VertexElementUsage, int>, 16> effectiveSemantics{};
            std::size_t effectiveSemanticCount = 0;
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

                if (declaration.size() > stream.effectiveUsageIndices.size() ||
                    (stream.effectiveUsageIndexCount != 0 &&
                     stream.effectiveUsageIndexCount != static_cast<int>(declaration.size())))
                {
                    throw std::invalid_argument(
                        "RLGL: invalid collision-remapped vertex semantic metadata");
                }

                for (std::size_t elementIndex = 0;
                     elementIndex < declaration.size(); ++elementIndex)
                {
                    const VertexElement& element = declaration[elementIndex];
                    const auto semantic = std::pair{
                        element.getVertexElementUsageProperty(),
                        stream.EffectiveUsageIndex(
                            elementIndex, element.getUsageIndexProperty())};
                    if (std::find(
                            effectiveSemantics.begin(),
                            effectiveSemantics.begin() + effectiveSemanticCount,
                            semantic) != effectiveSemantics.begin() + effectiveSemanticCount)
                    {
                        throw std::invalid_argument(
                            "RLGL: collision-remapped vertex semantics are not unique");
                    }
                    if (effectiveSemanticCount >= effectiveSemantics.size())
                    {
                        throw System::NotSupportedException(
                            "RLGL: multi-stream declarations exceed the 16-location GL limit");
                    }
                    effectiveSemantics[effectiveSemanticCount++] = semantic;
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
                if (stream.instanceFrequency != 0 || !stream.vertexShaderInputUsed)
                    continue;
                const auto& declaration = GetVertexDeclaration(*stream.buffer);
                for (std::size_t elementIndex = 0;
                     elementIndex < declaration.size(); ++elementIndex)
                {
                    const VertexElement& element = declaration[elementIndex];
                    if (element.getVertexElementUsageProperty() != usage ||
                        stream.EffectiveUsageIndex(
                            elementIndex, element.getUsageIndexProperty()) != usageIndex)
                    {
                        continue;
                    }
                    return LocatedVertexElement{
                        &element,
                        streamIndex,
                        stream.combinedByteBase + element.getOffsetProperty()};
                }
            }
            return {};
        }

        [[nodiscard]] VertexAttributeList BuildStockAttributes(
            const IVertexBufferRenderer& vertexBuffer, const GpuDrawParams& params)
        {
            if (params.vertexStreamCount < 2)
            {
                VertexAttributeList result =
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
                    const std::uint64_t bufferIdentity =
                        GetVertexBufferIdentity(*stream.buffer);
                    for (VertexAttributeBinding& attribute : result)
                    {
                        attribute.vertexBuffer = nativeBuffer;
                        attribute.vertexBufferIdentity = bufferIdentity;
                    }
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
            if (params.dualTexture && textureCoordinate1.element != nullptr &&
                textureCoordinate1.element->getVertexElementFormatProperty() !=
                    VertexElementFormat::Vector2)
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

            VertexAttributeList result;
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
                binding.vertexBufferIdentity = GetVertexBufferIdentity(*stream.buffer);
                result.push_back(binding);
            };

            append(position, 0);
            if (params.vertexColorEnabled) append(color, 1);
            if (params.textureEnabled) append(textureCoordinate, 2);
            if (params.lightingEnabled) append(normal, 3);
            if (params.dualTexture && textureCoordinate1.element != nullptr)
                append(textureCoordinate1, 4);
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
                        binding.vertexBufferIdentity = GetVertexBufferIdentity(*stream.buffer);
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

        [[nodiscard]] VertexAttributeList
        BuildShaderEffectAttributes(
            const IVertexBufferRenderer& primaryVertexBuffer,
            const GpuDrawParams& params, const bool allowInstancing)
        {
            VertexAttributeList result;
            unsigned int location = 0;
            bool foundPerVertexStream = false;
            bool foundInstanceStream = false;

            const auto appendBuffer = [&result, &location](
                const IVertexBufferRenderer& buffer, const int stride,
                const int vertexOffset, const int divisor)
            {
                const auto& declaration = GetVertexDeclaration(buffer);
                if (declaration.empty())
                {
                    throw System::NotSupportedException(
                        "RLGL ShaderEffect: every source-shader stream requires a "
                        "VertexDeclaration");
                }
                if (stride <= 0 ||
                    GetVertexStride(buffer) != static_cast<std::size_t>(stride) ||
                    vertexOffset < 0 || divisor < 0)
                {
                    throw std::invalid_argument(
                        "RLGL ShaderEffect: invalid vertex stream layout");
                }
                if (vertexOffset > std::numeric_limits<int>::max() / stride)
                    throw std::overflow_error(
                        "RLGL ShaderEffect: vertex stream byte offset exceeds Int32");
                if (declaration.size() > 16u - location)
                {
                    throw System::NotSupportedException(
                        "RLGL ShaderEffect: declarations exceed CNA's 16-location XNA input "
                        "convention");
                }
                const int baseOffset = vertexOffset * stride;
                for (const VertexElement& element : declaration)
                {
                    VertexAttributeBinding binding = DescribeVertexAttribute(
                        element, location++, stride, baseOffset);
                    binding.vertexBuffer = GetNativeBufferId(buffer);
                    binding.vertexBufferIdentity = GetVertexBufferIdentity(buffer);
                    binding.divisor = divisor;
                    result.push_back(binding);
                }
            };

            if (params.vertexStreamCount == 0)
            {
                const std::size_t nativeStride = GetVertexStride(primaryVertexBuffer);
                if (nativeStride == 0 || nativeStride >
                    static_cast<std::size_t>(std::numeric_limits<int>::max()))
                {
                    throw std::runtime_error(
                        "RLGL ShaderEffect: primary vertex buffer has no usable stride");
                }
                appendBuffer(
                    primaryVertexBuffer, static_cast<int>(nativeStride), 0, 0);
                foundPerVertexStream = true;
            }
            else
            {
                if (params.vertexStreamCount < 0 ||
                    params.vertexStreamCount >
                        static_cast<int>(params.vertexStreams.size()))
                {
                    throw std::invalid_argument(
                        "RLGL ShaderEffect: invalid vertex stream count");
                }
                std::array<bool, kMaxVertexStreams> occupiedSlots{};
                for (int streamIndex = 0;
                     streamIndex < params.vertexStreamCount; ++streamIndex)
                {
                    const GpuVertexStreamBinding& stream =
                        params.vertexStreams[streamIndex];
                    if (stream.buffer == nullptr || stream.slot < 0 ||
                        stream.slot >= kMaxVertexStreams)
                    {
                        throw std::invalid_argument(
                            "RLGL ShaderEffect: invalid vertex stream binding");
                    }
                    if (occupiedSlots[static_cast<std::size_t>(stream.slot)])
                        throw std::invalid_argument(
                            "RLGL ShaderEffect: duplicate vertex stream slot");
                    occupiedSlots[static_cast<std::size_t>(stream.slot)] = true;
                }
                for (int ratePass = 0; ratePass < 2; ++ratePass)
                {
                    for (int streamIndex = 0;
                         streamIndex < params.vertexStreamCount; ++streamIndex)
                    {
                        const GpuVertexStreamBinding& stream =
                            params.vertexStreams[streamIndex];
                        const bool instanceStream = stream.instanceFrequency > 0;
                        if (instanceStream != (ratePass == 1)) continue;
                        if (ratePass == 0)
                        {
                            foundPerVertexStream = true;
                        }
                        else
                        {
                            foundInstanceStream = true;
                            if (!allowInstancing)
                            {
                                throw System::NotSupportedException(
                                    "RLGL ShaderEffect: an ordinary draw cannot consume an "
                                    "instance stream");
                            }
                        }
                        appendBuffer(
                            *stream.buffer, stream.strideInBytes,
                            stream.vertexOffset, stream.instanceFrequency);
                    }
                }
            }

            if (!foundPerVertexStream || result.empty())
                throw std::invalid_argument(
                    "RLGL ShaderEffect: draw has no per-vertex declaration");
            if (allowInstancing && !foundInstanceStream)
                throw System::NotSupportedException(
                    "RLGL ShaderEffect: instanced drawing requires an instance stream");
            return result;
        }

        void SubmitShaderEffect(
            const IVertexBufferRenderer& vertexBuffer,
            const IIndexBufferRenderer* const indexBuffer,
            const Matrix& world, const Matrix& view, const Matrix& projection,
            const PrimitiveType primitive, const int elementCount,
            const int firstVertex, const int startIndex, const int baseVertex,
            const GpuDrawParams& params, const bool instanced)
        {
            if (params.customEffectRenderer == nullptr)
                throw System::NotSupportedException(
                    "RLGL ShaderEffect: the source program did not compile successfully");
            if ((!instanced && params.instanceCount != 1) ||
                (instanced && params.firstInstance != 0))
            {
                throw System::NotSupportedException(
                    "RLGL ShaderEffect: invalid classic instancing range");
            }
            const VertexAttributeList attributes =
                BuildShaderEffectAttributes(vertexBuffer, params, instanced);
            float worldValues[16]{};
            float viewValues[16]{};
            float projectionValues[16]{};
            world.ToColumnMajor(worldValues);
            view.ToColumnMajor(viewValues);
            projection.ToColumnMajor(projectionValues);
            Bridge::RecordMatrixConversions(3);
            Bridge::DrawShaderEffectGeometry(
                *params.customEffectRenderer,
                GetNativeBufferId(vertexBuffer),
                indexBuffer == nullptr ? 0u : GetNativeBufferId(*indexBuffer),
                attributes.data(), static_cast<int>(attributes.size()),
                worldValues, viewValues, projectionValues,
                static_cast<int>(primitive), elementCount,
                firstVertex, startIndex, baseVertex,
                params.instanceCount, instanced,
                indexBuffer != nullptr && indexBuffer->IsThirtyTwoBit());
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
        }

        void Submit(
            Bridge::PrimitivePipeline& pipeline,
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

            GpuDrawParams effectiveParams = params;
            if (effectiveParams.vertexColorEnabled ||
                (effectiveParams.lightingEnabled && !effectiveParams.skinned &&
                 !effectiveParams.envMapping))
            {
                bool hasExplicitDeclaration = false;
                bool hasNormal = false;
                bool hasColor = false;
                const int streamCount = effectiveParams.vertexStreamCount < 2
                    ? 1 : effectiveParams.vertexStreamCount;
                for (int stream = 0; stream < streamCount; ++stream)
                {
                    const IVertexBufferRenderer* source = &vertexBuffer;
                    if (effectiveParams.vertexStreamCount >= 2)
                    {
                        const GpuVertexStreamBinding& binding =
                            effectiveParams.vertexStreams[stream];
                        if (binding.instanceFrequency != 0) continue;
                        source = binding.buffer;
                    }
                    if (source == nullptr) continue;
                    const auto& declaration = GetVertexDeclaration(*source);
                    hasExplicitDeclaration = hasExplicitDeclaration || !declaration.empty();
                    hasNormal = hasNormal ||
                        FindElement(declaration, VertexElementUsage::Normal, 0) != nullptr;
                    hasColor = hasColor ||
                        FindElement(declaration, VertexElementUsage::Color, 0) != nullptr;
                }
                // XNA stock shader binding selects an unlit input shape when an explicit
                // declaration has no Normal0, even if BasicEffect.LightingEnabled remains true.
                if (hasExplicitDeclaration && !hasNormal && !effectiveParams.skinned &&
                    !effectiveParams.envMapping)
                    effectiveParams.lightingEnabled = false;
                if (hasExplicitDeclaration && !hasColor)
                    effectiveParams.vertexColorEnabled = false;
            }
            const VertexAttributeList attributes =
                BuildStockAttributes(vertexBuffer, effectiveParams);
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
            Bridge::RecordMatrixConversions(1);
            const unsigned int texture =
                effectiveParams.textureEnabled && effectiveParams.texture0 != nullptr
                    ? GetNativeTextureId(*effectiveParams.texture0) : 0u;
            const unsigned int texture1 =
                effectiveParams.dualTexture && effectiveParams.texture1 != nullptr
                    ? GetNativeTextureId(*effectiveParams.texture1) : 0u;
            Bridge::DrawPrimitiveGeometry(
                pipeline, GetNativeBufferId(vertexBuffer),
                indexBuffer == nullptr ? 0u : GetNativeBufferId(*indexBuffer),
                attributes.data(), static_cast<int>(attributes.size()),
                columnMajor, texture, texture1, effectiveParams,
                static_cast<int>(primitive), elementCount,
                firstVertex, startIndex, baseVertex,
                indexBuffer != nullptr && indexBuffer->IsThirtyTwoBit());
        }
    }

    Bridge::PrimitivePipeline& RlglRenderer::GetPrimitivePipeline()
    {
        restorePrimitivePipeline_ = true;
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
        if (params.customEffectRequested || params.customEffectRenderer != nullptr)
        {
            const int elementCount = PrimitiveElementCount(primitive, primitiveCount);
            ValidateDrawRange(
                vertexBuffer, nullptr, elementCount,
                params.vertexStart, 0, 0);
            SubmitShaderEffect(
                vertexBuffer, nullptr, world, view, projection,
                primitive, elementCount, params.vertexStart, 0, 0,
                params, false);
            return;
        }
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
        if (params.customEffectRequested || params.customEffectRenderer != nullptr)
        {
            const int elementCount = PrimitiveElementCount(primitive, primitiveCount);
            ValidateDrawRange(
                vertexBuffer, &indexBuffer, elementCount,
                0, params.startIndex, params.baseVertex);
            SubmitShaderEffect(
                vertexBuffer, &indexBuffer, world, view, projection,
                primitive, elementCount, 0, params.startIndex, params.baseVertex,
                params, false);
            return;
        }
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
        if (params.customEffectRequested || params.customEffectRenderer != nullptr)
        {
            const int elementCount = PrimitiveElementCount(primitive, primitiveCount);
            ValidateDrawRange(
                vertexBuffer, &indexBuffer, elementCount,
                0, params.startIndex, params.baseVertex);
            SubmitShaderEffect(
                vertexBuffer, &indexBuffer, world, view, projection,
                primitive, elementCount, 0, params.startIndex, params.baseVertex,
                params, true);
            return;
        }
        RequireBaselineEffect(params, true);
        Submit(
            GetPrimitivePipeline(), vertexBuffer, &indexBuffer,
            world, view, projection, primitive, primitiveCount,
            0, params.startIndex, params.baseVertex, params, true,
            currentViewportWidth_, currentViewportHeight_,
            GetCurrentSampleCount());
    }
}
