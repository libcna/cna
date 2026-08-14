// SPDX-License-Identifier: MS-PL
#pragma once

// plan_gltf.md GLTF-005 / GLTF-006: the L2, L3 and L4 rungs of the numerical oracle ladder
// (plan_gltf.md §7.1).
//
// DIAGNOSTIC/TEST SCOPE ONLY. Nothing here is CNA public API, CNAEXT surface, or part of
// CNA::Internal::GltfImport. It is compiled into CnaTests and the standalone glTF converter's
// `--dump-oracle` diagnostic mode; no translation unit under modules/*/src may call it. The
// EXT-suffixed names are the ones plan_gltf.md's task rows cite, so a later session searching for
// DumpAccessorEXT / DumpMeshOutEXT / EvaluateWorldPositionsEXT finds them -- the namespace, not
// the suffix, is what marks them non-runtime.
//
// The point of these helpers is to answer "at which layer does reality first diverge?" with
// numbers rather than screenshots. They therefore never drive production behaviour: the world
// oracle recomputes glTF's node composition independently instead of asking CNA for it, precisely
// so it can measure how far CNA is from the specification while D1-D3 remain unfixed.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "cgltf.h"

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

namespace CnaTest::GltfOracle
{
    /** @brief A 4x4 transform in glTF's own flat column-major layout (specification §3.5.3). */
    using GltfMatrix = std::array<float, 16>;

    /** @brief The 4x4 identity in glTF column-major layout. */
    [[nodiscard]] GltfMatrix IdentityMatrix();

    /**
     * @brief Composes a node's local transform per specification §3.5.3.
     *
     * Uses `matrix` when present, otherwise `T * R * S` with glTF's own defaults for the absent
     * TRS properties. Implemented here rather than delegated to cgltf so the oracle is an
     * independent second opinion.
     */
    [[nodiscard]] GltfMatrix NodeLocalMatrix(const cgltf_node& node);

    /** @brief `a * b` for column-major, column-vector matrices: `(a*b)v == a(b*v)`. */
    [[nodiscard]] GltfMatrix Multiply(const GltfMatrix& a, const GltfMatrix& b);

    /** @brief Transforms a point by a column-major matrix, with an implicit `w = 1`. */
    [[nodiscard]] std::array<float, 3> TransformPoint(const GltfMatrix& m, float x, float y, float z);

    // --- L2: decoded accessor arrays -------------------------------------------------------

    /**
     * @brief One accessor's structure and fully decoded component array (oracle layer L2).
     *
     * Decoded exactly as CNA's own `UnpackAccessor` decodes an attribute -- through
     * `cgltf_accessor_unpack_floats`, which resolves `byteStride`, both byte offsets, `normalized`
     * conversion and `sparse` overrides. Recording the structural fields alongside the values is
     * what lets a failure name the *cause* (a wrong stride, a wrong offset, a missed sparse
     * override) instead of just reporting different numbers.
     */
    struct AccessorDump
    {
        /** @brief The accessor's index in `cgltf_data::accessors`. */
        std::size_t index = 0;
        /** @brief The accessor's element type as spelled in the file (e.g. "VEC3"). */
        std::string type;
        /** @brief The accessor's glTF component type constant (e.g. 5126 for FLOAT). */
        int componentType = 0;
        /** @brief The component type's spec name (e.g. "FLOAT"). */
        std::string componentTypeName;
        /** @brief Number of elements in the accessor. */
        std::size_t count = 0;
        /** @brief Components per element (3 for VEC3, 16 for MAT4, ...). */
        std::size_t componentsPerElement = 0;
        /** @brief True when the accessor declares `normalized`. */
        bool normalized = false;
        /** @brief True when the accessor carries a `sparse` object. */
        bool sparse = false;
        /** @brief Index of the base bufferView, or -1 when the accessor has none. */
        int bufferView = -1;
        /** @brief The accessor's own `byteOffset`. */
        std::size_t byteOffset = 0;
        /** @brief The base bufferView's `byteOffset`, or -1 when there is no base bufferView. */
        long long bufferViewByteOffset = -1;
        /** @brief The base bufferView's `byteStride`, or -1 when it declares none. */
        long long bufferViewByteStride = -1;
        /** @brief `count * componentsPerElement` decoded components, in element order. */
        std::vector<float> values;
        /** @brief True when decoding succeeded; false leaves `values` empty and sets `error`. */
        bool decoded = false;
        /** @brief Human-readable reason `decoded` is false. */
        std::string error;
    };

    /** @brief Decodes one accessor into an `AccessorDump` (L2). Never throws. */
    [[nodiscard]] AccessorDump DumpAccessorEXT(const cgltf_data& data, std::size_t accessorIndex);

    /** @brief Stable JSON serialisation of an `AccessorDump`, for diagnostics and round-tripping. */
    [[nodiscard]] std::string ToJson(const AccessorDump& dump);

    // --- L3: semantic mesh ------------------------------------------------------------------

    /**
     * @brief One extracted primitive's semantic streams, unpacked from `MeshOut` (oracle layer L3).
     *
     * `MeshOut` stores vertices as opaque bytes whose meaning is decided solely by `stride` (the
     * de-facto CNA/glTF ABI, plan_gltf.md §2.3). This dump reverses that packing back into named
     * streams so an L3 comparison can say *which attribute* diverged rather than *which byte*.
     */
    struct MeshOutDump
    {
        /** @brief The mesh part's name, as `ExtractMesh` assigned it. */
        std::string name;
        /** @brief The selected vertex stride, which implies the whole layout. */
        int stride = 0;
        /** @brief Number of vertices (`vertexBytes.size() / stride`). */
        std::size_t vertexCount = 0;
        /** @brief Per-vertex positions, always present. */
        std::vector<std::array<float, 3>> positions;
        /** @brief Per-vertex normals, empty for a layout with no normal slot. */
        std::vector<std::array<float, 3>> normals;
        /** @brief Per-vertex tangents, empty outside the PBR layouts. */
        std::vector<std::array<float, 4>> tangents;
        /** @brief Per-vertex texture coordinates, empty when the layout has no UV slot. */
        std::vector<std::array<float, 2>> texcoords;
        /** @brief Per-vertex colours as the bytes actually packed, empty when uncoloured. */
        std::vector<std::array<std::uint8_t, 4>> colors;
        /** @brief Per-vertex blend weights, empty when unskinned. */
        std::vector<std::array<float, 4>> weights;
        /** @brief Per-vertex blend indices (already remapped to reordered bone indices). */
        std::vector<std::array<std::uint8_t, 4>> joints;
        /** @brief The index list, widened to 32 bits regardless of the stored width. */
        std::vector<std::uint32_t> indices;
        /** @brief True when the indices were stored as 32-bit values. */
        bool use32BitIndices = false;
        /** @brief `MeshOut::skinned`. */
        bool skinned = false;
        /** @brief `MeshOut::colored`. */
        bool colored = false;
        /** @brief `MeshOut::usePbr`. */
        bool usePbr = false;
        /** @brief `MeshOut::useDualTexture`. */
        bool useDualTexture = false;
        /** @brief The material's base colour factor (RGBA) as it reached `MeshOut` (`GLTF-216`). */
        std::array<float, 4> baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        /** @brief `MeshOut::material.metallicFactor`. */
        float metallicFactor = 0.0f;
        /** @brief `MeshOut::material.roughnessFactor`. */
        float roughnessFactor = 0.0f;
        /** @brief `MeshOut::material.iorEXT`. */
        float ior = 1.5f;
        /** @brief `MeshOut::material.specularFactorEXT`. */
        float specularFactor = 1.0f;
        /** @brief `MeshOut::material.specularColorFactorEXT`. */
        std::array<float, 3> specularColorFactor{1.0f, 1.0f, 1.0f};
        /** @brief `MeshOut::material.emissiveFactor`. */
        std::array<float, 3> emissiveFactor{};
        /** @brief Which material images survived import, by presence only. */
        bool hasBaseColorImage = false;
        /** @brief True when an occlusion image survived import. */
        bool hasOcclusionImage = false;
        /** @brief True when a normal map survived import. */
        bool hasNormalImage = false;
        /** @brief True when a metallic-roughness map survived import. */
        bool hasMetallicRoughnessImage = false;
        /** @brief True when an emissive map survived import. */
        bool hasEmissiveImage = false;
        /** @brief Number of morph targets carried. */
        std::size_t morphTargetCount = 0;
        /**
         * @brief Whether `MeshOut` carries the source primitive's topology at all.
         *
         * False on the campaign baseline, where `MeshOut` had no mode/topology field at all --
         * defect D5 stated structurally rather than as a value comparison. `GLTF-071` made it
         * true, so a fixture's L3 topology can now be asserted rather than inferred.
         */
        bool topologyCarried = false;
        /**
         * @brief The `mesh.primitive.mode` the SOURCE file declared, or -1 when none is carried.
         *
         * This is the spec-derived quantity a fixture's `l3.mode` states. It is unaffected by any
         * import-time conversion, which is exactly what makes a conversion checkable.
         */
        int topologyMode = -1;
        /** @brief The source topology's specification name, empty when no topology is carried. */
        std::string topologyName;
        /**
         * @brief The mode the emitted index list is actually in, after `GLTF-072`'s conversion.
         *
         * `4` (`TRIANGLES`) for every primitive `ExtractMesh` returns. A fixture's
         * `l3.importPolicy` states this separately from `l3.mode`, because it is CNA's own
         * documented conversion policy (plan_gltf.md §10.1) rather than a specification value.
         */
        int importedTopologyMode = -1;
        /** @brief The imported topology's specification name, empty when no topology is carried. */
        std::string importedTopologyName;
    };

    /** @brief Unpacks a `MeshOut` into named semantic streams (L3). */
    [[nodiscard]] MeshOutDump DumpMeshOutEXT(const CNA::Internal::GltfImport::MeshOut& mesh);

    /** @brief One primitive as the production import path actually produced it. */
    struct ExtractedPrimitive
    {
        /** @brief The source mesh's index in `cgltf_data::meshes`. */
        int mesh = -1;
        /** @brief The primitive's index within that mesh. */
        int primitive = -1;
        /** @brief The source mesh's name, empty when unnamed. */
        std::string meshName;
        /** @brief True when `ExtractMesh` returned; false leaves `dump` default and sets `error`. */
        bool extracted = false;
        /** @brief The exception message when extraction failed. */
        std::string error;
        /** @brief The L3 view of what `ExtractMesh` produced. */
        MeshOutDump dump;
        /** @brief Exact L5 vertex bytes emitted by `ExtractMesh`. */
        std::vector<std::uint8_t> vertexBytes;
        /** @brief Exact L5 index bytes emitted by `ExtractMesh`. */
        std::vector<std::uint8_t> indexBytes;
    };

    /**
     * @brief Runs the real import path over a file and dumps every primitive it yields (L3).
     *
     * Goes through `CollectMeshGroups` and `ExtractMesh` exactly as both loaders do, so what comes
     * back is CNA's genuine output -- including duplicates where a mesh is instanced more than
     * once, and exclusions where a mesh is not reachable from the default scene.
     *
     * @param data The parsed glTF file, with buffers already loaded.
     * @return One entry per extracted primitive, in import order.
     */
    [[nodiscard]] std::vector<ExtractedPrimitive> ExtractSceneMeshesEXT(const cgltf_data& data);

    /** @brief Stable JSON serialisation of a `MeshOutDump`, for diagnostics and round-tripping. */
    [[nodiscard]] std::string ToJson(const MeshOutDump& dump);

    // --- L4: world-space geometry -----------------------------------------------------------

    /** @brief One mesh instance's world placement and transformed vertex positions (layer L4). */
    struct WorldInstance
    {
        /** @brief The instancing node's index, or -1 when the producer has no node concept. */
        int node = -1;
        /** @brief The instancing node's name, empty when unnamed or absent. */
        std::string nodeName;
        /** @brief The instanced mesh's index. */
        int mesh = -1;
        /** @brief The primitive within the instanced mesh. */
        int primitive = 0;
        /** @brief The composed world transform, glTF column-major. */
        GltfMatrix worldMatrix = IdentityMatrix();
        /** @brief The instance's vertex positions in world space. */
        std::vector<std::array<float, 3>> worldPositions;
    };

    /** @brief Every mesh instance of a file in world space, plus the union bounds. */
    struct WorldPositions
    {
        /** @brief One entry per instanced primitive, in scene traversal then primitive order. */
        std::vector<WorldInstance> instances;
        /** @brief Component-wise minimum over every instance's positions. */
        std::array<float, 3> min{};
        /** @brief Component-wise maximum over every instance's positions. */
        std::array<float, 3> max{};
        /** @brief False when no instance contributed any vertex, leaving the bounds meaningless. */
        bool hasBounds = false;
        /**
         * @brief True when this oracle's own node composition agreed with `cgltf_node_transform_world`.
         *
         * The expected-side oracle computes every world matrix from the specification's equations
         * itself and then cross-checks the result against cgltf's independent implementation. A
         * false here means the harness disagrees with itself and no verdict it produces can be
         * trusted -- it is not a statement about CNA.
         */
        bool selfCheckPassed = true;
    };

    /**
     * @brief The expected world-space geometry of a file, computed independently of CNA (layer L4).
     *
     * Walks the default scene, composes `world(node) = world(parent) * local(node)` per §3.5.3,
     * and applies each instance's world matrix to its mesh's decoded positions. Positions are read
     * straight from the POSITION accessors rather than from `MeshOut`, so this stays an oracle
     * even for a fixture whose L3 output is wrong.
     *
     * @param data The parsed glTF file, with buffers already loaded.
     * @return The expected instances and bounds.
     */
    [[nodiscard]] WorldPositions EvaluateWorldPositionsEXT(const cgltf_data& data);

    /**
     * @brief The world-space geometry CNA actually produces today, for comparison against the oracle.
     *
     * Runs the real import path -- `CollectMeshGroups` then `ExtractMesh` -- and reports the
     * result. Because `MeshGroup` carries no node and no matrix, every instance necessarily comes
     * back with an identity world transform; that is the measurement, not a limitation of this
     * helper. When Phase 5 gives mesh instances a real transform, this function starts returning
     * composed matrices and the corresponding known-defect tests fail, which is the intended
     * signal.
     *
     * @param data The parsed glTF file, with buffers already loaded.
     * @return CNA's current instances and bounds.
     */
    [[nodiscard]] WorldPositions EvaluateCnaWorldPositionsEXT(const cgltf_data& data);

    /** @brief Stable JSON serialisation of a `WorldPositions`, for diagnostics. */
    [[nodiscard]] std::string ToJson(const WorldPositions& positions);
}
