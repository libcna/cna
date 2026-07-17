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

#include <array>
#include <cerrno>
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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

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
    "byteLength": 74,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AQAAAABAAABAQAAAgEA="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 60, "byteLength": 2 },
    { "buffer": 0, "byteOffset": 62, "byteLength": 12 }
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
    ASSERT_EQ(bytes.size(), 3u * 32u); // stride 32, unskinned

    auto readVec3 = [&](std::size_t vertexIndex) {
        float v[3];
        std::memcpy(v, bytes.data() + vertexIndex * 32, sizeof(v));
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
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);

    Model model = cm.Load<Model>("textest");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];
    ASSERT_EQ(mesh->getMeshPartsProperty().getCountProperty(), 1);

    auto* skinnedFx = dynamic_cast<SkinnedEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(skinnedFx, nullptr);
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
    ASSERT_EQ(bytes.size(), 3u * 32u); // stride 32, unskinned, untextured-color

    // UV lives at byte offset 24 within each stride-32 vertex (pos12+normal12+uv8). TEXCOORD_0
    // was deliberately filled with (9,9) sentinels the tool must never emit; the real values,
    // from TEXCOORD_1 (the set the material's baseColorTexture actually selects), are (0,0),
    // (1,0), (0,1).
    float uv0[2], uv1[2], uv2[2];
    std::memcpy(uv0, bytes.data() + 0 * 32 + 24, sizeof(uv0));
    std::memcpy(uv1, bytes.data() + 1 * 32 + 24, sizeof(uv1));
    std::memcpy(uv2, bytes.data() + 2 * 32 + 24, sizeof(uv2));
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
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("colortest");
    ModelMesh* mesh = model.getMeshesProperty()[0];
    auto* basicFx = dynamic_cast<BasicEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(basicFx, nullptr);
    EXPECT_TRUE(basicFx->VertexColorEnabled);
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
    ASSERT_EQ(bytes.size(), 3u * 52u); // stride 52, skinned

    // Authored positions were (0,0,0)/(100,0,0)/(0,100,0) (centimeters); with unitScale=0.01,
    // vertex 1's X must come out as 1.0, not 100.0.
    float pos1[3];
    std::memcpy(pos1, bytes.data() + 1 * 52, sizeof(pos1));
    EXPECT_NEAR(pos1[0], 1.0f, 1e-4f);

    GraphicsDevice gd;
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

// CNB-72/73: a material with both a base-color and an occlusion texture must be imported through
// DualTextureEffect (Texture=base color, Texture2=occlusion), with the mesh's vertex buffer using
// the stride-20 VertexPositionTexture layout DualTextureEffect's shader actually expects.
TEST(GltfToCnjToolTest, WiresBaseColorAndOcclusionTexturesThroughDualTextureEffect)
{
    ScratchDir gltfDir;
    ScratchDir contentRoot;

    const std::filesystem::path gltfPath = gltfDir.path() / "dualtex.gltf";
    WriteFile(gltfPath, kDualTextureGltf);

    const int exitCode = RunGltfToCnjTool(gltfPath.string(), contentRoot.path().string(), "dualtex");
    ASSERT_EQ(exitCode, 0);
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "dualtex_tex0.png"));
    ASSERT_TRUE(std::filesystem::exists(contentRoot.path() / "dualtex_tex1.png"));

    const std::filesystem::path vertsPath = contentRoot.path() / "dualtex_mesh0_verts.bin";
    std::ifstream f(vertsPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_EQ(bytes.size(), 3u * 20u); // stride 20, VertexPositionTexture (no Normal)

    GraphicsDevice gd;
    ContentManager cm(nullptr, contentRoot.path().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("dualtex");
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    ModelMesh* mesh = model.getMeshesProperty()[0];

    auto* dualFx = dynamic_cast<DualTextureEffect*>(mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(dualFx, nullptr);
    Texture2D* tex1 = dualFx->getTextureProperty();
    Texture2D* tex2 = dualFx->getTexture2Property();
    ASSERT_NE(tex1, nullptr);
    ASSERT_NE(tex2, nullptr);
    EXPECT_EQ(tex1->getWidthProperty(), 1);
    EXPECT_EQ(tex2->getWidthProperty(), 1);
}
