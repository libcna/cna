// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-246 / GLTF-250 / GLTF-253 / GLTF-259 / GLTF-269 / GLTF-273 / GLTF-286: the
// matrices a skin composes, when it composes them at all, what the import reports about the
// approximations it made, and how skinning and morphing share one vertex buffer.
//
// Skinning is where this campaign's two collapse mechanisms live (D8 and H12), and both were
// arithmetic in the same handful of multiplications: the inverse bind matrix, the joint's world
// transform, and the order the two are combined in. Every rule below has a failure mode that still
// produces a posed mesh -- a mesh posed *wrongly*, usually collapsed toward the origin, which reads
// as a modelling or export problem rather than an importer one.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "GltfFixtureCorpus.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"

using namespace CNA::Internal::GltfImport;
using namespace Microsoft::Xna::Framework;

namespace
{
    constexpr float kTolerance = 1e-4f;

    struct Parsed
    {
        cgltf_data* data = nullptr;
        ~Parsed() { if (data != nullptr) { cgltf_free(data); } }
        Parsed() = default;
        Parsed(const Parsed&) = delete;
        Parsed& operator=(const Parsed&) = delete;
    };

    std::string Base64(const std::vector<std::uint8_t>& bytes)
    {
        static const char* kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const std::uint32_t chunk =
                (static_cast<std::uint32_t>(bytes[i]) << 16) |
                (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0u) |
                (i + 2 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 2]) : 0u);
            out += kAlphabet[(chunk >> 18) & 0x3F];
            out += kAlphabet[(chunk >> 12) & 0x3F];
            out += (i + 1 < bytes.size()) ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
            out += (i + 2 < bytes.size()) ? kAlphabet[chunk & 0x3F] : '=';
        }
        return out;
    }

    void AppendFloats(std::vector<std::uint8_t>& buffer, const std::vector<float>& values)
    {
        for (const float value : values)
        {
            std::uint8_t bytes[4];
            std::memcpy(bytes, &value, 4);
            buffer.insert(buffer.end(), bytes, bytes + 4);
        }
    }

    /// A one-joint skin. When `inverseBind` is empty the skin declares no `inverseBindMatrices` at
    /// all, which §3.7.3.2 says means identity for every joint -- the case GLTF-250 owns.
    std::string SkinnedDocument(const std::string& jointTransform,
                                 const std::vector<float>& inverseBind)
    {
        std::vector<std::uint8_t> buffer;
        AppendFloats(buffer, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
        const std::size_t jointsOffset = buffer.size();
        for (int v = 0; v < 3; ++v) { buffer.insert(buffer.end(), {0, 0, 0, 0}); }
        const std::size_t weightsOffset = buffer.size();
        AppendFloats(buffer, {1, 0, 0, 0,  1, 0, 0, 0,  1, 0, 0, 0});
        const std::size_t ibmOffset = buffer.size();
        if (!inverseBind.empty()) { AppendFloats(buffer, inverseBind); }

        std::string views =
            R"({ "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": )" + std::to_string(jointsOffset) + R"(, "byteLength": 12 },
    { "buffer": 0, "byteOffset": )" + std::to_string(weightsOffset) + R"(, "byteLength": 48 })";
        std::string accessors =
            R"({ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" })";
        std::string skin = R"({ "name": "Skin", "joints": [0] })";
        if (!inverseBind.empty())
        {
            views += R"(,
    { "buffer": 0, "byteOffset": )" + std::to_string(ibmOffset) + R"(, "byteLength": 64 })";
            accessors += R"(,
    { "bufferView": 3, "componentType": 5126, "count": 1, "type": "MAT4" })";
            skin = R"({ "name": "Skin", "joints": [0], "inverseBindMatrices": 3 })";
        }

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "Joint0")GLTF") + (jointTransform.empty() ? "" : ", " + jointTransform) +
               R"GLTF( },
    { "name": "SkinnedMeshNode", "mesh": 0, "skin": 0 }
  ],
  "skins": [ )GLTF" + skin + R"GLTF( ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 }, "mode": 4
  } ] } ],
  "buffers": [ { "byteLength": )GLTF" + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(buffer) +
               R"GLTF(" } ],
  "bufferViews": [ )GLTF" + views + R"GLTF( ],
  "accessors": [ )GLTF" + accessors + R"GLTF( ]
})GLTF";
    }

    bool Parse(Parsed& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }

    SkeletonResult SkeletonOf(const Parsed& parsed)
    {
        const SceneGraphOut scene = BuildSceneGraph(parsed.data);
        return BuildSkeleton(parsed.data->skins, scene, Matrix::getIdentityProperty(), 1.0f);
    }
}

// --- GLTF-250: an absent inverseBindMatrices means identity -------------------------------------

TEST(GltfSkinComposition, AnAbsentInverseBindMatricesMeansIdentityForEveryJoint)
{
    // §3.7.3.2: when `inverseBindMatrices` is undefined, each matrix is the identity. That is a
    // real authoring shape -- a rig whose joints sit at the origin in bind pose needs no matrices
    // at all -- and the failure mode is not an error but a zero matrix, which multiplies every
    // vertex to the origin. The whole mesh collapses to a point, which is D8's symptom exactly.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, SkinnedDocument("", {})));
    ASSERT_EQ(nullptr, parsed.data->skins[0].inverse_bind_matrices)
        << "the fixture must declare no inverseBindMatrices, or it tests the wrong thing";

    const SkeletonResult skeleton = SkeletonOf(parsed);
    ASSERT_EQ(1u, skeleton.bones.size());
    const Matrix& ibm = skeleton.bones[0].inverseBindGlobal;

    const Matrix identity = Matrix::getIdentityProperty();
    const float expected[16] = {identity.M11, identity.M12, identity.M13, identity.M14,
                                identity.M21, identity.M22, identity.M23, identity.M24,
                                identity.M31, identity.M32, identity.M33, identity.M34,
                                identity.M41, identity.M42, identity.M43, identity.M44};
    const float actual[16] = {ibm.M11, ibm.M12, ibm.M13, ibm.M14,
                              ibm.M21, ibm.M22, ibm.M23, ibm.M24,
                              ibm.M31, ibm.M32, ibm.M33, ibm.M34,
                              ibm.M41, ibm.M42, ibm.M43, ibm.M44};
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_NEAR(expected[i], actual[i], kTolerance)
            << "element " << i << " -- a zero matrix here multiplies every bound vertex to the "
               "origin, collapsing the whole mesh to a point";
    }
}

// --- GLTF-246: the inverse bind matrix is read in glTF's own layout ----------------------------

TEST(GltfSkinComposition, AnAuthoredInverseBindMatrixIsReadColumnMajorSoItsTranslationLandsInRow4)
{
    // glTF writes a matrix column-major with the column-vector convention, so its translation is
    // the FOURTH COLUMN -- elements 12..14 of the flat array. XNA's row-vector Matrix keeps a
    // translation in its fourth ROW (M41..M43). Reading the array as row-major instead puts the
    // translation in M14..M34, which is a projective term: the mesh does not merely move, it
    // shears and diverges as the perspective divide picks it up.
    //
    // -100 on Y, which is D8's own fixture scale, so a leak of this term is unmistakable.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, SkinnedDocument(
        "", {1, 0, 0, 0,   0, 1, 0, 0,   0, 0, 1, 0,   0, -100, 0, 1})));

    const SkeletonResult skeleton = SkeletonOf(parsed);
    ASSERT_EQ(1u, skeleton.bones.size());
    const Matrix& ibm = skeleton.bones[0].inverseBindGlobal;

    EXPECT_NEAR(-100.0f, ibm.M42, kTolerance)
        << "the authored translation is not in the fourth row";
    EXPECT_NEAR(0.0f, ibm.M24, kTolerance)
        << "the translation landed in the fourth COLUMN -- the matrix was read row-major, which "
           "turns a translation into a projective term";
    EXPECT_NEAR(1.0f, ibm.M44, kTolerance);
}

// --- GLTF-253: the composition order ------------------------------------------------------------

TEST(GltfSkinComposition, TheBindPoseIsLocalTimesParentWorldInXnasRowVectorOrder)
{
    // XNA applies the LEFT operand first. A joint's bind-pose world transform is therefore
    // `local * parentWorld`, and the swapped order is indistinguishable from correct for any rig
    // whose parents sit at the origin -- which is most test rigs, and none of the real ones.
    //
    // The parent both scales and translates, so the two orders give different answers: the child's
    // own 1-unit offset scaled by 2 and then moved by 100 is 102; the other way round it is 201.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "Root", "translation": [100, 0, 0], "scale": [2, 2, 2], "children": [1] },
    { "name": "Child", "translation": [1, 0, 0] }
  ],
  "skins": [ { "name": "Skin", "joints": [0, 1] } ]
})GLTF")));

    const SceneGraphOut scene = BuildSceneGraph(parsed.data);
    const SkeletonResult skeleton =
        BuildSkeleton(parsed.data->skins, scene, Matrix::getIdentityProperty(), 1.0f);
    ASSERT_EQ(2u, skeleton.bones.size());

    // The topological reorder may renumber the joints, so the child is found by name rather than
    // assumed to be index 1 -- GLTF-247's own reordering is not this test's subject.
    const BoneOut* child = nullptr;
    for (const BoneOut& bone : skeleton.bones)
    {
        if (bone.name == "Child") { child = &bone; }
    }
    ASSERT_NE(nullptr, child);

    // bindPoseLocal is the joint's OWN transform; the composition is what the world transform of
    // its parent chain produces. Transforming the origin through local * parentWorld gives 102.
    const Matrix& parentLocal = skeleton.bones[0].bindPoseLocal;
    const Vector3 composed =
        Vector3::Transform(Vector3::Transform(Vector3(0.0f, 0.0f, 0.0f), child->bindPoseLocal),
                            parentLocal);
    EXPECT_NEAR(102.0f, composed.X, kTolerance)
        << "the child's own offset must be applied first and then the parent's transform; 201 "
           "would mean the operands were swapped";
}

// --- GLTF-259: skinning applies only where the file asked for it -------------------------------

TEST(GltfSkinComposition, APrimitiveWithoutJointsAndWeightsIsNotSkinnedEvenInsideASkinnedFile)
{
    // A file routinely mixes skinned and static geometry -- a character plus its props. The gate is
    // the PRIMITIVE's own JOINTS_0/WEIGHTS_0, not the node's `skin`, and treating everything in a
    // skinned file as skinned would give the static parts a bone palette they have no indices for,
    // binding every vertex to joint 0 and dragging the props onto the character's root.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, SkinnedDocument("", {})));
    const SkeletonResult skeleton = SkeletonOf(parsed);

    const MeshOut skinned = ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "probe",
                                         &skeleton, 1.0f);
    EXPECT_TRUE(skinned.skinned) << "a primitive with JOINTS_0 and WEIGHTS_0 was not skinned";

    // The same primitive extracted with no skeleton at all: the attributes are still there, and the
    // decision must follow the skeleton's absence rather than the attributes' presence.
    const MeshOut unskinned = ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0],
                                           "probe", nullptr, 1.0f);
    EXPECT_FALSE(unskinned.skinned)
        << "a primitive was skinned with no skeleton to skin it against";
    EXPECT_NE(skinned.stride, unskinned.stride)
        << "the two must land on different vertex layouts -- a skinned stride carries BlendWeight "
           "and BlendIndices, and an unskinned one has nowhere to put them";
}

TEST(GltfSkinComposition, AStaticPrimitiveInASkinnedFileGetsNoBlendSlots)
{
    // The other direction, and the one that matters for a mixed file: a primitive with no
    // JOINTS_0/WEIGHTS_0 must stay unskinned even when a skeleton is handed to the extractor,
    // because the file has one for its other meshes.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "Joint0" },
    { "name": "StaticMeshNode", "mesh": 0 }
  ],
  "skins": [ { "name": "Skin", "joints": [0] } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "mode": 4 } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAA==" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] }
  ]
})GLTF")));

    const SkeletonResult skeleton = SkeletonOf(parsed);
    const MeshOut mesh = ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "probe",
                                      &skeleton, 1.0f);
    EXPECT_FALSE(mesh.skinned)
        << "a primitive with no JOINTS_0/WEIGHTS_0 was skinned because the FILE has a skin -- every "
           "vertex would bind to joint 0 and the prop would follow the character's root";

    const CNA::Internal::Graphics::InferredVertexLayout layout =
        CNA::Internal::Graphics::InferredLayoutForStride(
            mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(layout.known);
    for (std::size_t i = 0; i < layout.count; ++i)
    {
        EXPECT_NE(Microsoft::Xna::Framework::Graphics::VertexElementUsage::BlendIndices,
                  layout.elements[i].usage)
            << "the chosen layout carries blend slots for a primitive that has no influences";
    }
}

// --- GLTF-273: the skinning import report ---------------------------------------------------------

TEST(GltfSkinComposition, TheSkinReportCarriesEveryApproximationTheImportMade)
{
    // Four quantities, each a place a rig is imported approximately and each silent on its own.
    // The corpus has a fixture per approximation, so the report is checked against the file that
    // isolates it rather than against one document trying to be all four at once.
    //
    // `skin-eight-influences` authors two influence sets; XNA carries four influences, so the
    // second set is dropped -- and *how much* was dropped is the only thing that says whether that
    // matters. A fifth influence weighted 0.002 is exporter noise; one weighted 0.4 is a visibly
    // different pose.
    {
        const CnaTest::GltfOracle::LoadedFixture fixture("skin-eight-influences");
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const SceneGraphOut scene = BuildSceneGraph(&fixture.Data());
        const SkeletonResult skeleton = BuildSkeleton(fixture.Data().skins, scene,
                                                      Matrix::getIdentityProperty(), 1.0f);
        const MeshOut mesh = ExtractMesh(&fixture.Data(),
                                          fixture.Data().meshes[0].primitives[0], "probe",
                                          &skeleton, 1.0f);
        const SkinReportEXT report = BuildSkinReportEXT(mesh, &skeleton);

        EXPECT_GT(report.jointCount, 0);
        EXPECT_GT(report.droppedInfluenceSets, 0) << "the fixture no longer authors a second set";
        EXPECT_GT(report.worstDroppedInfluence, 0.0f)
            << "the dropped weight is what says whether the drop mattered";
    }

    // `skin-unnormalized` authors weights summing to 0.75, to 1 within float error, and to zero.
    // Exactly one of those is renormalised, and the deviation is what separates a quantised
    // exporter's rounding from a broken file.
    {
        const CnaTest::GltfOracle::LoadedFixture fixture("skin-unnormalized");
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const SceneGraphOut scene = BuildSceneGraph(&fixture.Data());
        const SkeletonResult skeleton = BuildSkeleton(fixture.Data().skins, scene,
                                                      Matrix::getIdentityProperty(), 1.0f);
        const MeshOut mesh = ExtractMesh(&fixture.Data(),
                                          fixture.Data().meshes[0].primitives[0], "probe",
                                          &skeleton, 1.0f);
        const SkinReportEXT report = BuildSkinReportEXT(mesh, &skeleton);

        EXPECT_EQ(1u, report.renormalisedVertexCount)
            << "exactly one vertex is off by more than float error; the other two must be left "
               "alone";
        EXPECT_NEAR(0.25f, report.worstWeightSumDeviation, 1e-4f);
    }

    // And `skin-skeleton-hint` is the one fixture that declares a skeleton root, which the report
    // records without ever letting it bound the ancestry walk (GLTF-249).
    {
        const CnaTest::GltfOracle::LoadedFixture fixture("skin-skeleton-hint");
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const SceneGraphOut scene = BuildSceneGraph(&fixture.Data());
        const SkeletonResult skeleton = BuildSkeleton(fixture.Data().skins, scene,
                                                      Matrix::getIdentityProperty(), 1.0f);
        const MeshOut mesh = ExtractMesh(&fixture.Data(),
                                          fixture.Data().meshes[0].primitives[0], "probe",
                                          &skeleton, 1.0f);
        EXPECT_TRUE(BuildSkinReportEXT(mesh, &skeleton).hasDeclaredSkeletonRoot);
    }
}

TEST(GltfSkinComposition, AnUnskinnedPrimitiveReportsNoJointsRatherThanAnEmptySkeletonsWorth)
{
    // The control. A report built for an unskinned primitive must say zero joints and zero of
    // everything else -- not the previous skin's numbers, and not a plausible-looking default.
    const CnaTest::GltfOracle::LoadedFixture fixture("xf-identity");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const MeshOut mesh = ExtractMesh(&fixture.Data(), fixture.Data().meshes[0].primitives[0],
                                      "probe", nullptr, 1.0f);
    const SkinReportEXT report = BuildSkinReportEXT(mesh, nullptr);
    EXPECT_EQ(0, report.jointCount);
    EXPECT_EQ(0, report.droppedInfluenceSets);
    EXPECT_EQ(0u, report.renormalisedVertexCount);
    EXPECT_FALSE(report.hasDeclaredSkeletonRoot);
}

// --- GLTF-269 / GLTF-286: morphing and skinning share one vertex buffer ---------------------------

TEST(GltfSkinComposition, AMorphedSkinnedPrimitiveKeepsBothItsSkinAndItsBlendedPositions)
{
    // The two features meet in one place: the CPU morph blend rewrites the vertex buffer, and the
    // skinning shader reads that same buffer. So they must not be mutually exclusive at import
    // (a primitive with both has to keep both) and the blend must write into the layout the SKIN
    // is in -- a blend that assumed an unskinned stride would move whichever field happens to sit
    // at the unskinned position offset, which for a skinned layout is still Position, so the
    // failure would appear only in the fields *after* it.
    //
    // The document is a one-joint skinned triangle with one morph target moving vertex 1 by +7 on
    // Y, and a mesh-level default weight of 1, so the imported rest pose is already morphed.
    const char* json = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "Joint0" },
    { "name": "SkinnedMeshNode", "mesh": 0, "skin": 0 }
  ],
  "skins": [ { "name": "Skin", "joints": [0], "inverseBindMatrices": 5 } ],
  "meshes": [ { "name": "MorphedSkin", "weights": [1.0], "primitives": [ {
      "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 },
      "indices": 4,
      "targets": [ { "POSITION": 3 } ] } ] } ],
  "buffers": [ { "byteLength": 204, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA4EAAAAAAAAAAAAAAAAAAAAAAAAABAAIAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 12 },
    { "buffer": 0, "byteOffset": 48,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 132, "byteLength": 6 },
    { "buffer": 0, "byteOffset": 140, "byteLength": 64 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [0,7,0] },
    { "bufferView": 4, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 5, "componentType": 5126, "count": 1, "type": "MAT4" }
  ]
})GLTF";

    Parsed doc;
    ASSERT_TRUE(Parse(doc, json));
    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    const SkeletonResult skeleton =
        BuildSkeleton(doc.data->skins, scene, Matrix::getIdentityProperty(), 1.0f);
    const MeshOut mesh =
        ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe", &skeleton, 1.0f);

    // Both features survived: the primitive is skinned AND carries its morph deltas.
    EXPECT_TRUE(mesh.skinned) << "the skin was dropped because the primitive also morphs";
    ASSERT_EQ(1u, mesh.morphPositionDeltas.size())
        << "the morph target was dropped because the primitive is also skinned";
    EXPECT_FLOAT_EQ(7.0f, mesh.morphPositionDeltas[0][1].Y);

    // The default weight is the mesh's own, so the rest pose really is morphed rather than the
    // deltas merely being present (GLTF-281).
    const std::vector<float> defaults = GetMeshDefaultWeights(&doc.data->meshes[0], 1);
    ASSERT_EQ(1u, defaults.size());
    EXPECT_FLOAT_EQ(1.0f, defaults[0]);

    // And the blend writes into the SKINNED layout, not an assumed unskinned one: the blended
    // buffer keeps every skinning field intact while the position moves.
    Microsoft::Xna::Framework::Graphics::MorphTargetDataEXT morph;
    morph.BaseVertexBytes = mesh.vertexBytes;
    morph.Stride = mesh.stride;
    morph.PositionDeltas.push_back(mesh.morphPositionDeltas[0]);
    morph.NormalDeltas.push_back({});
    morph.TangentDeltas.push_back({});
    const std::vector<std::uint8_t> blended =
        Microsoft::Xna::Framework::Graphics::BlendMorphTargetsEXT(morph, {1.0f});
    ASSERT_EQ(mesh.vertexBytes.size(), blended.size());

    const CNA::Internal::Graphics::InferredVertexLayout layout =
        CNA::Internal::Graphics::InferredLayoutForStride(
            mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(layout.known);
    int blendWeightOffset = -1;
    for (std::size_t e = 0; e < layout.count; ++e)
    {
        if (layout.elements[e].usage ==
            Microsoft::Xna::Framework::Graphics::VertexElementUsage::BlendWeight)
        {
            blendWeightOffset = layout.elements[e].offset;
        }
    }
    ASSERT_GE(blendWeightOffset, 0) << "this stride is not a skinned one after all";

    float position[3];
    std::memcpy(position, blended.data() + static_cast<std::size_t>(mesh.stride), sizeof(position));
    EXPECT_FLOAT_EQ(7.0f, position[1]) << "the morph did not reach the skinned buffer";

    float blendWeights[4];
    std::memcpy(blendWeights, blended.data() + static_cast<std::size_t>(mesh.stride) +
                                   static_cast<std::size_t>(blendWeightOffset),
                sizeof(blendWeights));
    EXPECT_FLOAT_EQ(1.0f, blendWeights[0])
        << "the blend overwrote the skinning weights -- it is writing at an unskinned layout's "
           "offsets";
}
