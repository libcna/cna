// SPDX-License-Identifier: MS-PL
//
// plan_cnj.md CNB-52: end-to-end regression test for the offline glTF -> .cnj converter
// (tools/gltf_to_cnj/gltf_to_cnj.cpp), spawned as a real subprocess (CNA_GLTF_TO_CNJ_TOOL_PATH,
// baked in by cmake/UnitTests.cmake) -- same "needs a real separate executable, not a library
// call" reasoning as TwoProcessLoopbackTest.cpp/AudioMixerTests.cpp.
//
// The embedded fixture below is a deliberately adversarial hand-built glTF (self-contained via a
// base64 data-URI buffer, no external files) exercising, in one small file, both real bugs
// tools/avatar_asset_pipeline/convert_avatar.py already found and this converter's own doc
// comment says it carries forward:
//   - Non-indexed primitive (no "indices" on the mesh primitive) -- Khronos's own "Fox" sample has
//     this shape; the fixture's single triangle omits "indices" entirely.
//   - Non-topological skin.joints order -- "skin.joints": [1, 0] deliberately lists the CHILD
//     bone (node 1) before the PARENT bone (node 0), so a converter that failed to reorder would
//     produce SkeletonHierarchy[1] (a real bone) with a parent index >= its own index, or would
//     mis-map the vertex JOINTS_0/animation-channel bone indices against the wrong entry.
// It additionally exercises the "channel missing for a bone that has other animated channels"
// fallback: only the child bone's rotation is animated (no translation/scale channel at all), so
// a converter that defaulted a missing channel to Vector3.Zero/Quaternion.Identity instead of the
// bone's own real bind pose would produce a wrong translation, not just a missing one.
//
// This test was also verified against a real official Khronos glTF-Sample-Assets model
// (CesiumMan.glb, 19 real bones, a real 2-second walk-cycle animation, and a real embedded JPEG
// base-color texture) during development -- downloading that ~430KB asset is not appropriate for
// an automated, network-free test, so this fixture reproduces the specific hard cases in
// miniature instead.
//
// Three more small fixtures below exercise capabilities added after the initial tool landed:
//   - kSparseAccessorGltf: a POSITION accessor with no base bufferView (implicit all-zero) and a
//     sparse override on exactly one vertex -- proves cgltf_accessor_unpack_floats (which resolves
//     sparse data) is used throughout, not cgltf_accessor_read_float (which rejects sparse
//     accessors outright).
//   - kTexturedSkinnedGltf: a material with an embedded (bufferView-backed) PNG base-color
//     texture -- proves image extraction and the mesh's "texture" .cnj field both work.
//   - kMultiSkinGltf: two independent one-bone skins, each with its own mesh node -- proves the
//     tool no longer silently imports only the first skin in a file.

#include <algorithm>
#include <array>
#include <cerrno>
#include <optional>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

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
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "../../../../CNA/Internal/GltfImport/GltfDrawParamsOracleEXT.hpp"

extern char** environ;

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Content;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_gltf_tool_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    // Deliberately adversarial: non-indexed triangle; skin.joints lists the child bone (node 1)
    // before the parent (node 0); only the child bone's rotation is animated. See file header.
    const char* kTinySkinnedGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 2] } ],
  "nodes": [
    { "name": "RootBone", "children": [1], "translation": [0, 0, 0] },
    { "name": "ChildBone", "translation": [0, 1.5, 0] },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "meshes": [
    { "primitives": [ { "attributes": {
        "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4
    } } ] }
  ],
  "skins": [ { "joints": [1, 0], "inverseBindMatrices": 5 } ],
  "animations": [ {
    "name": "Spin",
    "samplers": [ { "input": 6, "output": 7, "interpolation": "LINEAR" } ],
    "channels": [ { "sampler": 0, "target": { "node": 1, "path": "rotation" } } ]
  } ],
  "buffers": [ {
    "byteLength": 336,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA8wQ1PwAAAADzBDU/"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 128 },
    { "buffer": 0, "byteOffset": 296, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 304, "byteLength": 32 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "MAT4" },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 7, "componentType": 5126, "count": 2, "type": "VEC4" }
  ]
})GLTF";

    // POSITION accessor has no base bufferView (implicit all-zero per the glTF spec) and one
    // sparse override on vertex index 1, setting it to (2,3,4). See file header.
    const char* kSparseAccessorGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 } } ] } ],
  "buffers": [ {
    "byteLength": 76,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AQAAAAAAAEAAAEBAAACAQA=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 60, "byteLength": 2 },
    { "buffer": 0, "byteOffset": 64, "byteLength": 12 }
  ],
  "accessors": [
    { "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [2,3,4],
      "sparse": { "count": 1, "indices": { "bufferView": 2, "componentType": 5123 }, "values": { "bufferView": 3 } } },
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    // A single-bone skinned triangle whose material has an embedded (bufferView-backed) 1x1 PNG
    // base-color texture. See file header.
    const char* kTexturedSkinnedGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "RootBone", "translation": [0, 0, 0] },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4
  }, "material": 0 } ] } ],
  "materials": [ { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 5, "mimeType": "image/png" } ],
  "skins": [ { "joints": [0], "inverseBindMatrices": 5 } ],
  "buffers": [ {
    "byteLength": 301,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAP4lQTkcNChoKAAAADUlIRFIAAAABAAAAAQgCAAAAkHdT3gAAAAxJREFUeJxj+M/AAAADAQEAyf6S7wAAAABJRU5ErkJggg=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 232, "byteLength": 69 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 64 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 6, "componentType": 5126, "count": 1, "type": "MAT4" }
  ]
})GLTF";

    // Morph target CLI/.cnj serialization: an unskinned triangle with one morph target (POSITION
    // delta only, uniform +Z=1 per vertex), a non-zero default weight (mesh.weights=[0.5]), and a
    // LINEAR "weights" animation channel (0.0 at t=0 -> 1.0 at t=1) -- identical fixture to
    // RuntimeGltfModelTests.cpp's own kMorphedTriangleGltf, reused here to prove the offline CLI/
    // .cnj round-trip produces the same MorphTargetDataEXT the runtime glTF path already does.
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

    // CUBICSPLINE interpolation for morph-weight animation tracks: identical fixture to
    // RuntimeGltfModelTests.cpp's own kCubicSplineMorphedTriangleGltf, reused here to prove the
    // offline CLI/.cnj round-trip preserves the real Hermite tangents (not just the sampled
    // middle-third value) through the new "inTangent"/"outTangent" .cnj JSON fields.
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

    // Two independent one-bone skins ("SkinA"/"SkinB"), each with its own mesh node. See file
    // header.
    const char* kMultiSkinGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1, 2, 3] } ],
  "nodes": [
    { "name": "BoneA" },
    { "name": "MeshNodeA", "mesh": 0, "skin": 0 },
    { "name": "BoneB" },
    { "name": "MeshNodeB", "mesh": 1, "skin": 1 }
  ],
  "meshes": [
    { "name": "PartA", "primitives": [ { "attributes": { "POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":4 } } ] },
    { "name": "PartB", "primitives": [ { "attributes": { "POSITION":6,"NORMAL":7,"TEXCOORD_0":8,"JOINTS_0":9,"WEIGHTS_0":10 } } ] }
  ],
  "skins": [
    { "joints": [0], "inverseBindMatrices": 5, "name": "SkinA" },
    { "joints": [2], "inverseBindMatrices": 11, "name": "SkinB" }
  ],
  "buffers": [ {
    "byteLength": 464,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAIEEAAAAAAAAAAAAAMEEAAAAAAAAAAAAAIEEAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 64 },
    { "buffer": 0, "byteOffset": 232, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 268, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 304, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 328, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 352, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 400, "byteLength": 64 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 1, "type": "MAT4" },
    { "bufferView": 6, "componentType": 5126, "count": 3, "type": "VEC3", "min": [10,0,0], "max": [11,1,0] },
    { "bufferView": 7, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 8, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 9, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 10, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 11, "componentType": 5126, "count": 1, "type": "MAT4" }
  ]
})GLTF";

    // ChildBone has a STEP-interpolated translation channel (keys at t=0 -> (0,0,0), t=2 ->
    // (10,10,10)) and a LINEAR-interpolated rotation channel with DIFFERENT keyframe times (t=0,
    // t=1) -- the union-time resampling this tool does forces the translation channel to be
    // evaluated at t=1, a time it has no native key at, which is exactly where a STEP channel
    // must hold its last key's value (0,0,0) rather than linearly interpolate towards (5,5,5).
    // Regression fixture for a real bug found during development: an earlier refactor (moving to
    // sparse-accessor-safe bulk unpacking) accidentally dropped the STEP special-case, silently
    // turning every STEP channel into LINEAR.
    const char* kStepInterpolationGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 2] } ],
  "nodes": [
    { "name": "RootBone", "children": [1] },
    { "name": "ChildBone", "translation": [0, 0, 0] },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4
  } } ] } ],
  "skins": [ { "joints": [1, 0], "inverseBindMatrices": 5 } ],
  "animations": [ {
    "name": "StepTest",
    "samplers": [
      { "input": 6, "output": 7, "interpolation": "STEP" },
      { "input": 8, "output": 9, "interpolation": "LINEAR" }
    ],
    "channels": [
      { "sampler": 0, "target": { "node": 1, "path": "translation" } },
      { "sampler": 1, "target": { "node": 1, "path": "rotation" } }
    ]
  } ],
  "buffers": [ {
    "byteLength": 368,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAQAAAAAAAAAAAAAAAAAAAIEEAACBBAAAgQQAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAADzBDU/AAAAAPMENT8="
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
    { "buffer": 0, "byteOffset": 328, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 336, "byteLength": 32 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "MAT4" },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [2.0] },
    { "bufferView": 7, "componentType": 5126, "count": 2, "type": "VEC3" },
    { "bufferView": 8, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 9, "componentType": 5126, "count": 2, "type": "VEC4" }
  ]
})GLTF";

    // A primitive with KHR_draco_mesh_compression must be rejected with a clear error, not
    // silently read as garbage (cgltf never decodes Draco itself).
    const char* kDracoGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0 },
      "extensions": { "KHR_draco_mesh_compression": { "bufferView": 0, "attributes": { "POSITION": 0 } } }
  } ] } ],
  "buffers": [ {
    "byteLength": 36,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"
  } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] }
  ]
})GLTF";

    // TEXCOORD_0 is deliberately filled with (9,9) sentinel values the tool must NEVER pick;
    // the material's baseColorTexture selects "texCoord": 1, so the real UVs must come from
    // TEXCOORD_1. Also carries a real embedded 1x1 PNG so the texture-extraction step (unrelated
    // to what this fixture actually tests) succeeds rather than erroring on a missing file.
    const char* kTexcoordSelectionGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TEXCOORD_1": 3 },
      "material": 0
  } ] } ],
  "materials": [ { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0, "texCoord": 1 } } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 4, "mimeType": "image/png" } ],
  "buffers": [ {
    "byteLength": 189,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAQQQAAEEEAABBBAAAQQQAAEEEAABBBAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    // Two nodes reference the same mesh; only "InSceneMesh" is listed in the default scene's own
    // node list -- "OrphanMesh" must not be imported.
    const char* kSceneScopedGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "InSceneMesh", "mesh": 0 },
    { "name": "OrphanMesh", "mesh": 0 }
  ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 } } ] } ],
  "buffers": [ {
    "byteLength": 96,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    // One bone's translation channel is CUBICSPLINE with distinctive tangents (key0:
    // value=(0,0,0) outTangent=(10,0,0); key1: inTangent=(-10,0,0) value=(10,0,0)) over
    // t=[0,2]; a second (LINEAR, identity throughout) rotation channel has a key at t=1,
    // forcing the union-time resample to evaluate the CUBICSPLINE channel at a time it has no
    // native key at. The real Hermite basis gives X=10.0 there; a buggy linear/value-only
    // fallback would give 5.0 or 0.0 instead.
    const char* kCubicSplineGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "RootBone" },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4
  } } ] } ],
  "skins": [ { "joints": [0], "inverseBindMatrices": 5 } ],
  "animations": [ {
    "name": "CubicTest",
    "samplers": [
      { "input": 6, "output": 7, "interpolation": "CUBICSPLINE" },
      { "input": 8, "output": 9, "interpolation": "LINEAR" }
    ],
    "channels": [
      { "sampler": 0, "target": { "node": 0, "path": "translation" } },
      { "sampler": 1, "target": { "node": 0, "path": "rotation" } }
    ]
  } ],
  "buffers": [ {
    "byteLength": 352,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgQQAAAAAAAAAAAAAgwQAAAAAAAAAAAAAgQQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPw=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 64 },
    { "buffer": 0, "byteOffset": 232, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 240, "byteLength": 72 },
    { "buffer": 0, "byteOffset": 312, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 320, "byteLength": 32 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 1, "type": "MAT4" },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [2.0] },
    { "bufferView": 7, "componentType": 5126, "count": 6, "type": "VEC3" },
    { "bufferView": 8, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 9, "componentType": 5126, "count": 2, "type": "VEC4" }
  ]
})GLTF";

    // COLOR_0 (VEC4 float) on an unskinned mesh: vertex 0 red, vertex 1 green, vertex 2 blue,
    // all opaque.
    const char* kVertexColorGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "TEXCOORD_0": 1, "COLOR_0": 2 } } ] } ],
  "buffers": [ {
    "byteLength": 108,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAAAAgD8AAAAAAAAAAAAAgD8AAIA/"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 60, "byteLength": 48 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" }
  ]
})GLTF";

    // Authored in centimeters (RootBone at [50,0,0], triangle spanning 100 units): with
    // unitScale=0.01, every position and bone translation must come out divided by 100.
    const char* kUnitScaleGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "RootBone", "translation": [50, 0, 0] },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4
  } } ] } ],
  "skins": [ { "joints": [0], "inverseBindMatrices": 5 } ],
  "buffers": [ {
    "byteLength": 232,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAADIQgAAAAAAAAAAAAAAAAAAyEIAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPw=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 64 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [100,100,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 1, "type": "MAT4" }
  ]
})GLTF";

    // glTF extensions (CNB-97, Phase 14H): identical fixture and reasoning to
    // RuntimeGltfModelTests.cpp's own kBasicTexturedWithLightGltf -- see that file's own doc
    // comment. Reused here to prove the offline CLI/.cnj path's own separate "lights" JSON
    // field writer (gltf_to_cnj.cpp) and reader (ContentManager.cpp's .cnj JSON path) both wire
    // correctly, independent of the runtime glTF path's own wiring.
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

    // CNB-72/73 (Phase 13E): an unskinned, uncolored mesh whose material has both a base-color
    // and an occlusion texture must import through DualTextureEffect (stride-20 VertexPosition
    // Texture, Texture=base color, Texture2=occlusion) instead of BasicEffect.
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

    // CNB-66/67/68 (Phase 13C): a skinned mesh with a COLOR_0 attribute must import through the
    // new stride-56 (skinned + Color) layout, with "vertexColorEnabled": true wired to
    // SkinnedEffect's new CNAEXT VertexColorEnabled property. One bone (identity inverse bind
    // matrix), 3 vertices each fully weighted to that bone, distinct RGBA colors per vertex.
    const char* kSkinnedVertexColorGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "RootBone" },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 3, "WEIGHTS_0": 4, "COLOR_0": 5
  } } ] } ],
  "skins": [ { "joints": [0], "inverseBindMatrices": 6 } ],
  "buffers": [ {
    "byteLength": 280,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAAAAgD8AAAAAAAAAAAAAgD8AAIA/AACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPw=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 216, "byteLength": 64 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 6, "componentType": 5126, "count": 1, "type": "MAT4" }
  ]
})GLTF";

    // CNB-56/59 + plan_gltf.md GLTF-236/237/343/344: an unskinned triangle with all five material
    // slots, an explicit TANGENT accessor, and deliberately non-default core/Fresnel/alpha/sampler
    // state -- proves the offline CLI tool's complete factor-only .cnj material serialization.
    const char* kPbrGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "KHR_materials_ior", "KHR_materials_specular" ],
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TANGENT": 2, "TEXCOORD_0": 3
  }, "material": 0 } ] } ],
  "materials": [ {
    "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0 },
      "baseColorFactor": [0.25, 0.5, 0.75, 0.4],
      "metallicRoughnessTexture": { "index": 2 },
      "metallicFactor": 0.5,
      "roughnessFactor": 0.3
    },
    "normalTexture": { "index": 1, "scale": 0.35 },
    "occlusionTexture": { "index": 3, "strength": 0.65 },
    "emissiveTexture": { "index": 3 },
    "emissiveFactor": [0.1, 0.2, 0.3],
    "alphaMode": "MASK",
    "alphaCutoff": 0.73,
    "doubleSided": true,
    "extensions": {
      "KHR_materials_ior": { "ior": 2.0 },
      "KHR_materials_specular": {
        "specularFactor": 0.3,
        "specularColorFactor": [0.25, 1.0, 12.0]
      }
    }
  } ],
  "textures": [
    { "source": 0, "sampler": 0 }, { "source": 1 }, { "source": 2 }, { "source": 3 }
  ],
  "samplers": [ { "magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33648 } ],
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
    // maps and an explicit TANGENT accessor -- proves the offline CLI tool serializes
    // SkinnedPbrEffect (stride 68, VertexPositionNormalTangentTextureSkinned) rather than falling
    // back to SkinnedEffect or PbrEffect alone.
    const char* kSkinnedPbrGltf = R"GLTF({
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

    // Draco mesh compression decoding (CNB-91, Phase 14F): identical fixture and encoded bytes to
    // GltfImportCoreTests.cpp's own kDracoTriangleGltf -- see that file's own doc comment for how
    // the Draco bitstream was produced (a real draco::Encoder via draco::TriangleSoupMeshBuilder,
    // not hand-authored bytes).
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

    // Spawns the real cna_tool_gltf_to_cnj executable and waits for it to exit. Returns the exit
    // code, or -1 on a spawn-side failure (already reported via ADD_FAILURE). unitScale is passed
    // as the tool's optional 5th CLI argument when non-empty.
    int RunGltfToCnjTool(const std::string& input, const std::string& outDir, const std::string& baseName,
                         const std::string& unitScale = "")
    {
        char* argv[] = {
            const_cast<char*>(CNA_GLTF_TO_CNJ_TOOL_PATH),
            const_cast<char*>(input.c_str()),
            const_cast<char*>(outDir.c_str()),
            const_cast<char*>(baseName.c_str()),
            unitScale.empty() ? nullptr : const_cast<char*>(unitScale.c_str()),
            nullptr,
        };

        pid_t pid = -1;
        const int rc = posix_spawn(&pid, CNA_GLTF_TO_CNJ_TOOL_PATH, nullptr, nullptr, argv, environ);
        if (rc != 0)
        {
            ADD_FAILURE() << "posix_spawn(" << CNA_GLTF_TO_CNJ_TOOL_PATH << ") failed: " << std::strerror(rc);
            return -1;
        }

        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    // GLTF-237 compares the material contract only. Mesh naming, hierarchy and transforms have
    // their own runtime/offline parity sweeps; including them here made a perfectly equal material
    // fail on the offline tool's intentionally different generated mesh name. Floats use the same
    // 1e-5 tolerance as the L6 oracle: .cnj is decimal JSON, so a float such as 1 - 0.8 may parse
    // back one ULP away without being a material divergence.
    void ExpectL6MaterialStateEqual(
        const CnaTest::GltfOracle::DrawParamsDump& runtime,
        const CnaTest::GltfOracle::DrawParamsDump& offline)
    {
        constexpr float kTolerance = 1e-5f;
        const auto expectArrayNear = [&](const auto& expected, const auto& actual,
                                         const char* field)
        {
            ASSERT_EQ(expected.size(), actual.size()) << field;
            for (std::size_t i = 0; i < expected.size(); ++i)
            {
                EXPECT_NEAR(expected[i], actual[i], kTolerance) << field << '[' << i << ']';
            }
        };

        EXPECT_EQ(runtime.effectTypeName, offline.effectTypeName);
        ASSERT_EQ(runtime.samplers.size(), offline.samplers.size());
        for (std::size_t slot = 0; slot < runtime.samplers.size(); ++slot)
        {
            EXPECT_EQ(runtime.samplers[slot].filter, offline.samplers[slot].filter)
                << "sampler slot " << slot;
            EXPECT_EQ(runtime.samplers[slot].addressU, offline.samplers[slot].addressU)
                << "sampler slot " << slot;
            EXPECT_EQ(runtime.samplers[slot].addressV, offline.samplers[slot].addressV)
                << "sampler slot " << slot;
        }

        expectArrayNear(runtime.diffuseColor, offline.diffuseColor, "diffuseColor");
        EXPECT_NEAR(runtime.metallicFactor, offline.metallicFactor, kTolerance);
        EXPECT_NEAR(runtime.roughnessFactor, offline.roughnessFactor, kTolerance);
        EXPECT_NEAR(runtime.ior, offline.ior, kTolerance);
        EXPECT_NEAR(runtime.specularFactor, offline.specularFactor, kTolerance);
        expectArrayNear(runtime.specularColorFactor, offline.specularColorFactor,
                        "specularColorFactor");
        expectArrayNear(runtime.dielectricF0, offline.dielectricF0, "dielectricF0");
        EXPECT_NEAR(runtime.dielectricF90, offline.dielectricF90, kTolerance);
        EXPECT_NEAR(runtime.normalScale, offline.normalScale, kTolerance);
        EXPECT_NEAR(runtime.occlusionStrength, offline.occlusionStrength, kTolerance);
        expectArrayNear(runtime.emissiveColor, offline.emissiveColor, "emissiveColor");
        expectArrayNear(runtime.ambientColor, offline.ambientColor, "ambientColor");

        EXPECT_EQ(runtime.hasBaseColorMap, offline.hasBaseColorMap);
        EXPECT_EQ(runtime.hasNormalMap, offline.hasNormalMap);
        EXPECT_EQ(runtime.hasMetallicRoughnessMap, offline.hasMetallicRoughnessMap);
        EXPECT_EQ(runtime.hasOcclusionMap, offline.hasOcclusionMap);
        EXPECT_EQ(runtime.hasEmissiveMap, offline.hasEmissiveMap);
        EXPECT_EQ(runtime.baseColorTextureIsSrgb, offline.baseColorTextureIsSrgb);
        EXPECT_EQ(runtime.emissiveTextureIsSrgb, offline.emissiveTextureIsSrgb);
        EXPECT_EQ(runtime.encodeOutputToSrgb, offline.encodeOutputToSrgb);

        expectArrayNear(runtime.alphaTest, offline.alphaTest, "alphaTest");
        EXPECT_EQ(runtime.carriesAlphaState, offline.carriesAlphaState);
        EXPECT_EQ(runtime.alphaMode, offline.alphaMode);
        EXPECT_NEAR(runtime.alphaCutoff, offline.alphaCutoff, kTolerance);
        EXPECT_EQ(runtime.doubleSided, offline.doubleSided);

        EXPECT_EQ(runtime.pbr, offline.pbr);
        EXPECT_EQ(runtime.skinned, offline.skinned);
        EXPECT_EQ(runtime.dualTexture, offline.dualTexture);
        EXPECT_EQ(runtime.lightingEnabled, offline.lightingEnabled);
        EXPECT_EQ(runtime.textureEnabled, offline.textureEnabled);
        EXPECT_EQ(runtime.vertexColorEnabled, offline.vertexColorEnabled);
    }
}

TEST(GltfToCnjToolTest, ConvertsIndexlessDualBoneSkinnedFixtureAndLoadsBackThroughContentManager)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "tiny.gltf";
    WriteFile(gltfPath, kTinySkinnedGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "tiny");
    ASSERT_EQ(exitCode, 0);

    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "tiny.cnj"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "tiny.skeleton.bin"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "tiny_Spin.cnj"));

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("tiny");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);

    auto* skinningData = static_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(skinningData, nullptr);
    ASSERT_EQ(skinningData->BoneCount, 2);

    // The fixture authored skin.joints as [ChildBone, RootBone] (child first) specifically to
    // force a real topological reorder -- after conversion, bone 0 must be the root (RootBone,
    // parent -1) and bone 1 must be its child (ChildBone, parent 0), regardless of glTF's own
    // authoring order.
    ASSERT_EQ(skinningData->SkeletonHierarchy.size(), 2u);
    EXPECT_EQ(skinningData->SkeletonHierarchy[0], -1);
    EXPECT_EQ(skinningData->SkeletonHierarchy[1], 0);

    // ChildBone's own authored bind-pose translation is [0, 1.5, 0] -- BindPose[1] must reflect
    // it (proves BuildSkeleton's node-transform extraction, not just the parent-index remap).
    ASSERT_EQ(skinningData->BindPose.size(), 2u);
    EXPECT_NEAR(skinningData->BindPose[1].getTranslationProperty().Y, 1.5f, 1e-4f);

    ASSERT_TRUE(skinningData->AnimationClips.count("Spin"));
    const auto& clip = skinningData->AnimationClips.at("Spin");
    ASSERT_EQ(clip.Tracks.size(), 1u);
    // The animated bone must be reported as new-index 1 (ChildBone after reorder), not the
    // old, pre-reorder glTF joints-array index 0 the animation channel's target node actually
    // had.
    EXPECT_EQ(clip.Tracks[0].BoneIndex, 1);
    ASSERT_EQ(clip.Tracks[0].Keys.size(), 2u);

    // Only "rotation" was animated (no translation/scale channel at all) -- every key's
    // Translation must still be ChildBone's own bind-pose value (1.5), not an unrelated
    // Vector3.Zero default.
    EXPECT_NEAR(clip.Tracks[0].Keys[0].Translation.Y, 1.5f, 1e-4f);
    EXPECT_NEAR(clip.Tracks[0].Keys[1].Translation.Y, 1.5f, 1e-4f);

    // Rotation samples: identity at t=0, a 90-degree turn about Y at t=1 (as authored).
    EXPECT_NEAR(clip.Tracks[0].Keys[0].Rotation.W, 1.0f, 1e-4f);
    EXPECT_NEAR(clip.Tracks[0].Keys[1].Rotation.Y, 0.70710678f, 1e-4f);
    EXPECT_NEAR(clip.Tracks[0].Keys[1].Rotation.W, 0.70710678f, 1e-4f);

    // Real playback end-to-end: at the animation's midpoint, the child bone's skin transform
    // must differ from bind pose (proves the resampled keyframes actually drive AnimationPlayer,
    // not just that the .cnj text looks right).
    AnimationPlayer player(*skinningData);
    player.StartClip(clip);
    player.Update(System::TimeSpan::FromSeconds(0.5), false, true);
    const auto& skin = player.GetSkinTransforms();
    ASSERT_EQ(skin.size(), 2u);
    const Matrix identity = Matrix::getIdentityProperty();
    const Matrix& childSkin = skin[1];
    const bool differsFromIdentity =
        std::fabs(childSkin.M11 - identity.M11) > 1e-4f || std::fabs(childSkin.M13 - identity.M13) > 1e-4f;
    EXPECT_TRUE(differsFromIdentity);
}

TEST(GltfToCnjToolTest, MissingInputFileFailsCleanly)
{
    ScratchDir contentRoot;
    const int exitCode = RunGltfToCnjTool("/nonexistent/path/does_not_exist.gltf",
                                           contentRoot.path().string(), "wont_happen");
    EXPECT_NE(exitCode, 0);
    EXPECT_FALSE(std::filesystem::exists(contentRoot.path() / "wont_happen.cnj"));
}

// Vertex 1's POSITION comes entirely from a sparse override on an accessor with no base
// bufferView -- proves cgltf_accessor_unpack_floats (sparse-safe) is used, not
// cgltf_accessor_read_float (rejects sparse accessors outright, which would fail this conversion).
//
// plan_gltf.md GLTF-036: the sparse values bufferView sits at byteOffset 64, not 62 as originally
// authored. 62 is not a multiple of a float's 4 bytes, and cgltf reads a component with a raw
// `*(const float*)` cast, so the old fixture made the parser perform a misaligned load -- the
// undefined behaviour UBSan flagged as REMED-NA-016. The fixture was the defect, not the parser;
// ValidateGltfEXT now refuses such a file, so this document would be rejected unchanged.
TEST(GltfToCnjToolTest, ResolvesSparseAccessorOverride)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "sparse.gltf";
    WriteFile(gltfPath, kSparseAccessorGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "sparsetest");
    ASSERT_EQ(exitCode, 0);

    const std::filesystem::path vertsPath = contentRoot.path() / "sparsetest_mesh0_verts.bin";
    ASSERT_TRUE(std::filesystem::exists(vertsPath));

    std::ifstream f(vertsPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // Stride 48, not 32: GLTF-215 selects PBR for any metallic-roughness material, and a
    // primitive with no material declared gets glTF's default material, which is exactly that.
    // Position still begins each vertex, which is all this test is about.
    ASSERT_EQ(bytes.size(), 3u * 48u); // stride 48, unskinned PBR

    auto readVec3 = [&](std::size_t vertexIndex) {
        float v[3];
        std::memcpy(v, bytes.data() + vertexIndex * 48, sizeof(v));
        return Vector3(v[0], v[1], v[2]);
    };

    EXPECT_EQ(readVec3(0), Vector3(0.0f, 0.0f, 0.0f)); // base (implicit-zero) value, unaffected
    EXPECT_EQ(readVec3(1), Vector3(2.0f, 3.0f, 4.0f)); // sparse-overridden value
    EXPECT_EQ(readVec3(2), Vector3(0.0f, 0.0f, 0.0f)); // base (implicit-zero) value, unaffected
}

TEST(GltfToCnjToolTest, ExtractsEmbeddedBaseColorTextureAndLoadsIt)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "textured.gltf";
    WriteFile(gltfPath, kTexturedSkinnedGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "textest");
    ASSERT_EQ(exitCode, 0);
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "textest_tex0.png"));

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("textest");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];
    ASSERT_EQ(mesh->getMeshPartsProperty().getCountProperty(), 1);

    // SkinnedPbrEffect, not SkinnedEffect: the primitive is skinned AND its material is
    // metallic-roughness, and GLTF-215 made the effect follow the material MODEL the file declares
    // rather than which texture maps happen to be present. This assertion said `SkinnedEffect`
    // until the first run on a renderer that reports ThreeD -- STUB does not, so the whole block
    // below the skip had never executed since GLTF-215 landed (plan_gltf.md GLTF-383).
    auto* skinnedFx =
        dynamic_cast<SkinnedPbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(skinnedFx, nullptr)
        << "a skinned primitive with a metallic-roughness material must select SkinnedPbrEffect";
    Texture2D* tex = skinnedFx->getTextureProperty();
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getWidthProperty(), 1);
    EXPECT_EQ(tex->getHeightProperty(), 1);
}

// Two independent skins in one file must produce two separate Model .cnj outputs, not just the
// first skin silently winning.
TEST(GltfToCnjToolTest, ImportsAllSkinsAsSeparateModels)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "multiskin.gltf";
    WriteFile(gltfPath, kMultiSkinGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "ms");
    ASSERT_EQ(exitCode, 0);

    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "ms_SkinA.cnj"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "ms_SkinB.cnj"));

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model modelA = cm.Load<Model>("ms_SkinA");
    Model modelB = cm.Load<Model>("ms_SkinB");

    auto* dataA = static_cast<SkinningData*>(modelA.getTagProperty());
    auto* dataB = static_cast<SkinningData*>(modelB.getTagProperty());
    ASSERT_NE(dataA, nullptr);
    ASSERT_NE(dataB, nullptr);
    EXPECT_EQ(dataA->BoneCount, 1);
    EXPECT_EQ(dataB->BoneCount, 1);
}

TEST(GltfToCnjToolTest, StepInterpolatedChannelHoldsValueAcrossAForeignResampleTime)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "step.gltf";
    WriteFile(gltfPath, kStepInterpolationGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "steptest");
    ASSERT_EQ(exitCode, 0);

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("steptest");
    auto* skinningData = static_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(skinningData, nullptr);
    ASSERT_TRUE(skinningData->AnimationClips.count("StepTest"));
    const auto& clip = skinningData->AnimationClips.at("StepTest");
    ASSERT_EQ(clip.Tracks.size(), 1u);

    // Union-time resampling must have produced 3 keys (t=0, t=1 -- foreign to the STEP channel,
    // t=2), not just the STEP channel's own 2 native keys.
    ASSERT_EQ(clip.Tracks[0].Keys.size(), 3u);

    bool foundForeignTime = false;
    for (const auto& key : clip.Tracks[0].Keys)
    {
        if (std::fabs(key.Time.getTotalSecondsProperty() - 1.0) < 1e-4)
        {
            foundForeignTime = true;
            // STEP semantics: at t=1 (between the STEP channel's own t=0/t=2 keys), the value
            // must still be the t=0 key (0,0,0), not linearly interpolated towards (10,10,10).
            EXPECT_NEAR(key.Translation.X, 0.0f, 1e-4f);
            EXPECT_NEAR(key.Translation.Y, 0.0f, 1e-4f);
            EXPECT_NEAR(key.Translation.Z, 0.0f, 1e-4f);
        }
    }
    EXPECT_TRUE(foundForeignTime);
}

TEST(GltfToCnjToolTest, RejectsDracoCompressedPrimitive)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "draco.gltf";
    WriteFile(gltfPath, kDracoGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "draco");
    EXPECT_NE(exitCode, 0);
    EXPECT_FALSE(std::filesystem::exists(contentRoot.path() / "draco.cnj"));
}

TEST(GltfToCnjToolTest, UsesTexcoordSetSelectedByMaterial)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "texc.gltf";
    WriteFile(gltfPath, kTexcoordSelectionGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "texc");
    ASSERT_EQ(exitCode, 0);

    const std::filesystem::path vertsPath = contentRoot.path() / "texc_mesh0_verts.bin";
    ASSERT_TRUE(std::filesystem::exists(vertsPath));
    std::ifstream f(vertsPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_EQ(bytes.size(), 3u * 48u); // stride 48, unskinned PBR (GLTF-215)

    // UV lives at byte offset 40 within each stride-48 vertex
    // (pos12 + normal12 + tangent16 + uv8). TEXCOORD_0 was deliberately filled with (9,9)
    // sentinels the tool must never emit; the real values, from TEXCOORD_1 (the set the
    // material's baseColorTexture actually selects), are (0,0), (1,0), (0,1).
    float uv0[2], uv1[2], uv2[2];
    std::memcpy(uv0, bytes.data() + 0 * 48 + 40, sizeof(uv0));
    std::memcpy(uv1, bytes.data() + 1 * 48 + 40, sizeof(uv1));
    std::memcpy(uv2, bytes.data() + 2 * 48 + 40, sizeof(uv2));
    EXPECT_FLOAT_EQ(uv0[0], 0.0f); EXPECT_FLOAT_EQ(uv0[1], 0.0f);
    EXPECT_FLOAT_EQ(uv1[0], 1.0f); EXPECT_FLOAT_EQ(uv1[1], 0.0f);
    EXPECT_FLOAT_EQ(uv2[0], 0.0f); EXPECT_FLOAT_EQ(uv2[1], 1.0f);
}

TEST(GltfToCnjToolTest, OnlyImportsNodesReachableFromTheDefaultScene)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "scene.gltf";
    WriteFile(gltfPath, kSceneScopedGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "scenetest");
    ASSERT_EQ(exitCode, 0);

    // "OrphanMesh" (not listed in the default scene's own node list) must not have produced a
    // second mesh part -- only mesh0 (from "InSceneMesh") should exist.
    EXPECT_TRUE(std::filesystem::exists(contentRoot.path() / "scenetest_mesh0_verts.bin"));
    EXPECT_FALSE(std::filesystem::exists(contentRoot.path() / "scenetest_mesh1_verts.bin"));

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("scenetest");
    EXPECT_EQ(model.getMeshesProperty().getCountProperty(), 1);
}

TEST(GltfToCnjToolTest, EvaluatesCubicSplineWithRealHermiteBasis)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "cubic.gltf";
    WriteFile(gltfPath, kCubicSplineGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "cubictest");
    ASSERT_EQ(exitCode, 0);

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("cubictest");
    auto* skinningData = static_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(skinningData, nullptr);
    ASSERT_TRUE(skinningData->AnimationClips.count("CubicTest"));
    const auto& clip = skinningData->AnimationClips.at("CubicTest");
    ASSERT_EQ(clip.Tracks.size(), 1u);

    // Union-time resampling forces evaluation at t=1 (native to the rotation channel, foreign to
    // the CUBICSPLINE translation channel) -- the real Hermite basis (key0: value=(0,0,0),
    // outTangent=(10,0,0); key1: inTangent=(-10,0,0), value=(10,0,0); deltaT=2) gives X=10.0
    // there. A buggy fallback using only the sampled value (no tangents) would give 0.0; a buggy
    // linear fallback would give 5.0 -- neither is close to the real answer.
    bool foundForeignTime = false;
    for (const auto& key : clip.Tracks[0].Keys)
    {
        if (std::fabs(key.Time.getTotalSecondsProperty() - 1.0) < 1e-4)
        {
            foundForeignTime = true;
            EXPECT_NEAR(key.Translation.X, 10.0f, 1e-3f);
        }
    }
    EXPECT_TRUE(foundForeignTime);
}

TEST(GltfToCnjToolTest, ExtractsVertexColorAndEnablesItOnBasicEffect)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "color.gltf";
    WriteFile(gltfPath, kVertexColorGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "colortest");
    ASSERT_EQ(exitCode, 0);

    const std::filesystem::path vertsPath = contentRoot.path() / "colortest_mesh0_verts.bin";
    ASSERT_TRUE(std::filesystem::exists(vertsPath));
    std::ifstream f(vertsPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_EQ(bytes.size(), 3u * 24u); // stride 24 (VertexPositionColorTexture): pos12+color4+uv8

    auto readRgba = [&](std::size_t vertexIndex) {
        std::uint8_t rgba[4];
        std::memcpy(rgba, bytes.data() + vertexIndex * 24 + 12, 4);
        return std::array<std::uint8_t, 4>{rgba[0], rgba[1], rgba[2], rgba[3]};
    };
    EXPECT_EQ(readRgba(0), (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    EXPECT_EQ(readRgba(1), (std::array<std::uint8_t, 4>{0, 255, 0, 255}));
    EXPECT_EQ(readRgba(2), (std::array<std::uint8_t, 4>{0, 0, 255, 255}));

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("colortest");
    ModelMesh* mesh = model.getMeshesProperty()[0];
    auto* basicFx = dynamic_cast<BasicEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(basicFx, nullptr);
    EXPECT_TRUE(basicFx->VertexColorEnabled);
}

// plan_gltf.md GLTF-139/GLTF-130: the offline path has to produce the SAME Model shape as the
// runtime one, or the two loaders disagree about what a mesh is. The .cnj "meshes" array is per
// primitive, so a multi-primitive mesh carries a "partOfMesh" grouping key and the reader folds
// those entries back into one ModelMesh with one part each.
TEST(GltfToCnjToolTest, AMultiPrimitiveMeshRoundTripsAsOneModelMeshWithTwoParts)
{
    static const char* kTwoPrimitiveGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "TwoMaterialNode", "mesh": 0, "translation": [10, 20, 30] } ],
  "materials": [
    { "name": "Red",  "pbrMetallicRoughness": { "baseColorFactor": [1, 0, 0, 1] } },
    { "name": "Blue", "pbrMetallicRoughness": { "baseColorFactor": [0, 0, 1, 1] } }
  ],
  "meshes": [ { "name": "TwoMaterialMesh", "primitives": [
    { "attributes": { "POSITION": 0 }, "material": 0 },
    { "attributes": { "POSITION": 1 }, "material": 1 }
  ] } ],
  "buffers": [ { "byteLength": 72, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACgQAAAAAAAAAAAAADAQAAAAAAAAAAAAACgQAAAgD8AAAAA" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0, 0, 0], "max": [1, 1, 0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [5, 0, 0], "max": [6, 1, 0] }
  ]
})GLTF";

    ScratchDir gltfDir;
    ScratchDir contentRoot;
    WriteFile(gltfDir.path() / "two.gltf", kTwoPrimitiveGltf);
    ASSERT_EQ(0, RunGltfToCnjTool((gltfDir.path() / "two.gltf").string(),
                                   contentRoot.path().string(), "two"));

    // The grouping key is in the file, and only because this mesh has more than one primitive.
    std::ifstream cnj(contentRoot.path() / "two.cnj");
    const std::string text((std::istreambuf_iterator<char>(cnj)), std::istreambuf_iterator<char>());
    EXPECT_NE(std::string::npos, text.find("\"partOfMesh\""))
        << "a multi-primitive mesh was written with no way to regroup its parts";

    GraphicsDevice gd;
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("two");
    ASSERT_EQ(1, model.getMeshesProperty().getCountProperty())
        << "the offline path produced a different Model shape from the runtime one";
    EXPECT_EQ(2, model.getMeshesProperty()[0]->getMeshPartsProperty().getCountProperty());
    EXPECT_EQ(2, model.getMeshesProperty()[0]->getEffectsProperty().getCountProperty())
        << "the two materials collapsed, or an effect never registered on its owning mesh";

    // GLTF-128: bounds are derived independently by the direct loader and the offline .cnj
    // reader, then transformed through the node hierarchy by Model itself. The non-identity node
    // makes an implementation that returns only the local mesh sphere fail unmistakably.
    ContentManager directCm(nullptr, gltfDir.path().string());
    directCm.setGraphicsDevice(gd);
    Model direct = directCm.Load<Model>("two");
    const auto offlineBounds = model.getBoundingSphereEXTProperty();
    const auto directBounds = direct.getBoundingSphereEXTProperty();
    ASSERT_TRUE(offlineBounds.has_value());
    ASSERT_TRUE(directBounds.has_value());
    EXPECT_EQ(directBounds->Center, offlineBounds->Center);
    EXPECT_FLOAT_EQ(directBounds->Radius, offlineBounds->Radius);

    const std::vector<Vector3> expectedWorldPositions{
        Vector3(10.0f, 20.0f, 30.0f), Vector3(11.0f, 20.0f, 30.0f),
        Vector3(10.0f, 21.0f, 30.0f), Vector3(15.0f, 20.0f, 30.0f),
        Vector3(16.0f, 20.0f, 30.0f), Vector3(15.0f, 21.0f, 30.0f),
    };
    for (const Vector3& position : expectedWorldPositions)
    {
        EXPECT_LE(Vector3::Distance(directBounds->Center, position),
                  directBounds->Radius + 1e-5f);
    }
}

// plan_gltf.md GLTF-272: the two loaders must produce the SAME SkinningData.
//
// The runtime path builds a skeleton from the glTF directly; the offline path builds the same one,
// writes it to a .skeleton.bin sidecar, and the .cnj reader reads it back. Those are two
// independent serialisations of one computation, and GLTF-130 already requires the two paths to
// place geometry identically -- a skeleton that differed between them would pose that geometry
// differently while every position assertion still passed.
//
// Compared field for field rather than by a digest, so a divergence names the bone and the matrix
// element rather than "the skeletons differ".
TEST(GltfToCnjToolTest, TheOfflineAndRuntimePathsProduceIdenticalSkinningDataForEverySkinFixture)
{
    // Swept over every `skin-*` fixture in the corpus rather than one, because the ways the two
    // paths can diverge are per-shape: an armature above the joints, a transformed mesh node, a
    // declared skeleton root, 73 joints, unnormalised weights, eight influences. One fixture would
    // prove the serialisation round-trips for one shape.
    //
    // The two paths must also agree on REFUSAL: a fixture the runtime path rejects and the offline
    // path accepts would be a file that imports or not depending on which loader you used.
    const std::filesystem::path corpus = std::filesystem::path("tests") / "assets" / "gltf";
    if (!std::filesystem::exists(corpus)) { GTEST_SKIP() << "the fixture corpus is not present"; }

    const auto expectMatrixEqual = [](const Matrix& expected, const Matrix& actual,
                                       const std::string& what)
    {
        const float* e = &expected.M11;
        const float* a = &actual.M11;
        for (int i = 0; i < 16; ++i)
        {
            EXPECT_NEAR(e[i], a[i], 1e-5f) << what << ", element " << i;
        }
    };

    std::vector<std::string> ids;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(corpus))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind("skin-", 0) == 0 && entry.path().extension() == ".gltf")
        {
            ids.push_back(entry.path().stem().string());
        }
    }
    std::sort(ids.begin(), ids.end());
    ASSERT_FALSE(ids.empty()) << "no skin fixtures found -- the sweep proved nothing";

    std::size_t compared = 0;
    for (const std::string& id : ids)
    {
        SCOPED_TRACE(id);
        ScratchDir contentRoot;
        const int toolExit = RunGltfToCnjTool((corpus / (id + ".gltf")).string(),
                                               contentRoot.path().string(), "skin");

        GraphicsDevice gd;
        ContentManager runtimeCm(nullptr, corpus.string());
        runtimeCm.setGraphicsDevice(gd);

        bool runtimeRefused = false;
        std::unique_ptr<Model> runtime;
        try
        {
            runtime = std::make_unique<Model>(runtimeCm.Load<Model>(id));
        }
        catch (const std::exception&)
        {
            runtimeRefused = true;
        }

        if (toolExit != 0 || runtimeRefused)
        {
            EXPECT_EQ(toolExit != 0, runtimeRefused)
                << "one loader refused this file and the other imported it";
            continue;
        }

        // The tool writes one Model .cnj per mesh group, named after the skin (GLTF-137/GLTF-138),
        // plus one per animation clip -- so the Models are found by their own "type" field rather
        // than by filenames this test would have to predict.
        //
        // ALL of them, sorted, not the first one directory iteration happens to yield.
        // `skin-plus-static-mesh` produces two -- a skinned character and a static prop -- and
        // iteration order is a filesystem detail rather than an ordering, so "the first Model"
        // compared the prop against the runtime path's skinned model on some filesystems and the
        // right pair on others. Which pair to compare is decided by what the models carry: when
        // the runtime path produced skinning data, the offline model to compare with is the one
        // that has skinning data too, and the point of the assertion is that such a model exists.
        std::vector<std::string> modelAssets;
        for (const std::filesystem::directory_entry& out :
             std::filesystem::directory_iterator(contentRoot.path()))
        {
            if (out.path().extension() != ".cnj") { continue; }
            std::ifstream cnj(out.path());
            const std::string text((std::istreambuf_iterator<char>(cnj)),
                                    std::istreambuf_iterator<char>());
            if (text.find("\"type\": \"Model\"") != std::string::npos)
            {
                modelAssets.push_back(out.path().stem().string());
            }
        }
        std::sort(modelAssets.begin(), modelAssets.end());
        ASSERT_FALSE(modelAssets.empty()) << "the tool wrote no Model .cnj at all";

        auto* runtimeSkin = dynamic_cast<SkinningData*>(runtime->getTagProperty());

        ContentManager offlineCm(nullptr, contentRoot.path().string());
        offlineCm.setGraphicsDevice(gd);
        std::optional<Model> offline;
        SkinningData* offlineSkin = nullptr;
        for (const std::string& asset : modelAssets)
        {
            Model candidate = offlineCm.Load<Model>(asset);
            auto* candidateSkin = dynamic_cast<SkinningData*>(candidate.getTagProperty());
            const bool wanted = (runtimeSkin != nullptr) ? (candidateSkin != nullptr) : true;
            if (!offline.has_value() || (wanted && offlineSkin == nullptr))
            {
                offline = std::move(candidate);
                offlineSkin = dynamic_cast<SkinningData*>(offline->getTagProperty());
            }
            if (runtimeSkin != nullptr && offlineSkin != nullptr) { break; }
        }
        ASSERT_TRUE(offline.has_value());
        if (runtimeSkin == nullptr && offlineSkin == nullptr) { continue; }
        ASSERT_NE(nullptr, offlineSkin)
            << "the offline path produced no SkinningData in any of its " << modelAssets.size()
            << " Model .cnj file(s)";
        ASSERT_NE(nullptr, runtimeSkin) << "the runtime path produced no SkinningData";
        ++compared;

        ASSERT_EQ(runtimeSkin->BoneCount, offlineSkin->BoneCount);
        ASSERT_EQ(runtimeSkin->SkeletonHierarchy, offlineSkin->SkeletonHierarchy)
            << "the two paths disagree about the bone tree itself";
        ASSERT_EQ(runtimeSkin->BindPose.size(), offlineSkin->BindPose.size());
        ASSERT_EQ(runtimeSkin->InverseBindPose.size(), offlineSkin->InverseBindPose.size());
        for (std::size_t b = 0; b < runtimeSkin->BindPose.size(); ++b)
        {
            expectMatrixEqual(runtimeSkin->BindPose[b], offlineSkin->BindPose[b],
                              "bone " + std::to_string(b) + " bind pose");
            expectMatrixEqual(runtimeSkin->InverseBindPose[b], offlineSkin->InverseBindPose[b],
                              "bone " + std::to_string(b) + " inverse bind pose");
        }
        ASSERT_EQ(runtimeSkin->SkeletonRootPrefix.size(), offlineSkin->SkeletonRootPrefix.size())
            << "the root-bone prefix -- where GLTF-245/247's own terms live -- differs in size";
        for (std::size_t b = 0; b < runtimeSkin->SkeletonRootPrefix.size(); ++b)
        {
            expectMatrixEqual(runtimeSkin->SkeletonRootPrefix[b], offlineSkin->SkeletonRootPrefix[b],
                              "bone " + std::to_string(b) + " skeleton root prefix");
        }
    }
    EXPECT_GT(compared, 0u) << "every skin fixture was skipped -- the sweep proved nothing";
}

// plan_gltf.md GLTF-121: the node-hierarchy half of the same conversion.
//
// Before GLTF-114 the node translations were discarded outright, so there was nothing for
// unitScale to reach; now they are emitted as the .cnj "bones" array and the two must convert
// together. A model authored in centimetres whose vertices shrink by 100 while its node offsets
// do not is worse than one that ignored unitScale entirely: it comes apart, with each part
// correctly sized and standing a hundred times too far from the next.
TEST(GltfToCnjToolTest, UnitScaleReachesTheNodeHierarchyAndNotOnlyTheVertices)
{
    static const char* kRigidCentimetreGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "Pivot", "translation": [200, 0, 0], "children": [1] },
    { "name": "MeshNode", "mesh": 0, "translation": [0, 300, 0] }
  ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 36,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAADIQgAAAAAAAAAAAAAAAAAAyEIAAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                   "min": [0,0,0], "max": [100,100,0] } ]
})GLTF";

    ScratchDir gltfDir;
    ScratchDir contentRoot;
    const std::filesystem::path gltfPath = gltfDir.path() / "cm.gltf";
    WriteFile(gltfPath, kRigidCentimetreGltf);

    ASSERT_EQ(0, RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "cm", "0.01"));

    // The vertices convert -- the half that already worked, asserted here so a regression in
    // either half fails this test rather than only one of them.
    std::ifstream verts(contentRoot.path() / "cm_mesh0_verts.bin", std::ios::binary);
    const std::vector<char> vertexBytes((std::istreambuf_iterator<char>(verts)),
                                         std::istreambuf_iterator<char>());
    ASSERT_FALSE(vertexBytes.empty());
    const std::size_t stride = vertexBytes.size() / 3u;
    float position[3];
    std::memcpy(position, vertexBytes.data() + stride, sizeof(position));
    EXPECT_NEAR(1.0f, position[0], 1e-4f) << "100 cm did not become 1 unit";

    // And so do the node translations, which is what this test is for. The emitted transform is
    // XNA row-major, so a translation is elements 12..14 of the 16.
    std::ifstream cnj(contentRoot.path() / "cm.cnj");
    const std::string text((std::istreambuf_iterator<char>(cnj)), std::istreambuf_iterator<char>());
    ASSERT_NE(std::string::npos, text.find("\"bones\""));

    // 200 cm -> 2 units and 300 cm -> 3 units. Searched by the value rather than by parsing the
    // whole document, but anchored to the bone's own name so a coincidental 2 elsewhere in the
    // file cannot satisfy it.
    const std::size_t pivot = text.find("\"name\": \"Pivot\"");
    ASSERT_NE(std::string::npos, pivot);
    const std::size_t pivotEnd = text.find('\n', pivot);
    const std::string pivotLine = text.substr(pivot, pivotEnd - pivot);
    EXPECT_NE(std::string::npos, pivotLine.find(", 2, 0, 0, 1]"))
        << "Pivot's translation did not convert with the vertices: " << pivotLine;

    const std::size_t meshNode = text.find("\"name\": \"MeshNode\"");
    ASSERT_NE(std::string::npos, meshNode);
    const std::size_t meshNodeEnd = text.find('\n', meshNode);
    const std::string meshNodeLine = text.substr(meshNode, meshNodeEnd - meshNode);
    EXPECT_NE(std::string::npos, meshNodeLine.find(", 0, 3, 0, 1]"))
        << "MeshNode's translation did not convert with the vertices: " << meshNodeLine;
}

TEST(GltfToCnjToolTest, UnitScaleAppliesToPositionsAndBoneTranslations)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "scale.gltf";
    WriteFile(gltfPath, kUnitScaleGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "scaletest", "0.01");
    ASSERT_EQ(exitCode, 0);

    const std::filesystem::path vertsPath = contentRoot.path() / "scaletest_mesh0_verts.bin";
    std::ifstream f(vertsPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // Stride 68, not 52: this primitive declares no material, so GLTF-215 selects the skinned
    // PBR layout. Position still begins each vertex, which is what this test measures.
    ASSERT_EQ(bytes.size(), 3u * 68u); // stride 68, skinned PBR

    // Authored positions were (0,0,0)/(100,0,0)/(0,100,0) (centimeters); with unitScale=0.01,
    // vertex 1's X must come out as 1.0, not 100.0.
    float pos1[3];
    std::memcpy(pos1, bytes.data() + 1 * 68, sizeof(pos1));
    EXPECT_NEAR(pos1[0], 1.0f, 1e-4f);

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("scaletest");
    auto* skinningData = static_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(skinningData, nullptr);
    ASSERT_EQ(skinningData->BindPose.size(), 1u);
    // RootBone's authored translation was [50,0,0] (centimeters); scaled, its bind-pose X must
    // come out as 0.5.
    EXPECT_NEAR(skinningData->BindPose[0].getTranslationProperty().X, 0.5f, 1e-4f);
}

// CNB-72/73 + plan_gltf.md GLTF-215: a material with both a base-color and an occlusion texture.
//
// This case used to import through DualTextureEffect (Texture=base colour, Texture2=an occlusion
// image halved by RemapOcclusionImageForDualTextureEXT so its own always-multiply blend
// approximated a lightmap), on the stride-20 VertexPositionTexture layout. That whole arrangement
// existed only because the old selection rule asked which texture MAPS were present, and a
// base-colour + occlusion pair matched no PBR map, so PBR was unavailable to it.
//
// GLTF-215 replaced that rule with the material MODEL the file declares. This material is
// metallic-roughness, so it now imports through PbrEffect, which has a real OcclusionMap and needs
// no brightness fake at all. The DualTextureEffect glTF path is therefore superseded rather than
// broken -- the effect itself is untouched and still reachable through every other content path.
// This test now pins the replacement, so the change cannot be undone silently.
TEST(GltfToCnjToolTest, BaseColorAndOcclusionTexturesImportThroughPbrEffectWithARealOcclusionMap)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "dualtex.gltf";
    WriteFile(gltfPath, kDualTextureGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "dualtex");
    ASSERT_EQ(exitCode, 0);

    // Both images are written, and the occlusion one is now carried UNMODIFIED under the ordinary
    // "_tex" sequence rather than halved into its own "_texocc" file: PbrEffect samples occlusion
    // properly, so the CNB-88 brightness compensation would now be a distortion rather than a fix.
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "dualtex_tex0.png"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "dualtex_tex1.png"));
    EXPECT_FALSE(std::filesystem::exists(contentRoot.path() / "dualtex_texocc0.png"))
        << "the DualTextureEffect occlusion remap ran, so the old selection rule is back";

    const std::filesystem::path vertsPath = contentRoot.path() / "dualtex_mesh0_verts.bin";
    std::ifstream f(vertsPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // Stride 48 (Position+Normal+Tangent+TextureCoordinate), not the stride-20
    // VertexPositionTexture layout DualTextureEffect required.
    ASSERT_EQ(bytes.size(), 3u * 48u);

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("dualtex");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    EXPECT_EQ(nullptr, dynamic_cast<DualTextureEffect*>(
                           mesh->getMeshPartsProperty()[0]->getEffectProperty()))
        << "the material still selects DualTextureEffect -- GLTF-215's rule did not apply";
    auto* pbr = dynamic_cast<PbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(nullptr, pbr);

    Texture2D* baseColor = pbr->getTextureProperty();
    ASSERT_NE(nullptr, baseColor);
    EXPECT_EQ(1, baseColor->getWidthProperty());

    // The fixture's occlusion image is a solid (255,0,0) 1x1 PNG. It must arrive intact: the
    // halving existed only to compensate DualTextureEffect's multiply blend.
    Texture2D* occlusion = pbr->getOcclusionMapProperty();
    ASSERT_NE(nullptr, occlusion) << "the occlusion map was dropped rather than carried to PBR";
    Color occlusionPixel(0, 0, 0, 0);
    occlusion->GetData(&occlusionPixel, 1);
    EXPECT_EQ(255, occlusionPixel.getRProperty())
        << "the occlusion texel was halved -- that compensation belongs to DualTextureEffect only";
    EXPECT_EQ(0, occlusionPixel.getGProperty());
    EXPECT_EQ(0, occlusionPixel.getBProperty());
    EXPECT_EQ(255, occlusionPixel.getAProperty());
}

// CNB-66/67/68: a skinned mesh with a COLOR_0 attribute must import through the new stride-56
// (skinned + Color) layout, wiring "vertexColorEnabled": true to SkinnedEffect's new CNAEXT
// VertexColorEnabled property, and the loaded vertex buffer must carry the real per-vertex colors.
TEST(GltfToCnjToolTest, ExtractsVertexColorOnASkinnedMeshAndEnablesItOnSkinnedEffect)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "skincolor.gltf";
    WriteFile(gltfPath, kSkinnedVertexColorGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "skincolor");
    ASSERT_EQ(exitCode, 0);

    const std::filesystem::path vertsPath = contentRoot.path() / "skincolor_mesh0_verts.bin";
    std::ifstream f(vertsPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_EQ(bytes.size(), 3u * 56u); // stride 56, skinned + Color

    // Color is appended after BlendIndices (offset 52); vertex 0 was authored fully-opaque red.
    unsigned char color0[4];
    std::memcpy(color0, bytes.data() + 0 * 56 + 52, sizeof(color0));
    EXPECT_EQ(static_cast<int>(color0[0]), 255);
    EXPECT_EQ(static_cast<int>(color0[1]), 0);
    EXPECT_EQ(static_cast<int>(color0[2]), 0);
    EXPECT_EQ(static_cast<int>(color0[3]), 255);

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skincolor");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    auto* skinnedFx = dynamic_cast<SkinnedEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(skinnedFx, nullptr);
    EXPECT_TRUE(skinnedFx->VertexColorEnabled);
}

// plan_gltf.md GLTF-236/GLTF-237: every core material field must survive the offline .cnj path,
// including the four values that used to be dropped there (base colour/alpha, normal scale and
// occlusion strength). The direct and offline effects are compared at L6, not just against another
// JSON parser, and deliberately non-default sampler/alpha state makes a missing field observable.
TEST(GltfToCnjToolTest, SerializesAndReloadsPbrMaterialThroughTheOfflineCnjPath)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "pbr.gltf";
    WriteFile(gltfPath, kPbrGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "pbr");
    ASSERT_EQ(exitCode, 0);
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "pbr_tex0.png"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "pbr_tex1.png"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "pbr_tex2.png"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "pbr_tex3.png"));

    std::ifstream cnjFile(contentRoot.path() / "pbr.cnj");
    const std::string cnj((std::istreambuf_iterator<char>(cnjFile)),
                          std::istreambuf_iterator<char>());
    EXPECT_NE(std::string::npos, cnj.find("\"normalScale\": 0.35"));
    EXPECT_NE(std::string::npos, cnj.find("\"occlusionStrength\": 0.65"));
    EXPECT_NE(std::string::npos, cnj.find("\"diffuseColor\": [0.25, 0.5, 0.75]"));
    EXPECT_NE(std::string::npos, cnj.find("\"alpha\": 0.4"));
    EXPECT_NE(std::string::npos, cnj.find("\"alphaMode\": \"MASK\""));
    EXPECT_NE(std::string::npos, cnj.find("\"alphaCutoff\": 0.73"));
    EXPECT_NE(std::string::npos, cnj.find("\"doubleSided\": true"));
    EXPECT_NE(std::string::npos, cnj.find("\"ior\": 2"));
    EXPECT_NE(std::string::npos, cnj.find("\"specularFactor\": 0.3"));
    EXPECT_NE(std::string::npos,
              cnj.find("\"specularColorFactor\": [0.25, 1, 12]"));
    EXPECT_NE(std::string::npos, cnj.find("\"sampler0Filter\": 1"));
    EXPECT_NE(std::string::npos, cnj.find("\"sampler0AddressU\": 1"));
    EXPECT_NE(std::string::npos, cnj.find("\"sampler0AddressV\": 2"));

    const std::filesystem::path vertsPath = contentRoot.path() / "pbr_mesh0_verts.bin";
    std::ifstream f(vertsPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_EQ(bytes.size(), 3u * 48u); // stride 48, VertexPositionNormalTangentTexture

    float tangent0[4];
    std::memcpy(tangent0, bytes.data() + 24, sizeof(tangent0));
    EXPECT_NEAR(tangent0[0], 1.0f, 1e-5f);
    EXPECT_NEAR(tangent0[3], 1.0f, 1e-5f);

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    ContentManager runtimeCm(nullptr, gltfDir.path().string());
    runtimeCm.setGraphicsDevice(gd);
    Model runtimeModel = runtimeCm.Load<Model>("pbr");

    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("pbr");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    auto* pbrFx = dynamic_cast<PbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(pbrFx, nullptr);
    ASSERT_NE(pbrFx->getTextureProperty(), nullptr);
    ASSERT_NE(pbrFx->getNormalMapProperty(), nullptr);
    ASSERT_NE(pbrFx->getMetallicRoughnessMapProperty(), nullptr);
    ASSERT_NE(pbrFx->getEmissiveMapProperty(), nullptr);
    ASSERT_NE(pbrFx->getOcclusionMapProperty(), nullptr);
    EXPECT_NEAR(pbrFx->getMetallicFactorProperty(), 0.5f, 1e-5f);
    EXPECT_NEAR(pbrFx->getRoughnessFactorProperty(), 0.3f, 1e-5f);
    EXPECT_NEAR(pbrFx->getIorEXTProperty(), 2.0f, 1e-5f);
    EXPECT_NEAR(pbrFx->getSpecularFactorEXTProperty(), 0.3f, 1e-5f);
    EXPECT_EQ(pbrFx->getSpecularColorFactorEXTProperty(), Vector3(0.25f, 1.0f, 12.0f));
    const Vector3 emissiveFactor = pbrFx->getEmissiveFactorProperty();
    EXPECT_NEAR(emissiveFactor.X, 0.1f, 1e-5f);
    EXPECT_NEAR(emissiveFactor.Y, 0.2f, 1e-5f);
    EXPECT_NEAR(emissiveFactor.Z, 0.3f, 1e-5f);
    EXPECT_NEAR(pbrFx->getNormalScaleEXTProperty(), 0.35f, 1e-5f);
    EXPECT_NEAR(pbrFx->getOcclusionStrengthEXTProperty(), 0.65f, 1e-5f);
    const Vector3 diffuse = pbrFx->getDiffuseColorProperty();
    EXPECT_NEAR(diffuse.X, 0.25f, 1e-5f);
    EXPECT_NEAR(diffuse.Y, 0.5f, 1e-5f);
    EXPECT_NEAR(diffuse.Z, 0.75f, 1e-5f);
    EXPECT_NEAR(pbrFx->getAlphaProperty(), 0.4f, 1e-5f);
    EXPECT_EQ(pbrFx->getAlphaModeEXTProperty(), AlphaModeEXT::Mask);
    EXPECT_NEAR(pbrFx->getAlphaCutoffEXTProperty(), 0.73f, 1e-5f);
    EXPECT_TRUE(pbrFx->getDoubleSidedEXTProperty());

    const auto& sampler = mesh->getMeshPartsProperty()[0]->getSamplerStatesEXTProperty()[0];
    EXPECT_EQ(TextureFilter::Point, sampler.getFilterProperty());
    EXPECT_EQ(TextureAddressMode::Clamp, sampler.getAddressUProperty());
    EXPECT_EQ(TextureAddressMode::Mirror, sampler.getAddressVProperty());

    const Matrix identity = Matrix::getIdentityProperty();
    const auto runtimeDraws = CnaTest::GltfOracle::CaptureDrawParamsEXT(
        runtimeModel, identity, identity, identity);
    const auto offlineDraws = CnaTest::GltfOracle::CaptureDrawParamsEXT(
        model, identity, identity, identity);
    ASSERT_EQ(runtimeDraws.size(), offlineDraws.size());
    ASSERT_EQ(runtimeDraws.size(), 1u);
    ExpectL6MaterialStateEqual(runtimeDraws.front(), offlineDraws.front());

    // A pre-GLTF-216/343/344 or hand-written PBR .cnj may omit diffuseColor/alpha and the Fresnel
    // extension fields. Removing all of them from the rich output must retain the historical
    // white/opaque/core-glTF defaults rather than manufacturing black or zero reflectance.
    std::string legacyCnj = cnj;
    const auto eraseArrayField = [&](const std::string& field) {
        const std::string prefix = ", \"" + field + "\": [";
        const std::size_t begin = legacyCnj.find(prefix);
        ASSERT_NE(begin, std::string::npos);
        const std::size_t end = legacyCnj.find(']', begin + prefix.size());
        ASSERT_NE(end, std::string::npos);
        legacyCnj.erase(begin, end - begin + 1);
    };
    const auto eraseScalarField = [&](const std::string& field) {
        const std::string prefix = ", \"" + field + "\": ";
        const std::size_t begin = legacyCnj.find(prefix);
        ASSERT_NE(begin, std::string::npos);
        const std::size_t end = legacyCnj.find_first_of(",}", begin + prefix.size());
        ASSERT_NE(end, std::string::npos);
        legacyCnj.erase(begin, end - begin);
    };
    eraseArrayField("diffuseColor");
    eraseArrayField("specularColorFactor");
    eraseScalarField("alpha");
    eraseScalarField("ior");
    eraseScalarField("specularFactor");
    WriteFile(contentRoot.path() / "legacy.cnj", legacyCnj);

    Model legacy = cm.Load<Model>("legacy");
    ASSERT_EQ(legacy.getMeshesProperty().getCountProperty(), 1);
    auto* legacyFx = dynamic_cast<PbrEffect*>(
        legacy.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(legacyFx, nullptr);
    EXPECT_EQ(legacyFx->getDiffuseColorProperty(), Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(legacyFx->getAlphaProperty(), 1.0f);
    EXPECT_FLOAT_EQ(legacyFx->getIorEXTProperty(), 1.5f);
    EXPECT_FLOAT_EQ(legacyFx->getSpecularFactorEXTProperty(), 1.0f);
    EXPECT_EQ(legacyFx->getSpecularColorFactorEXTProperty(), Vector3(1.0f, 1.0f, 1.0f));
}

// plan_gltf.md GLTF-237 and the L6 half of GLTF-244: one rich probe can prove that every schema
// field exists, but not that each effect-selection/material shape reaches it. Sweep the complete
// generated `mat-*` group and compare the draw parameter record, including map presence, factors,
// alpha state and all five sampler slots. The floor stops a renamed/shrunk group from passing.
TEST(GltfToCnjToolTest, OfflineAndRuntimePathsHaveIdenticalL6MaterialStateForTheCorpus)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    const std::filesystem::path corpus = std::filesystem::path("tests") / "assets" / "gltf";
    if (!std::filesystem::exists(corpus)) { GTEST_SKIP() << "the fixture corpus is not present"; }

    std::vector<std::string> ids;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(corpus))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind("mat-", 0) == 0 && entry.path().extension() == ".gltf")
        {
            ids.push_back(entry.path().stem().string());
        }
    }
    std::sort(ids.begin(), ids.end());
    ASSERT_GE(ids.size(), 12u) << "the material sweep silently lost corpus coverage";

    std::size_t comparedParts = 0;
    for (const std::string& id : ids)
    {
        SCOPED_TRACE(id);
        ScratchDir contentRoot;
        ASSERT_EQ(0, RunGltfToCnjTool((corpus / (id + ".gltf")).string(),
                                      contentRoot.path().string(), "material"));

        std::string modelAsset;
        for (const std::filesystem::directory_entry& out :
             std::filesystem::directory_iterator(contentRoot.path()))
        {
            if (out.path().extension() != ".cnj") { continue; }
            std::ifstream cnjFile(out.path());
            const std::string text((std::istreambuf_iterator<char>(cnjFile)),
                                   std::istreambuf_iterator<char>());
            if (text.find("\"type\": \"Model\"") != std::string::npos)
            {
                ASSERT_TRUE(modelAsset.empty()) << "material fixture emitted multiple Models";
                modelAsset = out.path().stem().string();
            }
        }
        ASSERT_FALSE(modelAsset.empty()) << "the tool wrote no Model .cnj";

        ContentManager runtimeCm(nullptr, corpus.string());
        runtimeCm.setGraphicsDevice(gd);
        Model runtime = runtimeCm.Load<Model>(id);

        ContentManager offlineCm(nullptr, contentRoot.path().string());
        offlineCm.setGraphicsDevice(gd);
        Model offline = offlineCm.Load<Model>(modelAsset);

        const Matrix identity = Matrix::getIdentityProperty();
        const auto runtimeDraws = CnaTest::GltfOracle::CaptureDrawParamsEXT(
            runtime, identity, identity, identity);
        const auto offlineDraws = CnaTest::GltfOracle::CaptureDrawParamsEXT(
            offline, identity, identity, identity);
        ASSERT_EQ(runtimeDraws.size(), offlineDraws.size());
        ASSERT_FALSE(runtimeDraws.empty()) << "fixture produced no drawable material state";
        comparedParts += runtimeDraws.size();
        for (std::size_t part = 0; part < runtimeDraws.size(); ++part)
        {
            SCOPED_TRACE("draw " + std::to_string(part));
            ExpectL6MaterialStateEqual(runtimeDraws[part], offlineDraws[part]);
        }
    }
    EXPECT_GE(comparedParts, ids.size()) << "the sweep compared fewer parts than fixtures";
}

// plan_gltf.md GLTF-341/GLTF-342: material variants are complete mesh-part states, not merely
// alternate colours. Prove the offline schema keeps the source-order name table, the sparse
// primitive mapping and the PBR-to-unlit vertex-layout/effect transition, then compare every
// selectable state against the direct runtime path at the draw-parameter boundary.
TEST(GltfToCnjToolTest, MaterialVariantsRoundTripAsSelectableCompletePartStates)
{
    const std::filesystem::path corpus = std::filesystem::path("tests") / "assets" / "gltf";
    const std::filesystem::path input = corpus / "mat-material-variants.gltf";
    if (!std::filesystem::exists(input)) { GTEST_SKIP() << "the fixture corpus is not present"; }

    ScratchDir contentRoot;
    ASSERT_EQ(0, RunGltfToCnjTool(input.string(), contentRoot.path().string(), "variants"));

    std::ifstream cnjFile(contentRoot.path() / "variants.cnj");
    const std::string cnj((std::istreambuf_iterator<char>(cnjFile)),
                          std::istreambuf_iterator<char>());
    EXPECT_NE(std::string::npos,
              cnj.find("\"materialVariantNames\": [\"Ocean blue\", \"Unlit green\", "
                       "\"No mapping\"]"));
    EXPECT_NE(std::string::npos, cnj.find("\"variantOf\": 0, \"materialVariant\": 0"));
    EXPECT_NE(std::string::npos, cnj.find("\"variantOf\": 0, \"materialVariant\": 1"));
    EXPECT_NE(std::string::npos, cnj.find("\"vertexStride\": 32, \"effect\": \"BasicEffect\""));

    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";

    ContentManager runtimeContent(nullptr, corpus.string());
    runtimeContent.setGraphicsDevice(gd);
    Model runtime = runtimeContent.Load<Model>("mat-material-variants");

    ContentManager offlineContent(nullptr, contentRoot.path().string());
    offlineContent.setGraphicsDevice(gd);
    Model offline = offlineContent.Load<Model>("variants");

    const std::vector<std::string> expectedNames = {
        "Ocean blue", "Unlit green", "No mapping"};
    EXPECT_EQ(expectedNames, runtime.getMaterialVariantNamesEXTProperty());
    EXPECT_EQ(expectedNames, offline.getMaterialVariantNamesEXTProperty());
    ASSERT_EQ(1, offline.getMeshesProperty().getCountProperty());
    ASSERT_EQ(1, offline.getMeshesProperty()[0]->getMeshPartsProperty().getCountProperty());
    ModelMeshPart* offlinePart = offline.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    auto* defaultEffect = offlinePart->getEffectProperty();
    auto* defaultVertices = offlinePart->getVertexBufferProperty();

    const Matrix identity = Matrix::getIdentityProperty();
    for (const int selection : {-1, 0, 1, 2})
    {
        SCOPED_TRACE("material variant " + std::to_string(selection));
        runtime.setMaterialVariantEXTProperty(selection);
        offline.setMaterialVariantEXTProperty(selection);
        const auto runtimeDraws = CnaTest::GltfOracle::CaptureDrawParamsEXT(
            runtime, identity, identity, identity);
        const auto offlineDraws = CnaTest::GltfOracle::CaptureDrawParamsEXT(
            offline, identity, identity, identity);
        ASSERT_EQ(1u, runtimeDraws.size());
        ASSERT_EQ(1u, offlineDraws.size());
        ExpectL6MaterialStateEqual(runtimeDraws.front(), offlineDraws.front());
    }

    offline.setMaterialVariantEXTProperty(1);
    EXPECT_NE(nullptr, dynamic_cast<BasicEffect*>(offlinePart->getEffectProperty()));
    EXPECT_NE(defaultVertices, offlinePart->getVertexBufferProperty());
    offline.setMaterialVariantEXTProperty(2);
    EXPECT_EQ(defaultEffect, offlinePart->getEffectProperty())
        << "an unmapped variant retained the previously selected primitive state";
    EXPECT_EQ(defaultVertices, offlinePart->getVertexBufferProperty());
    EXPECT_EQ(1, offline.getMeshesProperty()[0]->getEffectsProperty().getCountProperty());

    const auto malformedLoadError = [&](std::string text, const std::string& from,
                                        const std::string& to, const std::string& asset) {
        const std::size_t position = text.find(from);
        if (position == std::string::npos)
        {
            ADD_FAILURE() << "generated variants.cnj does not contain " << from;
            return std::string{};
        }
        text.replace(position, from.size(), to);
        WriteFile(contentRoot.path() / (asset + ".cnj"), text);
        ContentManager malformedContent(nullptr, contentRoot.path().string());
        malformedContent.setGraphicsDevice(gd);
        try
        {
            (void)malformedContent.Load<Model>(asset);
        }
        catch (const ContentLoadException& ex)
        {
            return std::string(ex.what());
        }
        return std::string{};
    };
    EXPECT_NE(std::string::npos,
              malformedLoadError(cnj, "\"variantOf\": 0", "\"variantOf\": 99",
                                 "variants_bad_owner").find("unknown or later mesh entry 99"));
    EXPECT_NE(std::string::npos,
              malformedLoadError(cnj, "\"materialVariant\": 0", "\"materialVariant\": 9",
                                 "variants_bad_index").find("out-of-range materialVariant 9"));
}

// PBR + skinning combo: the offline CLI tool must serialize a skinned, PBR-mapped mesh through
// SkinnedPbrEffect (stride 68), not fall back to plain SkinnedEffect (losing the normal map) or
// plain PbrEffect (losing the skin -- see gltf_to_cnj.cpp's own effect-selection comment).
TEST(GltfToCnjToolTest, SerializesAndReloadsSkinnedPbrMaterialThroughTheOfflineCnjPath)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "skinnedpbr.gltf";
    WriteFile(gltfPath, kSkinnedPbrGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "skinnedpbr");
    ASSERT_EQ(exitCode, 0);
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "skinnedpbr_tex0.png"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "skinnedpbr_tex1.png"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "skinnedpbr.skeleton.bin"));

    const std::filesystem::path vertsPath = contentRoot.path() / "skinnedpbr_mesh0_verts.bin";
    std::ifstream f(vertsPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_EQ(bytes.size(), 3u * 68u); // stride 68, VertexPositionNormalTangentTextureSkinned

    float tangent0[4];
    std::memcpy(tangent0, bytes.data() + 24, sizeof(tangent0));
    EXPECT_NEAR(tangent0[0], 1.0f, 1e-5f);
    EXPECT_NEAR(tangent0[3], 1.0f, 1e-5f);

    float weight0[4];
    std::memcpy(weight0, bytes.data() + 48, sizeof(weight0));
    EXPECT_NEAR(weight0[0], 1.0f, 1e-5f);

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skinnedpbr");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    auto* skinningData = static_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(skinningData, nullptr);
    EXPECT_EQ(skinningData->BoneCount, 1);

    auto* skinnedPbrFx = dynamic_cast<SkinnedPbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(skinnedPbrFx, nullptr);
    ASSERT_NE(skinnedPbrFx->getTextureProperty(), nullptr);
    ASSERT_NE(skinnedPbrFx->getNormalMapProperty(), nullptr);
}

// Morph target CLI/.cnj serialization: the offline CLI tool must write a binary morph sidecar +
// "morphTargets"/"morphWeights"/"morphWeightTrack" JSON fields, and ModelTypeReader's own .cnj
// JSON path must reconstruct the same MorphTargetDataEXT the runtime glTF path already builds
// directly (formerly a documented scope cut -- CNB-64/Phase 13B -- that only emitted a warning).
TEST(GltfToCnjToolTest, SerializesAndReloadsMorphTargetsThroughTheOfflineCnjPath)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "morph.gltf";
    WriteFile(gltfPath, kMorphedTriangleGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "morph");
    ASSERT_EQ(exitCode, 0);
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "morph_mesh0_morph.bin"));

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
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
    EXPECT_TRUE(morph->NormalDeltas[0].empty());

    // mesh.weights=[0.5] must already be reflected in both Weights and the uploaded vertex
    // buffer, exactly like RuntimeGltfModelTest's own identical assertion for the runtime path.
    ASSERT_EQ(morph->Weights.size(), 1u);
    EXPECT_NEAR(morph->Weights[0], 0.5f, 1e-5f);
    const auto blendedAtDefault = BlendMorphTargetsEXT(*morph, morph->Weights);
    float z0;
    std::memcpy(&z0, blendedAtDefault.data() + 2 * sizeof(float), sizeof(float));
    EXPECT_NEAR(z0, 0.5f, 1e-5f);

    // GLTF-128: the imported mesh sphere describes the default pose that was actually uploaded,
    // not the all-zero base pose hidden in MorphTargetDataEXT. All three vertices moved to z=.5.
    const auto offlineBounds = model.getBoundingSphereEXTProperty();
    ASSERT_TRUE(offlineBounds.has_value());
    for (const Vector3& position : std::vector<Vector3>{
             Vector3(0.0f, 0.0f, 0.5f), Vector3(1.0f, 0.0f, 0.5f),
             Vector3(0.0f, 1.0f, 0.5f)})
    {
        EXPECT_LE(Vector3::Distance(offlineBounds->Center, position),
                  offlineBounds->Radius + 1e-5f);
    }

    ContentManager directCm(nullptr, gltfDir.path().string());
    directCm.setGraphicsDevice(gd);
    Model direct = directCm.Load<Model>("morph");
    const auto directBounds = direct.getBoundingSphereEXTProperty();
    ASSERT_TRUE(directBounds.has_value());
    EXPECT_EQ(directBounds->Center, offlineBounds->Center);
    EXPECT_FLOAT_EQ(directBounds->Radius, offlineBounds->Radius);

    // Weight animation: LINEAR 0.0 at t=0 -> 1.0 at t=1.
    ASSERT_EQ(morph->WeightTrack.Keys.size(), 2u);
    EXPECT_FALSE(morph->WeightTrack.StepInterpolation);
    EXPECT_NEAR(morph->WeightTrack.Keys[0].Weights[0], 0.0f, 1e-5f);
    EXPECT_NEAR(morph->WeightTrack.Keys[1].Weights[0], 1.0f, 1e-5f);
    const auto midWeights = EvaluateMorphWeightsEXT(morph->WeightTrack, 0.5);
    ASSERT_EQ(midWeights.size(), 1u);
    EXPECT_NEAR(midWeights[0], 0.5f, 1e-5f);
}

// CUBICSPLINE interpolation for morph-weight animation tracks: the offline CLI/.cnj round-trip
// must preserve the real Hermite tangents through the new "inTangent"/"outTangent" JSON fields,
// not just the sampled middle-third value -- same fixture and hand-derived expected value as
// RuntimeGltfModelTest.LoadsCubicSplineMorphWeightAnimationFromGltf.
TEST(GltfToCnjToolTest, SerializesAndReloadsCubicSplineMorphWeightsThroughTheOfflineCnjPath)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "morphcubic.gltf";
    WriteFile(gltfPath, kCubicSplineMorphedTriangleGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "morphcubic");
    ASSERT_EQ(exitCode, 0);

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
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

    const auto quarterWeights = EvaluateMorphWeightsEXT(morph->WeightTrack, 0.25);
    ASSERT_EQ(quarterWeights.size(), 1u);
    EXPECT_NEAR(quarterWeights[0], 0.15625f, 1e-5f);
}

#ifdef CNA_DRACO_AVAILABLE
// Draco mesh compression decoding (CNB-91, Phase 14F): the offline CLI/.cnj path must decode a
// KHR_draco_mesh_compression primitive too, not just the runtime glTF path -- proves ExtractMesh's
// new `data` parameter threads correctly through gltf_to_cnj.cpp's own ConvertGroup call site.
TEST(GltfToCnjToolTest, ConvertsDracoCompressedTriangleAndLoadsBackThroughContentManager)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "draco.gltf";
    WriteFile(gltfPath, kDracoTriangleGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "draco");
    ASSERT_EQ(exitCode, 0);

    const std::filesystem::path vertsPath = contentRoot.path() / "draco_mesh0_verts.bin";
    std::ifstream f(vertsPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_EQ(bytes.size(), 3u * 32u); // stride 32, VertexPositionNormalTexture

    float px1;
    std::memcpy(&px1, bytes.data() + 32, sizeof(float)); // vertex 1's Position.X
    EXPECT_NEAR(px1, 1.0f, 1e-5f);

    const std::filesystem::path idxPath = contentRoot.path() / "draco_mesh0_idx.bin";
    std::ifstream fi(idxPath, std::ios::binary);
    std::vector<char> idxBytes((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
    ASSERT_EQ(idxBytes.size(), 3u * sizeof(std::uint16_t));

    GraphicsDevice gd;
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("draco");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    auto* basicFx = dynamic_cast<BasicEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(basicFx, nullptr);
}
#endif

// glTF extensions (CNB-97, Phase 14H): the offline CLI/.cnj path's own "lights" JSON field
// (gltf_to_cnj.cpp's writer, ContentManager.cpp's .cnj JSON reader) must round-trip a
// KHR_lights_punctual directional light onto the loaded BasicEffect's own DirectionalLight0.
TEST(GltfToCnjToolTest, SerializesAndReloadsKhrLightsPunctualThroughTheOfflineCnjPath)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "lit.gltf";
    WriteFile(gltfPath, kBasicTexturedWithLightGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "lit");
    ASSERT_EQ(exitCode, 0);

    const std::filesystem::path cnjPath = contentRoot.path() / "lit.cnj";
    std::ifstream f(cnjPath);
    const std::string cnjText((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_NE(cnjText.find("\"lights\""), std::string::npos);

    GraphicsDevice gd;
    // glTF->Model loading builds a real VertexBuffer -- a renderer with no 3D pipeline
    // rejects it before this test's ContentManager::Load<Model> is reached. The offline
    // glTF->.cnj conversion above this point does not touch the renderer and has already
    // run and recorded its own assertions.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("lit");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    // PbrEffect, not BasicEffect, for the same reason as the textured-skinned case above: the
    // material is metallic-roughness, so GLTF-215 selects the PBR path. What this test is actually
    // about is the LIGHT, and the light reaches the same three `DirectionalLight` slots either way
    // -- they are `IEffectLights`, which both effects implement.
    auto* litFx =
        dynamic_cast<PbrEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(litFx, nullptr)
        << "a metallic-roughness material must select PbrEffect (GLTF-215)";

    EXPECT_TRUE(litFx->DirectionalLight0.getEnabledProperty());
    const Vector3 dir = litFx->DirectionalLight0.getDirectionProperty();
    EXPECT_NEAR(dir.X, 0.0f, 1e-4f);
    EXPECT_NEAR(dir.Y, 0.0f, 1e-4f);
    EXPECT_NEAR(dir.Z, -1.0f, 1e-4f);
    const Vector3 color = litFx->DirectionalLight0.getDiffuseColorProperty();
    EXPECT_NEAR(color.X, 0.25f, 1e-5f);
    EXPECT_NEAR(color.Y, 0.5f, 1e-5f);
    EXPECT_NEAR(color.Z, 0.75f, 1e-5f);
}

// plan_gltf.md GLTF-314: the two loaders must agree, clip for clip, on every animation fixture.
//
// The runtime path builds a ModelAnimationsEXT straight from ExtractSceneNodeClips; the offline
// path writes each clip to its own .cnj and reads it back through a JSON parser. Those are two
// entirely separate pieces of code producing the same type, and everything that can go wrong
// between them is silent: a dropped track, a key time rounded through a decimal round-trip, a
// track order that depends on an unordered_map's rehash, a targetSpace that defaults to
// JointPalette on the way back in and poses the wrong bones.
//
// Swept over the whole `anim-*` corpus rather than one fixture, because the divergences are
// per-shape -- disjoint key times, STEP, CUBICSPLINE, two clips, an unnamed clip, a parent and a
// child, a channel path that is skipped.
TEST(GltfToCnjToolTest, TheOfflineAndRuntimePathsProduceIdenticalAnimationClipsForEveryAnimFixture)
{
    const std::filesystem::path corpus = std::filesystem::path("tests") / "assets" / "gltf";
    if (!std::filesystem::exists(corpus)) { GTEST_SKIP() << "the fixture corpus is not present"; }

    std::vector<std::string> ids;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(corpus))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind("anim-", 0) == 0 && entry.path().extension() == ".gltf")
        {
            ids.push_back(entry.path().stem().string());
        }
    }
    std::sort(ids.begin(), ids.end());
    ASSERT_FALSE(ids.empty()) << "no anim fixtures found -- the sweep proved nothing";

    std::size_t compared = 0;
    for (const std::string& id : ids)
    {
        SCOPED_TRACE(id);
        ScratchDir contentRoot;
        const int toolExit = RunGltfToCnjTool((corpus / (id + ".gltf")).string(),
                                               contentRoot.path().string(), "anim");
        ASSERT_EQ(0, toolExit) << "the offline tool refused a fixture the corpus says imports";

        GraphicsDevice gd;
        ContentManager runtimeCm(nullptr, corpus.string());
        runtimeCm.setGraphicsDevice(gd);
        Model runtime = runtimeCm.Load<Model>(id);

        // One Model .cnj per file here (no fixture in this group has a skin), plus one per clip.
        // Found by its "type" field rather than by a filename this test would have to predict.
        std::string modelAsset;
        for (const std::filesystem::directory_entry& out :
             std::filesystem::directory_iterator(contentRoot.path()))
        {
            if (out.path().extension() != ".cnj") { continue; }
            std::ifstream cnj(out.path());
            const std::string text((std::istreambuf_iterator<char>(cnj)),
                                    std::istreambuf_iterator<char>());
            if (text.find("\"type\": \"Model\"") != std::string::npos)
            {
                modelAsset = out.path().stem().string();
                break;
            }
        }
        ASSERT_FALSE(modelAsset.empty()) << "the tool wrote no Model .cnj at all";

        ContentManager offlineCm(nullptr, contentRoot.path().string());
        offlineCm.setGraphicsDevice(gd);
        Model offline = offlineCm.Load<Model>(modelAsset);

        auto* runtimeAnims = dynamic_cast<ModelAnimationsEXT*>(runtime.getTagProperty());
        auto* offlineAnims = dynamic_cast<ModelAnimationsEXT*>(offline.getTagProperty());
        if (runtimeAnims == nullptr && offlineAnims == nullptr) { continue; }
        ASSERT_NE(nullptr, runtimeAnims) << "the runtime path produced no ModelAnimationsEXT";
        ASSERT_NE(nullptr, offlineAnims) << "the offline path produced no ModelAnimationsEXT";
        ++compared;

        ASSERT_EQ(runtimeAnims->Clips.size(), offlineAnims->Clips.size())
            << "the two paths disagree about how many clips this file has";
        for (const auto& [name, runtimeClip] : runtimeAnims->Clips)
        {
            SCOPED_TRACE("clip " + name);
            const auto found = offlineAnims->Clips.find(name);
            ASSERT_NE(offlineAnims->Clips.end(), found)
                << "a clip the runtime path names is missing from the .cnj -- and a clip is looked "
                   "up BY NAME, so this is a lookup that silently returns nothing";
            const AnimationClipEXT& offlineClip = found->second;

            EXPECT_NEAR(runtimeClip.Duration.getTotalSecondsProperty(),
                        offlineClip.Duration.getTotalSecondsProperty(), 1e-5);
            // Applying a joint-palette clip's indices to Model::Bones would pose the wrong bones
            // with no symptom but wrong motion, so the space must survive the round-trip too.
            EXPECT_EQ(runtimeClip.TargetSpace, offlineClip.TargetSpace);
            ASSERT_EQ(runtimeClip.Tracks.size(), offlineClip.Tracks.size());

            for (std::size_t t = 0; t < runtimeClip.Tracks.size(); ++t)
            {
                SCOPED_TRACE("track " + std::to_string(t));
                const BoneTrackEXT& r = runtimeClip.Tracks[t];
                const BoneTrackEXT& o = offlineClip.Tracks[t];
                EXPECT_EQ(r.BoneIndex, o.BoneIndex)
                    << "track order is observable: byBone is an unordered_map, so without a sort "
                       "the two paths agree only until a rehash";
                ASSERT_EQ(r.Keys.size(), o.Keys.size());
                for (std::size_t k = 0; k < r.Keys.size(); ++k)
                {
                    SCOPED_TRACE("key " + std::to_string(k));
                    EXPECT_NEAR(r.Keys[k].Time.getTotalSecondsProperty(),
                                o.Keys[k].Time.getTotalSecondsProperty(), 1e-5);
                    EXPECT_NEAR(r.Keys[k].Translation.X, o.Keys[k].Translation.X, 1e-4f);
                    EXPECT_NEAR(r.Keys[k].Translation.Y, o.Keys[k].Translation.Y, 1e-4f);
                    EXPECT_NEAR(r.Keys[k].Translation.Z, o.Keys[k].Translation.Z, 1e-4f);
                    EXPECT_NEAR(r.Keys[k].Rotation.X, o.Keys[k].Rotation.X, 1e-4f);
                    EXPECT_NEAR(r.Keys[k].Rotation.Y, o.Keys[k].Rotation.Y, 1e-4f);
                    EXPECT_NEAR(r.Keys[k].Rotation.Z, o.Keys[k].Rotation.Z, 1e-4f);
                    EXPECT_NEAR(r.Keys[k].Rotation.W, o.Keys[k].Rotation.W, 1e-4f);
                    EXPECT_NEAR(r.Keys[k].Scale.X, o.Keys[k].Scale.X, 1e-4f);
                    EXPECT_NEAR(r.Keys[k].Scale.Y, o.Keys[k].Scale.Y, 1e-4f);
                    EXPECT_NEAR(r.Keys[k].Scale.Z, o.Keys[k].Scale.Z, 1e-4f);
                }
            }
        }
    }
    EXPECT_GT(compared, 0u) << "every anim fixture was skipped -- the sweep proved nothing";
}
