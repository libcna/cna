// SPDX-License-Identifier: MS-PL
// SKIA-99: isolated CPU vertex/index/primitive feasibility spike.
//
// This models the input-assembly boundary needed by the CPU depth/stencil bridge from SKIA-97/98.
// It deliberately remains test-only: public buffers, Draw* entry points and capabilities reject.

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::FillMode;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTextureSkinned;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    constexpr std::size_t kMaximumBufferBytes = 256u * 1024u * 1024u;
    constexpr int kMaximumStride = 4096;
    constexpr std::size_t kMaximumElements = 32u;

    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }

    template <typename Function>
    [[nodiscard]] bool Throws(Function&& function)
    {
        try
        {
            function();
            return false;
        }
        catch (const std::exception&)
        {
            return true;
        }
    }

    [[nodiscard]] bool Near(float left, float right, float epsilon = 1.0e-5f)
    {
        return std::fabs(left - right) <= epsilon;
    }

    struct FormatInfo
    {
        int components = 0;
        int bytes = 0;
        bool normalized = false;
        bool integer = false;
        enum class Scalar
        {
            Float,
            UnsignedByte,
            SignedShort,
            Half,
        } scalar = Scalar::Float;
    };

    [[nodiscard]] std::optional<FormatInfo> Describe(VertexElementFormat format)
    {
        using Scalar = FormatInfo::Scalar;
        switch (format)
        {
        case VertexElementFormat::Single:           return FormatInfo{1, 4, false, false, Scalar::Float};
        case VertexElementFormat::Vector2:          return FormatInfo{2, 8, false, false, Scalar::Float};
        case VertexElementFormat::Vector3:          return FormatInfo{3, 12, false, false, Scalar::Float};
        case VertexElementFormat::Vector4:          return FormatInfo{4, 16, false, false, Scalar::Float};
        case VertexElementFormat::Color:            return FormatInfo{4, 4, true, false, Scalar::UnsignedByte};
        case VertexElementFormat::Byte4:            return FormatInfo{4, 4, false, true, Scalar::UnsignedByte};
        case VertexElementFormat::Short2:           return FormatInfo{2, 4, false, false, Scalar::SignedShort};
        case VertexElementFormat::Short4:           return FormatInfo{4, 8, false, false, Scalar::SignedShort};
        case VertexElementFormat::NormalizedShort2: return FormatInfo{2, 4, true, false, Scalar::SignedShort};
        case VertexElementFormat::NormalizedShort4: return FormatInfo{4, 8, true, false, Scalar::SignedShort};
        case VertexElementFormat::HalfVector2:      return FormatInfo{2, 4, false, false, Scalar::Half};
        case VertexElementFormat::HalfVector4:      return FormatInfo{4, 8, false, false, Scalar::Half};
        }
        return std::nullopt;
    }

    [[nodiscard]] bool IsKnownUsage(VertexElementUsage usage)
    {
        const int value = static_cast<int>(usage);
        return value >= static_cast<int>(VertexElementUsage::Position) &&
               value <= static_cast<int>(VertexElementUsage::TessellateFactor);
    }

    void ValidateDeclaration(const VertexDeclaration& declaration)
    {
        const int stride = declaration.getVertexStrideProperty();
        const auto& elements = declaration.GetVertexElements();
        if (stride <= 0 || stride > kMaximumStride)
            throw std::invalid_argument("CPU vertex stride must be within 1..4096 bytes");
        if (elements.empty() || elements.size() > kMaximumElements)
            throw std::invalid_argument("CPU vertex declaration must contain 1..32 elements");
        for (const VertexElement& element : elements)
        {
            const auto format = Describe(element.getVertexElementFormatProperty());
            const int offset = element.getOffsetProperty();
            if (!format || offset < 0 || offset > stride - format->bytes)
                throw std::invalid_argument("CPU vertex element is unsupported or outside its stride");
            if (!IsKnownUsage(element.getVertexElementUsageProperty()) ||
                element.getUsageIndexProperty() < 0 || element.getUsageIndexProperty() > 31)
                throw std::invalid_argument("CPU vertex semantic or usage index is invalid");
        }
    }

    [[nodiscard]] float HalfToFloat(std::uint16_t bits)
    {
        const bool negative = (bits & 0x8000u) != 0u;
        const unsigned int exponent = (bits >> 10u) & 0x1fu;
        const unsigned int mantissa = bits & 0x03ffu;
        float value = 0.0f;
        if (exponent == 0u)
            value = std::ldexp(static_cast<float>(mantissa), -24);
        else if (exponent == 0x1fu)
            value = mantissa == 0u ? std::numeric_limits<float>::infinity()
                                   : std::numeric_limits<float>::quiet_NaN();
        else
            value = std::ldexp(static_cast<float>(0x400u + mantissa),
                               static_cast<int>(exponent) - 25);
        return negative ? -value : value;
    }

    template <typename T>
    [[nodiscard]] T Read(const std::uint8_t* source)
    {
        T value{};
        std::memcpy(&value, source, sizeof(T));
        return value;
    }

    struct DecodedAttribute
    {
        VertexElementUsage usage = VertexElementUsage::Position;
        int usageIndex = 0;
        int components = 0;
        bool integer = false;
        std::array<float, 4> values{0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct DecodedVertex
    {
        std::vector<DecodedAttribute> attributes;
    };

    [[nodiscard]] DecodedAttribute DecodeAttribute(
        const std::uint8_t* vertex, const VertexElement& element)
    {
        const FormatInfo format = Describe(element.getVertexElementFormatProperty()).value();
        DecodedAttribute result;
        result.usage = element.getVertexElementUsageProperty();
        result.usageIndex = element.getUsageIndexProperty();
        result.components = format.components;
        result.integer = format.integer;
        const std::uint8_t* source = vertex + element.getOffsetProperty();
        for (int component = 0; component < format.components; ++component)
        {
            switch (format.scalar)
            {
            case FormatInfo::Scalar::Float:
                result.values[component] = Read<float>(source + component * sizeof(float));
                break;
            case FormatInfo::Scalar::UnsignedByte:
                result.values[component] = format.normalized
                    ? static_cast<float>(source[component]) / 255.0f
                    : static_cast<float>(source[component]);
                break;
            case FormatInfo::Scalar::SignedShort:
            {
                const std::int16_t value =
                    Read<std::int16_t>(source + component * sizeof(std::int16_t));
                result.values[component] = format.normalized
                    ? std::max(-1.0f, static_cast<float>(value) / 32767.0f)
                    : static_cast<float>(value);
                break;
            }
            case FormatInfo::Scalar::Half:
                result.values[component] = HalfToFloat(
                    Read<std::uint16_t>(source + component * sizeof(std::uint16_t)));
                break;
            }
        }
        return result;
    }

    [[nodiscard]] std::size_t CheckedBytes(int count, int stride)
    {
        if (count < 0)
            throw std::invalid_argument("CPU buffer element count must be non-negative");
        const std::size_t unsignedCount = static_cast<std::size_t>(count);
        if (unsignedCount > kMaximumBufferBytes / static_cast<std::size_t>(stride))
            throw std::length_error("CPU buffer exceeds the 256 MiB limit");
        return unsignedCount * static_cast<std::size_t>(stride);
    }

    class CpuVertexBuffer
    {
    public:
        CpuVertexBuffer(VertexDeclaration declaration, int capacity)
            : declaration_(std::move(declaration)), capacity_(capacity)
        {
            ValidateDeclaration(declaration_);
            (void)CheckedBytes(capacity_, declaration_.getVertexStrideProperty());
        }

        void Upload(std::span<const std::uint8_t> source, int sourceVertexOffset,
                    int vertexCount, int sourceStride, SetDataOptions /*options*/)
        {
            if (sourceVertexOffset < 0 || vertexCount < 0 || vertexCount > capacity_)
                throw std::out_of_range("CPU vertex upload range is outside logical capacity");
            if (sourceStride != declaration_.getVertexStrideProperty())
                throw std::invalid_argument("CPU vertex upload stride must match its declaration");
            const std::size_t sourceOffset = CheckedBytes(sourceVertexOffset, sourceStride);
            const std::size_t byteCount = CheckedBytes(vertexCount, sourceStride);
            if (sourceOffset > source.size() || byteCount > source.size() - sourceOffset)
                throw std::out_of_range("CPU vertex source range is outside caller storage");

            // CNA's exposed uploads always replace the destination from byte zero. Discard and
            // NoOverwrite are driver synchronization hints, not different logical byte results.
            std::vector<std::uint8_t> replacement(
                source.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                source.begin() + static_cast<std::ptrdiff_t>(sourceOffset + byteCount));
            bytes_.swap(replacement);
            count_ = vertexCount;
        }

        [[nodiscard]] DecodedVertex Decode(int index) const
        {
            if (index < 0 || index >= count_)
                throw std::out_of_range("CPU vertex fetch is outside uploaded vertices");
            const int stride = declaration_.getVertexStrideProperty();
            const std::uint8_t* vertex =
                bytes_.data() + static_cast<std::size_t>(index) * stride;
            DecodedVertex result;
            result.attributes.reserve(declaration_.GetVertexElements().size());
            for (const VertexElement& element : declaration_.GetVertexElements())
                result.attributes.push_back(DecodeAttribute(vertex, element));
            return result;
        }

        [[nodiscard]] int Count() const { return count_; }
        [[nodiscard]] int Capacity() const { return capacity_; }
        [[nodiscard]] const VertexDeclaration& Declaration() const { return declaration_; }
        [[nodiscard]] const std::vector<std::uint8_t>& Bytes() const { return bytes_; }

    private:
        VertexDeclaration declaration_;
        int capacity_ = 0;
        int count_ = 0;
        std::vector<std::uint8_t> bytes_;
    };

    class CpuIndexBuffer
    {
    public:
        CpuIndexBuffer(bool thirtyTwoBit, int capacity)
            : thirtyTwoBit_(thirtyTwoBit), capacity_(capacity)
        {
            (void)CheckedBytes(capacity_, thirtyTwoBit_ ? 4 : 2);
        }

        template <typename Index>
        void Upload(std::span<const Index> source, int sourceIndexOffset, int indexCount,
                    SetDataOptions /*options*/)
        {
            const bool sourceIs32 = sizeof(Index) == sizeof(std::uint32_t);
            if (sourceIs32 != thirtyTwoBit_)
                throw std::invalid_argument("CPU index source width does not match buffer width");
            if (sourceIndexOffset < 0 || indexCount < 0 || indexCount > capacity_ ||
                static_cast<std::size_t>(sourceIndexOffset) > source.size() ||
                static_cast<std::size_t>(indexCount) >
                    source.size() - static_cast<std::size_t>(sourceIndexOffset))
                throw std::out_of_range("CPU index upload range is outside capacity/storage");
            std::vector<std::uint32_t> replacement;
            replacement.reserve(static_cast<std::size_t>(indexCount));
            for (int i = 0; i < indexCount; ++i)
                replacement.push_back(static_cast<std::uint32_t>(source[sourceIndexOffset + i]));
            indices_.swap(replacement);
        }

        [[nodiscard]] std::uint32_t At(int index) const
        {
            if (index < 0 || index >= static_cast<int>(indices_.size()))
                throw std::out_of_range("CPU index fetch is outside uploaded indices");
            return indices_[static_cast<std::size_t>(index)];
        }

        [[nodiscard]] int Count() const { return static_cast<int>(indices_.size()); }
        [[nodiscard]] bool IsThirtyTwoBit() const { return thirtyTwoBit_; }
        [[nodiscard]] const std::vector<std::uint32_t>& Values() const { return indices_; }

    private:
        bool thirtyTwoBit_ = false;
        int capacity_ = 0;
        std::vector<std::uint32_t> indices_;
    };

    [[nodiscard]] int PrimitiveElementCount(PrimitiveType type, int primitiveCount)
    {
        if (primitiveCount <= 0)
            throw std::invalid_argument("CPU primitive count must be positive");
        std::int64_t count = 0;
        switch (type)
        {
        case PrimitiveType::TriangleList:  count = static_cast<std::int64_t>(primitiveCount) * 3; break;
        case PrimitiveType::TriangleStrip: count = static_cast<std::int64_t>(primitiveCount) + 2; break;
        case PrimitiveType::LineList:      count = static_cast<std::int64_t>(primitiveCount) * 2; break;
        case PrimitiveType::LineStrip:     count = static_cast<std::int64_t>(primitiveCount) + 1; break;
        case PrimitiveType::PointListEXT:  count = primitiveCount; break;
        default: throw std::invalid_argument("CPU primitive type is unknown");
        }
        if (count > std::numeric_limits<int>::max())
            throw std::out_of_range("CPU primitive range overflows Int32");
        return static_cast<int>(count);
    }

    enum class RecordKind
    {
        Triangle,
        Line,
        Point,
    };

    struct PrimitiveRecord
    {
        RecordKind kind = RecordKind::Point;
        std::vector<std::uint32_t> indices;
        std::vector<DecodedVertex> vertices;
        bool clockwiseOnScreen = false;
    };

    struct DrawRange
    {
        int vertexStart = 0;
        int startIndex = 0;
        int baseVertex = 0;
        int minVertexIndex = 0;
        int numVertices = 0;
    };

    [[nodiscard]] const DecodedAttribute& Position(const DecodedVertex& vertex)
    {
        for (const DecodedAttribute& attribute : vertex.attributes)
            if (attribute.usage == VertexElementUsage::Position && attribute.usageIndex == 0 &&
                attribute.components >= 2)
                return attribute;
        throw std::invalid_argument("CPU geometry requires POSITION0 with at least two components");
    }

    [[nodiscard]] float SignedScreenArea(const PrimitiveRecord& triangle)
    {
        const auto& a = Position(triangle.vertices[0]).values;
        const auto& b = Position(triangle.vertices[1]).values;
        const auto& c = Position(triangle.vertices[2]).values;
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    }

    class CpuGeometryPipeline
    {
    public:
        [[nodiscard]] std::vector<PrimitiveRecord> Draw(
            const CpuVertexBuffer& vertices, const CpuIndexBuffer* indices,
            PrimitiveType type, int primitiveCount, const DrawRange& range,
            CullMode cullMode, FillMode fillMode) const
        {
            if (cullMode != CullMode::None && cullMode != CullMode::CullClockwiseFace &&
                cullMode != CullMode::CullCounterClockwiseFace)
                throw std::invalid_argument("CPU cull mode is unknown");
            if (fillMode != FillMode::Solid && fillMode != FillMode::WireFrame)
                throw std::invalid_argument("CPU fill mode is unknown");
            const int consumed = PrimitiveElementCount(type, primitiveCount);
            const std::vector<std::uint32_t> sequence =
                FetchSequence(vertices, indices, consumed, range);
            std::vector<PrimitiveRecord> assembled;
            switch (type)
            {
            case PrimitiveType::TriangleList:
                for (int i = 0; i < primitiveCount; ++i)
                    AppendTriangle(assembled, vertices,
                                   {sequence[3 * i], sequence[3 * i + 1], sequence[3 * i + 2]},
                                   cullMode, fillMode);
                break;
            case PrimitiveType::TriangleStrip:
                for (int i = 0; i < primitiveCount; ++i)
                {
                    // Swap the first two vertices of odd triangles so a consistently wound strip
                    // stays consistently wound after explicit list expansion.
                    const std::array<std::uint32_t, 3> triangle = (i & 1) == 0
                        ? std::array<std::uint32_t, 3>{sequence[i], sequence[i + 1], sequence[i + 2]}
                        : std::array<std::uint32_t, 3>{sequence[i + 1], sequence[i], sequence[i + 2]};
                    AppendTriangle(assembled, vertices, triangle, cullMode, fillMode);
                }
                break;
            case PrimitiveType::LineList:
                for (int i = 0; i < primitiveCount; ++i)
                    assembled.push_back(MakeRecord(vertices, RecordKind::Line,
                                                   {sequence[2 * i], sequence[2 * i + 1]}));
                break;
            case PrimitiveType::LineStrip:
                for (int i = 0; i < primitiveCount; ++i)
                    assembled.push_back(MakeRecord(vertices, RecordKind::Line,
                                                   {sequence[i], sequence[i + 1]}));
                break;
            case PrimitiveType::PointListEXT:
                for (int i = 0; i < primitiveCount; ++i)
                    assembled.push_back(MakeRecord(vertices, RecordKind::Point, {sequence[i]}));
                break;
            default: throw std::invalid_argument("CPU primitive type is unknown");
            }
            return assembled;
        }

        [[nodiscard]] std::vector<PrimitiveRecord> DrawUser(
            std::span<const std::uint8_t> source, int vertexOffset,
            const VertexDeclaration& declaration, PrimitiveType type, int primitiveCount,
            CullMode cullMode = CullMode::None, FillMode fillMode = FillMode::Solid) const
        {
            const int count = PrimitiveElementCount(type, primitiveCount);
            CpuVertexBuffer vertices(declaration, count);
            vertices.Upload(source, vertexOffset, count, declaration.getVertexStrideProperty(),
                            SetDataOptions::None);
            return Draw(vertices, nullptr, type, primitiveCount, {}, cullMode, fillMode);
        }

        template <typename Vertex>
        [[nodiscard]] std::vector<PrimitiveRecord> DrawUserObjects(
            std::span<const Vertex> source, int vertexOffset,
            const VertexDeclaration& declaration, PrimitiveType type, int primitiveCount,
            CullMode cullMode = CullMode::None, FillMode fillMode = FillMode::Solid) const
        {
            using Stream = CNA::Internal::Graphics::VertexStream<Vertex>;
            const int count = PrimitiveElementCount(type, primitiveCount);
            if (declaration.getVertexStrideProperty() !=
                CNA::Internal::Graphics::VertexStreamStride<Vertex>)
                throw std::invalid_argument(
                    "typed DrawUser declaration does not match the built-in GPU stream stride");
            if (vertexOffset < 0 || static_cast<std::size_t>(vertexOffset) > source.size() ||
                static_cast<std::size_t>(count) >
                    source.size() - static_cast<std::size_t>(vertexOffset))
                throw std::out_of_range("typed DrawUser source range is outside caller objects");
            std::vector<Stream> packed(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i)
                packed[static_cast<std::size_t>(i)] =
                    CNA::Internal::Graphics::Pack(source[vertexOffset + i]);
            const auto packedBytes = std::as_bytes(std::span(packed));
            return DrawUser(
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t*>(packedBytes.data()), packedBytes.size()),
                0, declaration, type, primitiveCount, cullMode, fillMode);
        }

        template <typename Index>
        [[nodiscard]] std::vector<PrimitiveRecord> DrawUserIndexed(
            std::span<const std::uint8_t> vertexSource, int vertexOffset, int numVertices,
            std::span<const Index> indexSource, int indexOffset,
            const VertexDeclaration& declaration, PrimitiveType type, int primitiveCount,
            CullMode cullMode = CullMode::None, FillMode fillMode = FillMode::Solid) const
        {
            const int indexCount = PrimitiveElementCount(type, primitiveCount);
            CpuVertexBuffer vertices(declaration, numVertices);
            vertices.Upload(vertexSource, vertexOffset, numVertices,
                            declaration.getVertexStrideProperty(), SetDataOptions::None);
            CpuIndexBuffer indices(sizeof(Index) == sizeof(std::uint32_t), indexCount);
            indices.Upload(indexSource, indexOffset, indexCount, SetDataOptions::None);
            return Draw(vertices, &indices, type, primitiveCount,
                        DrawRange{0, 0, 0, 0, numVertices}, cullMode, fillMode);
        }

    private:
        [[nodiscard]] static std::vector<std::uint32_t> FetchSequence(
            const CpuVertexBuffer& vertices, const CpuIndexBuffer* indices,
            int consumed, const DrawRange& range)
        {
            if (range.vertexStart < 0 || range.startIndex < 0 || range.baseVertex < 0 ||
                range.minVertexIndex < 0 || range.numVertices < 0)
                throw std::out_of_range("CPU draw range values must be non-negative");
            std::vector<std::uint32_t> result;
            result.reserve(static_cast<std::size_t>(consumed));
            if (indices == nullptr)
            {
                if (range.vertexStart > vertices.Count() ||
                    consumed > vertices.Count() - range.vertexStart)
                    throw std::out_of_range("CPU non-indexed draw exceeds uploaded vertices");
                for (int i = 0; i < consumed; ++i)
                    result.push_back(static_cast<std::uint32_t>(range.vertexStart + i));
                return result;
            }

            if (range.numVertices <= 0 || range.startIndex > indices->Count() ||
                consumed > indices->Count() - range.startIndex ||
                range.baseVertex > vertices.Count() ||
                range.minVertexIndex > vertices.Count() - range.baseVertex ||
                range.numVertices > vertices.Count() - range.baseVertex - range.minVertexIndex)
                throw std::out_of_range("CPU indexed draw exceeds declared buffer ranges");
            const std::uint64_t declaredEnd =
                static_cast<std::uint64_t>(range.minVertexIndex) + range.numVertices;
            for (int i = 0; i < consumed; ++i)
            {
                const std::uint32_t sourceIndex = indices->At(range.startIndex + i);
                if (sourceIndex < static_cast<std::uint32_t>(range.minVertexIndex) ||
                    static_cast<std::uint64_t>(sourceIndex) >= declaredEnd)
                    throw std::out_of_range("CPU index is outside caller-declared vertex window");
                const std::uint64_t fetched =
                    static_cast<std::uint64_t>(range.baseVertex) + sourceIndex;
                if (fetched >= static_cast<std::uint64_t>(vertices.Count()))
                    throw std::out_of_range("CPU decoded index exceeds uploaded vertices");
                result.push_back(static_cast<std::uint32_t>(fetched));
            }
            return result;
        }

        [[nodiscard]] static PrimitiveRecord MakeRecord(
            const CpuVertexBuffer& vertices, RecordKind kind,
            std::initializer_list<std::uint32_t> indices)
        {
            PrimitiveRecord result;
            result.kind = kind;
            result.indices.assign(indices);
            result.vertices.reserve(result.indices.size());
            for (const std::uint32_t index : result.indices)
            {
                result.vertices.push_back(vertices.Decode(static_cast<int>(index)));
                (void)Position(result.vertices.back());
            }
            return result;
        }

        static void AppendTriangle(std::vector<PrimitiveRecord>& output,
                                   const CpuVertexBuffer& vertices,
                                   const std::array<std::uint32_t, 3>& indices,
                                   CullMode cullMode, FillMode fillMode)
        {
            PrimitiveRecord triangle = MakeRecord(
                vertices, RecordKind::Triangle, {indices[0], indices[1], indices[2]});
            const float area = SignedScreenArea(triangle);
            if (area == 0.0f) return;
            triangle.clockwiseOnScreen = area > 0.0f; // Y grows down in the CPU raster target.
            if ((cullMode == CullMode::CullClockwiseFace && triangle.clockwiseOnScreen) ||
                (cullMode == CullMode::CullCounterClockwiseFace &&
                 !triangle.clockwiseOnScreen))
                return;
            if (fillMode == FillMode::Solid)
            {
                output.push_back(std::move(triangle));
                return;
            }
            output.push_back(MakeRecord(vertices, RecordKind::Line, {indices[0], indices[1]}));
            output.push_back(MakeRecord(vertices, RecordKind::Line, {indices[1], indices[2]}));
            output.push_back(MakeRecord(vertices, RecordKind::Line, {indices[2], indices[0]}));
        }
    };

    template <typename T>
    void Write(std::vector<std::uint8_t>& bytes, std::size_t offset, T value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }

    [[nodiscard]] VertexDeclaration PositionColorDeclaration()
    {
        return VertexPositionColor::getVertexDeclarationStatic();
    }

    [[nodiscard]] std::vector<std::uint8_t> PositionColorBytes(
        const std::vector<std::array<float, 2>>& positions)
    {
        constexpr int stride = CNA::Internal::Graphics::VertexStreamStride<VertexPositionColor>;
        std::vector<std::uint8_t> bytes(positions.size() * stride, 0u);
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            const std::size_t offset = i * stride;
            Write(bytes, offset + 0u, positions[i][0]);
            Write(bytes, offset + 4u, positions[i][1]);
            Write(bytes, offset + 8u, 0.0f);
            bytes[offset + 12u] = static_cast<std::uint8_t>(10u + i);
            bytes[offset + 13u] = static_cast<std::uint8_t>(20u + i);
            bytes[offset + 14u] = static_cast<std::uint8_t>(30u + i);
            bytes[offset + 15u] = 255u;
        }
        return bytes;
    }

    [[nodiscard]] CpuVertexBuffer MakePositionBuffer(
        const std::vector<std::array<float, 2>>& positions)
    {
        CpuVertexBuffer result(PositionColorDeclaration(), static_cast<int>(positions.size()));
        const auto bytes = PositionColorBytes(positions);
        result.Upload(bytes, 0, static_cast<int>(positions.size()), 16, SetDataOptions::None);
        return result;
    }

    void TestBuiltInDeclarations()
    {
        struct BuiltIn
        {
            const VertexDeclaration* declaration;
            int stride;
            std::size_t elements;
        };
        const std::array<BuiltIn, 7> builtIns = {{
            {&VertexPositionColor::getVertexDeclarationStatic(), 16, 2},
            {&VertexPositionTexture::getVertexDeclarationStatic(), 20, 2},
            {&VertexPositionColorTexture::getVertexDeclarationStatic(), 24, 3},
            {&VertexPositionNormalTexture::getVertexDeclarationStatic(), 32, 3},
            {&VertexPositionNormalTangentTexture::getVertexDeclarationStatic(), 48, 4},
            {&VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 52, 5},
            {&VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic(), 68, 6},
        }};
        bool allMatch = true;
        for (const BuiltIn& builtIn : builtIns)
        {
            allMatch = allMatch &&
                builtIn.declaration->getVertexStrideProperty() == builtIn.stride &&
                builtIn.declaration->GetVertexElements().size() == builtIn.elements;
            CpuVertexBuffer buffer(*builtIn.declaration, 1);
            std::vector<std::uint8_t> zero(static_cast<std::size_t>(builtIn.stride), 0u);
            buffer.Upload(zero, 0, 1, builtIn.stride, SetDataOptions::None);
            const DecodedVertex decoded = buffer.Decode(0);
            allMatch = allMatch && decoded.attributes.size() == builtIn.elements;
            (void)Position(decoded);
        }
        Check(allMatch,
              "all 7 built-in declarations preserve exact strides/elements and decode POSITION0");
    }

    void TestAllFormatsAndUsages()
    {
        const std::array<VertexElementFormat, 12> formats = {
            VertexElementFormat::Single, VertexElementFormat::Vector2,
            VertexElementFormat::Vector3, VertexElementFormat::Vector4,
            VertexElementFormat::Color, VertexElementFormat::Byte4,
            VertexElementFormat::Short2, VertexElementFormat::Short4,
            VertexElementFormat::NormalizedShort2, VertexElementFormat::NormalizedShort4,
            VertexElementFormat::HalfVector2, VertexElementFormat::HalfVector4,
        };
        std::vector<VertexElement> elements;
        int stride = 0;
        for (std::size_t i = 0; i < formats.size(); ++i)
        {
            elements.emplace_back(stride, formats[i], VertexElementUsage::TextureCoordinate,
                                  static_cast<int>(i));
            stride += Describe(formats[i])->bytes;
        }
        VertexDeclaration declaration(stride, elements);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stride), 0u);
        int offset = 0;
        const std::array<std::vector<float>, 12> expected = {{
            {1.25f}, {2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f, 10.0f},
            {64.0f / 255.0f, 128.0f / 255.0f, 1.0f, 32.0f / 255.0f},
            {1.0f, 2.0f, 200.0f, 255.0f}, {-123.0f, 456.0f},
            {-1.0f, 2.0f, -3.0f, 4.0f}, {-1.0f, 1.0f},
            {-16384.0f / 32767.0f, 0.0f, 16384.0f / 32767.0f, 1.0f},
            {1.0f, -2.0f}, {0.5f, 2.0f, 3.0f, 4.0f},
        }};
        const std::array<std::vector<std::uint8_t>, 12> payloads = {{
            {}, {}, {}, {}, {64u, 128u, 255u, 32u}, {1u, 2u, 200u, 255u},
            {}, {}, {}, {}, {}, {},
        }};
        const std::array<std::vector<std::uint16_t>, 4> shortPayloads = {{
            {static_cast<std::uint16_t>(static_cast<std::int16_t>(-123)), 456u},
            {static_cast<std::uint16_t>(static_cast<std::int16_t>(-1)), 2u,
             static_cast<std::uint16_t>(static_cast<std::int16_t>(-3)), 4u},
            {0x8000u, 0x7fffu},
            {0xc000u, 0u, 0x4000u, 0x7fffu},
        }};
        const std::array<std::vector<std::uint16_t>, 2> halfPayloads = {{
            {0x3c00u, 0xc000u}, {0x3800u, 0x4000u, 0x4200u, 0x4400u},
        }};
        const std::array<std::vector<float>, 4> floatPayloads = {{
            {1.25f}, {2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f, 10.0f},
        }};
        for (std::size_t i = 0; i < formats.size(); ++i)
        {
            if (i < 4u)
                std::memcpy(bytes.data() + offset, floatPayloads[i].data(),
                            floatPayloads[i].size() * sizeof(float));
            else if (i < 6u)
                std::copy(payloads[i].begin(), payloads[i].end(), bytes.begin() + offset);
            else if (i < 10u)
                std::memcpy(bytes.data() + offset, shortPayloads[i - 6u].data(),
                            shortPayloads[i - 6u].size() * sizeof(std::uint16_t));
            else
                std::memcpy(bytes.data() + offset, halfPayloads[i - 10u].data(),
                            halfPayloads[i - 10u].size() * sizeof(std::uint16_t));
            offset += Describe(formats[i])->bytes;
        }
        CpuVertexBuffer buffer(declaration, 1);
        buffer.Upload(bytes, 0, 1, stride, SetDataOptions::None);
        const DecodedVertex decoded = buffer.Decode(0);
        bool allFormats = decoded.attributes.size() == formats.size();
        for (std::size_t i = 0; i < formats.size(); ++i)
        {
            allFormats = allFormats && decoded.attributes[i].usageIndex == static_cast<int>(i) &&
                decoded.attributes[i].components == static_cast<int>(expected[i].size()) &&
                decoded.attributes[i].integer == (formats[i] == VertexElementFormat::Byte4);
            for (std::size_t component = 0; component < expected[i].size(); ++component)
                allFormats = allFormats &&
                    Near(decoded.attributes[i].values[component], expected[i][component]);
        }
        Check(allFormats,
              "all 12 VertexElementFormat values decode with exact GL conversion semantics");

        std::vector<VertexElement> usageElements;
        for (int usage = 0; usage <= 12; ++usage)
            usageElements.emplace_back(usage * 4, VertexElementFormat::Single,
                                       static_cast<VertexElementUsage>(usage), usage + 1);
        VertexDeclaration usageDeclaration(13 * 4, usageElements);
        CpuVertexBuffer usageBuffer(usageDeclaration, 1);
        std::vector<std::uint8_t> usageBytes(13u * 4u, 0u);
        usageBuffer.Upload(usageBytes, 0, 1, 13 * 4, SetDataOptions::None);
        const DecodedVertex usageDecoded = usageBuffer.Decode(0);
        bool allUsages = usageDecoded.attributes.size() == 13u;
        for (int usage = 0; usage <= 12; ++usage)
            allUsages = allUsages &&
                usageDecoded.attributes[usage].usage == static_cast<VertexElementUsage>(usage) &&
                usageDecoded.attributes[usage].usageIndex == usage + 1;
        Check(allUsages, "all 13 VertexElementUsage values and usage indices are retained");
    }

    void TestUploadsAndValidation()
    {
        const auto declaration = PositionColorDeclaration();
        const auto source = PositionColorBytes({{-9.0f, -9.0f}, {1.0f, 2.0f},
                                                {3.0f, 4.0f}, {5.0f, 6.0f}});
        CpuVertexBuffer vertices(declaration, 3);
        bool optionsMatch = true;
        for (const SetDataOptions option :
             {SetDataOptions::None, SetDataOptions::Discard, SetDataOptions::NoOverwrite})
        {
            vertices.Upload(source, 1, 3, 16, option);
            const DecodedVertex decoded = vertices.Decode(0);
            const auto& position = Position(decoded);
            optionsMatch = optionsMatch && vertices.Count() == 3 && vertices.Capacity() == 3 &&
                Near(position.values[0], 1.0f) && Near(position.values[1], 2.0f);
        }
        Check(optionsMatch,
              "vertex source offsets and all SetDataOptions preserve the same logical upload");

        const auto before = vertices.Bytes();
        const bool invalidVertexRanges =
            Throws([&] { vertices.Upload(source, 0, 4, 16, SetDataOptions::None); }) &&
            Throws([&] { vertices.Upload(source, 0, 3, 20, SetDataOptions::None); }) &&
            Throws([&] { vertices.Upload(source, 3, 3, 16, SetDataOptions::None); }) &&
            vertices.Bytes() == before;
        Check(invalidVertexRanges, "invalid vertex uploads reject atomically before mutation");

        const std::array<std::uint16_t, 5> source16 = {99u, 3u, 2u, 1u, 0u};
        CpuIndexBuffer indices16(false, 4);
        indices16.Upload<std::uint16_t>(source16, 1, 4, SetDataOptions::Discard);
        const std::array<std::uint32_t, 4> source32 = {70000u, 70001u, 70002u, 0xffffffffu};
        CpuIndexBuffer indices32(true, 4);
        indices32.Upload<std::uint32_t>(source32, 0, 4, SetDataOptions::NoOverwrite);
        Check(indices16.Values() == std::vector<std::uint32_t>({3u, 2u, 1u, 0u}) &&
                  indices32.Values() == std::vector<std::uint32_t>(source32.begin(), source32.end()) &&
                  indices32.IsThirtyTwoBit(),
              "16/32-bit index uploads preserve source offsets and never truncate 32-bit values");

        const auto indexBefore = indices32.Values();
        const bool invalidIndexRanges =
            Throws([&] { indices32.Upload<std::uint16_t>(source16, 0, 4, SetDataOptions::None); }) &&
            Throws([&] { indices32.Upload<std::uint32_t>(source32, 2, 4, SetDataOptions::None); }) &&
            indices32.Values() == indexBefore;
        Check(invalidIndexRanges, "invalid index width/ranges reject atomically before mutation");

        const VertexDeclaration outside(8, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const VertexDeclaration unknownFormat(4, {
            VertexElement(0, static_cast<VertexElementFormat>(99), VertexElementUsage::Position, 0),
        });
        const VertexDeclaration unknownUsage(4, {
            VertexElement(0, VertexElementFormat::Single,
                          static_cast<VertexElementUsage>(99), 0),
        });
        Check(Throws([&] { CpuVertexBuffer invalid(outside, 1); }) &&
                  Throws([&] { CpuVertexBuffer invalid(unknownFormat, 1); }) &&
                  Throws([&] { CpuVertexBuffer invalid(unknownUsage, 1); }),
              "invalid layouts reject instead of falling back to position-only bytes");
    }

    void TestPrimitiveExpansionAndCulling()
    {
        const CpuGeometryPipeline pipeline;
        const auto buffer = MakePositionBuffer({
            {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f},
            {2.0f, 0.0f}, {2.0f, 1.0f},
        });
        const auto triangleList = pipeline.Draw(
            buffer, nullptr, PrimitiveType::TriangleList, 2, {}, CullMode::None, FillMode::Solid);
        const auto triangleStrip = pipeline.Draw(
            buffer, nullptr, PrimitiveType::TriangleStrip, 2, {}, CullMode::None, FillMode::Solid);
        const auto lineList = pipeline.Draw(
            buffer, nullptr, PrimitiveType::LineList, 3, {}, CullMode::None, FillMode::Solid);
        const auto lineStrip = pipeline.Draw(
            buffer, nullptr, PrimitiveType::LineStrip, 5, {}, CullMode::None, FillMode::Solid);
        const auto points = pipeline.Draw(
            buffer, nullptr, PrimitiveType::PointListEXT, 6, {}, CullMode::None, FillMode::Solid);
        Check(triangleList.size() == 2u && triangleStrip.size() == 2u &&
                  triangleStrip[0].clockwiseOnScreen && triangleStrip[1].clockwiseOnScreen &&
                  lineList.size() == 3u && lineStrip.size() == 5u && points.size() == 6u,
              "all 5 primitive types expand with exact counts and alternating strip winding");

        const auto opposite = MakePositionBuffer({
            {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
            {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f},
        });
        const auto cullNone = pipeline.Draw(
            opposite, nullptr, PrimitiveType::TriangleList, 2, {}, CullMode::None, FillMode::Solid);
        const auto cullClockwise = pipeline.Draw(
            opposite, nullptr, PrimitiveType::TriangleList, 2, {},
            CullMode::CullClockwiseFace, FillMode::Solid);
        const auto cullCounterClockwise = pipeline.Draw(
            opposite, nullptr, PrimitiveType::TriangleList, 2, {},
            CullMode::CullCounterClockwiseFace, FillMode::Solid);
        Check(cullNone.size() == 2u && cullClockwise.size() == 1u &&
                  !cullClockwise[0].clockwiseOnScreen && cullCounterClockwise.size() == 1u &&
                  cullCounterClockwise[0].clockwiseOnScreen,
              "all 3 CullMode values follow XNA clockwise-as-displayed front-face convention");

        const auto wire = pipeline.Draw(
            buffer, nullptr, PrimitiveType::TriangleStrip, 2, {},
            CullMode::None, FillMode::WireFrame);
        const auto culledWire = pipeline.Draw(
            opposite, nullptr, PrimitiveType::TriangleList, 2, {},
            CullMode::CullClockwiseFace, FillMode::WireFrame);
        const bool onlyLines = std::all_of(wire.begin(), wire.end(), [](const PrimitiveRecord& record) {
            return record.kind == RecordKind::Line && record.indices.size() == 2u;
        });
        Check(wire.size() == 6u && onlyLines && culledWire.size() == 3u,
              "wireframe emits 3 explicit edges per surviving triangle after culling");
    }

    void TestIndexedRangesAndDrawUser()
    {
        const CpuGeometryPipeline pipeline;
        const auto small = MakePositionBuffer({
            {-9.0f, -9.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
            {5.0f, 5.0f}, {6.0f, 6.0f},
        });
        const std::array<std::uint16_t, 4> source16 = {99u, 0u, 1u, 2u};
        CpuIndexBuffer indices16(false, 4);
        indices16.Upload<std::uint16_t>(source16, 0, 4, SetDataOptions::None);
        const auto indexed16 = pipeline.Draw(
            small, &indices16, PrimitiveType::TriangleList, 1,
            DrawRange{0, 1, 1, 0, 3}, CullMode::None, FillMode::Solid);
        Check(indexed16.size() == 1u &&
                  indexed16[0].indices == std::vector<std::uint32_t>({1u, 2u, 3u}),
              "16-bit indexed draw applies startIndex and baseVertex exactly once");

        std::vector<std::array<float, 2>> manyPositions(70003u, {0.0f, 0.0f});
        manyPositions[70000u] = {0.0f, 0.0f};
        manyPositions[70001u] = {1.0f, 0.0f};
        manyPositions[70002u] = {0.0f, 1.0f};
        const auto many = MakePositionBuffer(manyPositions);
        const std::array<std::uint32_t, 4> source32 = {123456u, 70000u, 70001u, 70002u};
        CpuIndexBuffer indices32(true, 4);
        indices32.Upload<std::uint32_t>(source32, 0, 4, SetDataOptions::None);
        const auto indexed32 = pipeline.Draw(
            many, &indices32, PrimitiveType::TriangleList, 1,
            DrawRange{0, 1, 0, 70000, 3}, CullMode::None, FillMode::Solid);
        Check(indexed32.size() == 1u && indexed32[0].indices[0] == 70000u &&
                  indexed32[0].indices[2] == 70002u,
              "32-bit indexed draw fetches vertices above 65535 without truncation");

        const auto manyBefore = many.Bytes();
        const VertexDeclaration colorOnlyDeclaration(4, {
            VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
        CpuVertexBuffer colorOnly(colorOnlyDeclaration, 1);
        const std::array<std::uint8_t, 4> colorOnlyBytes = {255u, 0u, 0u, 255u};
        colorOnly.Upload(colorOnlyBytes, 0, 1, 4, SetDataOptions::None);
        Check(Throws([&] {
                  (void)pipeline.Draw(many, &indices32, PrimitiveType::TriangleList, 1,
                                      DrawRange{0, 1, 0, 0, 3}, CullMode::None,
                                      FillMode::Solid);
              }) &&
                  Throws([&] {
                      (void)pipeline.Draw(small, nullptr, PrimitiveType::TriangleList, 2,
                                          DrawRange{1}, CullMode::None, FillMode::Solid);
                  }) &&
                  Throws([&] {
                      (void)pipeline.Draw(small, nullptr, static_cast<PrimitiveType>(99), 1, {},
                                          CullMode::None, FillMode::Solid);
                  }) &&
                  Throws([&] {
                      (void)pipeline.Draw(small, nullptr, PrimitiveType::PointListEXT, 1, {},
                                          static_cast<CullMode>(99), FillMode::Solid);
                  }) &&
                  Throws([&] {
                      (void)pipeline.Draw(small, nullptr, PrimitiveType::PointListEXT, 1, {},
                                          CullMode::None, static_cast<FillMode>(99));
                  }) &&
                  Throws([&] {
                      (void)pipeline.Draw(colorOnly, nullptr, PrimitiveType::PointListEXT, 1, {},
                                          CullMode::None, FillMode::Solid);
                  }) && many.Bytes() == manyBefore,
              "range/topology/state/missing-position failures reject atomically");

        struct CustomVertex
        {
            std::uint8_t r, g, b, a;
            float x, y, z;
        };
        static_assert(sizeof(CustomVertex) == 16);
        const VertexDeclaration customDeclaration(16, {
            VertexElement(4, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
        const std::array<CustomVertex, 4> custom = {{
            {0u, 255u, 0u, 255u, -9.0f, -9.0f, 0.0f},
            {255u, 0u, 0u, 255u, 0.0f, 0.0f, 0.0f},
            {255u, 0u, 0u, 255u, 1.0f, 0.0f, 0.0f},
            {255u, 0u, 0u, 255u, 0.0f, 1.0f, 0.0f},
        }};
        const auto customBytes = std::as_bytes(std::span(custom));
        const auto rawUser = pipeline.DrawUser(
            std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(customBytes.data()), customBytes.size()),
            1, customDeclaration, PrimitiveType::TriangleList, 1);
        const auto& firstColor = rawUser[0].vertices[0].attributes[1];
        Check(rawUser.size() == 1u && rawUser[0].vertices[0].attributes.size() == 2u &&
                  Near(Position(rawUser[0].vertices[0]).values[0], 0.0f) &&
                  Near(firstColor.values[0], 1.0f) && Near(firstColor.values[1], 0.0f),
              "raw DrawUser applies vertexOffset and preserves custom declaration order/bytes");

        auto typedDrawWorks = [&](const auto& typed, const VertexDeclaration& declaration) {
            using Vertex = typename std::decay_t<decltype(typed)>::value_type;
            const auto records = pipeline.DrawUserObjects<Vertex>(
                typed, 1, declaration, PrimitiveType::TriangleList, 1);
            return records.size() == 1u && records[0].vertices[0].attributes.size() ==
                declaration.GetVertexElements().size() &&
                Near(Position(records[0].vertices[2]).values[1], 1.0f);
        };
        const std::array<Vector3, 4> typedPositions = {
            Vector3(-9.0f, -9.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f),
            Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f),
        };
        const Color white(255, 255, 255, 255);
        const Vector2 uv(0.25f, 0.75f);
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const std::array<VertexPositionColor, 4> typedVpc = {
            VertexPositionColor(typedPositions[0], white),
            VertexPositionColor(typedPositions[1], white),
            VertexPositionColor(typedPositions[2], white),
            VertexPositionColor(typedPositions[3], white),
        };
        const std::array<VertexPositionTexture, 4> typedVpt = {
            VertexPositionTexture(typedPositions[0], uv),
            VertexPositionTexture(typedPositions[1], uv),
            VertexPositionTexture(typedPositions[2], uv),
            VertexPositionTexture(typedPositions[3], uv),
        };
        const std::array<VertexPositionColorTexture, 4> typedVpct = {
            VertexPositionColorTexture(typedPositions[0], white, uv),
            VertexPositionColorTexture(typedPositions[1], white, uv),
            VertexPositionColorTexture(typedPositions[2], white, uv),
            VertexPositionColorTexture(typedPositions[3], white, uv),
        };
        const std::array<VertexPositionNormalTexture, 4> typedVpnt = {
            VertexPositionNormalTexture(typedPositions[0], normal, uv),
            VertexPositionNormalTexture(typedPositions[1], normal, uv),
            VertexPositionNormalTexture(typedPositions[2], normal, uv),
            VertexPositionNormalTexture(typedPositions[3], normal, uv),
        };
        Check(typedDrawWorks(typedVpc,
                             VertexPositionColor::getVertexDeclarationStatic()) &&
                  typedDrawWorks(typedVpt,
                                 VertexPositionTexture::getVertexDeclarationStatic()) &&
                  typedDrawWorks(typedVpct,
                                 VertexPositionColorTexture::getVertexDeclarationStatic()) &&
                  typedDrawWorks(typedVpnt,
                                 VertexPositionNormalTexture::getVertexDeclarationStatic()),
              "all 4 typed DrawUser overload streams pack object values instead of ABI bytes");

        const std::array<std::uint16_t, 4> user16 = {99u, 0u, 1u, 2u};
        const std::array<std::uint32_t, 4> user32 = {99u, 0u, 1u, 2u};
        const auto rawSpan = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(customBytes.data()), customBytes.size());
        const auto userIndexed16 = pipeline.DrawUserIndexed<std::uint16_t>(
            rawSpan, 1, 3, user16, 1, customDeclaration, PrimitiveType::TriangleList, 1);
        const auto userIndexed32 = pipeline.DrawUserIndexed<std::uint32_t>(
            rawSpan, 1, 3, user32, 1, customDeclaration, PrimitiveType::TriangleList, 1);
        Check(userIndexed16.size() == 1u && userIndexed32.size() == 1u &&
                  userIndexed16[0].indices == userIndexed32[0].indices &&
                  userIndexed32[0].indices == std::vector<std::uint32_t>({0u, 1u, 2u}),
              "DrawUserIndexed applies independent vertex/index offsets for both index widths");
    }
}

int main()
{
    TestBuiltInDeclarations();
    TestAllFormatsAndUsages();
    TestUploadsAndValidation();
    TestPrimitiveExpansionAndCulling();
    TestIndexedRangesAndDrawUser();
    return failures == 0 ? 0 : 1;
}
