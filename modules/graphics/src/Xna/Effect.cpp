// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>

namespace
{
    constexpr std::size_t kMaximumCompiledEffectBytes = 64u * 1024u * 1024u;
    constexpr std::uint32_t kEffectFrameworkToken = 0xFEFF0901u;
    constexpr std::uint32_t kXna4EffectWrapperToken = 0xBCF00BCFu;
    constexpr std::uint32_t kMaximumReflectedParameters = 16u * 1024u;
    constexpr std::uint32_t kMaximumReflectedTechniques = 4u * 1024u;
    constexpr std::uint32_t kMaximumEffectObjects = 64u * 1024u;
    constexpr std::size_t kMaximumReflectionDepth = 32u;
    constexpr std::size_t kMaximumReflectedItems = 64u * 1024u;

    std::uint32_t ReadUInt32LittleEndian(
        const std::vector<SharpRuntime::bytecs>& bytes, std::size_t offset)
    {
        return static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    /**
     * Bounded preflight for the legacy Effect Framework container consumed by pinned MojoShader.
     * MojoShader's mature parser predates hostile-content parsing and deliberately trusts several
     * relative offsets. Validate the complete container graph before the native call so malformed
     * game assets cannot turn those historical assumptions into an out-of-bounds read or an
     * allocation bomb. Shader object bodies remain opaque and are validated by MojoShader's D3D9
     * shader parser after their enclosing length/padding has been checked here.
     */
    class EffectFrameworkPreflight final
    {
    public:
        explicit EffectFrameworkPreflight(const std::vector<SharpRuntime::bytecs>& bytes)
            : bytes_(bytes)
        {
        }

        bool Validate()
        {
            if (bytes_.size() < 24) return false;

            std::size_t tokenOffset = 0;
            const std::uint32_t outerToken = ReadUInt32LittleEndian(bytes_, 0);
            if (outerToken == kXna4EffectWrapperToken)
            {
                const std::uint32_t wrappedOffset = ReadUInt32LittleEndian(bytes_, 4);
                if (wrappedOffset < 8 || wrappedOffset > bytes_.size() - 8 ||
                    (wrappedOffset & 3u) != 0)
                {
                    return false;
                }
                tokenOffset = wrappedOffset;
            }
            else if (outerToken != kEffectFrameworkToken)
            {
                return false;
            }

            if (ReadUInt32LittleEndian(bytes_, tokenOffset) != kEffectFrameworkToken) return false;
            base_ = tokenOffset + 8;
            const std::uint32_t structureOffset =
                ReadUInt32LittleEndian(bytes_, tokenOffset + 4);
            if (structureOffset > bytes_.size() - base_ || (structureOffset & 3u) != 0)
                return false;
            std::size_t cursor = base_ + structureOffset;

            std::uint32_t parameterCount = 0;
            std::uint32_t techniqueCount = 0;
            std::uint32_t ignored = 0;
            std::uint32_t objectCount = 0;
            if (!Read(cursor, parameterCount) || !Read(cursor, techniqueCount) ||
                !Read(cursor, ignored) || !Read(cursor, objectCount) ||
                parameterCount > kMaximumReflectedParameters || techniqueCount == 0 ||
                techniqueCount > kMaximumReflectedTechniques ||
                objectCount > kMaximumEffectObjects || !ConsumeGraphItems(techniqueCount))
            {
                return false;
            }
            objectTypes_.assign(objectCount, -1);
            objectRecordsSeen_.assign(objectCount, false);

            parameters_.reserve(parameterCount);
            for (std::uint32_t i = 0; i < parameterCount; ++i)
            {
                std::uint32_t typeOffset = 0;
                std::uint32_t valueOffset = 0;
                std::uint32_t flags = 0;
                std::uint32_t annotationCount = 0;
                if (!Read(cursor, typeOffset) || !Read(cursor, valueOffset) ||
                    !Read(cursor, flags) || !Read(cursor, annotationCount) ||
                    !ValidateAnnotations(cursor, annotationCount))
                {
                    return false;
                }
                ValueInfo info;
                if (!ValidateValue(typeOffset, valueOffset, 0, info)) return false;
                parameters_.push_back(std::move(info));
            }

            techniques_.reserve(techniqueCount);
            for (std::uint32_t techniqueIndex = 0;
                 techniqueIndex < techniqueCount; ++techniqueIndex)
            {
                std::uint32_t nameOffset = 0;
                std::uint32_t annotationCount = 0;
                std::uint32_t passCount = 0;
                if (!Read(cursor, nameOffset) || !Read(cursor, annotationCount) ||
                    !Read(cursor, passCount) || !ValidateString(nameOffset) || passCount == 0 ||
                    passCount > kMaximumReflectedItems || !ConsumeGraphItems(passCount) ||
                    !ValidateAnnotations(cursor, annotationCount))
                {
                    return false;
                }

                TechniqueInfo technique;
                technique.passes.reserve(passCount);
                for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
                {
                    std::uint32_t passNameOffset = 0;
                    std::uint32_t passAnnotationCount = 0;
                    std::uint32_t stateCount = 0;
                    if (!Read(cursor, passNameOffset) ||
                        !Read(cursor, passAnnotationCount) || !Read(cursor, stateCount) ||
                        !ValidateString(passNameOffset) || stateCount > kMaximumReflectedItems ||
                        !ConsumeGraphItems(stateCount) ||
                        !ValidateAnnotations(cursor, passAnnotationCount))
                    {
                        return false;
                    }

                    PassInfo pass;
                    pass.stateObjectReferences.reserve(stateCount);
                    for (std::uint32_t stateIndex = 0; stateIndex < stateCount; ++stateIndex)
                    {
                        std::uint32_t stateType = 0;
                        std::uint32_t legacy = 0;
                        std::uint32_t stateTypeOffset = 0;
                        std::uint32_t stateValueOffset = 0;
                        if (!Read(cursor, stateType) || !Read(cursor, legacy) ||
                            !Read(cursor, stateTypeOffset) ||
                            !Read(cursor, stateValueOffset))
                        {
                            return false;
                        }
                        ValueInfo info;
                        if (!ValidateValue(stateTypeOffset, stateValueOffset, 0, info))
                            return false;
                        pass.stateObjectReferences.push_back(info.firstObjectReference);
                    }
                    technique.passes.push_back(std::move(pass));
                }
                techniques_.push_back(std::move(technique));
            }

            std::uint32_t smallObjectCount = 0;
            std::uint32_t largeObjectCount = 0;
            if (!Read(cursor, smallObjectCount) || !Read(cursor, largeObjectCount) ||
                smallObjectCount > kMaximumEffectObjects ||
                largeObjectCount > kMaximumEffectObjects ||
                smallObjectCount > objectTypes_.size() ||
                largeObjectCount > objectTypes_.size() - smallObjectCount)
            {
                return false;
            }
            for (std::uint32_t i = 0; i < smallObjectCount; ++i)
            {
                std::uint32_t objectIndex = 0;
                std::uint32_t length = 0;
                if (!Read(cursor, objectIndex) || !Read(cursor, length) ||
                    objectIndex >= objectTypes_.size() || objectTypes_[objectIndex] < kStringType ||
                    objectTypes_[objectIndex] > kVertexShaderType || !MarkObjectRecord(objectIndex) ||
                    !ValidateObjectBlob(cursor, length,
                        objectTypes_[objectIndex] <= kSamplerCubeType))
                {
                    return false;
                }
            }
            for (std::uint32_t i = 0; i < largeObjectCount; ++i)
            {
                std::uint32_t techniqueIndex = 0;
                std::uint32_t index = 0;
                std::uint32_t legacy = 0;
                std::uint32_t stateIndex = 0;
                std::uint32_t objectEncoding = 0;
                std::uint32_t length = 0;
                if (!Read(cursor, techniqueIndex) || !Read(cursor, index) ||
                    !Read(cursor, legacy) || !Read(cursor, stateIndex) ||
                    !Read(cursor, objectEncoding) || !Read(cursor, length))
                {
                    return false;
                }

                std::optional<std::uint32_t> objectIndex;
                if (techniqueIndex == UINT32_MAX)
                {
                    if (index >= parameters_.size() ||
                        stateIndex >= parameters_[index].stateObjectReferences.size())
                        return false;
                    objectIndex = parameters_[index].stateObjectReferences[stateIndex];
                }
                else
                {
                    if (techniqueIndex >= techniques_.size() ||
                        index >= techniques_[techniqueIndex].passes.size() ||
                        stateIndex >= techniques_[techniqueIndex].passes[index]
                            .stateObjectReferences.size())
                    {
                        return false;
                    }
                    objectIndex = techniques_[techniqueIndex].passes[index]
                        .stateObjectReferences[stateIndex];
                }
                if (!objectIndex || *objectIndex >= objectTypes_.size())
                {
                    return false;
                }
                const int objectType = objectTypes_[*objectIndex];
                if (objectType < static_cast<int>(kTextureType) ||
                    objectType > static_cast<int>(kVertexShaderType) ||
                    !MarkObjectRecord(*objectIndex) ||
                    (objectEncoding == 2 && objectType >= static_cast<int>(kPixelShaderType) &&
                     !ValidateStandalonePreshaderHeader(cursor, length)) ||
                    !ValidateObjectBlob(cursor, length,
                        objectType <= static_cast<int>(kSamplerCubeType)))
                {
                    return false;
                }
            }
            return true;
        }

    private:
        static constexpr std::uint32_t kScalarClass = 0;
        static constexpr std::uint32_t kMatrixColumnsClass = 3;
        static constexpr std::uint32_t kObjectClass = 4;
        static constexpr std::uint32_t kStructClass = 5;
        static constexpr std::uint32_t kBoolType = 1;
        static constexpr std::uint32_t kFloatType = 3;
        static constexpr std::uint32_t kStringType = 4;
        static constexpr std::uint32_t kTextureType = 5;
        static constexpr std::uint32_t kTextureCubeType = 9;
        static constexpr std::uint32_t kSamplerType = 10;
        static constexpr std::uint32_t kSamplerCubeType = 14;
        static constexpr std::uint32_t kPixelShaderType = 15;
        static constexpr std::uint32_t kVertexShaderType = 16;
        static constexpr std::uint32_t kSamplerTextureState = 4;

        struct ValueInfo
        {
            std::optional<std::uint32_t> firstObjectReference;
            std::vector<std::optional<std::uint32_t>> stateObjectReferences;
        };

        struct PassInfo
        {
            std::vector<std::optional<std::uint32_t>> stateObjectReferences;
        };

        struct TechniqueInfo
        {
            std::vector<PassInfo> passes;
        };

        bool Read(std::size_t& cursor, std::uint32_t& value) const
        {
            if (cursor > bytes_.size() || bytes_.size() - cursor < 4) return false;
            value = ReadUInt32LittleEndian(bytes_, cursor);
            cursor += 4;
            return true;
        }

        bool ResolveOffset(std::uint32_t relativeOffset, std::size_t bytes,
                           std::size_t& absoluteOffset) const
        {
            if ((relativeOffset & 3u) != 0 || relativeOffset > bytes_.size() - base_)
                return false;
            absoluteOffset = base_ + relativeOffset;
            return absoluteOffset <= bytes_.size() && bytes <= bytes_.size() - absoluteOffset;
        }

        bool ValidateString(std::uint32_t relativeOffset)
        {
            std::size_t stringOffset = 0;
            if (!ResolveOffset(relativeOffset, 4, stringOffset)) return false;
            const std::uint32_t length = ReadUInt32LittleEndian(bytes_, stringOffset);
            if (length == 0) return true;
            stringOffset += 4;
            return length <= bytes_.size() - stringOffset &&
                   bytes_[stringOffset + length - 1] == 0 &&
                   ConsumeAllocation(length);
        }

        bool ConsumeAllocation(std::size_t bytes)
        {
            if (bytes > kMaximumCompiledEffectBytes - reflectedAllocationBytes_) return false;
            reflectedAllocationBytes_ += bytes;
            return true;
        }

        bool ConsumeGraphItems(std::size_t count)
        {
            if (count > kMaximumReflectedItems - graphItemCount_) return false;
            graphItemCount_ += count;
            return true;
        }

        bool MarkObjectRecord(std::uint32_t objectIndex)
        {
            if (objectIndex >= objectRecordsSeen_.size() || objectRecordsSeen_[objectIndex])
                return false;
            objectRecordsSeen_[objectIndex] = true;
            return true;
        }

        bool ValidateAnnotations(std::size_t& cursor, std::uint32_t count)
        {
            if (count > kMaximumReflectedItems ||
                count > (bytes_.size() - std::min(cursor, bytes_.size())) / 8)
            {
                return false;
            }
            for (std::uint32_t i = 0; i < count; ++i)
            {
                std::uint32_t typeOffset = 0;
                std::uint32_t valueOffset = 0;
                ValueInfo ignored;
                if (!Read(cursor, typeOffset) || !Read(cursor, valueOffset) ||
                    !ValidateValue(typeOffset, valueOffset, 0, ignored))
                {
                    return false;
                }
            }
            return true;
        }

        bool AssignObjectType(std::uint32_t objectIndex, std::uint32_t type)
        {
            if (objectIndex >= objectTypes_.size()) return false;
            int& knownType = objectTypes_[objectIndex];
            if (knownType != -1 && knownType != static_cast<int>(type)) return false;
            knownType = static_cast<int>(type);
            return true;
        }

        bool ValidateValue(std::uint32_t typeOffset, std::uint32_t valueOffset,
                           std::size_t depth, ValueInfo& result)
        {
            if (depth >= kMaximumReflectionDepth ||
                ++validatedValueCount_ > kMaximumReflectedItems)
            {
                return false;
            }
            std::size_t typeCursor = 0;
            std::size_t valueCursor = 0;
            if (!ResolveOffset(typeOffset, 20, typeCursor) ||
                !ResolveOffset(valueOffset, 0, valueCursor))
            {
                return false;
            }

            std::uint32_t type = 0;
            std::uint32_t parameterClass = 0;
            std::uint32_t nameOffset = 0;
            std::uint32_t semanticOffset = 0;
            std::uint32_t elementCount = 0;
            if (!Read(typeCursor, type) || !Read(typeCursor, parameterClass) ||
                !Read(typeCursor, nameOffset) || !Read(typeCursor, semanticOffset) ||
                !Read(typeCursor, elementCount) || !ValidateString(nameOffset) ||
                !ValidateString(semanticOffset) || elementCount > kMaximumReflectedItems)
            {
                return false;
            }

            if (parameterClass >= kScalarClass &&
                parameterClass <= kMatrixColumnsClass)
            {
                std::uint32_t columnCount = 0;
                std::uint32_t rowCount = 0;
                if (type < kBoolType || type > kFloatType ||
                    !Read(typeCursor, columnCount) || !Read(typeCursor, rowCount) ||
                    columnCount == 0 || columnCount > 4 || rowCount == 0 || rowCount > 4)
                {
                    return false;
                }
                const std::size_t elements = std::max<std::uint32_t>(elementCount, 1);
                const std::size_t cells = static_cast<std::size_t>(columnCount) * rowCount * elements;
                const std::size_t reflectedStorageBytes = 16u * rowCount * elements;
                return cells <= kMaximumCompiledEffectBytes / 4 &&
                       cells * 4 <= bytes_.size() - valueCursor &&
                       ConsumeAllocation(reflectedStorageBytes);
            }

            if (parameterClass == kObjectClass)
            {
                if (type < kStringType || type > kVertexShaderType) return false;
                if (type >= kSamplerType && type <= kSamplerCubeType)
                {
                    std::uint32_t stateCount = 0;
                    if (!Read(valueCursor, stateCount) ||
                        stateCount > kMaximumReflectedItems ||
                        stateCount > (bytes_.size() - valueCursor) / 16 ||
                        !ConsumeAllocation(static_cast<std::size_t>(stateCount) * 16u))
                    {
                        return false;
                    }
                    result.stateObjectReferences.reserve(stateCount);
                    for (std::uint32_t i = 0; i < stateCount; ++i)
                    {
                        std::uint32_t rawStateType = 0;
                        std::uint32_t legacy = 0;
                        std::uint32_t stateTypeOffset = 0;
                        std::uint32_t stateValueOffset = 0;
                        if (!Read(valueCursor, rawStateType) || !Read(valueCursor, legacy) ||
                            !Read(valueCursor, stateTypeOffset) ||
                            !Read(valueCursor, stateValueOffset))
                        {
                            return false;
                        }
                        const std::uint32_t stateType = rawStateType & ~0xA0u;
                        if (stateType > 17) return false;
                        ValueInfo stateValue;
                        if (!ValidateValue(stateTypeOffset, stateValueOffset,
                                           depth + 1, stateValue))
                        {
                            return false;
                        }
                        if (stateType == kSamplerTextureState &&
                            !stateValue.firstObjectReference)
                        {
                            return false;
                        }
                        result.stateObjectReferences.push_back(
                            stateValue.firstObjectReference);
                    }
                    return true;
                }

                const std::size_t objectReferences =
                    std::max<std::uint32_t>(elementCount, 1);
                if (objectReferences > (bytes_.size() - valueCursor) / 4 ||
                    !ConsumeAllocation(objectReferences * 4u))
                {
                    return false;
                }
                for (std::size_t i = 0; i < objectReferences; ++i)
                {
                    std::uint32_t objectIndex = 0;
                    if (!Read(valueCursor, objectIndex) || !AssignObjectType(objectIndex, type))
                        return false;
                    if (i == 0) result.firstObjectReference = objectIndex;
                }
                return true;
            }

            if (parameterClass != kStructClass) return false;
            std::uint32_t memberCount = 0;
            if (!Read(typeCursor, memberCount) || memberCount > kMaximumReflectedItems ||
                memberCount > (bytes_.size() - typeCursor) / 28)
            {
                return false;
            }
            std::size_t structStorageCells = 0;
            std::size_t tightDefaultCells = 0;
            for (std::uint32_t i = 0; i < memberCount; ++i)
            {
                std::uint32_t memberType = 0;
                std::uint32_t memberClass = 0;
                std::uint32_t memberNameOffset = 0;
                std::uint32_t memberSemanticOffset = 0;
                std::uint32_t memberElements = 0;
                std::uint32_t memberColumns = 0;
                std::uint32_t memberRows = 0;
                if (!Read(typeCursor, memberType) || !Read(typeCursor, memberClass) ||
                    !Read(typeCursor, memberNameOffset) ||
                    !Read(typeCursor, memberSemanticOffset) ||
                    !Read(typeCursor, memberElements) || !Read(typeCursor, memberColumns) ||
                    !Read(typeCursor, memberRows) || !ValidateString(memberNameOffset) ||
                    !ValidateString(memberSemanticOffset) || memberType < kBoolType ||
                    memberType > kFloatType || memberClass > kMatrixColumnsClass ||
                    memberElements > kMaximumReflectedItems || memberColumns == 0 ||
                    memberColumns > 4 || memberRows == 0 || memberRows > 4)
                {
                    return false;
                }
                const std::size_t storageElements =
                    std::max<std::uint32_t>(memberElements, 1);
                const std::size_t memberStorage = 4u * memberRows * storageElements;
                const std::size_t memberDefaults =
                    static_cast<std::size_t>(memberColumns) * memberRows * memberElements;
                if (memberStorage > kMaximumCompiledEffectBytes / 4 - structStorageCells ||
                    memberDefaults > kMaximumCompiledEffectBytes / 4 - tightDefaultCells)
                {
                    return false;
                }
                structStorageCells += memberStorage;
                tightDefaultCells += memberDefaults;
            }
            const std::size_t elements = std::max<std::uint32_t>(elementCount, 1);
            if (structStorageCells > kMaximumCompiledEffectBytes / 4 / elements ||
                tightDefaultCells > (bytes_.size() - typeCursor) / 4 / elements)
            {
                return false;
            }
            return ConsumeAllocation(structStorageCells * elements * 4u);
        }

        bool ValidateObjectBlob(std::size_t& cursor, std::uint32_t length,
                                bool requireTerminator)
        {
            if (cursor > bytes_.size() || length > bytes_.size() - cursor) return false;
            if (requireTerminator && length > 0 && bytes_[cursor + length - 1] != 0)
                return false;
            if (requireTerminator && !ConsumeAllocation(length)) return false;
            const std::size_t paddedLength = (static_cast<std::size_t>(length) + 3u) & ~3u;
            if (paddedLength > bytes_.size() - cursor) return false;
            cursor += paddedLength;
            return true;
        }

        bool ValidateStandalonePreshaderHeader(std::size_t cursor, std::uint32_t length)
        {
            if (cursor > bytes_.size() || length < 4 || length > bytes_.size() - cursor)
                return false;
            const std::uint32_t nameLength = ReadUInt32LittleEndian(bytes_, cursor);
            return nameLength > 0 && nameLength <= length - 4u &&
                   bytes_[cursor + 4u + nameLength - 1u] == 0 &&
                   ConsumeAllocation(nameLength);
        }

        const std::vector<SharpRuntime::bytecs>& bytes_;
        std::size_t base_ = 0;
        std::size_t validatedValueCount_ = 0;
        std::size_t reflectedAllocationBytes_ = 0;
        std::size_t graphItemCount_ = 0;
        std::vector<int> objectTypes_;
        std::vector<bool> objectRecordsSeen_;
        std::vector<ValueInfo> parameters_;
        std::vector<TechniqueInfo> techniques_;
    };

    bool HasStructurallyValidEffectFrameworkHeader(
        const std::vector<SharpRuntime::bytecs>& bytes)
    {
        return EffectFrameworkPreflight(bytes).Validate();
    }

    Microsoft::Xna::Framework::Graphics::EffectAnnotation MakeAnnotation(
        const CNA::Internal::Renderers::CompiledEffectAnnotationDescription& description)
    {
        std::vector<float> data((description.rawValue.size() + sizeof(float) - 1) /
                                sizeof(float));
        if (!description.rawValue.empty())
        {
            std::memcpy(data.data(), description.rawValue.data(), description.rawValue.size());
        }
        return Microsoft::Xna::Framework::Graphics::EffectAnnotation(
            description.name, description.semantic,
            description.rowCount, description.columnCount,
            description.parameterClass, description.parameterType,
            std::move(data), description.stringValue);
    }

    void ValidateReflectedValue(
        const CNA::Internal::Renderers::CompiledEffectValueDescription& value,
        std::size_t& reflectedValueBytes)
    {
        const bool validDimensions = value.parameterClass ==
                Microsoft::Xna::Framework::Graphics::EffectParameterClass::Struct
            ? value.rowCount >= 0 && value.rowCount <= 1 && value.columnCount >= 0 &&
              static_cast<std::size_t>(value.columnCount) <=
                  kMaximumCompiledEffectBytes / sizeof(std::uint32_t)
            : value.rowCount >= 0 && value.rowCount <= 4 &&
              value.columnCount >= 0 && value.columnCount <= 4;
        if (!validDimensions ||
            value.elementCount < 0 ||
            static_cast<std::size_t>(value.elementCount) > kMaximumReflectedItems ||
            value.rawValue.size() > kMaximumCompiledEffectBytes - reflectedValueBytes ||
            value.name.size() > kMaximumCompiledEffectBytes ||
            value.semantic.size() > kMaximumCompiledEffectBytes ||
            value.stringValue.size() > kMaximumCompiledEffectBytes)
        {
            throw System::ArgumentException(
                "The compiled effect contains invalid or oversized reflection metadata.",
                "effectCode");
        }
        reflectedValueBytes += value.rawValue.size();
    }

    void ValidateAnnotations(
        const std::vector<CNA::Internal::Renderers::CompiledEffectAnnotationDescription>& values,
        std::size_t& itemCount, std::size_t& reflectedValueBytes)
    {
        if (values.size() > kMaximumReflectedItems - itemCount)
        {
            throw System::ArgumentException(
                "The compiled effect contains too many reflected annotations.", "effectCode");
        }
        itemCount += values.size();
        for (const auto& value : values)
            ValidateReflectedValue(value, reflectedValueBytes);
    }

    void ValidateReflectedParameter(
        const CNA::Internal::Renderers::CompiledEffectParameterDescription& parameter,
        std::size_t depth, std::size_t& itemCount, std::size_t& reflectedValueBytes)
    {
        if (depth >= kMaximumReflectionDepth || itemCount >= kMaximumReflectedItems)
        {
            throw System::ArgumentException(
                "The compiled effect parameter graph exceeds CNA's reflection limits.",
                "effectCode");
        }
        ++itemCount;
        ValidateReflectedValue(parameter, reflectedValueBytes);
        ValidateAnnotations(parameter.annotations, itemCount, reflectedValueBytes);
        for (const auto& member : parameter.structureMembers)
            ValidateReflectedParameter(member, depth + 1, itemCount, reflectedValueBytes);
    }

    std::size_t ReflectedParameterStorageBytes(
        const CNA::Internal::Renderers::CompiledEffectParameterDescription& parameter,
        std::size_t depth = 0);

    void ValidateCompiledDescription(
        const CNA::Internal::Renderers::CompiledEffectDescription& description)
    {
        if (description.parameters.size() > kMaximumReflectedParameters ||
            description.techniques.empty() ||
            description.techniques.size() > kMaximumReflectedTechniques)
        {
            throw System::ArgumentException(
                "The compiled effect contains an invalid number of parameters or techniques.",
                "effectCode");
        }

        std::size_t itemCount = 0;
        std::size_t reflectedValueBytes = 0;
        for (const auto& parameter : description.parameters)
        {
            ValidateReflectedParameter(parameter, 0, itemCount, reflectedValueBytes);
            if (ReflectedParameterStorageBytes(parameter) != parameter.rawValue.size())
            {
                throw System::ArgumentException(
                    "The compiled effect parameter storage does not match its reflected layout.",
                    "effectCode");
            }
        }

        for (const auto& technique : description.techniques)
        {
            if (itemCount >= kMaximumReflectedItems || technique.passes.empty() ||
                technique.name.size() > kMaximumCompiledEffectBytes)
            {
                throw System::ArgumentException(
                    "The compiled effect pass graph exceeds CNA's reflection limits.",
                    "effectCode");
            }
            ++itemCount;
            ValidateAnnotations(technique.annotations, itemCount, reflectedValueBytes);
            if (technique.passes.size() > kMaximumReflectedItems - itemCount)
            {
                throw System::ArgumentException(
                    "The compiled effect contains too many reflected passes.", "effectCode");
            }
            itemCount += technique.passes.size();
            for (const auto& pass : technique.passes)
            {
                if (pass.name.size() > kMaximumCompiledEffectBytes)
                {
                    throw System::ArgumentException(
                        "The compiled effect contains oversized pass metadata.", "effectCode");
                }
                ValidateAnnotations(pass.annotations, itemCount, reflectedValueBytes);
            }
        }
    }

    std::size_t ReflectedParameterStorageBytes(
        const CNA::Internal::Renderers::CompiledEffectParameterDescription& parameter,
        std::size_t depth)
    {
        using Microsoft::Xna::Framework::Graphics::EffectParameterClass;

        if (depth >= kMaximumReflectionDepth)
        {
            throw System::ArgumentException(
                "The compiled effect parameter graph exceeds CNA's reflection limits.",
                "effectCode");
        }

        std::size_t singleValueBytes = 0;
        if (parameter.parameterClass == EffectParameterClass::Struct)
        {
            for (const auto& member : parameter.structureMembers)
            {
                const std::size_t memberBytes =
                    ReflectedParameterStorageBytes(member, depth + 1);
                if (memberBytes > kMaximumCompiledEffectBytes - singleValueBytes)
                {
                    throw System::ArgumentException(
                        "The compiled effect structure storage exceeds CNA's safety limit.",
                        "effectCode");
                }
                singleValueBytes += memberBytes;
            }
        }
        else if (parameter.parameterClass == EffectParameterClass::Object)
        {
            // Effect Framework object values are indices into the native object table.
            singleValueBytes = sizeof(std::uint32_t);
        }
        else
        {
            // MojoShader expands every numeric row to a complete float4 register, including
            // scalar/vector structure members and array elements.
            singleValueBytes = static_cast<std::size_t>(std::max(parameter.rowCount, 1)) * 16;
        }

        const std::size_t elements =
            static_cast<std::size_t>(std::max(parameter.elementCount, 1));
        if (singleValueBytes > 0 && elements > kMaximumCompiledEffectBytes / singleValueBytes)
        {
            throw System::ArgumentException(
                "The compiled effect parameter storage exceeds CNA's safety limit.",
                "effectCode");
        }
        return singleValueBytes * elements;
    }
}

namespace Microsoft::Xna::Framework::Graphics
{
    Effect::Effect(GraphicsDevice& device)
        : GraphicsResource(&device)
        , device_(&device)
    {
        techniques_.Add(EffectTechnique(this, "Default"));
        currentTechnique_ = &techniques_[0];
    }

    Effect::Effect(GraphicsDevice& device, const std::vector<SharpRuntime::bytecs>& effectCode)
        : GraphicsResource(&device)
        , device_(&device)
    {
        if (effectCode.empty())
        {
            throw System::ArgumentException(
                "Compiled effect bytecode must not be empty.", "effectCode");
        }
        if (effectCode.size() > kMaximumCompiledEffectBytes)
        {
            throw System::ArgumentException(
                "Compiled effect bytecode exceeds CNA's 64 MiB safety limit.", "effectCode");
        }
        if (effectCode.size() >= 4 && effectCode[0] == 'M' && effectCode[1] == 'G' &&
            effectCode[2] == 'F' && effectCode[3] == 'X')
        {
            throw System::NotSupportedException(
                "The supplied bytes are a MonoGame MGFX effect. Effect(GraphicsDevice, byte[]) "
                "currently accepts XNA/FNA Direct3D 9 Effect Framework bytecode (.fxb/XNB), "
                "not the distinct MGFX container format.");
        }
        if (!HasStructurallyValidEffectFrameworkHeader(effectCode))
        {
            throw System::ArgumentException(
                "Compiled effect bytecode does not contain a structurally valid XNA Direct3D 9 "
                "Effect Framework header.", "effectCode");
        }
        if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        {
            throw System::NotSupportedException(
                "The active graphics renderer does not support compiled XNA/FNA Effect "
                "Framework bytecode (GraphicsCapability::CompiledEffects is false).");
        }

        compiledRuntime_ = device.GetRenderer().CreateCompiledEffect(
            reinterpret_cast<const std::uint8_t*>(effectCode.data()), effectCode.size());
        if (!compiledRuntime_)
        {
            throw System::NotSupportedException(
                "The active graphics renderer advertised compiled effects but failed to create "
                "a compiled-effect runtime.");
        }
        BuildCompiledObjectGraph();
    }

    Effect::Effect(GraphicsDevice& device,
                   std::unique_ptr<CNA::Internal::Renderers::ICompiledEffectRuntime> runtime,
                   const Effect* cloneSource)
        : GraphicsResource(&device)
        , device_(&device)
        , compiledRuntime_(std::move(runtime))
    {
        if (!compiledRuntime_)
        {
            throw System::InvalidOperationException(
                "Cannot construct a compiled Effect clone without a renderer runtime.");
        }
        BuildCompiledObjectGraph();

        if (cloneSource != nullptr)
        {
            AdoptCloneState(*cloneSource);
        }
    }

    Effect::Effect(const Effect& cloneSource)
        : GraphicsResource(cloneSource.device_)
        , device_(cloneSource.device_)
    {
        if (!cloneSource.compiledRuntime_)
        {
            // A source with no compiled runtime -- a stock effect, or one built by the
            // device-only constructor -- has nothing for the renderer to clone, so the
            // clone gets the same single "Default" technique a bare Effect has.
            techniques_.Add(EffectTechnique(this, "Default"));
            currentTechnique_ = &techniques_[0];
            return;
        }

        compiledRuntime_ = cloneSource.compiledRuntime_->Clone();
        if (!compiledRuntime_)
        {
            throw System::InvalidOperationException(
                "The active graphics renderer failed to clone this compiled effect.");
        }
        BuildCompiledObjectGraph();
        AdoptCloneState(cloneSource);
    }

    void Effect::AdoptCloneState(const Effect& cloneSource)
    {
        const int count = std::min(parameters_.getCountProperty(),
                                   cloneSource.parameters_.getCountProperty());
        for (int i = 0; i < count; ++i)
        {
            parameters_[i].CopyMutableValueFromInternal(cloneSource.parameters_[i]);
        }

        for (int i = 0; i < cloneSource.techniques_.getCountProperty(); ++i)
        {
            if (&cloneSource.techniques_[i] == cloneSource.currentTechnique_ &&
                i < techniques_.getCountProperty())
            {
                setCurrentTechniqueProperty(&techniques_[i]);
                break;
            }
        }
    }

    Effect::~Effect()
    {
        Dispose(false);
    }

    void Effect::Dispose(bool disposing)
    {
        if (!isDisposed_)
        {
            if (device_ != nullptr) device_->ClearCurrentEffectIf(this);
            compiledRuntime_.reset();
        }
        GraphicsResource::Dispose(disposing);
    }

    GraphicsDevice& Effect::getGraphicsDeviceInternal() const { return *device_; }

    EffectTechnique* Effect::getCurrentTechniqueProperty() const { return currentTechnique_; }

    void Effect::setCurrentTechniqueProperty(EffectTechnique* value)
    {
        currentTechnique_ = value;
        if (compiledRuntime_ && value != nullptr)
        {
            for (int i = 0; i < techniques_.getCountProperty(); ++i)
            {
                if (&techniques_[i] == value)
                {
                    compiledRuntime_->SetTechnique(value->getIndexInternal());
                    break;
                }
            }
        }
    }

    EffectParameterCollection& Effect::getParametersProperty() { return parameters_; }
    const EffectParameterCollection& Effect::getParametersProperty() const { return parameters_; }

    EffectTechniqueCollection& Effect::getTechniquesProperty() { return techniques_; }
    const EffectTechniqueCollection& Effect::getTechniquesProperty() const { return techniques_; }

    void Effect::Apply()
    {
        ApplyPassInternal(0);
    }

    void Effect::ApplyPassInternal(std::uint32_t passIndex)
    {
        if (isDisposed_)
            throw System::ObjectDisposedException(getNameProperty());
        OnApply();

        if (compiledRuntime_)
        {
            if (currentTechnique_ == nullptr)
            {
                throw System::InvalidOperationException(
                    "A compiled effect cannot be applied with a null CurrentTechnique.");
            }
            const auto& passes = currentTechnique_->getPassesProperty();
            if (passIndex >= static_cast<std::uint32_t>(passes.getCountProperty()))
            {
                throw System::InvalidOperationException(
                    "The compiled effect pass index is outside the current technique.");
            }
            SyncCompiledParameters();
            compiledRuntime_->SetTechnique(currentTechnique_->getIndexInternal());
            ApplyCompiledPassState(passIndex);
        }
        if (device_) device_->SetCurrentEffect(this);
    }

    void Effect::ApplyCompiledPassState(std::uint32_t passIndex)
    {
        CNA::Internal::Renderers::CompiledEffectDeviceState deviceState;
        CNA::Internal::Renderers::CompiledEffectPassStateChanges changes;
        if (device_ != nullptr)
        {
            deviceState.blend = &device_->getBlendStateProperty();
            deviceState.depthStencil = &device_->getDepthStencilStateProperty();
            deviceState.rasterizer = &device_->getRasterizerStateProperty();
            deviceState.samplerStates = &device_->getSamplerStatesProperty();
            deviceState.vertexSamplerStates = &device_->getVertexSamplerStatesProperty();
        }
        compiledRuntime_->ApplyPass(passIndex, deviceState, changes);
        if (device_ == nullptr) return;

        // XNA/FNA publish every state a compiled pass assigns through the device's own state
        // objects and collections, so a game observes them exactly as if it had assigned them.
        if (changes.blendChanged) device_->setBlendStateProperty(changes.blend);
        if (changes.depthStencilChanged)
            device_->setDepthStencilStateProperty(changes.depthStencil);
        if (changes.rasterizerChanged)
            device_->setRasterizerStateProperty(changes.rasterizer);
        for (const auto& sampler : changes.samplers)
        {
            const int slot = static_cast<int>(sampler.slot);
            if (sampler.samplerChanged)
            {
                if (sampler.vertexStage)
                    device_->getVertexSamplerStatesProperty()[slot] = sampler.sampler;
                else
                    device_->getSamplerStatesProperty()[slot] = sampler.sampler;
            }
            if (sampler.textureChanged)
            {
                if (sampler.vertexStage)
                    device_->getVertexTexturesProperty()(slot, sampler.texture);
                else
                    device_->getTexturesProperty()(slot, sampler.texture);
            }
        }
    }

    void Effect::FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const
    {
        params.compiledEffectRuntime = compiledRuntime_.get();
    }

    const std::string& Effect::GetVertexSource() const
    {
        static const std::string empty;
        return empty;
    }

    const std::string& Effect::GetFragmentSource() const
    {
        static const std::string empty;
        return empty;
    }

    CNA::Internal::Renderers::IEffectRenderer* Effect::GetEffectRendererPtr() const
    {
        return nullptr;
    }

    Effect* Effect::Clone()
    {
        if (!compiledRuntime_)
        {
            return new Effect(*device_);
        }
        return new Effect(*device_, compiledRuntime_->Clone(), this);
    }

    void Effect::OnApply()
    {
    }

    EffectParameter Effect::BuildCompiledParameter(
        const CNA::Internal::Renderers::CompiledEffectParameterDescription& description)
    {
        auto storage = std::make_shared<EffectParameter::CompiledStorage>();
        storage->bytes = description.rawValue;
        storage->stringValue = description.stringValue;
        storage->runtimeIndex = description.runtimeIndex;

        std::function<EffectParameter(
            const CNA::Internal::Renderers::CompiledEffectParameterDescription&,
            std::size_t, std::size_t, bool)> build;

        build = [&](const CNA::Internal::Renderers::CompiledEffectParameterDescription& desc,
                    std::size_t offset, std::size_t size, bool includeArrayElements)
        {
            const std::size_t available = offset <= storage->bytes.size()
                ? storage->bytes.size() - offset : 0;
            EffectParameter parameter(
                desc.name, desc.semantic, desc.rowCount, desc.columnCount,
                includeArrayElements ? desc.elementCount : 0,
                desc.parameterClass, desc.parameterType, storage,
                std::min(offset, storage->bytes.size()), std::min(size, available));

            for (const auto& annotation : desc.annotations)
            {
                parameter.annotations_.Add(MakeAnnotation(annotation));
            }

            std::size_t memberOffset = offset;
            for (const auto& member : desc.structureMembers)
            {
                const std::size_t memberBytes = ReflectedParameterStorageBytes(member);
                parameter.members_->Add(build(member, memberOffset, memberBytes, true));
                memberOffset += memberBytes;
            }

            if (includeArrayElements && desc.elementCount > 0)
            {
                const std::size_t elementStride = size /
                    static_cast<std::size_t>(desc.elementCount);
                for (int i = 0; i < desc.elementCount; ++i)
                {
                    auto elementDescription = desc;
                    elementDescription.name.clear();
                    elementDescription.semantic.clear();
                    elementDescription.elementCount = 0;
                    elementDescription.annotations.clear();
                    const std::size_t elementOffset = offset +
                        static_cast<std::size_t>(i) * elementStride;
                    parameter.elements_->Add(build(elementDescription, elementOffset,
                                                   elementStride, false));
                }
            }
            return parameter;
        };

        return build(description, 0, storage->bytes.size(), true);
    }

    void Effect::BuildCompiledObjectGraph()
    {
        const auto& description = compiledRuntime_->GetDescription();
        ValidateCompiledDescription(description);

        for (const auto& parameter : description.parameters)
        {
            parameters_.Add(BuildCompiledParameter(parameter));
        }

        for (std::size_t techniqueIndex = 0;
             techniqueIndex < description.techniques.size(); ++techniqueIndex)
        {
            const auto& reflectedTechnique = description.techniques[techniqueIndex];
            EffectTechnique technique(this, reflectedTechnique.name,
                                      static_cast<std::uint32_t>(techniqueIndex), false);
            for (const auto& annotation : reflectedTechnique.annotations)
            {
                technique.getAnnotationsProperty().Add(MakeAnnotation(annotation));
            }
            for (std::size_t passIndex = 0;
                 passIndex < reflectedTechnique.passes.size(); ++passIndex)
            {
                const auto& reflectedPass = reflectedTechnique.passes[passIndex];
                EffectPass pass(this, reflectedPass.name, technique.getIdInternal(),
                                static_cast<std::uint32_t>(passIndex));
                for (const auto& annotation : reflectedPass.annotations)
                {
                    pass.getAnnotationsProperty().Add(MakeAnnotation(annotation));
                }
                technique.getPassesProperty().Add(std::move(pass));
            }
            techniques_.Add(std::move(technique));
        }

        currentTechnique_ = &techniques_[0];
        compiledRuntime_->SetTechnique(0);
    }

    void Effect::SyncCompiledParameters()
    {
        for (auto& parameter : parameters_)
        {
            if (!parameter.IsDirtyInternal()) continue;

            switch (parameter.getParameterTypeProperty())
            {
                case EffectParameterType::Texture:
                case EffectParameterType::Texture1D:
                case EffectParameterType::Texture2D:
                case EffectParameterType::Texture3D:
                case EffectParameterType::TextureCube:
                    compiledRuntime_->SetParameterTexture(
                        parameter.GetRuntimeIndexInternal(), parameter.GetTextureInternal());
                    break;
                case EffectParameterType::String:
                    // A string object is pure CPU reflection data: no shader stage reads one, and
                    // the value storage behind it is the effect's object-table index, not text.
                    // Uploading it would overwrite that index with the string's own bytes. CNA
                    // owns the current value per Effect instance (EffectParameter::SetValue), so
                    // nothing has to reach the renderer runtime here.
                    break;
                default:
                    compiledRuntime_->SetParameterValue(
                        parameter.GetRuntimeIndexInternal(), parameter.GetRawValueInternal(),
                        parameter.GetRawValueSizeInternal());
                    break;
            }
            parameter.MarkCleanInternal();
        }
    }

    const std::string& Effect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.Effect";
        return name;
    }
}
