// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-005 / GLTF-006 -- see GltfOracleEXT.hpp for the scope rules. Test scope only.

#include "GltfOracleEXT.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_set>

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

        /// The primitive's POSITION values, decoded straight from its accessor. Deliberately not
        /// routed through MeshOut: the L4 oracle must stay usable on a fixture whose L3 is wrong.
        std::vector<std::array<float, 3>> PrimitivePositions(const cgltf_primitive& prim)
        {
            std::vector<std::array<float, 3>> out;
            const cgltf_accessor* accessor =
                cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
            if (accessor == nullptr || cgltf_num_components(accessor->type) != 3) { return out; }
            std::vector<float> raw(static_cast<std::size_t>(accessor->count) * 3);
            if (cgltf_accessor_unpack_floats(accessor, raw.data(), raw.size()) != raw.size())
            {
                return out;
            }
            out.reserve(static_cast<std::size_t>(accessor->count));
            for (cgltf_size v = 0; v < accessor->count; ++v)
            {
                const std::size_t o = static_cast<std::size_t>(v) * 3;
                out.push_back({raw[o], raw[o + 1], raw[o + 2]});
            }
            return out;
        }

        std::vector<std::array<float, 3>> MeshPositions(const cgltf_mesh& mesh)
        {
            std::vector<std::array<float, 3>> out;
            for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
            {
                const std::vector<std::array<float, 3>> prim = PrimitivePositions(mesh.primitives[p]);
                out.insert(out.end(), prim.begin(), prim.end());
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

        std::vector<float> values(dump.count * dump.componentsPerElement);
        const cgltf_size unpacked = values.empty()
            ? 0 : cgltf_accessor_unpack_floats(&accessor, values.data(), values.size());
        if (unpacked != values.size())
        {
            dump.error = "cgltf_accessor_unpack_floats decoded " + std::to_string(unpacked) +
                         " of " + std::to_string(values.size()) + " components";
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
        dump.metallicFactor = mesh.metallicFactor;
        dump.roughnessFactor = mesh.roughnessFactor;
        dump.emissiveFactor = {mesh.emissiveFactor.X, mesh.emissiveFactor.Y, mesh.emissiveFactor.Z};
        dump.hasBaseColorImage = mesh.baseColorImage != nullptr;
        dump.hasOcclusionImage = mesh.occlusionImage != nullptr;
        dump.hasNormalImage = mesh.normalImage != nullptr;
        dump.hasMetallicRoughnessImage = mesh.metallicRoughnessImage != nullptr;
        dump.hasEmissiveImage = mesh.emissiveImage != nullptr;
        dump.morphTargetCount = mesh.morphPositionDeltas.size();
        dump.baseColorFactor = {mesh.baseColorFactor.X, mesh.baseColorFactor.Y,
                                mesh.baseColorFactor.Z, mesh.baseColorFactor.W};
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
        long long normalOffset = -1, tangentOffset = -1, uvOffset = -1;
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
            case 68: normalOffset = 12; tangentOffset = 24; uvOffset = 40; weightOffset = 48;
                     jointOffset = 64; break;
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
            if (hasSkin) { skeleton = BuildSkeleton(group.skin, 1.0f); }

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
                    try
                    {
                        const MeshOut extracted = ExtractMesh(
                            &data, mesh->primitives[p], entry.meshName.empty() ? "primitive"
                                                                               : entry.meshName,
                            hasSkin ? &skeleton : nullptr, 1.0f);
                        entry.dump = DumpMeshOutEXT(extracted);
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
        if (scene == nullptr) { return positions; }

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
                    WorldInstance instance;
                    instance.node = static_cast<int>(&node - data.nodes);
                    instance.nodeName = node.name != nullptr ? node.name : "";
                    instance.mesh = static_cast<int>(node.mesh - data.meshes);
                    // A skinned mesh is not placed by its own node: the specification (§3.7.3) has
                    // the joints place it, and the joint matrix carries
                    // inverse(globalTransform(meshNode)) precisely so that node's transform cancels
                    // out. The node's world transform is still computed and self-checked above --
                    // it is simply not this mesh's placement. The skinned result is a separate
                    // expectation (l4.skin), not this one.
                    const GltfMatrix placement = node.skin != nullptr ? IdentityMatrix() : world;
                    instance.worldMatrix = placement;
                    for (const std::array<float, 3>& p : MeshPositions(*node.mesh))
                    {
                        instance.worldPositions.push_back(TransformPoint(placement, p[0], p[1], p[2]));
                    }
                    out.instances.push_back(std::move(instance));
                }

                for (cgltf_size i = 0; i < node.children_count; ++i)
                {
                    if (node.children[i] != nullptr) { Visit(*node.children[i], world); }
                }
                onPath.erase(&node);
            }
        };

        Walker walker{data, positions, {}};
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
        {
            walker.Visit(*scene->nodes[i], IdentityMatrix());
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
