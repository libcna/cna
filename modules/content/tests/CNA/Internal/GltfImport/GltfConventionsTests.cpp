// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-104 / GLTF-105 / GLTF-106 / GLTF-108 / GLTF-191.
//
// The coordinate conventions glTF 2.0 and XNA 4.0 already agree on -- handedness, up axis, forward
// axis, UV origin and quaternion component order -- and therefore the conversions the importer
// deliberately does NOT contain. Front-face winding is the one rasterizer-state difference and is
// handled at draw time rather than by rewriting shared buffers; see docs/gltf-conventions.md.
//
// These are the most dangerous invariants in the project precisely because they are invisible: an
// axis flip or a V flip is a few characters, looks like a fix to whoever writes it, and breaks
// every asset at once in a way that is easy to mistake for a modelling error. The absence of
// conversion code is a decision, and a decision with no test is a coincidence waiting to be
// "corrected". Any future flip must fail here first.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using namespace CNA::Internal::GltfImport;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr float kTolerance = 1e-5f;

    struct Parsed
    {
        cgltf_data* data = nullptr;
        ~Parsed() { if (data != nullptr) { cgltf_free(data); } }
        Parsed() = default;
        Parsed(const Parsed&) = delete;
        Parsed& operator=(const Parsed&) = delete;
    };

    bool Parse(Parsed& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }

    /// A one-triangle document whose single node carries @p nodeTransform (a JSON fragment such as
    /// `"matrix": [...]` or `"rotation": [...]`), with an authored NORMAL and TEXCOORD_0 so the
    /// pass-through invariants can be read off the same file.
    ///
    /// The buffer holds, in order: three VEC3 positions, three VEC3 normals, three VEC2 UVs, three
    /// u16 indices. Values are chosen so every component is distinct and non-symmetric -- a swapped
    /// or negated axis cannot land on the value it should have had.
    std::string ConventionDocument(const std::string& nodeTransform)
    {
        const std::vector<float> positions = {1.0f, 2.0f, 3.0f,
                                              4.0f, 5.0f, 6.0f,
                                              7.0f, 8.0f, 9.0f};
        const std::vector<float> normals = {0.0f, 0.0f, 1.0f,
                                            1.0f, 0.0f, 0.0f,
                                            0.0f, 1.0f, 0.0f};
        const std::vector<float> uvs = {0.25f, 0.75f,
                                        0.125f, 0.5f,
                                        0.875f, 0.0625f};
        const std::vector<std::uint16_t> indices = {0, 1, 2};

        std::vector<std::uint8_t> buffer;
        const auto appendFloats = [&](const std::vector<float>& values) {
            for (const float value : values)
            {
                std::uint8_t bytes[4];
                std::memcpy(bytes, &value, 4);
                buffer.insert(buffer.end(), bytes, bytes + 4);
            }
        };
        appendFloats(positions);
        appendFloats(normals);
        appendFloats(uvs);
        const std::size_t indexOffset = buffer.size();
        for (const std::uint16_t value : indices)
        {
            buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
            buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        }
        while (buffer.size() % 4 != 0) { buffer.push_back(0); }

        static const char* kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string base64;
        for (std::size_t i = 0; i < buffer.size(); i += 3)
        {
            const std::uint32_t chunk =
                (static_cast<std::uint32_t>(buffer[i]) << 16) |
                (i + 1 < buffer.size() ? static_cast<std::uint32_t>(buffer[i + 1]) << 8 : 0u) |
                (i + 2 < buffer.size() ? static_cast<std::uint32_t>(buffer[i + 2]) : 0u);
            base64 += kAlphabet[(chunk >> 18) & 0x3F];
            base64 += kAlphabet[(chunk >> 12) & 0x3F];
            base64 += (i + 1 < buffer.size()) ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
            base64 += (i + 2 < buffer.size()) ? kAlphabet[chunk & 0x3F] : '=';
        }

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "TransformNode", "mesh": 0)GLTF") +
               (nodeTransform.empty() ? "" : ", " + nodeTransform) + R"GLTF( } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }, "indices": 3, "mode": 4
  } ] } ],
  "buffers": [ { "byteLength": )GLTF" + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + base64 + R"GLTF(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(indexOffset) +
               R"GLTF(, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [1,2,3], "max": [7,8,9] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
})GLTF";
    }

    /// The byte offset of a usage within the primitive's chosen stride, read from the canonical
    /// table rather than hardcoded -- the lesson of GLTF-278.
    int OffsetOf(const MeshOut& mesh, VertexElementUsage usage)
    {
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known) << "stride " << mesh.stride << " is not in the canonical table";
        for (std::size_t i = 0; layout.known && i < layout.count; ++i)
        {
            if (layout.elements[i].usage == usage && layout.elements[i].usageIndex == 0)
            {
                return layout.elements[i].offset;
            }
        }
        return -1;
    }

    std::vector<float> ReadFloats(const MeshOut& mesh, std::size_t vertex, int offset,
                                  std::size_t count)
    {
        std::vector<float> out(count);
        std::memcpy(out.data(),
                    mesh.vertexBytes.data() + vertex * static_cast<std::size_t>(mesh.stride) +
                        static_cast<std::size_t>(offset),
                    count * sizeof(float));
        return out;
    }

    void ExpectMatrixNear(const Matrix& expected, const Matrix& actual)
    {
        const float e[16] = {expected.M11, expected.M12, expected.M13, expected.M14,
                             expected.M21, expected.M22, expected.M23, expected.M24,
                             expected.M31, expected.M32, expected.M33, expected.M34,
                             expected.M41, expected.M42, expected.M43, expected.M44};
        const float a[16] = {actual.M11, actual.M12, actual.M13, actual.M14,
                             actual.M21, actual.M22, actual.M23, actual.M24,
                             actual.M31, actual.M32, actual.M33, actual.M34,
                             actual.M41, actual.M42, actual.M43, actual.M44};
        for (int i = 0; i < 16; ++i)
        {
            EXPECT_NEAR(e[i], a[i], kTolerance) << "element " << i;
        }
    }
}

// --- GLTF-105: no axis, handedness or UV conversion ---------------------------------------------

TEST(GltfConventions, PositionsNormalsAndUvsPassThroughUnchanged)
{
    // Both formats are right-handed, +Y up, −Z forward, with a top-left UV origin, so the importer
    // contains no remap at all. Every authored component here is distinct, so a swapped or negated
    // axis cannot land on the value it should have had and pass by coincidence.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, ConventionDocument("")));
    const MeshOut mesh = ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "probe",
                                      nullptr, 1.0f);
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));

    const int positionOffset = OffsetOf(mesh, VertexElementUsage::Position);
    const int normalOffset = OffsetOf(mesh, VertexElementUsage::Normal);
    const int uvOffset = OffsetOf(mesh, VertexElementUsage::TextureCoordinate);
    ASSERT_EQ(0, positionOffset) << "Position is the first element of every stride";
    ASSERT_GE(normalOffset, 0);
    ASSERT_GE(uvOffset, 0);

    const std::vector<std::vector<float>> expectedPositions = {
        {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f}};
    const std::vector<std::vector<float>> expectedNormals = {
        {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    const std::vector<std::vector<float>> expectedUvs = {
        {0.25f, 0.75f}, {0.125f, 0.5f}, {0.875f, 0.0625f}};

    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> position = ReadFloats(mesh, v, positionOffset, 3);
        const std::vector<float> normal = ReadFloats(mesh, v, normalOffset, 3);
        const std::vector<float> uv = ReadFloats(mesh, v, uvOffset, 2);
        for (std::size_t c = 0; c < 3; ++c)
        {
            EXPECT_NEAR(expectedPositions[v][c], position[c], kTolerance) << "position " << c;
            EXPECT_NEAR(expectedNormals[v][c], normal[c], kTolerance)
                << "normal " << c << " -- a +Z-facing glTF normal must stay +Z";
        }
        // GLTF-191: the UV origin is top-left in both formats, so V is never mirrored. This is the
        // assertion a render-target V-flip must not be allowed to leak into (GLTF-192): that flip
        // belongs to the sampler at draw time, not to the asset's own coordinates.
        EXPECT_NEAR(expectedUvs[v][0], uv[0], kTolerance) << "U";
        EXPECT_NEAR(expectedUvs[v][1], uv[1], kTolerance)
            << "V -- a flip here would mirror every texture on every imported model";
    }
}

// --- GLTF-106: the matrix layout conversion, and only that -------------------------------------

TEST(GltfConventions, ANodeMatrixWithRotationAndScaleSurvivesTheLayoutConversion)
{
    // glTF stores a matrix column-major with the column-vector convention; XNA's Matrix is
    // row-major with the row-vector convention. ConvertGltfMatrix copies basis vectors into the
    // layout XNA reads them from -- a storage conversion, not a coordinate one.
    //
    // The rotation block is deliberately NON-SYMMETRIC. A translation-only matrix -- which is what
    // the corpus's own xf-matrix-node uses -- cannot tell a correct basis-vector copy from a naive
    // full transpose, because transposing the identity gives the identity. This one can.
    //
    // The matrix is a 90 degree rotation about +Y (which maps +X to -Z), then a scale of 2 on X and
    // 3 on Y, then a translation of (10, 20, 30). Written out column-major as glTF requires:
    //   column 0 = 2 * (rotated X axis) = (0, 0, -2)
    //   column 1 = 3 * (rotated Y axis) = (0, 3, 0)
    //   column 2 = 1 * (rotated Z axis) = (1, 0, 0)
    //   column 3 = translation          = (10, 20, 30)
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, ConventionDocument(
        R"("matrix": [0, 0, -2, 0,   0, 3, 0, 0,   1, 0, 0, 0,   10, 20, 30, 1])")));

    const SceneGraphOut scene = BuildSceneGraph(parsed.data);
    ASSERT_GE(scene.nodes.size(), 2u) << "index 0 is the synthetic root; the file's node follows";
    const SceneNodeOut& node = scene.nodes[1];
    ASSERT_EQ("TransformNode", node.name);

    // In XNA's row-vector form the basis vectors are the ROWS, so the same transform reads as the
    // transpose of the glTF array's own 4x4 -- which is exactly what a basis-vector copy produces.
    Matrix expected;
    expected.M11 = 0.0f;  expected.M12 = 0.0f;  expected.M13 = -2.0f; expected.M14 = 0.0f;
    expected.M21 = 0.0f;  expected.M22 = 3.0f;  expected.M23 = 0.0f;  expected.M24 = 0.0f;
    expected.M31 = 1.0f;  expected.M32 = 0.0f;  expected.M33 = 0.0f;  expected.M34 = 0.0f;
    expected.M41 = 10.0f; expected.M42 = 20.0f; expected.M43 = 30.0f; expected.M44 = 1.0f;
    ExpectMatrixNear(expected, node.localTransform);

    // And the transform must actually do what it says: (1,0,0) scaled by 2 and rotated 90 degrees
    // about +Y lands on -Z, then the translation moves it. A transposed rotation block would send
    // it to +Z instead, which the element comparison above already catches -- this states the
    // consequence in the terms a reader cares about.
    const Vector3 transformed = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), node.localTransform);
    EXPECT_NEAR(10.0f, transformed.X, kTolerance);
    EXPECT_NEAR(20.0f, transformed.Y, kTolerance);
    EXPECT_NEAR(28.0f, transformed.Z, kTolerance)
        << "+X should have been rotated onto -Z and scaled by 2, giving 30 - 2";
}

// --- GLTF-108: quaternion component order -------------------------------------------------------

TEST(GltfConventions, AQuaternionIsCopiedComponentForComponentInXyzwOrder)
{
    // glTF writes [x, y, z, w]; XNA's Quaternion is (X, Y, Z, W). A straight member copy, with no
    // reordering -- and the classic way to get this wrong is to read it as [w, x, y, z], which for
    // an identity rotation is indistinguishable from correct.
    //
    // 90 degrees about +X: [sin(45), 0, 0, cos(45)]. Under the correct order that maps +Y to +Z.
    // Under a w-first misread it is a rotation about +Y by a different angle, and +Y would be left
    // alone -- so this fixture cannot pass by coincidence.
    const float s = std::sin(3.14159265358979323846f / 4.0f);
    const float c = std::cos(3.14159265358979323846f / 4.0f);
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, ConventionDocument(
        R"("rotation": [)" + std::to_string(s) + ", 0, 0, " + std::to_string(c) + "]")));

    const SceneGraphOut scene = BuildSceneGraph(parsed.data);
    ASSERT_GE(scene.nodes.size(), 2u);
    const Vector3 up = Vector3::Transform(Vector3(0.0f, 1.0f, 0.0f), scene.nodes[1].localTransform);
    EXPECT_NEAR(0.0f, up.X, kTolerance);
    EXPECT_NEAR(0.0f, up.Y, kTolerance)
        << "+Y was left alone, which is what a [w,x,y,z] misread of this quaternion produces";
    EXPECT_NEAR(1.0f, up.Z, kTolerance) << "+Y should have rotated onto +Z";
}

TEST(GltfConventions, AMatrixNodeAndTheEquivalentTrsNodeProduceTheSameTransform)
{
    // §3.5.2 makes `matrix` and TRS mutually exclusive descriptions of the same thing, so the two
    // authorings of one transform must compose identically. This is the cross-check that catches a
    // transpose in exactly one of the two paths -- an error each path's own test would miss,
    // because each is self-consistent.
    const float s = std::sin(3.14159265358979323846f / 4.0f);
    const float c = std::cos(3.14159265358979323846f / 4.0f);

    Parsed trs;
    ASSERT_TRUE(Parse(trs, ConventionDocument(
        R"("translation": [10, 20, 30], "rotation": [)" + std::to_string(s) + ", 0, 0, " +
        std::to_string(c) + R"(], "scale": [2, 3, 4])")));
    const SceneGraphOut trsScene = BuildSceneGraph(trs.data);
    ASSERT_GE(trsScene.nodes.size(), 2u);

    // The same transform as a matrix: S then R then T, written column-major.
    //   90 degrees about +X maps +Y to +Z and +Z to -Y.
    //   column 0 = 2 * (1,0,0)  = (2, 0, 0)
    //   column 1 = 3 * (0,0,1)  = (0, 0, 3)
    //   column 2 = 4 * (0,-1,0) = (0, -4, 0)
    //   column 3 = (10, 20, 30)
    Parsed matrix;
    ASSERT_TRUE(Parse(matrix, ConventionDocument(
        R"("matrix": [2, 0, 0, 0,   0, 0, 3, 0,   0, -4, 0, 0,   10, 20, 30, 1])")));
    const SceneGraphOut matrixScene = BuildSceneGraph(matrix.data);
    ASSERT_GE(matrixScene.nodes.size(), 2u);

    ExpectMatrixNear(trsScene.nodes[1].localTransform, matrixScene.nodes[1].localTransform);
}

// --- GLTF-104: the transform orders that are load-bearing ---------------------------------------

TEST(GltfConventions, SceneGraphCompositionIsLocalTimesParentWorld)
{
    // XNA's row-vector convention applies the LEFT operand first, so a node's own transform must
    // come before its ancestry. Swapped, a child would be placed as though its parent's transform
    // were applied in its own local space -- which for a parent at the origin is indistinguishable
    // from correct, hence a parent that is NOT at the origin here.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "Parent", "translation": [100, 0, 0], "scale": [2, 2, 2], "children": [1] },
    { "name": "Child", "translation": [1, 0, 0] }
  ]
})GLTF")));

    const SceneGraphOut scene = BuildSceneGraph(parsed.data);
    ASSERT_GE(scene.nodes.size(), 3u);
    const SceneNodeOut& child = scene.nodes[2];
    ASSERT_EQ("Child", child.name);

    // local * parentWorld: the child's own (1,0,0) offset is scaled by the parent's 2 and then
    // moved by the parent's 100, giving 102. The swapped order would give 201 -- the parent's
    // translation scaled by the child's identity and then offset by 1.
    const Vector3 origin = Vector3::Transform(Vector3(0.0f, 0.0f, 0.0f), child.worldTransform);
    EXPECT_NEAR(102.0f, origin.X, kTolerance)
        << "scene composition is local * parentWorld; 201 would mean the operands were swapped";
    EXPECT_NEAR(0.0f, origin.Y, kTolerance);
    EXPECT_NEAR(0.0f, origin.Z, kTolerance);
}

TEST(GltfConventions, ModelDrawBindsAbsoluteBoneTransformTimesWorld)
{
    // Model::Draw binds `absoluteBoneTransform * world` -- the bone first, then the caller's world
    // matrix. Swapped, a model would be positioned by its bone hierarchy relative to the world
    // rather than the other way round, which for a rig near the origin looks almost right.
    //
    // The two matrices are chosen so the orders are unmistakable: a scale of 2 and a translation of
    // (5,0,0) commute one way to (10,0,0) and the other to (7,0,0).
    GraphicsDevice device;

    ModelBone bone{0, "Bone"};
    bone.setTransformProperty(Matrix::CreateTranslation(5.0f, 0.0f, 0.0f));

    BasicEffect effect(device);
    // primitiveCount 0, so ModelMesh::Draw skips the part entirely and no device work happens --
    // Model::Draw's matrix binding is all that runs.
    ModelMeshPart part(nullptr, nullptr, 0, 0, 0, 0);
    ModelMesh mesh(nullptr, std::vector<ModelMeshPart*>{&part});
    part.setEffectProperty(&effect);
    mesh.setParentBoneProperty(&bone);
    Model model(nullptr, {&bone}, {&mesh});

    model.Draw(Matrix::CreateScale(2.0f), Matrix::getIdentityProperty(),
               Matrix::getIdentityProperty());

    const Vector3 placed = Vector3::Transform(Vector3(0.0f, 0.0f, 0.0f),
                                              effect.getWorldProperty());
    EXPECT_NEAR(10.0f, placed.X, kTolerance)
        << "bone * world puts the bone's translation through the world scale; 5 would mean the "
           "operands were swapped";
    EXPECT_NEAR(0.0f, placed.Y, kTolerance);
    EXPECT_NEAR(0.0f, placed.Z, kTolerance);
}
