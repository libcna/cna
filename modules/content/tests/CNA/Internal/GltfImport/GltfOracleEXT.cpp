// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-005 / GLTF-006 -- see GltfOracleEXT.hpp for the diagnostic/test scope rules.

#include "GltfOracleEXT.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_set>

#ifdef CNA_DRACO_AVAILABLE
#include "draco/compression/decode.h"
#endif

namespace CnaTest::GltfOracle
{
    namespace
    {
        /// Fixed-precision float formatting: %.9g round-trips every finite float exactly and is
        /// locale-independent for the C locale the tests run under, so two dumps of equal values
        /// always produce byte-identical text.
        std::string Num(float v)
        {
            if (!std::isfinite(v)) { return "null"; }
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(v));
            return buffer;
        }

        std::string Quote(const std::string& s)
        {
            std::string out = "\"";
            for (const char c : s)
            {
                switch (c)
                {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20)
                        {
                            char esc[8];
                            std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(c) & 0xFFu);
                            out += esc;
                        }
                        else { out += c; }
                        break;
                }
            }
            return out + "\"";
        }

        /// glTF §3.6.2.2's component reader, restated on the oracle side (plan_gltf.md GLTF-062).
        /// A normalized integer maps onto its unit range with the divisors the specification names
        /// -- 255/127/65535/32767 -- and a signed value clamps at -1 rather than reaching -1.008.
        float ReadFloatComponent(const std::uint8_t* p, cgltf_component_type type, bool normalized)
        {
            switch (type)
            {
                case cgltf_component_type_r_8:
                {
                    std::int8_t v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    return normalized ? std::max(static_cast<float>(v) / 127.0f, -1.0f)
                                      : static_cast<float>(v);
                }
                case cgltf_component_type_r_8u:
                {
                    std::uint8_t v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    return normalized ? static_cast<float>(v) / 255.0f : static_cast<float>(v);
                }
                case cgltf_component_type_r_16:
                {
                    std::int16_t v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    return normalized ? std::max(static_cast<float>(v) / 32767.0f, -1.0f)
                                      : static_cast<float>(v);
                }
                case cgltf_component_type_r_16u:
                {
                    std::uint16_t v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    return normalized ? static_cast<float>(v) / 65535.0f : static_cast<float>(v);
                }
                case cgltf_component_type_r_32u:
                {
                    std::uint32_t v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    return normalized ? static_cast<float>(v) / 4294967295.0f
                                      : static_cast<float>(v);
                }
                case cgltf_component_type_r_32f:
                {
                    float v = 0.0f;
                    std::memcpy(&v, p, sizeof(v));
                    return v;
                }
                default: return 0.0f;
            }
        }

        /// The unsigned integer at `p`, for a sparse block's own index array (§3.6.2.3).
        std::size_t ReadIndexComponent(const std::uint8_t* p, cgltf_component_type type)
        {
            switch (type)
            {
                case cgltf_component_type_r_8u:
                {
                    std::uint8_t v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    return v;
                }
                case cgltf_component_type_r_16u:
                {
                    std::uint16_t v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    return v;
                }
                case cgltf_component_type_r_32u:
                {
                    std::uint32_t v = 0;
                    std::memcpy(&v, p, sizeof(v));
                    return v;
                }
                default: return 0;
            }
        }

        /// Byte offset of component `c` inside one element, per §3.6.2.4. Only ``MAT2``/``MAT3``
        /// differ from plain `c * componentSize`: their columns are padded up to a 4-byte boundary.
        std::size_t ComponentByteOffset(cgltf_type type, std::size_t componentSize, std::size_t c)
        {
            std::size_t rows = 0;
            if (type == cgltf_type_mat2) { rows = 2; }
            else if (type == cgltf_type_mat3) { rows = 3; }
            else if (type == cgltf_type_mat4) { rows = 4; }
            if (rows == 0) { return c * componentSize; }
            const std::size_t columnBytes = ((rows * componentSize + 3) / 4) * 4;
            return (c / rows) * columnBytes + (c % rows) * componentSize;
        }

        /// Decodes a whole accessor to floats, as `cgltf_accessor_unpack_floats` does but with
        /// §3.6.2.3's sparse addressing rather than cgltf's (plan_gltf.md GLTF-062).
        ///
        /// The oracle normally delegates the decode to cgltf precisely so it is not CNA's importer
        /// judging itself -- but for one combination cgltf is the component under test. A sparse
        /// accessor's `values` array is tightly packed, while cgltf's reader walks it at the BASE
        /// accessor's stride; the two coincide unless the base bufferView is interleaved. So the
        /// specification's own addressing is restated here rather than inheriting the bug and
        /// calling the result "expected". This is an independent restatement, not a call into CNA:
        /// both this and `GltfImportCore::UnpackAccessor` are checked against the Python-generated
        /// manifest, which is where the truth actually lives.
        /// `GltfAccessorDecodeLock.VendoredParserStillMisreadsSparseValuesAtTheBaseStride` pins the
        /// underlying bug, so a cgltf upgrade that fixes it retires both copies at once.
        ///
        /// :return: an empty string on success, or a description of what could not be decoded.
        std::string UnpackAccessorFloats(const cgltf_accessor& accessor, std::vector<float>& values)
        {
            const std::size_t components = static_cast<std::size_t>(cgltf_num_components(accessor.type));
            if (components == 0) { return "accessor has an invalid type"; }
            values.assign(static_cast<std::size_t>(accessor.count) * components, 0.0f);
            if (values.empty()) { return ""; }

            const cgltf_size unpacked =
                cgltf_accessor_unpack_floats(&accessor, values.data(), values.size());
            if (unpacked != values.size())
            {
                return "cgltf_accessor_unpack_floats decoded " + std::to_string(unpacked) + " of " +
                       std::to_string(values.size()) + " components";
            }

            // plan_gltf.md GLTF-056. §3.6.2.2's signed normalized conversions are
            // `max(c / 127, -1)` and `max(c / 32767, -1)`; cgltf divides and returns with no clamp,
            // so -128 decodes to -1.0079. Restated on the oracle side for the same reason as
            // GLTF-062's sparse addressing: the oracle must apply the specification, not inherit
            // the defect and certify it as "expected".
            if (accessor.normalized != 0 &&
                (accessor.component_type == cgltf_component_type_r_8 ||
                 accessor.component_type == cgltf_component_type_r_16))
            {
                for (float& value : values)
                {
                    if (value < -1.0f) { value = -1.0f; }
                }
            }

            if (accessor.is_sparse == 0) { return ""; }
            const cgltf_accessor_sparse& sparse = accessor.sparse;
            const cgltf_size elementSize = cgltf_calc_size(accessor.type, accessor.component_type);
            if (accessor.stride == elementSize || sparse.count == 0 ||
                sparse.values_buffer_view == nullptr || sparse.indices_buffer_view == nullptr)
            {
                return "";
            }

            const auto* valueBytes =
                static_cast<const std::uint8_t*>(cgltf_buffer_view_data(sparse.values_buffer_view));
            const auto* indexBytes =
                static_cast<const std::uint8_t*>(cgltf_buffer_view_data(sparse.indices_buffer_view));
            if (valueBytes == nullptr || indexBytes == nullptr)
            {
                return "sparse accessor has an unreadable indices or values bufferView";
            }
            valueBytes += sparse.values_byte_offset;
            indexBytes += sparse.indices_byte_offset;

            const cgltf_size indexStride = cgltf_component_size(sparse.indices_component_type);
            const std::size_t componentSize =
                static_cast<std::size_t>(cgltf_component_size(accessor.component_type));
            for (cgltf_size i = 0; i < sparse.count; ++i)
            {
                const std::size_t writer =
                    ReadIndexComponent(indexBytes + i * indexStride, sparse.indices_component_type);
                if (writer >= static_cast<std::size_t>(accessor.count))
                {
                    return "sparse override " + std::to_string(i) + " targets element " +
                           std::to_string(writer) + ", past the accessor's own " +
                           std::to_string(accessor.count);
                }
                for (std::size_t c = 0; c < components; ++c)
                {
                    values[writer * components + c] = ReadFloatComponent(
                        valueBytes + i * elementSize +
                            ComponentByteOffset(accessor.type, componentSize, c),
                        accessor.component_type, accessor.normalized != 0);
                }
            }
            return "";
        }

        template <std::size_t N>
        std::string FloatArray(const std::vector<std::array<float, N>>& values)
        {
            std::string out = "[";
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                for (std::size_t c = 0; c < N; ++c)
                {
                    if (i != 0 || c != 0) { out += ","; }
                    out += Num(values[i][c]);
                }
            }
            return out + "]";
        }

        template <std::size_t N>
        std::string ByteArray(const std::vector<std::array<std::uint8_t, N>>& values)
        {
            std::string out = "[";
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                for (std::size_t c = 0; c < N; ++c)
                {
                    if (i != 0 || c != 0) { out += ","; }
                    out += std::to_string(static_cast<unsigned>(values[i][c]));
                }
            }
            return out + "]";
        }

        const char* ComponentTypeName(cgltf_component_type type)
        {
            switch (type)
            {
                case cgltf_component_type_r_8: return "BYTE";
                case cgltf_component_type_r_8u: return "UNSIGNED_BYTE";
                case cgltf_component_type_r_16: return "SHORT";
                case cgltf_component_type_r_16u: return "UNSIGNED_SHORT";
                case cgltf_component_type_r_32u: return "UNSIGNED_INT";
                case cgltf_component_type_r_32f: return "FLOAT";
                default: return "INVALID";
            }
        }

        int ComponentTypeConstant(cgltf_component_type type)
        {
            switch (type)
            {
                case cgltf_component_type_r_8: return 5120;
                case cgltf_component_type_r_8u: return 5121;
                case cgltf_component_type_r_16: return 5122;
                case cgltf_component_type_r_16u: return 5123;
                case cgltf_component_type_r_32u: return 5125;
                case cgltf_component_type_r_32f: return 5126;
                default: return 0;
            }
        }

        const char* AccessorTypeName(cgltf_type type)
        {
            switch (type)
            {
                case cgltf_type_scalar: return "SCALAR";
                case cgltf_type_vec2: return "VEC2";
                case cgltf_type_vec3: return "VEC3";
                case cgltf_type_vec4: return "VEC4";
                case cgltf_type_mat2: return "MAT2";
                case cgltf_type_mat3: return "MAT3";
                case cgltf_type_mat4: return "MAT4";
                default: return "INVALID";
            }
        }

        /// Reads a float from a packed vertex at `offset`, via memcpy -- the packed bytes carry no
        /// alignment guarantee, so a reinterpret_cast here would be exactly the undefined
        /// behaviour plan_gltf.md §8.4 flags in production code.
        float ReadFloat(const std::vector<std::uint8_t>& bytes, std::size_t offset)
        {
            float value = 0.0f;
            std::memcpy(&value, bytes.data() + offset, sizeof(float));
            return value;
        }

        void AccumulateBounds(WorldPositions& positions)
        {
            float lo[3] = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::infinity()};
            float hi[3] = {-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity()};
            bool any = false;
            for (const WorldInstance& instance : positions.instances)
            {
                for (const std::array<float, 3>& p : instance.worldPositions)
                {
                    any = true;
                    for (int c = 0; c < 3; ++c)
                    {
                        lo[c] = std::min(lo[c], p[static_cast<std::size_t>(c)]);
                        hi[c] = std::max(hi[c], p[static_cast<std::size_t>(c)]);
                    }
                }
            }
            positions.hasBounds = any;
            if (any)
            {
                positions.min = {lo[0], lo[1], lo[2]};
                positions.max = {hi[0], hi[1], hi[2]};
            }
        }

        /// The primitive's POSITION values, decoded straight from its declared source.
        /// Deliberately not routed through MeshOut: the L4 oracle must stay usable on a fixture
        /// whose L3 is wrong. For Draco the independent oracle invokes the pinned reference
        /// decoder itself; only the extension's cgltf unique-id representation is shared with the
        /// production importer.
        std::vector<std::array<float, 3>> PrimitivePositions(const cgltf_data& data,
                                                              const cgltf_primitive& prim)
        {
            std::vector<std::array<float, 3>> out;
            const cgltf_accessor* accessor =
                cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
            if (accessor == nullptr || cgltf_num_components(accessor->type) != 3) { return out; }

#ifdef CNA_DRACO_AVAILABLE
            if (prim.has_draco_mesh_compression != 0)
            {
                const cgltf_buffer_view* view = prim.draco_mesh_compression.buffer_view;
                const std::uint8_t* bytes = view != nullptr ? cgltf_buffer_view_data(view) : nullptr;
                if (view == nullptr || bytes == nullptr) { return out; }

                draco::DecoderBuffer buffer;
                buffer.Init(reinterpret_cast<const char*>(bytes), view->size);
                draco::Decoder decoder;
                draco::StatusOr<std::unique_ptr<draco::Mesh>> decoded =
                    decoder.DecodeMeshFromBuffer(&buffer);
                if (!decoded.ok()) { return out; }

                const int uniqueId = CNA::Internal::GltfImport::FindDracoUniqueIdEXT(
                    prim, &data, cgltf_attribute_type_position, 0);
                const draco::PointAttribute* position = uniqueId >= 0
                    ? decoded.value()->GetAttributeByUniqueId(static_cast<std::uint32_t>(uniqueId))
                    : nullptr;
                if (position == nullptr) { return out; }

                out.reserve(static_cast<std::size_t>(decoded.value()->num_points()));
                for (draco::PointIndex point(0); point < decoded.value()->num_points(); ++point)
                {
                    std::array<float, 3> value{};
                    if (!position->ConvertValue<float>(position->mapped_index(point), 3,
                                                       value.data()))
                    {
                        return {};
                    }
                    out.push_back(value);
                }
                return out;
            }
#endif

            std::vector<float> raw;
            if (!UnpackAccessorFloats(*accessor, raw).empty()) { return out; }
            out.reserve(static_cast<std::size_t>(accessor->count));
            for (cgltf_size v = 0; v < accessor->count; ++v)
            {
                const std::size_t o = static_cast<std::size_t>(v) * 3;
                out.push_back({raw[o], raw[o + 1], raw[o + 2]});
            }
            return out;
        }

    }

    GltfMatrix IdentityMatrix()
    {
        return GltfMatrix{1.0f, 0.0f, 0.0f, 0.0f,
                          0.0f, 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 1.0f, 0.0f,
                          0.0f, 0.0f, 0.0f, 1.0f};
    }

    GltfMatrix Multiply(const GltfMatrix& a, const GltfMatrix& b)
    {
        GltfMatrix out{};
        for (std::size_t col = 0; col < 4; ++col)
        {
            for (std::size_t row = 0; row < 4; ++row)
            {
                float sum = 0.0f;
                for (std::size_t k = 0; k < 4; ++k)
                {
                    sum += a[k * 4 + row] * b[col * 4 + k];
                }
                out[col * 4 + row] = sum;
            }
        }
        return out;
    }

    GltfMatrix NodeLocalMatrix(const cgltf_node& node)
    {
        if (node.has_matrix != 0)
        {
            GltfMatrix out{};
            for (std::size_t i = 0; i < 16; ++i) { out[i] = node.matrix[i]; }
            return out;
        }

        GltfMatrix translation = IdentityMatrix();
        if (node.has_translation != 0)
        {
            translation[12] = node.translation[0];
            translation[13] = node.translation[1];
            translation[14] = node.translation[2];
        }

        GltfMatrix rotation = IdentityMatrix();
        if (node.has_rotation != 0)
        {
            const float x = node.rotation[0], y = node.rotation[1];
            const float z = node.rotation[2], w = node.rotation[3];
            rotation = GltfMatrix{
                1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w), 2.0f * (x * z - y * w), 0.0f,
                2.0f * (x * y - z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w), 0.0f,
                2.0f * (x * z + y * w), 2.0f * (y * z - x * w), 1.0f - 2.0f * (x * x + y * y), 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
        }

        GltfMatrix scale = IdentityMatrix();
        if (node.has_scale != 0)
        {
            scale[0] = node.scale[0];
            scale[5] = node.scale[1];
            scale[10] = node.scale[2];
        }

        // T * R * S, exactly the order §3.5.3 mandates: scale first, then rotate, then translate.
        return Multiply(Multiply(translation, rotation), scale);
    }

    // plan_gltf.md GLTF-113: converts a CNA Matrix back into this oracle's glTF-shaped
    // column-major array. XNA is row-major with a row-vector convention and glTF is column-major
    // with a column-vector one, so the two are transposes of the same transform -- and the flat
    // byte order of a row-major M is therefore already the flat order of column-major M^T. This is
    // exactly GltfImportCore::ConvertGltfMatrix run backwards, and it is a straight member copy for
    // the same reason: every matrix crossing here is a plain affine transform.
    GltfMatrix ToGltfMatrixEXT(const Microsoft::Xna::Framework::Matrix& m)
    {
        return GltfMatrix{m.M11, m.M12, m.M13, m.M14,
                          m.M21, m.M22, m.M23, m.M24,
                          m.M31, m.M32, m.M33, m.M34,
                          m.M41, m.M42, m.M43, m.M44};
    }

    std::array<float, 3> TransformPoint(const GltfMatrix& m, float x, float y, float z)
    {
        return {m[0] * x + m[4] * y + m[8] * z + m[12],
                m[1] * x + m[5] * y + m[9] * z + m[13],
                m[2] * x + m[6] * y + m[10] * z + m[14]};
    }

    AccessorDump DumpAccessorEXT(const cgltf_data& data, std::size_t accessorIndex)
    {
        AccessorDump dump;
        dump.index = accessorIndex;
        if (accessorIndex >= data.accessors_count)
        {
            dump.error = "accessor index " + std::to_string(accessorIndex) + " is out of range (" +
                         std::to_string(data.accessors_count) + " accessors)";
            return dump;
        }

        const cgltf_accessor& accessor = data.accessors[accessorIndex];
        dump.type = AccessorTypeName(accessor.type);
        dump.componentType = ComponentTypeConstant(accessor.component_type);
        dump.componentTypeName = ComponentTypeName(accessor.component_type);
        dump.count = static_cast<std::size_t>(accessor.count);
        dump.componentsPerElement = static_cast<std::size_t>(cgltf_num_components(accessor.type));
        dump.normalized = accessor.normalized != 0;
        dump.sparse = accessor.is_sparse != 0;
        dump.byteOffset = static_cast<std::size_t>(accessor.offset);
        if (accessor.buffer_view != nullptr)
        {
            dump.bufferView = static_cast<int>(accessor.buffer_view - data.buffer_views);
            dump.bufferViewByteOffset = static_cast<long long>(accessor.buffer_view->offset);
            dump.bufferViewByteStride = accessor.buffer_view->stride != 0
                ? static_cast<long long>(accessor.buffer_view->stride) : -1;
        }

        if (dump.componentsPerElement == 0)
        {
            dump.error = "accessor has an invalid type";
            return dump;
        }

        std::vector<float> values;
        const std::string unpackError = UnpackAccessorFloats(accessor, values);
        if (!unpackError.empty())
        {
            dump.error = unpackError;
            return dump;
        }

        dump.values = std::move(values);
        dump.decoded = true;
        return dump;
    }

    std::string ToJson(const AccessorDump& dump)
    {
        std::string out = "{";
        out += "\"index\":" + std::to_string(dump.index);
        out += ",\"type\":" + Quote(dump.type);
        out += ",\"componentType\":" + std::to_string(dump.componentType);
        out += ",\"componentTypeName\":" + Quote(dump.componentTypeName);
        out += ",\"count\":" + std::to_string(dump.count);
        out += ",\"componentsPerElement\":" + std::to_string(dump.componentsPerElement);
        out += ",\"normalized\":" + std::string(dump.normalized ? "true" : "false");
        out += ",\"sparse\":" + std::string(dump.sparse ? "true" : "false");
        out += ",\"bufferView\":" + std::to_string(dump.bufferView);
        out += ",\"byteOffset\":" + std::to_string(dump.byteOffset);
        out += ",\"bufferViewByteOffset\":" + std::to_string(dump.bufferViewByteOffset);
        out += ",\"bufferViewByteStride\":" + std::to_string(dump.bufferViewByteStride);
        out += ",\"decoded\":" + std::string(dump.decoded ? "true" : "false");
        out += ",\"error\":" + Quote(dump.error);
        out += ",\"values\":[";
        for (std::size_t i = 0; i < dump.values.size(); ++i)
        {
            if (i != 0) { out += ","; }
            out += Num(dump.values[i]);
        }
        out += "]}";
        return out;
    }

    MeshOutDump DumpMeshOutEXT(const CNA::Internal::GltfImport::MeshOut& mesh)
    {
        MeshOutDump dump;
        dump.name = mesh.name;
        dump.stride = mesh.stride;
        dump.use32BitIndices = mesh.use32BitIndices;
        dump.skinned = mesh.skinned;
        dump.colored = mesh.colored;
        dump.usePbr = mesh.usePbr;
        dump.useDualTexture = mesh.useDualTexture;
        dump.metallicFactor = mesh.material.metallicFactor;
        dump.roughnessFactor = mesh.material.roughnessFactor;
        dump.ior = mesh.material.iorEXT;
        dump.specularFactor = mesh.material.specularFactorEXT;
        dump.specularColorFactor = {mesh.material.specularColorFactorEXT.X,
                                    mesh.material.specularColorFactorEXT.Y,
                                    mesh.material.specularColorFactorEXT.Z};
        dump.emissiveFactor = {mesh.material.emissiveFactor.X,
                               mesh.material.emissiveFactor.Y,
                               mesh.material.emissiveFactor.Z};
        dump.hasBaseColorImage = mesh.material.baseColorImage != nullptr;
        dump.hasOcclusionImage = mesh.material.occlusionImage != nullptr;
        dump.hasNormalImage = mesh.material.normalImage != nullptr;
        dump.hasMetallicRoughnessImage = mesh.material.metallicRoughnessImage != nullptr;
        dump.hasEmissiveImage = mesh.material.emissiveImage != nullptr;
        dump.morphTargetCount = mesh.morphPositionDeltas.size();
        dump.baseColorFactor = {mesh.material.baseColorFactor.X,
                                mesh.material.baseColorFactor.Y,
                                mesh.material.baseColorFactor.Z,
                                mesh.material.baseColorFactor.W};
        // GLTF-071 gave MeshOut a real topology member, so the source primitive's mode now reaches
        // L3 instead of being assumed. Reading it back here is what lets an L3 comparison assert
        // the topology rather than infer it from an index count.
        dump.topologyCarried = true;
        dump.topologyMode = CNA::Internal::GltfImport::PrimitiveTopologyMode(mesh.sourceTopology);
        dump.topologyName = CNA::Internal::GltfImport::PrimitiveTopologyName(mesh.sourceTopology);
        // GLTF-072 made the two distinct: the file's declared mode and the mode its index list is
        // in after conversion. Reading both back is what lets L3 assert the conversion happened
        // rather than merely that a topology is present.
        dump.importedTopologyMode = CNA::Internal::GltfImport::PrimitiveTopologyMode(mesh.topology);
        dump.importedTopologyName = CNA::Internal::GltfImport::PrimitiveTopologyName(mesh.topology);

        // Every layout in the stride ABI begins with a 3-float position, so a stride that cannot
        // hold one is not a layout this helper can read. Reporting zero vertices is the right
        // answer for a MeshOut that was never populated; guessing would read past the buffer.
        if (mesh.stride < 12) { return dump; }
        const std::size_t stride = static_cast<std::size_t>(mesh.stride);
        dump.vertexCount = mesh.vertexBytes.size() / stride;

        // Slot offsets per the stride ABI (plan_gltf.md §2.3). -1 means "this layout has no such
        // slot", which is itself part of the L3 answer.
        long long normalOffset = -1, tangentOffset = -1, uvOffset = -1, uv1Offset = -1;
        long long weightOffset = -1, jointOffset = -1, colorOffset = -1;
        switch (mesh.stride)
        {
            case 20: uvOffset = 12; break;
            case 24: colorOffset = 12; uvOffset = 16; break;
            case 32: normalOffset = 12; uvOffset = 24; break;
            case 48: normalOffset = 12; tangentOffset = 24; uvOffset = 40; break;
            case 52: normalOffset = 12; uvOffset = 24; weightOffset = 32; jointOffset = 48; break;
            case 56: normalOffset = 12; uvOffset = 24; weightOffset = 32; jointOffset = 48;
                     colorOffset = 52; break;
            case 60: normalOffset = 12; tangentOffset = 24; uvOffset = 40; uv1Offset = 48; break;
            case 68: normalOffset = 12; tangentOffset = 24; uvOffset = 40; weightOffset = 48;
                     jointOffset = 64; break;
            case 76: normalOffset = 12; tangentOffset = 24; uvOffset = 40; weightOffset = 48;
                     jointOffset = 64; uv1Offset = 68; break;
            default: break;
        }

        for (std::size_t v = 0; v < dump.vertexCount; ++v)
        {
            const std::size_t base = v * stride;
            dump.positions.push_back({ReadFloat(mesh.vertexBytes, base),
                                      ReadFloat(mesh.vertexBytes, base + 4),
                                      ReadFloat(mesh.vertexBytes, base + 8)});
            if (normalOffset >= 0)
            {
                const std::size_t o = base + static_cast<std::size_t>(normalOffset);
                dump.normals.push_back({ReadFloat(mesh.vertexBytes, o),
                                        ReadFloat(mesh.vertexBytes, o + 4),
                                        ReadFloat(mesh.vertexBytes, o + 8)});
            }
            if (tangentOffset >= 0)
            {
                const std::size_t o = base + static_cast<std::size_t>(tangentOffset);
                dump.tangents.push_back({ReadFloat(mesh.vertexBytes, o),
                                         ReadFloat(mesh.vertexBytes, o + 4),
                                         ReadFloat(mesh.vertexBytes, o + 8),
                                         ReadFloat(mesh.vertexBytes, o + 12)});
            }
            if (uvOffset >= 0)
            {
                const std::size_t o = base + static_cast<std::size_t>(uvOffset);
                dump.texcoords.push_back({ReadFloat(mesh.vertexBytes, o),
                                          ReadFloat(mesh.vertexBytes, o + 4)});
            }
            if (uv1Offset >= 0)
            {
                const std::size_t o = base + static_cast<std::size_t>(uv1Offset);
                dump.texcoords1.push_back({ReadFloat(mesh.vertexBytes, o),
                                           ReadFloat(mesh.vertexBytes, o + 4)});
            }
            if (weightOffset >= 0)
            {
                const std::size_t o = base + static_cast<std::size_t>(weightOffset);
                dump.weights.push_back({ReadFloat(mesh.vertexBytes, o),
                                        ReadFloat(mesh.vertexBytes, o + 4),
                                        ReadFloat(mesh.vertexBytes, o + 8),
                                        ReadFloat(mesh.vertexBytes, o + 12)});
            }
            if (jointOffset >= 0)
            {
                const std::size_t o = base + static_cast<std::size_t>(jointOffset);
                dump.joints.push_back({mesh.vertexBytes[o], mesh.vertexBytes[o + 1],
                                       mesh.vertexBytes[o + 2], mesh.vertexBytes[o + 3]});
            }
            if (colorOffset >= 0)
            {
                const std::size_t o = base + static_cast<std::size_t>(colorOffset);
                dump.colors.push_back({mesh.vertexBytes[o], mesh.vertexBytes[o + 1],
                                       mesh.vertexBytes[o + 2], mesh.vertexBytes[o + 3]});
            }
        }

        const std::size_t indexWidth = mesh.use32BitIndices ? 4u : 2u;
        for (std::size_t o = 0; o + indexWidth <= mesh.indexBytes.size(); o += indexWidth)
        {
            if (mesh.use32BitIndices)
            {
                std::uint32_t value = 0;
                std::memcpy(&value, mesh.indexBytes.data() + o, sizeof(value));
                dump.indices.push_back(value);
            }
            else
            {
                std::uint16_t value = 0;
                std::memcpy(&value, mesh.indexBytes.data() + o, sizeof(value));
                dump.indices.push_back(value);
            }
        }
        return dump;
    }

    std::string ToJson(const MeshOutDump& dump)
    {
        std::string out = "{";
        out += "\"name\":" + Quote(dump.name);
        out += ",\"stride\":" + std::to_string(dump.stride);
        out += ",\"vertexCount\":" + std::to_string(dump.vertexCount);
        out += ",\"topologyCarried\":" + std::string(dump.topologyCarried ? "true" : "false");
        out += ",\"topologyMode\":" + std::to_string(dump.topologyMode);
        out += ",\"topologyName\":" + Quote(dump.topologyName);
        out += ",\"importedTopologyMode\":" + std::to_string(dump.importedTopologyMode);
        out += ",\"importedTopologyName\":" + Quote(dump.importedTopologyName);
        out += ",\"skinned\":" + std::string(dump.skinned ? "true" : "false");
        out += ",\"colored\":" + std::string(dump.colored ? "true" : "false");
        out += ",\"usePbr\":" + std::string(dump.usePbr ? "true" : "false");
        out += ",\"useDualTexture\":" + std::string(dump.useDualTexture ? "true" : "false");
        out += ",\"use32BitIndices\":" + std::string(dump.use32BitIndices ? "true" : "false");
        out += ",\"metallicFactor\":" + Num(dump.metallicFactor);
        out += ",\"roughnessFactor\":" + Num(dump.roughnessFactor);
        out += ",\"ior\":" + Num(dump.ior);
        out += ",\"specularFactor\":" + Num(dump.specularFactor);
        out += ",\"specularColorFactor\":[" + Num(dump.specularColorFactor[0]) + "," +
               Num(dump.specularColorFactor[1]) + "," + Num(dump.specularColorFactor[2]) + "]";
        out += ",\"emissiveFactor\":[" + Num(dump.emissiveFactor[0]) + "," +
               Num(dump.emissiveFactor[1]) + "," + Num(dump.emissiveFactor[2]) + "]";
        out += ",\"hasBaseColorImage\":" + std::string(dump.hasBaseColorImage ? "true" : "false");
        out += ",\"hasOcclusionImage\":" + std::string(dump.hasOcclusionImage ? "true" : "false");
        out += ",\"hasNormalImage\":" + std::string(dump.hasNormalImage ? "true" : "false");
        out += ",\"hasMetallicRoughnessImage\":" +
               std::string(dump.hasMetallicRoughnessImage ? "true" : "false");
        out += ",\"hasEmissiveImage\":" + std::string(dump.hasEmissiveImage ? "true" : "false");
        out += ",\"morphTargetCount\":" + std::to_string(dump.morphTargetCount);
        out += ",\"positions\":" + FloatArray(dump.positions);
        out += ",\"normals\":" + FloatArray(dump.normals);
        out += ",\"tangents\":" + FloatArray(dump.tangents);
        out += ",\"texcoords\":" + FloatArray(dump.texcoords);
        out += ",\"texcoords1\":" + FloatArray(dump.texcoords1);
        out += ",\"colors\":" + ByteArray(dump.colors);
        out += ",\"weights\":" + FloatArray(dump.weights);
        out += ",\"joints\":" + ByteArray(dump.joints);
        out += ",\"indices\":[";
        for (std::size_t i = 0; i < dump.indices.size(); ++i)
        {
            if (i != 0) { out += ","; }
            out += std::to_string(dump.indices[i]);
        }
        out += "]}";
        return out;
    }

    std::vector<ExtractedPrimitive> ExtractSceneMeshesEXT(const cgltf_data& data)
    {
        using namespace CNA::Internal::GltfImport;

        std::vector<ExtractedPrimitive> out;
        for (const MeshGroup& group : CollectMeshGroups(&data))
        {
            SkeletonResult skeleton;
            const bool hasSkin = group.skin != nullptr;
            std::string skeletonError;
            if (hasSkin)
            {
                // Total, like every other helper here: a skin CNA refuses -- an over-limit rig
                // (GLTF-261), say -- is recorded as this group's error rather than thrown through
                // the sweep, so one malformed fixture cannot take every other one's L3 with it.
                try { skeleton = BuildSkeleton(group.skin, 1.0f); }
                catch (const std::exception& e) { skeletonError = e.what(); }
            }

            for (const MeshInstanceOut& placement : group.instances)
            {
                const cgltf_mesh* mesh = placement.mesh;
                if (mesh == nullptr) { continue; }
                for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
                {
                    ExtractedPrimitive entry;
                    entry.mesh = static_cast<int>(mesh - data.meshes);
                    entry.primitive = static_cast<int>(p);
                    entry.meshName = mesh->name != nullptr ? mesh->name : "";
                    if (!skeletonError.empty())
                    {
                        entry.error = skeletonError;
                        out.push_back(std::move(entry));
                        continue;
                    }
                    try
                    {
                        const MeshOut extracted = ExtractMesh(
                            &data, mesh->primitives[p], entry.meshName.empty() ? "primitive"
                                                                               : entry.meshName,
                            hasSkin ? &skeleton : nullptr, 1.0f);
                        entry.dump = DumpMeshOutEXT(extracted);
                        entry.vertexBytes = extracted.vertexBytes;
                        entry.indexBytes = extracted.indexBytes;
                        entry.extracted = true;
                    }
                    catch (const std::exception& e)
                    {
                        entry.error = e.what();
                    }
                    out.push_back(std::move(entry));
                }
            }
        }
        return out;
    }

    WorldPositions EvaluateWorldPositionsEXT(const cgltf_data& data)
    {
        WorldPositions positions;

        const cgltf_scene* scene = data.scene != nullptr
            ? data.scene : (data.scenes_count > 0 ? &data.scenes[0] : nullptr);
        // §3.5 permits a file with no `scenes` array at all, and defines that as "nothing is
        // REQUIRED to be rendered" -- which is not "nothing may be". CNA imports every root node
        // in that case, which is the reading every viewer takes, and the oracle mirrors the
        // decision deliberately: returning nothing here would make `scene-no-scenes` assert that
        // CNA does the opposite of what it documents (plan_gltf.md GLTF-399).
        std::vector<const cgltf_node*> fallbackRoots;
        if (scene == nullptr)
        {
            for (cgltf_size i = 0; i < data.nodes_count; ++i)
            {
                if (data.nodes[i].parent == nullptr) { fallbackRoots.push_back(&data.nodes[i]); }
            }
            if (fallbackRoots.empty()) { return positions; }
        }

        struct Walker
        {
            const cgltf_data& data;
            WorldPositions& out;
            // A node graph is a tree in any valid file, but nothing in the parse prevents a
            // malformed one from containing a cycle, and this helper must terminate on any input
            // it is handed rather than recursing until the stack runs out.
            std::unordered_set<const cgltf_node*> onPath;

            void Visit(const cgltf_node& node, const GltfMatrix& parentWorld)
            {
                if (!onPath.insert(&node).second)
                {
                    out.selfCheckPassed = false;  // a cycle: no world transform is well defined
                    return;
                }
                const GltfMatrix world = Multiply(parentWorld, NodeLocalMatrix(node));

                // Independent cross-check: cgltf composes the same chain from the same file with
                // its own implementation. Disagreement means the harness is broken, not CNA.
                cgltf_float reference[16];
                cgltf_node_transform_world(&node, reference);
                for (std::size_t i = 0; i < 16; ++i)
                {
                    if (std::fabs(reference[i] - world[i]) > 1e-4f) { out.selfCheckPassed = false; }
                }

                if (node.mesh != nullptr)
                {
                    // A skinned mesh is not placed by its own node: the specification (§3.7.3) has
                    // the joints place it, and the joint matrix carries
                    // inverse(globalTransform(meshNode)) precisely so that node's transform cancels
                    // out. The node's world transform is still computed and self-checked above --
                    // it is simply not this mesh's placement. The skinned result is a separate
                    // expectation (l4.skin), not this one.
                    const GltfMatrix placement = node.skin != nullptr ? IdentityMatrix() : world;
                    for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
                    {
                        WorldInstance instance;
                        instance.node = static_cast<int>(&node - data.nodes);
                        instance.nodeName = node.name != nullptr ? node.name : "";
                        instance.mesh = static_cast<int>(node.mesh - data.meshes);
                        instance.primitive = static_cast<int>(p);
                        instance.worldMatrix = placement;
                        for (const std::array<float, 3>& local :
                             PrimitivePositions(data, node.mesh->primitives[p]))
                        {
                            instance.worldPositions.push_back(
                                TransformPoint(placement, local[0], local[1], local[2]));
                        }
                        out.instances.push_back(std::move(instance));
                    }
                }

                for (cgltf_size i = 0; i < node.children_count; ++i)
                {
                    if (node.children[i] != nullptr) { Visit(*node.children[i], world); }
                }
                onPath.erase(&node);
            }
        };

        Walker walker{data, positions, {}};
        if (scene == nullptr)
        {
            for (const cgltf_node* root : fallbackRoots) { walker.Visit(*root, IdentityMatrix()); }
        }
        else
        {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            {
                walker.Visit(*scene->nodes[i], IdentityMatrix());
            }
        }
        AccumulateBounds(positions);
        return positions;
    }

    WorldPositions EvaluateCnaWorldPositionsEXT(const cgltf_data& data)
    {
        using namespace CNA::Internal::GltfImport;

        WorldPositions positions;
        for (const MeshGroup& group : CollectMeshGroups(&data))
        {
            SkeletonResult skeleton;
            const bool hasSkin = group.skin != nullptr;
            if (hasSkin) { skeleton = BuildSkeleton(group.skin, 1.0f); }

            for (const MeshInstanceOut& placement : group.instances)
            {
                const cgltf_mesh* mesh = placement.mesh;
                if (mesh == nullptr) { continue; }
                for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
                {
                    WorldInstance instance;
                    // plan_gltf.md GLTF-113/GLTF-114: MeshGroup now carries the instancing node and
                    // its composed world transform, so this reports what CNA really places rather
                    // than a hardcoded identity. A skinned instance is deliberately reported at the
                    // identity root, matching what both loaders do with it -- glTF ignores a skinned
                    // mesh's own node transform, and completing that rule is GLTF-245/247/260.
                    instance.node = placement.node != nullptr
                        ? static_cast<int>(placement.node - data.nodes) : -1;
                    instance.nodeName = (placement.node != nullptr && placement.node->name != nullptr)
                        ? placement.node->name : "";
                    instance.mesh = static_cast<int>(mesh - data.meshes);
                    instance.primitive = static_cast<int>(p);
                    const GltfMatrix world = placement.skinned
                        ? IdentityMatrix() : ToGltfMatrixEXT(placement.worldTransform);
                    instance.worldMatrix = world;
                    try
                    {
                        const MeshOut out = ExtractMesh(&data, mesh->primitives[p], "oracle",
                                                        hasSkin ? &skeleton : nullptr, 1.0f);
                        for (const std::array<float, 3>& local : DumpMeshOutEXT(out).positions)
                        {
                            instance.worldPositions.push_back(
                                TransformPoint(world, local[0], local[1], local[2]));
                        }
                    }
                    catch (const std::exception&)
                    {
                        // A primitive CNA cannot extract contributes no geometry; the empty
                        // instance still records that the group referenced it.
                    }
                    positions.instances.push_back(std::move(instance));
                }
            }
        }
        AccumulateBounds(positions);
        return positions;
    }

    std::string ToJson(const WorldPositions& positions)
    {
        std::string out = "{\"selfCheckPassed\":";
        out += positions.selfCheckPassed ? "true" : "false";
        out += ",\"hasBounds\":" + std::string(positions.hasBounds ? "true" : "false");
        out += ",\"min\":[" + Num(positions.min[0]) + "," + Num(positions.min[1]) + "," +
               Num(positions.min[2]) + "]";
        out += ",\"max\":[" + Num(positions.max[0]) + "," + Num(positions.max[1]) + "," +
               Num(positions.max[2]) + "]";
        out += ",\"instances\":[";
        for (std::size_t i = 0; i < positions.instances.size(); ++i)
        {
            const WorldInstance& instance = positions.instances[i];
            if (i != 0) { out += ","; }
            out += "{\"node\":" + std::to_string(instance.node);
            out += ",\"nodeName\":" + Quote(instance.nodeName);
            out += ",\"mesh\":" + std::to_string(instance.mesh);
            out += ",\"primitive\":" + std::to_string(instance.primitive);
            out += ",\"worldMatrixColumnMajor\":[";
            for (std::size_t c = 0; c < 16; ++c)
            {
                if (c != 0) { out += ","; }
                out += Num(instance.worldMatrix[c]);
            }
            out += "],\"worldPositions\":" + FloatArray(instance.worldPositions) + "}";
        }
        out += "]}";
        return out;
    }
}
