// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-007. See GltfBufferOracleEXT.hpp for what this layer is for and why it is test
// scope only.

#include "GltfBufferOracleEXT.hpp"

#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;

namespace CnaTest::GltfOracle
{
    namespace
    {
        std::string Hex(int value)
        {
            if (value < 0) { return "<past the end>"; }
            std::ostringstream out;
            out << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << value;
            return out.str();
        }

        std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path, bool& ok)
        {
            std::ifstream file(path, std::ios::binary);
            ok = file.good();
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                             std::istreambuf_iterator<char>());
        }

        /// The first offset at which two byte spans differ, or the length of the shorter one when
        /// its whole content matches.
        std::size_t FirstDifference(const std::vector<std::uint8_t>& a,
                                     const std::vector<std::uint8_t>& b)
        {
            const std::size_t common = a.size() < b.size() ? a.size() : b.size();
            for (std::size_t i = 0; i < common; ++i)
            {
                if (a[i] != b[i]) { return i; }
            }
            return common;
        }

        int ByteAt(const std::vector<std::uint8_t>& bytes, std::size_t offset)
        {
            return offset < bytes.size() ? static_cast<int>(bytes[offset]) : -1;
        }

        void FillReport(BufferDifference& diff, const std::string& what)
        {
            std::ostringstream out;
            out << diff.fixture << ' ' << BufferKindName(diff.kind) << " differs";
            if (diff.sizeDiffers)
            {
                out << ": size " << diff.actualSize << " bytes, golden " << diff.expectedSize
                    << " bytes";
            }
            out << " at byte " << diff.offset;
            if (diff.element >= 0)
            {
                out << " (" << what << ' ' << diff.element;
                if (!diff.field.empty())
                {
                    out << ", " << diff.field << " +" << diff.fieldByteOffset;
                }
                out << ')';
            }
            out << ": expected " << Hex(diff.expectedByte) << ", actual " << Hex(diff.actualByte);
            diff.report = out.str();
        }
    }

    const char* BufferKindName(BufferKind kind)
    {
        return kind == BufferKind::Vertex ? "VB" : "IB";
    }

    VertexLayout VertexLayoutForStrideEXT(int stride)
    {
        VertexLayout layout;
        layout.stride = stride;
        layout.known = true;
        // plan_gltf.md §2.3. Written out per stride rather than composed from rules, because that
        // is how every renderer's own ApplyLayout switch is written and a divergence between the
        // two is precisely what this oracle has to be able to see.
        switch (stride)
        {
            case 20:
                layout.fields = {{"Position", 0, 12}, {"TextureCoordinate", 12, 8}};
                break;
            case 24:
                layout.fields = {{"Position", 0, 12}, {"Color", 12, 4}, {"TextureCoordinate", 16, 8}};
                break;
            case 32:
                layout.fields = {{"Position", 0, 12}, {"Normal", 12, 12}, {"TextureCoordinate", 24, 8}};
                break;
            case 48:
                layout.fields = {{"Position", 0, 12}, {"Normal", 12, 12}, {"Tangent", 24, 16},
                                 {"TextureCoordinate", 40, 8}};
                break;
            case 52:
                layout.fields = {{"Position", 0, 12}, {"Normal", 12, 12}, {"TextureCoordinate", 24, 8},
                                 {"BlendWeight", 32, 16}, {"BlendIndices", 48, 4}};
                break;
            case 56:
                layout.fields = {{"Position", 0, 12}, {"Normal", 12, 12}, {"TextureCoordinate", 24, 8},
                                 {"BlendWeight", 32, 16}, {"BlendIndices", 48, 4}, {"Color", 52, 4}};
                break;
            case 60:
                layout.fields = {{"Position", 0, 12}, {"Normal", 12, 12}, {"Tangent", 24, 16},
                                 {"TextureCoordinate", 40, 8}, {"TextureCoordinate1", 48, 8},
                                 {"Padding", 56, 4}};
                break;
            case 68:
                layout.fields = {{"Position", 0, 12}, {"Normal", 12, 12}, {"Tangent", 24, 16},
                                 {"TextureCoordinate", 40, 8}, {"BlendWeight", 48, 16},
                                 {"BlendIndices", 64, 4}};
                break;
            case 76:
                layout.fields = {{"Position", 0, 12}, {"Normal", 12, 12}, {"Tangent", 24, 16},
                                 {"TextureCoordinate", 40, 8}, {"BlendWeight", 48, 16},
                                 {"BlendIndices", 64, 4}, {"TextureCoordinate1", 68, 8}};
                break;
            default:
                layout.known = false;
                break;
        }
        return layout;
    }

    BufferDifference CompareVertexBytesEXT(const std::string& fixture, int stride,
                                            const std::vector<std::uint8_t>& expected,
                                            const std::vector<std::uint8_t>& actual)
    {
        BufferDifference diff;
        diff.fixture = fixture;
        diff.kind = BufferKind::Vertex;
        diff.expectedSize = expected.size();
        diff.actualSize = actual.size();
        diff.sizeDiffers = expected.size() != actual.size();

        const std::size_t offset = FirstDifference(expected, actual);
        if (!diff.sizeDiffers && offset == expected.size()) { return diff; }

        diff.equal = false;
        diff.offset = offset;
        diff.expectedByte = ByteAt(expected, offset);
        diff.actualByte = ByteAt(actual, offset);

        if (stride > 0)
        {
            diff.element = static_cast<long long>(offset / static_cast<std::size_t>(stride));
            const std::size_t within = offset % static_cast<std::size_t>(stride);
            const VertexLayout layout = VertexLayoutForStrideEXT(stride);
            for (const VertexField& candidate : layout.fields)
            {
                if (within >= candidate.offset && within < candidate.offset + candidate.size)
                {
                    diff.field = candidate.name;
                    diff.fieldByteOffset = within - candidate.offset;
                    break;
                }
            }
            if (diff.field.empty())
            {
                // Either an unknown stride, or padding between two fields. Saying which is more
                // useful than saying nothing: a difference in padding is a real finding.
                diff.field = layout.known ? "<inter-field padding>" : "<unknown stride layout>";
                diff.fieldByteOffset = within;
            }
        }
        FillReport(diff, "vertex");
        return diff;
    }

    BufferDifference CompareIndexBytesEXT(const std::string& fixture, std::size_t indexElementSize,
                                           const std::vector<std::uint8_t>& expected,
                                           const std::vector<std::uint8_t>& actual)
    {
        BufferDifference diff;
        diff.fixture = fixture;
        diff.kind = BufferKind::Index;
        diff.expectedSize = expected.size();
        diff.actualSize = actual.size();
        diff.sizeDiffers = expected.size() != actual.size();

        const std::size_t offset = FirstDifference(expected, actual);
        if (!diff.sizeDiffers && offset == expected.size()) { return diff; }

        diff.equal = false;
        diff.offset = offset;
        diff.expectedByte = ByteAt(expected, offset);
        diff.actualByte = ByteAt(actual, offset);
        if (indexElementSize > 0)
        {
            diff.element = static_cast<long long>(offset / indexElementSize);
            diff.field = indexElementSize == 4 ? "uint32 index" : "uint16 index";
            diff.fieldByteOffset = offset % indexElementSize;
        }
        FillReport(diff, "index");
        return diff;
    }

    GoldenBuffers LoadGoldenBuffersEXT(const LoadedFixture& fixture)
    {
        GoldenBuffers golden;

        const JsonValue& l5 = Member(fixture.Expected(), "l5");
        if (l5.type != JsonType::Object) { return golden; }
        golden.declared = true;
        golden.supported = BoolOr(l5, "supported", false);
        golden.reason = StringOr(l5, "reason", "");
        golden.blockedBy = Strings(Member(l5, "blockedBy"));

        const JsonValue& parts = Member(l5, "parts");
        if (parts.type != JsonType::Array)
        {
            golden.ok = false;
            golden.error = "l5.parts is missing or is not an array";
            return golden;
        }

        for (const JsonValue& part : parts.arrayValue)
        {
            GoldenBufferPart out;
            out.mesh = static_cast<int>(NumberOr(part, "mesh", -1));
            out.primitive = static_cast<int>(NumberOr(part, "primitive", -1));
            out.stride = static_cast<int>(NumberOr(part, "stride", 0));
            out.vertexCount = static_cast<std::size_t>(NumberOr(part, "vertexCount", 0));
            out.indexCount = static_cast<std::size_t>(NumberOr(part, "indexCount", 0));
            out.indexElementSize = static_cast<std::size_t>(NumberOr(part, "indexElementSize", 0));
            out.topology = StringOr(part, "topology", "");
            out.primitiveCount = static_cast<std::size_t>(NumberOr(part, "primitiveCount", 0));

            for (const JsonValue& declared : Member(part, "fields").arrayValue)
            {
                VertexField declaredField;
                declaredField.name = StringOr(declared, "name", "");
                declaredField.offset = static_cast<std::size_t>(NumberOr(declared, "offset", 0));
                declaredField.size = static_cast<std::size_t>(NumberOr(declared, "size", 0));
                out.declaredFields.push_back(declaredField);
            }

            struct FileSpec
            {
                const char* key;
                const char* sizeKey;
                std::vector<std::uint8_t>* into;
            };
            const FileSpec specs[] = {
                {"vertexBufferFile", "vertexBufferBytes", &out.vertexBytes},
                {"indexBufferFile", "indexBufferBytes", &out.indexBytes},
            };
            for (const FileSpec& spec : specs)
            {
                const std::string name = StringOr(part, spec.key, "");
                if (name.empty())
                {
                    golden.ok = false;
                    golden.error = std::string("l5 part declares no ") + spec.key;
                    return golden;
                }
                bool readOk = false;
                *spec.into = ReadAllBytes(CorpusDirectory() / name, readOk);
                if (!readOk)
                {
                    golden.ok = false;
                    golden.error = "golden buffer '" + name + "' could not be read";
                    return golden;
                }
                const auto declaredSize = static_cast<std::size_t>(NumberOr(part, spec.sizeKey, 0));
                if (spec.into->size() != declaredSize)
                {
                    golden.ok = false;
                    golden.error = "golden buffer '" + name + "' is " +
                                   std::to_string(spec.into->size()) + " bytes, the manifest "
                                   "declares " + std::to_string(declaredSize);
                    return golden;
                }
            }
            golden.parts.push_back(std::move(out));
        }
        return golden;
    }
}
