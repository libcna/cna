// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "../Internal/StockCloneCubeAccess.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

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

    bool IsValidRenderStateType(std::uint32_t value)
    {
        // MojoShader's Effect ABI numbers the ordinary D3D9 render states contiguously from
        // ZENABLE through BLENDOPALPHA, then uses six values in the gap for the legacy texture-
        // stage bump matrix/luminance assignments before its two shader pseudo-states. The legacy
        // compiler also emits 160 for PixelShaderConstant assignments and 178 for SetSampler
        // metadata. They must remain distinguishable from ordinary enum values even when a
        // backend cannot execute the corresponding Shader Model 1 assignment.
        return value <= 102u || (value >= 112u && value <= 115u) || value == 117u ||
            value == 118u || value == 146u || value == 147u || value == 160u ||
            value == 178u;
    }

    // Framework Helpers.CheckDisposed IL_000d-0018 throws ObjectDisposedException(obj.GetType().Name):
    // the effect's runtime type, never its Name.
    std::string DisposedEffectName(const Microsoft::Xna::Framework::Graphics::Effect& effect)
    {
        const std::string& qualified = effect.GetTypeName();
        return qualified.substr(qualified.find_last_of('.') + 1);
    }

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
                        std::uint32_t rawStateType = 0;
                        std::uint32_t legacy = 0;
                        std::uint32_t stateTypeOffset = 0;
                        std::uint32_t stateValueOffset = 0;
                        if (!Read(cursor, rawStateType) ||
                            !IsValidRenderStateType(rawStateType) ||
                            !Read(cursor, legacy) ||
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
                smallObjectCount > (bytes_.size() - cursor) / 8u)
            {
                return false;
            }
            for (std::uint32_t i = 0; i < smallObjectCount; ++i)
            {
                std::uint32_t objectIndex = 0;
                std::uint32_t length = 0;
                if (!Read(cursor, objectIndex) || !Read(cursor, length) ||
                    objectIndex >= objectTypes_.size() || objectTypes_[objectIndex] < kStringType ||
                    objectTypes_[objectIndex] > kVertexShaderType ||
                    !ValidateObjectBlob(cursor, length,
                        objectTypes_[objectIndex] <= kSamplerCubeType))
                {
                    return false;
                }
            }
            if (largeObjectCount > (bytes_.size() - cursor) / 24u)
            {
                return false;
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
                if (objectIndex && *objectIndex >= objectTypes_.size())
                {
                    return false;
                }
                const int objectType = objectIndex ? objectTypes_[*objectIndex] : -1;
                if (objectType > static_cast<int>(kVertexShaderType) ||
                    (objectType >= 0 && objectType < static_cast<int>(kTextureType)) ||
                    (objectEncoding == 2 &&
                     objectType >= static_cast<int>(kPixelShaderType) &&
                     !ValidateStandalonePreshaderHeader(cursor, length)) ||
                    !ValidateObjectBlob(
                        cursor, length,
                        objectType >= static_cast<int>(kTextureType) &&
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
            if (knownType != -1 && knownType != static_cast<int>(type))
            {
                // Effect Texture states may use generic TEXTURE while the same object is a
                // typed texture parameter. Retain the specific type; incompatible types still fail.
                const bool knownTexture = knownType >= static_cast<int>(kTextureType) &&
                                          knownType <= static_cast<int>(kTextureCubeType);
                const bool newTexture = type >= kTextureType && type <= kTextureCubeType;
                if (!knownTexture || !newTexture ||
                    (knownType != static_cast<int>(kTextureType) && type != kTextureType))
                    return false;
                if (type == kTextureType) return true;
            }
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
                if (cells > kMaximumCompiledEffectBytes / 4 ||
                    cells * 4 > bytes_.size() - valueCursor ||
                    !ConsumeAllocation(reflectedStorageBytes))
                {
                    return false;
                }
                result.firstObjectReference =
                    ReadUInt32LittleEndian(bytes_, valueCursor);
                return true;
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
                        if (i == 0) result.firstObjectReference = stateType;
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

namespace CNA::Internal::Graphics
{
    /**
     * HND-TQ-25 D9: the raw parameter values a base-Effect or EffectMaterial clone of a stock
     * effect hands the cloned stock shaders. XNA latches them when the pass begins
     * (EffectPass.Apply IL_003b OnApply, then the native BeginPass IL_009f), so a draw uses what
     * the clone's last Apply read, never a value written after it.
     */
    struct StockCloneParameterSnapshot
    {
        int shaderIndex = 0;
        Microsoft::Xna::Framework::Vector4 diffuseColor{0.0f, 0.0f, 0.0f, 1.0f};
        Microsoft::Xna::Framework::Vector3 emissiveColor{0.0f, 0.0f, 0.0f};
        Microsoft::Xna::Framework::Vector3 specularColor{0.0f, 0.0f, 0.0f};
        float specularPower = 0.0f;
        Microsoft::Xna::Framework::Vector3 lightDirection[3]{};
        Microsoft::Xna::Framework::Vector3 lightDiffuseColor[3]{};
        Microsoft::Xna::Framework::Vector3 lightSpecularColor[3]{};
        Microsoft::Xna::Framework::Vector3 eyePosition{0.0f, 0.0f, 0.0f};
        Microsoft::Xna::Framework::Vector3 fogColor{0.0f, 0.0f, 0.0f};
        Microsoft::Xna::Framework::Vector4 fogVector{0.0f, 0.0f, 0.0f, 0.0f};
        Microsoft::Xna::Framework::Vector4 alphaTest{0.0f, 0.0f, 0.0f, 0.0f};
        Microsoft::Xna::Framework::Vector3 environmentMapSpecular{0.0f, 0.0f, 0.0f};
        float environmentMapAmount = 0.0f;
        float fresnelFactor = 0.0f;
        Microsoft::Xna::Framework::Matrix world = Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        Microsoft::Xna::Framework::Matrix worldInverseTranspose =
            Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        Microsoft::Xna::Framework::Matrix worldViewProj =
            Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        std::vector<Microsoft::Xna::Framework::Matrix> bones;
        // Sampler s0 (Texture) and s1 (DualTextureEffect's Texture2, EnvironmentMapEffect's
        // EnvironmentMap).
        Microsoft::Xna::Framework::Graphics::Texture* texture0 = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture* texture1 = nullptr;
    };
}

namespace Microsoft::Xna::Framework::Graphics
{
    Effect::Effect(GraphicsDevice& device)
        : GraphicsResource(&device)
        , device_(&device)
    {
        techniques_.Add(EffectTechnique(this, "Default"));
        currentTechnique_ = techniques_[0];
    }

    Effect::Effect(GraphicsDevice& device, const std::string& stockTechniqueName)
        : GraphicsResource(&device)
        , device_(&device)
    {
        // Each embedded stock effect has one technique holding one pass whose name string is
        // empty; EffectPass::.ctor IL_004a-005a copies it with PtrToStringAnsi, so the pass is
        // named "" rather than the "P0" a bare Effect's default technique gets (TQ-041).
        EffectTechnique technique(this, stockTechniqueName, 0, false);
        technique.getPassesProperty().Add(EffectPass(this, "", technique.getIdInternal(), 0));
        techniques_.Add(std::move(technique));
        currentTechnique_ = techniques_[0];
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

        try
        {
            compiledRuntime_ = device.GetRenderer().CreateCompiledEffect(
                reinterpret_cast<const std::uint8_t*>(effectCode.data()), effectCode.size());
        }
        catch (const std::runtime_error& error)
        {
            // Refresh Effect::.ctor(GraphicsDevice, byte[]) IL_0009 delegates to
            // CreateEffectFromCode: invalid code throws InvalidOperationException at
            // IL_0208-020d; native failures use GraphicsHelpers at IL_0128-012d
            // (Helpers.GetExceptionFromResult default IL_0252-025c). Translate every
            // renderer's native creation refusal here, preserving its diagnostic.
            throw System::InvalidOperationException(error.what());
        }
        if (!compiledRuntime_)
        {
            throw System::InvalidOperationException(
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
        cloneSource.ThrowIfDisposedForCloneInternal();
        if (!cloneSource.compiledRuntime_)
        {
            // A stock effect, or one built by the device-only constructor, has no renderer runtime
            // to clone, but XNA's clone keeps everything regardless: Effect::.ctor(Effect)
            // IL_0048-0085 clones the native ID3DXEffect -- techniques, passes, parameters and
            // their current values -- IL_00b8 reflects that copy and IL_00d0-016f copy the numeric
            // arrays again. Rebuild the same graph from the source's own reflection, with every
            // value in storage of the clone's own (TQ-035).
            CopyUncompiledObjectGraphInternal(cloneSource);
            // The cloned native effect is the stock effect's own, so the clone keeps drawing with
            // its shaders -- whatever type the clone is, and through a clone of a clone too
            // (HND-TQ-25 D9).
            const StockEffectKindInternal sourceKind = cloneSource.GetStockEffectKindInternal();
            stockCloneKind_ = sourceKind != StockEffectKindInternal::None
                ? sourceKind : cloneSource.stockCloneKind_;
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
            parameters_[i]->CopyMutableValueFromInternal(*cloneSource.parameters_[i]);
        }

        // XNA Effect..ctor(Effect) IL_00b8..00cb selects Techniques[0], even if
        // the source selected another technique. BuildCompiledObjectGraph already did so.
    }

    void Effect::CopyUncompiledObjectGraphInternal(const Effect& source)
    {
        const auto copyAnnotations = [](const EffectAnnotationCollection& from,
                                        EffectAnnotationCollection& to)
        {
            for (int i = 0; i < from.getCountProperty(); ++i)
                to.Add(*from[i]);
        };

        for (int i = 0; i < source.parameters_.getCountProperty(); ++i)
            parameters_.Add(CloneUncompiledParameterInternal(*source.parameters_[i]));

        for (int t = 0; t < source.techniques_.getCountProperty(); ++t)
        {
            const EffectTechnique& sourceTechnique = *source.techniques_[t];
            EffectTechnique technique(this, sourceTechnique.getNameProperty(),
                                      static_cast<std::uint32_t>(t), false);
            copyAnnotations(sourceTechnique.getAnnotationsProperty(),
                            technique.getAnnotationsProperty());
            const EffectPassCollection& sourcePasses = sourceTechnique.getPassesProperty();
            for (int p = 0; p < sourcePasses.getCountProperty(); ++p)
            {
                EffectPass pass(this, sourcePasses[p]->getNameProperty(), technique.getIdInternal(),
                                static_cast<std::uint32_t>(p));
                copyAnnotations(sourcePasses[p]->getAnnotationsProperty(),
                                pass.getAnnotationsProperty());
                technique.getPassesProperty().Add(std::move(pass));
            }
            techniques_.Add(std::move(technique));
        }

        // Effect::.ctor(Effect) IL_00be-00cb selects Techniques[0], whatever the source selected.
        currentTechnique_ = techniques_.getCountProperty() > 0 ? techniques_[0] : nullptr;
    }

    EffectParameter Effect::CloneUncompiledParameterInternal(const EffectParameter& source)
    {
        if (source.compiledStorage_)
        {
            // A stock matrix array (SkinnedEffect's Bones) keeps its matrices in one byte store
            // shared with its element views; the clone gets a store and views of its own.
            CNA::Internal::Renderers::CompiledEffectParameterDescription description;
            description.name = source.name_;
            description.semantic = source.semantic_;
            description.rowCount = source.rowCount_;
            description.columnCount = source.columnCount_;
            description.elementCount = source.elementCount_;
            description.parameterClass = source.paramClass_;
            description.parameterType = source.paramType_;
            const auto& bytes = source.compiledStorage_->bytes;
            const std::size_t first = std::min(source.compiledByteOffset_, bytes.size());
            const std::size_t count = std::min(source.compiledByteSize_, bytes.size() - first);
            description.rawValue.assign(bytes.begin() + static_cast<std::ptrdiff_t>(first),
                                        bytes.begin() + static_cast<std::ptrdiff_t>(first + count));
            description.stringValue = source.compiledStorage_->stringValue;
            EffectParameter clone = BuildCompiledParameter(description);
            clone.compiledStorage_->stockMatrixLayout = source.compiledStorage_->stockMatrixLayout;
            clone.compiledStorage_->texture = source.compiledStorage_->texture;
            clone.stockParameter_ = source.stockParameter_;
            for (int i = 0; i < source.annotations_.getCountProperty(); ++i)
                clone.annotations_.Add(*source.annotations_[i]);
            return clone;
        }

        EffectParameter clone(source.name_, source.semantic_, source.rowCount_, source.columnCount_,
                              source.paramClass_, source.paramType_);
        clone.elementCount_ = source.elementCount_;
        clone.stockParameter_ = source.stockParameter_;
        clone.floatData_ = source.floatData_;
        clone.intData_ = source.intData_;
        clone.stringData_ = source.stringData_;
        clone.textureData_ = source.textureData_;
        clone.texture2DData_ = source.texture2DData_;
        clone.texture3DData_ = source.texture3DData_;
        clone.textureCubeData_ = source.textureCubeData_;
        for (int i = 0; i < source.elements_->getCountProperty(); ++i)
            clone.elements_->Add(CloneUncompiledParameterInternal(*(*source.elements_)[i]));
        for (int i = 0; i < source.members_->getCountProperty(); ++i)
            clone.members_->Add(CloneUncompiledParameterInternal(*(*source.members_)[i]));
        for (int i = 0; i < source.annotations_.getCountProperty(); ++i)
            clone.annotations_.Add(*source.annotations_[i]);
        return clone;
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

    bool Effect::RaisesDisposingEventInternal() const noexcept
    {
        // Effect::Dispose(bool) IL_0003-000d: '~Effect' -> '!Effect' publishes isDisposed before
        // GraphicsResource.Dispose(true) runs, so XNA never raises Disposing for an effect.
        return false;
    }

    GraphicsDevice& Effect::getGraphicsDeviceInternal() const { return *device_; }

    void Effect::ThrowIfDisposedForCloneInternal() const
    {
        if (isDisposed_)
            throw System::ObjectDisposedException(DisposedEffectName(*this));
    }

    void Effect::BindStockTextureInternal(int slot, Texture* texture)
    {
        if (device_ != nullptr)
            device_->getTexturesProperty()(slot, texture);
    }

    Texture* Effect::SampledStockTextureInternal(int slot, Texture* own) const
    {
        if (device_ == nullptr || device_->currentEffect_ != this)
            return own;
        return device_->getTexturesProperty()[slot];
    }

    EffectTechnique* Effect::getCurrentTechniqueProperty() const { return currentTechnique_; }

    void Effect::setCurrentTechniqueProperty(EffectTechnique* value)
    {
        if (isDisposed_)
            throw System::ObjectDisposedException(DisposedEffectName(*this));
        if (value == nullptr)
            throw System::ArgumentNullException("value");
        if (value->owner_ != this)
            throw System::InvalidOperationException();
        if (value == currentTechnique_)
            return;

        if (compiledRuntime_)
        {
            compiledRuntime_->SetTechnique(value->getIndexInternal());
        }
        currentTechnique_ = value;
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
            throw System::ObjectDisposedException(DisposedEffectName(*this));
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
        else if (IsStockCloneInternal())
        {
            ApplyStockClonePassInternal();
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
        if (compiledRuntime_ != nullptr && device_ != nullptr)
        {
            params.compiledDeviceTextures = &device_->getTexturesProperty();
            params.compiledDeviceSamplerStates = &device_->getSamplerStatesProperty();
            params.compiledDeviceVertexTextures = &device_->getVertexTexturesProperty();
            params.compiledDeviceVertexSamplerStates =
                &device_->getVertexSamplerStatesProperty();
        }
        if (compiledRuntime_ == nullptr && IsStockCloneInternal())
            FillStockCloneDrawParamsInternal(params);
    }

    // HND-TQ-25 D9. XNA's Effect::.ctor(Effect) IL_0048-0085 clones the native stock effect, so a
    // base-Effect or EffectMaterial clone of BasicEffect and its siblings runs the very same
    // stock shader bytecode. Its type is not the stock class, so EffectPass.Apply IL_003b calls
    // the clone's own OnApply -- Effect's, which does nothing -- and the stock class's dirty-flag
    // derivation (WorldViewProj, FogVector, EmissiveColor, ShaderIndex, ...) never runs: the
    // shaders read whatever raw values the clone's parameters hold. The technique's single pass
    // selects its shaders with VSArray[VSIndices[ShaderIndex]] / PSArray[PSIndices[ShaderIndex]]
    // (the stock .fx files), and the native BeginPass (Apply IL_009f) binds the samplers those
    // shaders read and latches the constants.
    CNA::Internal::Graphics::StockCloneParameterSnapshot Effect::ReadStockCloneParametersInternal() const
    {
        CNA::Internal::Graphics::StockCloneParameterSnapshot snapshot;
        const auto find = [this](const char* name) -> const EffectParameter*
        {
            return parameters_[std::string(name)];
        };
        const auto vector3 = [&](const char* name, Vector3& out)
        {
            if (const EffectParameter* parameter = find(name)) out = parameter->GetValueVector3();
        };
        const auto vector4 = [&](const char* name, Vector4& out)
        {
            if (const EffectParameter* parameter = find(name)) out = parameter->GetValueVector4();
        };
        const auto single = [&](const char* name, float& out)
        {
            if (const EffectParameter* parameter = find(name)) out = parameter->GetValueSingle();
        };
        const auto matrix = [&](const char* name, Matrix& out)
        {
            if (const EffectParameter* parameter = find(name)) out = parameter->GetValueMatrix();
        };

        // The number of entries in each stock effect's VSIndices/PSIndices tables.
        int shaderCount = 0;
        switch (stockCloneKind_)
        {
            case StockEffectKindInternal::Basic:          shaderCount = 32; break;
            case StockEffectKindInternal::AlphaTest:      shaderCount = 8;  break;
            case StockEffectKindInternal::DualTexture:    shaderCount = 4;  break;
            case StockEffectKindInternal::EnvironmentMap: shaderCount = 16; break;
            case StockEffectKindInternal::Skinned:        shaderCount = 18; break;
            case StockEffectKindInternal::Sprite:         shaderCount = 1;  break;
            case StockEffectKindInternal::None:           break;
        }
        if (const EffectParameter* parameter = find("ShaderIndex"))
            snapshot.shaderIndex = parameter->GetValueInt32();
        if (snapshot.shaderIndex < 0 || snapshot.shaderIndex >= shaderCount)
        {
            // What the native effect does with an index outside its shader tables is not
            // established; CNA refuses it rather than drawing some variant (HND-TQ-25 D9).
            throw System::InvalidOperationException(
                "The ShaderIndex parameter value " + std::to_string(snapshot.shaderIndex) +
                " selects none of the " + std::to_string(shaderCount) +
                " shaders of the stock effect this effect was cloned from.");
        }

        vector4("DiffuseColor", snapshot.diffuseColor);
        vector3("EmissiveColor", snapshot.emissiveColor);
        vector3("SpecularColor", snapshot.specularColor);
        single("SpecularPower", snapshot.specularPower);
        static constexpr const char* lightNames[3][3] = {
            {"DirLight0Direction", "DirLight0DiffuseColor", "DirLight0SpecularColor"},
            {"DirLight1Direction", "DirLight1DiffuseColor", "DirLight1SpecularColor"},
            {"DirLight2Direction", "DirLight2DiffuseColor", "DirLight2SpecularColor"}};
        for (int light = 0; light < 3; ++light)
        {
            vector3(lightNames[light][0], snapshot.lightDirection[light]);
            vector3(lightNames[light][1], snapshot.lightDiffuseColor[light]);
            vector3(lightNames[light][2], snapshot.lightSpecularColor[light]);
        }
        vector3("EyePosition", snapshot.eyePosition);
        vector3("FogColor", snapshot.fogColor);
        vector4("FogVector", snapshot.fogVector);
        vector4("AlphaTest", snapshot.alphaTest);
        vector3("EnvironmentMapSpecular", snapshot.environmentMapSpecular);
        single("EnvironmentMapAmount", snapshot.environmentMapAmount);
        single("FresnelFactor", snapshot.fresnelFactor);
        matrix("World", snapshot.world);
        matrix("WorldInverseTranspose", snapshot.worldInverseTranspose);
        matrix("WorldViewProj", snapshot.worldViewProj);
        // FNA's SpriteEffect.fx: the sprite vertex shader's one matrix is MatrixTransform.
        if (stockCloneKind_ == StockEffectKindInternal::Sprite)
            matrix("MatrixTransform", snapshot.worldViewProj);
        if (const EffectParameter* bones = find("Bones"))
        {
            // SkinnedEffect's float4x3 Bones has no fourth column; the affine bone has M44 = 1, as
            // SkinnedEffect::GetBoneTransforms reads it.
            snapshot.bones = bones->GetValueMatrixArray(bones->getElementsProperty().getCountProperty());
            for (Matrix& bone : snapshot.bones)
                bone.M44 = 1.0f;
        }
        if (const EffectParameter* texture = find("Texture"))
            snapshot.texture0 = texture->GetValueTexture2D();
        if (const EffectParameter* texture = find("Texture2"))
            snapshot.texture1 = texture->GetValueTexture2D();
        else if (const EffectParameter* environmentMap = find("EnvironmentMap"))
        {
            if (const auto* cubes = CNA::Internal::Graphics::stockCloneCubeAccess.load())
                snapshot.texture1 = cubes->environmentMap(*environmentMap);
        }
        return snapshot;
    }

    void Effect::ApplyStockClonePassInternal()
    {
        auto snapshot = std::make_unique<CNA::Internal::Graphics::StockCloneParameterSnapshot>(
            ReadStockCloneParametersInternal());
        // The native pass binds the samplers its two shaders read, as each stock class's OnApply
        // does in CNA: BasicEffect's textured variants (ShaderIndex bit 4) read s0, every
        // AlphaTestEffect and SkinnedEffect variant reads s0, and every DualTextureEffect and
        // EnvironmentMapEffect variant reads s0 and s1.
        switch (stockCloneKind_)
        {
            case StockEffectKindInternal::Basic:
                if ((snapshot->shaderIndex & 4) != 0)
                    BindStockTextureInternal(0, snapshot->texture0);
                break;
            case StockEffectKindInternal::AlphaTest:
            case StockEffectKindInternal::Skinned:
                BindStockTextureInternal(0, snapshot->texture0);
                break;
            case StockEffectKindInternal::DualTexture:
            case StockEffectKindInternal::EnvironmentMap:
                BindStockTextureInternal(0, snapshot->texture0);
                BindStockTextureInternal(1, snapshot->texture1);
                break;
            case StockEffectKindInternal::Sprite:
                // CNA's SpriteEffect has no Texture parameter, so the sprite pixel shader samples
                // whatever s0 holds: SpriteBatch's texture, or the device's.
            case StockEffectKindInternal::None:
                break;
        }
        stockCloneApplied_ = std::move(snapshot);
    }

    void Effect::FillStockCloneDrawParamsInternal(CNA::Internal::Renderers::GpuDrawParams& p) const
    {
        // A draw selects an effect only through its Apply, so a captured snapshot is what draws;
        // a caller reading the parameters of a never-applied clone gets its current values.
        std::optional<CNA::Internal::Graphics::StockCloneParameterSnapshot> current;
        const CNA::Internal::Graphics::StockCloneParameterSnapshot* s = stockCloneApplied_.get();
        if (s == nullptr)
        {
            current.emplace(ReadStockCloneParametersInternal());
            s = &*current;
        }
        const int index = s->shaderIndex;

        const auto copy3 = [](float (&out)[3], const Vector3& value)
        {
            out[0] = value.X; out[1] = value.Y; out[2] = value.Z;
        };
        const auto copy4 = [](float (&out)[4], const Vector4& value)
        {
            out[0] = value.X; out[1] = value.Y; out[2] = value.Z; out[3] = value.W;
        };
        const auto sampled2D = [this](int slot, Texture* own)
            -> const CNA::Internal::Renderers::ITextureRenderer*
        {
            auto* texture = dynamic_cast<Texture2D*>(SampledStockTextureInternal(slot, own));
            return texture != nullptr ? &texture->GetRenderer() : nullptr;
        };
        // Lighting.fxh ComputeLights(..., numLights): the one-light vertex shaders read DirLight0
        // only, the others all three. A light the shader does not read contributes nothing,
        // which is what a renderer that always sums three lights computes for a black one.
        const auto lights = [&](bool oneLight, bool withSpecular)
        {
            float (*const directions[3])[3] = {&p.light0Dir, &p.light1Dir, &p.light2Dir};
            float (*const diffuse[3])[3] = {&p.light0Diffuse, &p.light1Diffuse, &p.light2Diffuse};
            float (*const specular[3])[3] = {&p.light0Specular, &p.light1Specular, &p.light2Specular};
            for (int light = 0; light < 3; ++light)
            {
                const bool read = light == 0 || !oneLight;
                copy3(*directions[light], s->lightDirection[light]);
                copy3(*diffuse[light], read ? s->lightDiffuseColor[light] : Vector3::Zero);
                copy3(*specular[light],
                      read && withSpecular ? s->lightSpecularColor[light] : Vector3::Zero);
            }
        };

        // The stock vertex shaders place a vertex with WorldViewProj, light it in World space with
        // World and WorldInverseTranspose, and fog it with FogVector -- three raw values here.
        p.rawStockTransforms = true;
        s->world.ToColumnMajor(p.worldColMajor);
        const Matrix& wit = s->worldInverseTranspose;
        const float witRows[9] = {wit.M11, wit.M12, wit.M13,
                                  wit.M21, wit.M22, wit.M23,
                                  wit.M31, wit.M32, wit.M33};
        std::copy(std::begin(witRows), std::end(witRows), p.worldInverseTransposeColMajor);
        copy4(p.diffuseColor, s->diffuseColor);
        copy3(p.fogColor, s->fogColor);
        copy4(p.fogVector, s->fogVector);
        // A lit stock shader adds EmissiveColor, which the stock OnApply had already made
        // (emissive + ambient * diffuse) * alpha (EffectHelpers.SetMaterialColor): the renderers'
        // separate ambient term stays zero, so the raw EmissiveColor is the whole constant term.
        p.ambientColor[0] = p.ambientColor[1] = p.ambientColor[2] = 0.0f;

        switch (stockCloneKind_)
        {
            case StockEffectKindInternal::Basic:
            {
                // ShaderIndex: +1 no fog, +2 vertex colour, +4 texture; 8-15 three lights per vertex,
                // 16-23 one light per vertex, 24-31 per pixel. PSIndices give every odd index below
                // 24 a NoFog pixel shader; PSBasicPixelLighting(Tx) fogs all eight per-pixel ones.
                const int lighting = index / 8;
                p.vertexColorEnabled = (index & 2) != 0;
                p.textureEnabled = (index & 4) != 0;
                p.lightingEnabled = lighting != 0;
                p.preferPerPixelLighting = lighting == 3;
                p.fogEnabled = (index & 1) == 0 || lighting == 3;
                if (p.textureEnabled)
                    p.texture0 = sampled2D(0, s->texture0);
                if (p.lightingEnabled)
                {
                    lights(lighting == 2, true);
                    copy3(p.emissiveColor, s->emissiveColor);
                    copy3(p.specularColor, s->specularColor);
                    p.specularPower = s->specularPower;
                    copy3(p.eyePositionWorld, s->eyePosition);
                }
                break;
            }
            case StockEffectKindInternal::AlphaTest:
            {
                // +1 no fog, +2 vertex colour, +4 the equal/not-equal pixel shader.
                p.alphaTestEffect = true;
                p.textureEnabled = true;
                p.lightingEnabled = false;
                p.vertexColorEnabled = (index & 2) != 0;
                p.fogEnabled = (index & 1) == 0;
                p.texture0 = sampled2D(0, s->texture0);
                copy4(p.alphaTest, s->alphaTest);
                // The renderers pick the equal/not-equal form by AlphaTest.y > 0, where XNA picks it
                // by ShaderIndex. PSAlphaTestLtGt never reads y, so zeroing it keeps the
                // less/greater form exact. PSAlphaTestEqNe's abs(a - x) < y is false for every
                // alpha when y is not positive, so it always takes w -- which the less/greater
                // form gives for x = -FLT_MAX.
                if ((index & 4) == 0)
                {
                    p.alphaTest[1] = 0.0f;
                }
                else if (!(p.alphaTest[1] > 0.0f))
                {
                    p.alphaTest[0] = -std::numeric_limits<float>::max();
                    p.alphaTest[1] = 0.0f;
                }
                break;
            }
            case StockEffectKindInternal::DualTexture:
            {
                // +1 no fog, +2 vertex colour.
                p.dualTexture = true;
                p.textureEnabled = true;
                p.lightingEnabled = false;
                p.vertexColorEnabled = (index & 2) != 0;
                p.fogEnabled = (index & 1) == 0;
                p.texture0 = sampled2D(0, s->texture0);
                p.texture1 = sampled2D(1, s->texture1);
                break;
            }
            case StockEffectKindInternal::EnvironmentMap:
            {
                // +1 no fog, +2 Fresnel, +4 specular, +8 one light.
                p.envMapping = true;
                p.textureEnabled = true;
                p.lightingEnabled = true;
                p.vertexColorEnabled = false;
                p.fogEnabled = (index & 1) == 0;
                p.fresnelEnabled = (index & 2) != 0;
                p.specularEnabled = (index & 4) != 0;
                p.texture0 = sampled2D(0, s->texture0);
                if (const auto* cubes = CNA::Internal::Graphics::stockCloneCubeAccess.load())
                    if (auto* cube = cubes->asCube(SampledStockTextureInternal(1, s->texture1)))
                        p.envMap = cube->GetRendererRaw();
                lights((index & 8) != 0, false);
                copy3(p.emissiveColor, s->emissiveColor);
                copy3(p.envMapSpecular, s->environmentMapSpecular);
                p.envMapAmount = s->environmentMapAmount;
                p.fresnelFactor = s->fresnelFactor;
                copy3(p.eyePositionWorld, s->eyePosition);
                break;
            }
            case StockEffectKindInternal::Skinned:
            {
                // +1 no fog; (index / 2) % 3 picks one, two or four bones; 0-5 three lights per
                // vertex, 6-11 one light per vertex, 12-17 per pixel, whose pixel shader fogs all six.
                static constexpr int weights[3] = {1, 2, 4};
                const int lighting = index / 6;
                p.skinned = true;
                p.textureEnabled = true;
                p.lightingEnabled = true;
                p.vertexColorEnabled = false;
                p.weightsPerVertex = weights[(index / 2) % 3];
                p.preferPerPixelLighting = lighting == 2;
                p.fogEnabled = (index & 1) == 0 || lighting == 2;
                p.texture0 = sampled2D(0, s->texture0);
                lights(lighting == 1, true);
                copy3(p.emissiveColor, s->emissiveColor);
                copy3(p.specularColor, s->specularColor);
                p.specularPower = s->specularPower;
                copy3(p.eyePositionWorld, s->eyePosition);
                const int boneCount = std::min(static_cast<int>(s->bones.size()), 72);
                p.boneCount = boneCount;
                for (int bone = 0; bone < boneCount; ++bone)
                    s->bones[static_cast<std::size_t>(bone)].ToColumnMajor(p.boneTransforms + bone * 16);
                break;
            }
            case StockEffectKindInternal::Sprite:
            {
                // FNA's SpriteEffect.fx: position * MatrixTransform with the colour and texture
                // coordinate passed through, then the s0 texel times that colour -- what the
                // renderers' unlit, unfogged, vertex-coloured textured stock path draws for a white
                // DiffuseColor.
                p.textureEnabled = true;
                p.vertexColorEnabled = true;
                p.lightingEnabled = false;
                p.fogEnabled = false;
                p.diffuseColor[0] = p.diffuseColor[1] = p.diffuseColor[2] = p.diffuseColor[3] = 1.0f;
                p.texture0 = sampled2D(0, nullptr);
                break;
            }
            case StockEffectKindInternal::None:
                break;
        }
        // A NoFog pixel shader never reads the fog factor. Some renderers fog by FogVector alone
        // (a stock class zeroes it when fog is off), so the selected variant's choice reaches them
        // as a zero vector.
        if (!p.fogEnabled)
            p.fogVector[0] = p.fogVector[1] = p.fogVector[2] = p.fogVector[3] = 0.0f;
    }

    bool Effect::GetStockCloneDrawMatricesInternal(Matrix& world, Matrix& view, Matrix& projection) const
    {
        if (!IsStockCloneInternal())
            return false;
        // GpuDrawParams::rawStockTransforms: the renderer's world*view*projection is then exactly
        // the raw WorldViewProj the stock vertex shader multiplies by.
        world = Matrix::getIdentityProperty();
        view = Matrix::getIdentityProperty();
        if (stockCloneApplied_ != nullptr)
        {
            projection = stockCloneApplied_->worldViewProj;
        }
        else
        {
            const EffectParameter* parameter = parameters_[std::string(
                stockCloneKind_ == StockEffectKindInternal::Sprite ? "MatrixTransform" : "WorldViewProj")];
            projection = parameter != nullptr ? parameter->GetValueMatrix() : Matrix::getIdentityProperty();
        }
        return true;
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
        ThrowIfDisposedForCloneInternal();
        if (!compiledRuntime_)
        {
            // Effect.Clone IL_0000-0006 is `new Effect(this)`: the clone constructor above keeps
            // the techniques and every parameter value.
            return new Effect(*this);
        }
        return new Effect(*device_, compiledRuntime_->Clone(), this);
    }

    void Effect::AddStockParameterInternal(EffectParameterCollection& parameters,
        const std::string& name, int rows, int columns,
        EffectParameterClass parameterClass, EffectParameterType parameterType)
    {
        EffectParameter parameter(name, "", rows, columns, parameterClass, parameterType);
        parameter.stockParameter_ = true;
        // The stock .fx constants have no initializers (ShaderIndex is `= 0`), so XNA's embedded
        // effect starts with every one of them zero: until an OnApply or the game writes one, a
        // matrix reads zero rather than identity and a Vector4 (0, 0, 0, 0), and a clone made
        // before its source's first Apply draws with a zero WorldViewProj.
        const std::size_t cells = parameterClass == EffectParameterClass::Matrix ? 16u
            : static_cast<std::size_t>(std::max(rows, 1)) * static_cast<std::size_t>(std::max(columns, 1));
        if (parameterType == EffectParameterType::Single)
            parameter.floatData_.assign(cells, 0.0f);
        else if (parameterType == EffectParameterType::Int32 || parameterType == EffectParameterType::Bool)
            parameter.intData_.assign(cells, 0);
        parameters.Add(std::move(parameter));
    }

    void Effect::OnApply()
    {
    }

    void Effect::AddStockMatrixArrayInternal(const std::string& name,
        int rows, int columns, int count)
    {
        CNA::Internal::Renderers::CompiledEffectParameterDescription description;
        description.name = name;
        description.parameterClass = EffectParameterClass::Matrix;
        description.parameterType = EffectParameterType::Single;
        description.rowCount = rows;
        description.columnCount = columns;
        description.elementCount = count;
        // XNA EffectParameter's D3DX metadata uses matrix rows/columns, whereas
        // the compiled FNA reflection describes column registers/components.
        description.rawValue.resize(static_cast<std::size_t>(columns) * 16 * count);
        auto parameter = BuildCompiledParameter(description);
        parameter.compiledStorage_->stockMatrixLayout = true;
        parameters_.Add(std::move(parameter));
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

            // XNA EffectParameter..ctor IL_00dd..0100 exposes StructureMembers only
            // when Elements is empty; structure-array members belong to each element.
            if (!includeArrayElements || desc.elementCount == 0)
            {
                std::size_t memberOffset = offset;
                for (const auto& member : desc.structureMembers)
                {
                    const std::size_t memberBytes = ReflectedParameterStorageBytes(member);
                    parameter.members_->Add(build(member, memberOffset, memberBytes, true));
                    memberOffset += memberBytes;
                }
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

        currentTechnique_ = techniques_[0];
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
