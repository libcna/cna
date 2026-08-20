// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-70/71 (Phase 13D): end-to-end regression test for runtime (non-CLI) glTF
// loading -- ContentManager::Load<Model>("name") finding and parsing a "name.gltf" file directly
// via ModelTypeReader::ReadGltfModel(), with no intermediate .cnj/binary sidecar files at all.
// Complements GltfToCnjToolTests.cpp (which tests the offline CLI tool, a separate process) --
// these tests call ContentManager in-process instead, since ReadGltfModel() is now a library
// function shared with the CLI tool via CNA::Internal::GltfImport::GltfImportCore, not a
// subprocess.
//
// Two embedded fixtures, both self-contained (single base64 data-URI buffer, no external files):
//   - kBasicTexturedGltf: an unskinned single triangle with a base-color texture -- proves the
//     extension resolution (.gltf found via ModelTypeReader::GetExtensions()), parsing, vertex/
//     index buffer construction, and in-memory texture decoding (MemoryStream + Texture2D::
//     FromStream, no temp file) all work for the simplest case.
//   - kSkinnedAnimatedGltf: a two-bone skinned, textured, animated triangle with skin.joints
//     deliberately reversed ([1, 0], child before parent) -- proves topological bone reordering,
//     SkinningData/AnimationClip construction, and SkinnedEffect wiring all work when built
//     directly in-memory (no .skeleton.bin/.clip.cnj round-trip at all).

#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Content;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Tests-only scratch content root, unique per test process run. Mirrors the established
    // convention in GltfToCnjToolTests.cpp/CnjModelTests.cpp.
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_runtime_gltf_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }
        ~ScratchDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        ScratchDir(const ScratchDir&) = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    const char* kBasicTexturedGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }, "material": 0 } ] } ],
  "materials": [ { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 3, "mimeType": "image/png" } ],
  "buffers": [ {
    "byteLength": 165,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    // glTF extensions (CNB-97, Phase 14H): identical geometry to kBasicTexturedGltf above, plus a
    // second scene node carrying one KHR_lights_punctual directional light pointing straight down
    // (no rotation authored -- glTF's own default node orientation already points -Z, i.e.
    // world-space (0,0,-1), so this deliberately does NOT re-derive the quaternion-rotation case
    // GltfImportCoreTests.cpp's own ExtractPunctualLightsEXT test already covers byte-for-byte).
    const char* kBasicTexturedWithLightGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "MeshNode", "mesh": 0 },
    { "name": "Light", "extensions": { "KHR_lights_punctual": { "light": 0 } } }
  ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }, "material": 0 } ] } ],
  "materials": [ { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 3, "mimeType": "image/png" } ],
  "extensions": {
    "KHR_lights_punctual": {
      "lights": [ { "type": "directional", "color": [0.25, 0.5, 0.75], "intensity": 1.0 } ]
    }
  },
  "extensionsUsed": [ "KHR_lights_punctual" ],
  "buffers": [ {
    "byteLength": 165,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    // Two bones: ParentBone (node 0, root) -> ChildBone (node 1). skin.joints = [1, 0] --
    // deliberately reversed (child listed before parent), the same topological-reorder stress
    // case GltfToCnjToolTests.cpp's own kTinySkinnedGltf uses. All 3 vertices are fully weighted
    // to joint index 1 (the child, per skin.joints[1]=0 -- i.e. the *second* entry names the
    // parent). One LINEAR translation channel on the child bone: (0,0,0) at t=0 -> (2,0,0) at t=1.
    const char* kSkinnedAnimatedGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 2] } ],
  "nodes": [
    { "name": "ParentBone", "children": [1] },
    { "name": "ChildBone" },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4
  }, "material": 0 } ] } ],
  "materials": [ { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 8, "mimeType": "image/png" } ],
  "skins": [ { "joints": [1, 0], "inverseBindMatrices": 5 } ],
  "animations": [ {
    "name": "Wave",
    "samplers": [ { "input": 6, "output": 7, "interpolation": "LINEAR" } ],
    "channels": [ { "sampler": 0, "target": { "node": 1, "path": "translation" } } ]
  } ],
  "buffers": [ {
    "byteLength": 397,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AQAAAAAAAAABAAAAAAAAAAEAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAIlQTkcNChoKAAAADUlIRFIAAAABAAAAAQgCAAAAkHdT3gAAAAxJREFUeJxj+M/AAAADAQEAyf6S7wAAAABJRU5ErkJggg=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 128 },
    { "buffer": 0, "byteOffset": 296, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 304, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 328, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "MAT4" },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 7, "componentType": 5126, "count": 2, "type": "VEC3" }
  ]
})GLTF";

    // CNB-64/65 (Phase 13B): an unskinned triangle with one morph target (POSITION delta only,
    // uniform +Z=1 per vertex), a non-zero default weight (mesh.weights=[0.5], to prove the
    // *initial* upload reflects the file's own default blend, not always the raw base pose), and
    // a LINEAR "weights" animation channel (0.0 at t=0 -> 1.0 at t=1) on the mesh's own node.
    const char* kMorphedTriangleGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ {
    "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }, "targets": [ { "POSITION": 3 } ] } ],
    "weights": [0.5]
  } ],
  "animations": [ {
    "name": "Morph",
    "samplers": [ { "input": 4, "output": 5, "interpolation": "LINEAR" } ],
    "channels": [ { "sampler": 0, "target": { "node": 0, "path": "weights" } } ]
  } ],
  "buffers": [ {
    "byteLength": 148,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAgD8AAAAAAACAPw=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 132, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 140, "byteLength": 8 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] }
  ]
})GLTF";

    // CUBICSPLINE interpolation for morph-weight animation tracks: identical shape to
    // kMorphedTriangleGltf above, but the "weights" channel is CUBICSPLINE-interpolated with both
    // endpoint tangents zero (keyframe0: outTangent=0, keyframe1: inTangent=0) -- the Hermite
    // basis then reduces to h00(s)*v0 + h01(s)*v1, giving 0.15625 at s=0.25 (not the LINEAR 0.25),
    // the same hand-derived value MorphTargetEXTTests.cpp's own
    // CubicSplineEvaluatesRealHermiteCurveNotLinear test already verifies in isolation.
    const char* kCubicSplineMorphedTriangleGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ {
    "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }, "targets": [ { "POSITION": 3 } ] } ],
    "weights": [0.0]
  } ],
  "animations": [ {
    "name": "MorphCubic",
    "samplers": [ { "input": 4, "output": 5, "interpolation": "CUBICSPLINE" } ],
    "channels": [ { "sampler": 0, "target": { "node": 0, "path": "weights" } } ]
  } ],
  "buffers": [ {
    "byteLength": 164,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAA="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 132, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 140, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 5, "componentType": 5126, "count": 6, "type": "SCALAR" }
  ]
})GLTF";

    // CNB-56/59 (Phase 13A): an unskinned triangle with base-color + normal + metallic-roughness
    // + emissive maps, an explicit TANGENT accessor (all tangents (1,0,0,1)), and non-default
    // factor values (metallicFactor=0.5, roughnessFactor=0.3, emissiveFactor=[0.1,0.2,0.3]).
    const char* kPbrTriangleGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TANGENT": 2, "TEXCOORD_0": 3
  }, "material": 0 } ] } ],
  "materials": [ {
    "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0 },
      "metallicRoughnessTexture": { "index": 2 },
      "metallicFactor": 0.5,
      "roughnessFactor": 0.3
    },
    "normalTexture": { "index": 1 },
    "emissiveTexture": { "index": 3 },
    "emissiveFactor": [0.1, 0.2, 0.3]
  } ],
  "textures": [ { "source": 0 }, { "source": 1 }, { "source": 2 }, { "source": 3 } ],
  "images": [
    { "bufferView": 4, "mimeType": "image/png" },
    { "bufferView": 5, "mimeType": "image/png" },
    { "bufferView": 6, "mimeType": "image/png" },
    { "bufferView": 7, "mimeType": "image/png" }
  ],
  "buffers": [ {
    "byteLength": 420,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCCiVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCCiVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCCiVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 144, "byteLength": 69 },
    { "buffer": 0, "byteOffset": 213, "byteLength": 69 },
    { "buffer": 0, "byteOffset": 282, "byteLength": 69 },
    { "buffer": 0, "byteOffset": 351, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    // PBR + skinning combo: a single-bone skinned triangle whose material has base-color + normal
    // maps and an explicit TANGENT accessor -- proves ReadGltfModel() wires up SkinnedPbrEffect
    // (stride 68) directly, with no .cnj/binary sidecars, same as kPbrTriangleGltf above but with
    // JOINTS_0/WEIGHTS_0/skin added (mirrors GltfToCnjToolTests.cpp's own kSkinnedPbrGltf fixture).
    const char* kSkinnedPbrTriangleGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "RootBone", "translation": [0, 0, 0] },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TANGENT": 2, "TEXCOORD_0": 3, "JOINTS_0": 4, "WEIGHTS_0": 5
  }, "material": 0 } ] } ],
  "materials": [ {
    "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } },
    "normalTexture": { "index": 1 }
  } ],
  "textures": [ { "source": 0 }, { "source": 1 } ],
  "images": [
    { "bufferView": 7, "mimeType": "image/png" },
    { "bufferView": 8, "mimeType": "image/png" }
  ],
  "skins": [ { "joints": [0], "inverseBindMatrices": 6 } ],
  "buffers": [ {
    "byteLength": 418,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAP4lQTkcNChoKAAAADUlIRFIAAAABAAAAAQgCAAAAkHdT3gAAAAxJREFUeJxj+M/AAAADAQEAyf6S7wAAAABJRU5ErkJggolQTkcNChoKAAAADUlIRFIAAAABAAAAAQgCAAAAkHdT3gAAAAxJREFUeJxj+M/AAAADAQEAyf6S7wAAAABJRU5ErkJggg=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 144, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 216, "byteLength": 64 },
    { "buffer": 0, "byteOffset": 280, "byteLength": 69 },
    { "buffer": 0, "byteOffset": 349, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 4, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 6, "componentType": 5126, "count": 1, "type": "MAT4" }
  ]
})GLTF";

    // DualTextureEffect occlusion brightness fix (CNB-88, Phase 14E): an unskinned, uncolored mesh
    // whose material has both a base-color and an occlusion texture, imported through
    // DualTextureEffect -- identical fixture to GltfToCnjToolTests.cpp's own kDualTextureGltf.
    // The occlusion image is a solid (255,0,0) 1x1 PNG; RemapOcclusionImageForDualTextureEXT must
    // halve it (~127,0,0) before it reaches the loaded Texture2D.
    const char* kDualTextureGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "TEXCOORD_0": 1 }, "material": 0 } ] } ],
  "materials": [ { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } }, "occlusionTexture": { "index": 1 } } ],
  "textures": [ { "source": 0 }, { "source": 1 } ],
  "images": [
    { "bufferView": 2, "mimeType": "image/png" },
    { "bufferView": 3, "mimeType": "image/png" }
  ],
  "buffers": [ {
    "byteLength": 198,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCCiVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 60,  "byteLength": 69 },
    { "buffer": 0, "byteOffset": 129, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    // Draco mesh compression decoding (CNB-91, Phase 14F): identical fixture and encoded bytes to
    // GltfImportCoreTests.cpp's own kDracoTriangleGltf -- see that file's own doc comment for how
    // the Draco bitstream was produced.
    const char* kDracoTriangleGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "extensionsUsed": [ "KHR_draco_mesh_compression" ],
  "extensionsRequired": [ "KHR_draco_mesh_compression" ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "extensions": {
        "KHR_draco_mesh_compression": {
          "bufferView": 0,
          "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }
        }
      }
  } ] } ],
  "buffers": [ {
    "byteLength": 156,
    "uri": "data:application/octet-stream;base64,RFJBQ08CAgEBAAAAAwECAQAAAQf/AREBAQABAQAD/wAAAAAAAQAAAQAJAwAAAAEBCQMAAQABAwkCAAIAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAPwAAAAAAAAAA"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 156 }
  ],
  "accessors": [
    { "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "componentType": 5126, "count": 3, "type": "VEC3" },
    { "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";
}

TEST(RuntimeGltfModelTest, LoadsUnskinnedTexturedModelDirectlyFromGltf)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "basic.gltf", kBasicTexturedGltf);

    GraphicsDevice gd;
    // Runtime glTF Model loading builds a real VertexBuffer -- a renderer with no 3D
    // pipeline rejects it before this test's ContentManager::Load<Model> is reached.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    // No "basic.cnj" exists -- ResolveAssetPath must fall through to ModelTypeReader's own
    // GetExtensions() list and find "basic.gltf".
    Model model = cm.Load<Model>("basic");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];
    ASSERT_EQ(mesh->getMeshPartsProperty().getCountProperty(), 1);

    // PbrEffect, not BasicEffect: the material is metallic-roughness, and GLTF-215 made effect
    // selection follow the material MODEL the file declares rather than which maps are present.
    // This assertion said BasicEffect until the first run on a renderer that reports ThreeD --
    // STUB does not, so nothing below the skip above had executed since GLTF-215 landed
    // (plans/plan_gltf.md GLTF-383).
    auto* pbrFx = dynamic_cast<PbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(pbrFx, nullptr) << "a metallic-roughness material must select PbrEffect";
    Texture2D* tex = pbrFx->getTextureProperty();
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getWidthProperty(), 1);
    EXPECT_EQ(tex->getHeightProperty(), 1);

    // No skeleton in this fixture -- Tag must stay null (SkinningData's own documented default).
    EXPECT_EQ(model.getTagProperty(), nullptr);
}

TEST(RuntimeGltfModelTest, LoadsSkinnedAnimatedModelDirectlyFromGltfWithReversedJointOrder)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "skinned.gltf", kSkinnedAnimatedGltf);

    GraphicsDevice gd;
    // Runtime glTF Model loading builds a real VertexBuffer -- a renderer with no 3D
    // pipeline rejects it before this test's ContentManager::Load<Model> is reached.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("skinned");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    // SkinnedPbrEffect for the same reason as the unskinned case above: skinned AND
    // metallic-roughness. The bone palette below is what this test is really about, and it is
    // identical on either effect -- both carry SkinnedEffect's own MaxBones/SetBoneTransforms API.
    auto* skinnedFx =
        dynamic_cast<SkinnedPbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(skinnedFx, nullptr)
        << "a skinned primitive with a metallic-roughness material must select SkinnedPbrEffect";
    Texture2D* tex = skinnedFx->getTextureProperty();
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getWidthProperty(), 1);

    auto* skinningData = static_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(skinningData, nullptr);
    ASSERT_EQ(skinningData->BoneCount, 2);
    ASSERT_EQ(skinningData->SkeletonHierarchy.size(), 2u);
    // Reversed skin.joints=[1,0] must be topologically reordered so the PARENT (glTF node 0)
    // ends up before the CHILD (glTF node 1) -- new index 0 must be a root (parentIndex -1), and
    // new index 1's parent must be new index 0.
    EXPECT_EQ(skinningData->SkeletonHierarchy[0], -1);
    EXPECT_EQ(skinningData->SkeletonHierarchy[1], 0);

    ASSERT_EQ(skinningData->AnimationClips.count("Wave"), 1u);
    const AnimationClip& clip = skinningData->AnimationClips.at("Wave");
    EXPECT_NEAR(clip.Duration.getTotalSecondsProperty(), 1.0, 1e-6);
    ASSERT_EQ(clip.Tracks.size(), 1u);
    // The animated bone is the CHILD (glTF node 1), which topologically reorders to new index 1.
    EXPECT_EQ(clip.Tracks[0].BoneIndex, 1);
    ASSERT_EQ(clip.Tracks[0].Keys.size(), 2u);
    EXPECT_NEAR(clip.Tracks[0].Keys[0].Time.getTotalSecondsProperty(), 0.0, 1e-6);
    EXPECT_NEAR(clip.Tracks[0].Keys[0].Translation.X, 0.0f, 1e-4f);
    EXPECT_NEAR(clip.Tracks[0].Keys[1].Time.getTotalSecondsProperty(), 1.0, 1e-6);
    EXPECT_NEAR(clip.Tracks[0].Keys[1].Translation.X, 2.0f, 1e-4f);
}

// CNB-64/65 (Phase 13B): morph target deltas, default weights, and weight animation, all
// extracted directly from a glTF file with no .cnj/binary sidecars.
TEST(RuntimeGltfModelTest, LoadsMorphTargetDataWithDefaultWeightsAndWeightAnimationFromGltf)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "morph.gltf", kMorphedTriangleGltf);

    GraphicsDevice gd;
    // Runtime glTF Model loading builds a real VertexBuffer -- a renderer with no 3D
    // pipeline rejects it before this test's ContentManager::Load<Model> is reached.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("morph");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];
    ModelMeshPart* part = mesh->getMeshPartsProperty()[0];

    auto* morph = dynamic_cast<MorphTargetDataEXT*>(part->getTagProperty());
    ASSERT_NE(morph, nullptr);
    ASSERT_EQ(morph->PositionDeltas.size(), 1u);
    ASSERT_EQ(morph->PositionDeltas[0].size(), 3u);
    EXPECT_NEAR(morph->PositionDeltas[0][0].Z, 1.0f, 1e-5f);
    // No NORMAL delta was authored for this target.
    EXPECT_TRUE(morph->NormalDeltas[0].empty());

    // mesh.weights=[0.5] must already be reflected: BaseVertexBytes is the raw (zero-weight)
    // pose, and Weights holds the applied default -- BlendMorphTargetsEXT(morph, morph->Weights)
    // must reproduce what was actually uploaded (vertex 0's Z = 0 + 0.5*1.0).
    ASSERT_EQ(morph->Weights.size(), 1u);
    EXPECT_NEAR(morph->Weights[0], 0.5f, 1e-5f);
    const auto blendedAtDefault = BlendMorphTargetsEXT(*morph, morph->Weights);
    float z0;
    std::memcpy(&z0, blendedAtDefault.data() + 2 * sizeof(float), sizeof(float));
    EXPECT_NEAR(z0, 0.5f, 1e-5f);

    // Weight animation: LINEAR 0.0 at t=0 -> 1.0 at t=1.
    ASSERT_EQ(morph->WeightTrack.Keys.size(), 2u);
    EXPECT_FALSE(morph->WeightTrack.StepInterpolation);
    EXPECT_NEAR(morph->WeightTrack.Keys[0].Weights[0], 0.0f, 1e-5f);
    EXPECT_NEAR(morph->WeightTrack.Keys[1].Weights[0], 1.0f, 1e-5f);
    const auto midWeights = EvaluateMorphWeightsEXT(morph->WeightTrack, 0.5);
    ASSERT_EQ(midWeights.size(), 1u);
    EXPECT_NEAR(midWeights[0], 0.5f, 1e-5f);
}

// CUBICSPLINE interpolation for morph-weight animation tracks: proves the full glTF ->
// GltfImportCore -> MorphTargetDataEXT pipeline preserves real Hermite tangents (not just the
// sampled middle-third value), and EvaluateMorphWeightsEXT evaluates the real curve at playback.
TEST(RuntimeGltfModelTest, LoadsCubicSplineMorphWeightAnimationFromGltf)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "morphcubic.gltf", kCubicSplineMorphedTriangleGltf);

    GraphicsDevice gd;
    // Runtime glTF Model loading builds a real VertexBuffer -- a renderer with no 3D
    // pipeline rejects it before this test's ContentManager::Load<Model> is reached.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("morphcubic");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];
    ModelMeshPart* part = mesh->getMeshPartsProperty()[0];

    auto* morph = dynamic_cast<MorphTargetDataEXT*>(part->getTagProperty());
    ASSERT_NE(morph, nullptr);
    ASSERT_EQ(morph->WeightTrack.Keys.size(), 2u);
    EXPECT_TRUE(morph->WeightTrack.CubicSpline);
    ASSERT_EQ(morph->WeightTrack.Keys[0].OutTangent.size(), 1u);
    ASSERT_EQ(morph->WeightTrack.Keys[1].InTangent.size(), 1u);
    EXPECT_NEAR(morph->WeightTrack.Keys[0].OutTangent[0], 0.0f, 1e-5f);
    EXPECT_NEAR(morph->WeightTrack.Keys[1].InTangent[0], 0.0f, 1e-5f);

    // Same hand-derived value as MorphTargetEXTTests.cpp's own
    // CubicSplineEvaluatesRealHermiteCurveNotLinear test (zero endpoint tangents -> h00/h01-only
    // Hermite basis) -- 0.15625, not the LINEAR 0.25.
    const auto quarterWeights = EvaluateMorphWeightsEXT(morph->WeightTrack, 0.25);
    ASSERT_EQ(quarterWeights.size(), 1u);
    EXPECT_NEAR(quarterWeights[0], 0.15625f, 1e-5f);
}

// CNB-56/59 (Phase 13A): PbrEffect's 4 maps + factor values, extracted directly from a glTF file
// with an explicit TANGENT accessor, no .cnj/binary sidecars.
TEST(RuntimeGltfModelTest, LoadsPbrMaterialWithAllFourMapsAndFactorsFromGltf)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "pbr.gltf", kPbrTriangleGltf);

    GraphicsDevice gd;
    // Runtime glTF Model loading builds a real VertexBuffer -- a renderer with no 3D
    // pipeline rejects it before this test's ContentManager::Load<Model> is reached.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("pbr");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    auto* pbrFx = dynamic_cast<PbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(pbrFx, nullptr);

    Texture2D* baseColor = pbrFx->getTextureProperty();
    Texture2D* normalMap = pbrFx->getNormalMapProperty();
    Texture2D* mrMap     = pbrFx->getMetallicRoughnessMapProperty();
    Texture2D* emissiveMap = pbrFx->getEmissiveMapProperty();
    ASSERT_NE(baseColor, nullptr);
    ASSERT_NE(normalMap, nullptr);
    ASSERT_NE(mrMap, nullptr);
    ASSERT_NE(emissiveMap, nullptr);
    EXPECT_EQ(baseColor->getWidthProperty(), 1);
    EXPECT_EQ(normalMap->getWidthProperty(), 1);
    EXPECT_EQ(mrMap->getWidthProperty(), 1);
    EXPECT_EQ(emissiveMap->getWidthProperty(), 1);
    // No occlusion texture in this fixture.
    EXPECT_EQ(pbrFx->getOcclusionMapProperty(), nullptr);

    EXPECT_NEAR(pbrFx->getMetallicFactorProperty(), 0.5f, 1e-5f);
    EXPECT_NEAR(pbrFx->getRoughnessFactorProperty(), 0.3f, 1e-5f);
    const Vector3 emissiveFactor = pbrFx->getEmissiveFactorProperty();
    EXPECT_NEAR(emissiveFactor.X, 0.1f, 1e-5f);
    EXPECT_NEAR(emissiveFactor.Y, 0.2f, 1e-5f);
    EXPECT_NEAR(emissiveFactor.Z, 0.3f, 1e-5f);
}

// PBR + skinning combo: a skinned, PBR-mapped mesh loaded directly from a .gltf file (no
// .cnj/binary sidecars) must wire up SkinnedPbrEffect (stride 68), not fall back to plain
// SkinnedEffect or plain PbrEffect.
TEST(RuntimeGltfModelTest, LoadsSkinnedPbrMaterialDirectlyFromGltf)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "skinnedpbr.gltf", kSkinnedPbrTriangleGltf);

    GraphicsDevice gd;
    // Runtime glTF Model loading builds a real VertexBuffer -- a renderer with no 3D
    // pipeline rejects it before this test's ContentManager::Load<Model> is reached.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("skinnedpbr");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    auto* skinnedPbrFx = dynamic_cast<SkinnedPbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(skinnedPbrFx, nullptr);

    Texture2D* baseColor = skinnedPbrFx->getTextureProperty();
    Texture2D* normalMap = skinnedPbrFx->getNormalMapProperty();
    ASSERT_NE(baseColor, nullptr);
    ASSERT_NE(normalMap, nullptr);
    EXPECT_EQ(baseColor->getWidthProperty(), 1);
    EXPECT_EQ(normalMap->getWidthProperty(), 1);

    auto* skinningData = static_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(skinningData, nullptr);
    EXPECT_EQ(skinningData->BoneCount, 1);
}

// DualTextureEffect occlusion brightness fix (CNB-88, Phase 14E): the runtime glTF path must
// also apply RemapOcclusionImageForDualTextureEXT before loading the occlusion image into
// DualTextureEffect::Texture2, not just the offline CLI/.cnj path.
TEST(RuntimeGltfModelTest, RemapsOcclusionTextureBrightnessForDualTextureEffectFromGltf)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "dualtex.gltf", kDualTextureGltf);
    // The same document with KHR_materials_unlit declared, which is what routes it to the
    // dual-texture lightmap path (GLTF-215/GLTF-337). Built by substitution rather than as a
    // second literal, so the two cannot drift apart in any way except the extension.
    {
        std::string unlit = kDualTextureGltf;
        const std::string material = R"("materials": [ { "pbrMetallicRoughness")";
        const auto at = unlit.find(material);
        ASSERT_NE(std::string::npos, at) << "the dual-texture fixture's material moved";
        unlit.insert(at, R"("extensionsUsed": [ "KHR_materials_unlit" ],
  )");
        const std::string closeMaterial = R"("occlusionTexture": { "index": 1 } } ],)";
        const auto occlusionAt = unlit.find(closeMaterial);
        ASSERT_NE(std::string::npos, occlusionAt) << "the occlusion texture reference moved";
        unlit.replace(occlusionAt, closeMaterial.size(),
                      R"("occlusionTexture": { "index": 1 }, "extensions": { "KHR_materials_unlit": {} } } ],)");
        WriteFile(contentRoot.path() / "dualtex-unlit.gltf", unlit);
    }

    GraphicsDevice gd;
    // Runtime glTF Model loading builds a real VertexBuffer -- a renderer with no 3D
    // pipeline rejects it before this test's ContentManager::Load<Model> is reached.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    // The occlusion-as-lightmap approximation is a NON-PBR path, and since GLTF-215 the only way
    // a base-colour-plus-occlusion material reaches it is by declaring KHR_materials_unlit -- an
    // ordinary metallic-roughness material now goes to PbrEffect, which has a real occlusion map
    // and needs no approximation. The corpus fixture `tex-dual-texture-stride` is authored exactly
    // that way; this one was not, and had been asserting DualTextureEffect against a model that
    // stopped being one. Both branches are pinned here, because which branch a material takes is
    // the actual contract.
    Model model = cm.Load<Model>("dualtex-unlit");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    auto* dualFx = dynamic_cast<DualTextureEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(dualFx, nullptr)
        << "an unlit base-colour + occlusion material must take the dual-texture lightmap path";
    Texture2D* tex2 = dualFx->getTexture2Property();
    ASSERT_NE(tex2, nullptr);
    EXPECT_EQ(tex2->getWidthProperty(), 1);

    // Source occlusion image is solid (255,0,0); the loaded texture must be halved (~127,0,0),
    // not the raw passthrough value.
    Color occlusionPixel(0, 0, 0, 0);
    tex2->GetData(&occlusionPixel, 1);
    EXPECT_NEAR(occlusionPixel.getRProperty(), 127, 2);
    EXPECT_EQ(occlusionPixel.getGProperty(), 0);
    EXPECT_EQ(occlusionPixel.getBProperty(), 0);
    EXPECT_EQ(occlusionPixel.getAProperty(), 255);

    // The other branch: the same material without the extension is metallic-roughness, so it
    // selects PbrEffect and its occlusion map is bound as authored -- the shader applies
    // §3.9.3's own `1 + strength * (sampled - 1)`, so halving it here would darken it twice.
    Model lit = cm.Load<Model>("dualtex");
    auto* pbrFx = dynamic_cast<PbrEffect*>(
        lit.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(pbrFx, nullptr)
        << "the same material without KHR_materials_unlit must select PbrEffect";
    Texture2D* occlusion = pbrFx->getOcclusionMapProperty();
    ASSERT_NE(occlusion, nullptr) << "the occlusion texture must reach PbrEffect's own map slot";
    Color rawPixel(0, 0, 0, 0);
    occlusion->GetData(&rawPixel, 1);
    EXPECT_EQ(rawPixel.getRProperty(), 255)
        << "the PBR path must bind the occlusion map unremapped -- the shader applies the strength "
           "formula itself, so halving it at import would apply the approximation twice";
}

#ifdef CNA_DRACO_AVAILABLE
// Draco mesh compression decoding (CNB-91, Phase 14F): the runtime glTF path must also decode a
// KHR_draco_mesh_compression primitive, not just the offline CLI/.cnj path.
TEST(RuntimeGltfModelTest, LoadsDracoCompressedTriangleDirectlyFromGltf)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "draco.gltf", kDracoTriangleGltf);

    GraphicsDevice gd;
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("draco");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    auto* pbrFx = dynamic_cast<PbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(pbrFx, nullptr);

    // No TEXCOORD-referencing material was authored, so PbrEffect stays untextured -- this
    // test only needs to prove the mesh geometry itself decoded correctly (no crash, right
    // vertex/index counts implicitly verified via GltfImportCoreTests.cpp's own more detailed
    // byte-level assertions for the identical fixture).
    EXPECT_EQ(pbrFx->getTextureProperty(), nullptr);
}
#endif

// glTF extensions (CNB-97, Phase 14H): a KHR_lights_punctual directional light in the file must
// end up on the constructed BasicEffect's own DirectionalLight0, enabled, with the file's own
// color -- proves ApplyPunctualLightsEXT's own IEffectLights wiring end-to-end through the
// runtime glTF path (ExtractPunctualLightsEXT's own extraction math is already covered
// byte-for-byte in GltfImportCoreTests.cpp).
TEST(RuntimeGltfModelTest, AppliesKhrLightsPunctualToBasicEffectFromGltf)
{
    ScratchDir contentRoot;
    WriteFile(contentRoot.path() / "lit.gltf", kBasicTexturedWithLightGltf);

    GraphicsDevice gd;
    // Runtime glTF Model loading builds a real VertexBuffer -- a renderer with no 3D
    // pipeline rejects it before this test's ContentManager::Load<Model> is reached.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("lit");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    // PbrEffect: metallic-roughness again. The light is what this test is about, and it reaches
    // the same three DirectionalLight slots on either effect -- they are IEffectLights, which both
    // implement.
    auto* litFx = dynamic_cast<PbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(litFx, nullptr) << "a metallic-roughness material must select PbrEffect (GLTF-215)";

    EXPECT_TRUE(litFx->DirectionalLight0.getEnabledProperty());
    const Vector3 dir = litFx->DirectionalLight0.getDirectionProperty();
    EXPECT_NEAR(dir.X, 0.0f, 1e-4f);
    EXPECT_NEAR(dir.Y, 0.0f, 1e-4f);
    EXPECT_NEAR(dir.Z, -1.0f, 1e-4f);
    const Vector3 color = litFx->DirectionalLight0.getDiffuseColorProperty();
    EXPECT_NEAR(color.X, 0.25f, 1e-5f);
    EXPECT_NEAR(color.Y, 0.5f, 1e-5f);
    EXPECT_NEAR(color.Z, 0.75f, 1e-5f);

    // Unused slots stay at BasicEffect's own default (disabled).
    EXPECT_FALSE(litFx->DirectionalLight1.getEnabledProperty());
    EXPECT_FALSE(litFx->DirectionalLight2.getEnabledProperty());
}
