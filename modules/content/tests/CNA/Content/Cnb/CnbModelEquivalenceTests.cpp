// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-075 (Phase E): the test that justifies the whole Model schema.
//
// It takes a REAL asset -- a real glTF fixture from the committed conformance corpus, converted by
// the real cna_tool_gltf_to_cnj into a .cnj plus its binary sidecars -- compiles that into one
// .cnb, and then loads the same asset BOTH ways through ContentManager and compares the two
// Models field by field: bone names, parents and transforms; mesh names, parent bones and part
// counts; per-part vertex/index/primitive counts and index width; effect types and the material
// state that reaches them; morph target data; the skeleton arrays; and every animation clip.
//
// It is also the test that proves the sidecars really are gone: before loading the .cnb, every
// source file the compiler reported absorbing is DELETED. If the compiled asset still needed any
// of them, this test fails rather than silently reading a file that happened to still be there.
//
// Two paths that merely both "work" is not the claim. The claim is that they produce the same
// model, which is what makes .cnb a compiled form of .cnj rather than a second, subtly different
// content format.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

extern char** environ;

using CNA::Content::Cnb::CnjToCnbResult;
using CNA::Content::Cnb::CompileCnjToCnb;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::ModelBone;
using Microsoft::Xna::Framework::Graphics::ModelMesh;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
using Microsoft::Xna::Framework::Graphics::MorphTargetDataEXT;
using Microsoft::Xna::Framework::Graphics::SkinningData;

namespace
{
    class ScratchDir
    {
    public:
        explicit ScratchDir(const std::string& tag)
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_cnb_equivalence_" + tag + "_" +
                    std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }

    int RunGltfToCnj(const std::string& input, const std::string& outDir,
                     const std::string& outName)
    {
        char* argv[] = {
            const_cast<char*>(CNA_GLTF_TO_CNJ_TOOL_PATH),
            const_cast<char*>(input.c_str()),
            const_cast<char*>(outDir.c_str()),
            const_cast<char*>(outName.c_str()),
            nullptr,
        };
        pid_t pid = -1;
        const int rc =
            posix_spawn(&pid, CNA_GLTF_TO_CNJ_TOOL_PATH, nullptr, nullptr, argv, environ);
        if (rc != 0)
        {
            ADD_FAILURE() << "posix_spawn(" << CNA_GLTF_TO_CNJ_TOOL_PATH
                          << ") failed: " << std::strerror(rc);
            return -1;
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    void ExpectMatrixEq(const Matrix& expected, const Matrix& actual, const std::string& what)
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
            EXPECT_FLOAT_EQ(a[i], e[i]) << what << " element " << i;
        }
    }

    void ExpectMorphEquivalent(const MorphTargetDataEXT* expected,
                               const MorphTargetDataEXT* actual, const std::string& what)
    {
        ASSERT_EQ(expected == nullptr, actual == nullptr) << what << ": morph presence";
        if (expected == nullptr) { return; }

        EXPECT_EQ(actual->Stride, expected->Stride) << what;
        EXPECT_EQ(actual->BaseVertexBytes, expected->BaseVertexBytes) << what;
        EXPECT_EQ(actual->RecomputeFlatNormalsEXT, expected->RecomputeFlatNormalsEXT) << what;
        EXPECT_EQ(actual->TriangleIndicesEXT, expected->TriangleIndicesEXT) << what;
        EXPECT_EQ(actual->Weights, expected->Weights) << what;
        ASSERT_EQ(actual->PositionDeltas.size(), expected->PositionDeltas.size()) << what;
        for (std::size_t t = 0; t < expected->PositionDeltas.size(); ++t)
        {
            ASSERT_EQ(actual->PositionDeltas[t].size(), expected->PositionDeltas[t].size())
                << what << " target " << t;
            for (std::size_t v = 0; v < expected->PositionDeltas[t].size(); ++v)
            {
                EXPECT_EQ(actual->PositionDeltas[t][v], expected->PositionDeltas[t][v])
                    << what << " target " << t << " vertex " << v;
            }
            ASSERT_EQ(actual->NormalDeltas[t].size(), expected->NormalDeltas[t].size())
                << what << " target " << t;
            ASSERT_EQ(actual->TangentDeltas[t].size(), expected->TangentDeltas[t].size())
                << what << " target " << t;
            for (std::size_t v = 0; v < expected->NormalDeltas[t].size(); ++v)
            {
                EXPECT_EQ(actual->NormalDeltas[t][v], expected->NormalDeltas[t][v])
                    << what << " target " << t << " normal " << v;
            }
            for (std::size_t v = 0; v < expected->TangentDeltas[t].size(); ++v)
            {
                EXPECT_EQ(actual->TangentDeltas[t][v], expected->TangentDeltas[t][v])
                    << what << " target " << t << " tangent " << v;
            }
        }
        ASSERT_EQ(actual->WeightTrack.Keys.size(), expected->WeightTrack.Keys.size()) << what;
        EXPECT_EQ(actual->WeightTrack.StepInterpolation, expected->WeightTrack.StepInterpolation)
            << what;
        EXPECT_EQ(actual->WeightTrack.CubicSpline, expected->WeightTrack.CubicSpline) << what;
        for (std::size_t k = 0; k < expected->WeightTrack.Keys.size(); ++k)
        {
            EXPECT_EQ(actual->WeightTrack.Keys[k].Time.getTicksProperty(),
                      expected->WeightTrack.Keys[k].Time.getTicksProperty()) << what;
            EXPECT_EQ(actual->WeightTrack.Keys[k].Weights, expected->WeightTrack.Keys[k].Weights)
                << what;
            EXPECT_EQ(actual->WeightTrack.Keys[k].InTangent,
                      expected->WeightTrack.Keys[k].InTangent) << what;
            EXPECT_EQ(actual->WeightTrack.Keys[k].OutTangent,
                      expected->WeightTrack.Keys[k].OutTangent) << what;
        }
    }

    void ExpectSkinningEquivalent(const SkinningData* expected, const SkinningData* actual)
    {
        ASSERT_EQ(expected == nullptr, actual == nullptr) << "skeleton presence";
        if (expected == nullptr) { return; }

        EXPECT_EQ(actual->BoneCount, expected->BoneCount);
        EXPECT_EQ(actual->SkeletonHierarchy, expected->SkeletonHierarchy);
        ASSERT_EQ(actual->BindPose.size(), expected->BindPose.size());
        ASSERT_EQ(actual->InverseBindPose.size(), expected->InverseBindPose.size());
        ASSERT_EQ(actual->SkeletonRootPrefix.size(), expected->SkeletonRootPrefix.size());
        for (std::size_t b = 0; b < expected->BindPose.size(); ++b)
        {
            ExpectMatrixEq(expected->BindPose[b], actual->BindPose[b],
                           "BindPose[" + std::to_string(b) + "]");
            ExpectMatrixEq(expected->InverseBindPose[b], actual->InverseBindPose[b],
                           "InverseBindPose[" + std::to_string(b) + "]");
        }
        for (std::size_t b = 0; b < expected->SkeletonRootPrefix.size(); ++b)
        {
            ExpectMatrixEq(expected->SkeletonRootPrefix[b], actual->SkeletonRootPrefix[b],
                           "SkeletonRootPrefix[" + std::to_string(b) + "]");
        }

        ASSERT_EQ(actual->AnimationClips.size(), expected->AnimationClips.size());
        for (const auto& [name, clip] : expected->AnimationClips)
        {
            const auto found = actual->AnimationClips.find(name);
            ASSERT_NE(found, actual->AnimationClips.end()) << "clip '" << name << "' is missing";
            EXPECT_EQ(found->second.Duration.getTicksProperty(), clip.Duration.getTicksProperty())
                << name;
            EXPECT_EQ(found->second.TargetSpace, clip.TargetSpace) << name;
            ASSERT_EQ(found->second.Tracks.size(), clip.Tracks.size()) << name;
            for (std::size_t t = 0; t < clip.Tracks.size(); ++t)
            {
                EXPECT_EQ(found->second.Tracks[t].BoneIndex, clip.Tracks[t].BoneIndex) << name;
                ASSERT_EQ(found->second.Tracks[t].Keys.size(), clip.Tracks[t].Keys.size()) << name;
                for (std::size_t k = 0; k < clip.Tracks[t].Keys.size(); ++k)
                {
                    const auto& e = clip.Tracks[t].Keys[k];
                    const auto& a = found->second.Tracks[t].Keys[k];
                    EXPECT_EQ(a.Time.getTicksProperty(), e.Time.getTicksProperty()) << name;
                    EXPECT_EQ(a.Translation, e.Translation) << name;
                    EXPECT_EQ(a.Rotation, e.Rotation) << name;
                    EXPECT_EQ(a.Scale, e.Scale) << name;
                }
            }
        }
    }

    void ExpectModelsEquivalent(const Model& expected, const Model& actual)
    {
        ASSERT_EQ(actual.getBonesProperty().getCountProperty(),
                  expected.getBonesProperty().getCountProperty()) << "bone count";
        for (int b = 0; b < expected.getBonesProperty().getCountProperty(); ++b)
        {
            const ModelBone* e = expected.getBonesProperty()[b];
            const ModelBone* a = actual.getBonesProperty()[b];
            ASSERT_NE(e, nullptr);
            ASSERT_NE(a, nullptr);
            EXPECT_EQ(a->getNameProperty(), e->getNameProperty()) << "bone " << b;
            EXPECT_EQ(a->getIndexProperty(), e->getIndexProperty()) << "bone " << b;
            ExpectMatrixEq(e->getTransformProperty(), a->getTransformProperty(),
                           "bone " + std::to_string(b) + " transform");
            const ModelBone* eParent = e->getParentProperty();
            const ModelBone* aParent = a->getParentProperty();
            ASSERT_EQ(eParent == nullptr, aParent == nullptr) << "bone " << b << " parent presence";
            if (eParent != nullptr)
            {
                EXPECT_EQ(aParent->getIndexProperty(), eParent->getIndexProperty())
                    << "bone " << b << " parent";
            }
        }

        ASSERT_EQ(actual.getMeshesProperty().getCountProperty(),
                  expected.getMeshesProperty().getCountProperty()) << "mesh count";
        for (int m = 0; m < expected.getMeshesProperty().getCountProperty(); ++m)
        {
            const ModelMesh* e = expected.getMeshesProperty()[m];
            const ModelMesh* a = actual.getMeshesProperty()[m];
            const std::string what = "mesh " + std::to_string(m) + " ('" + e->getNameProperty() + "')";

            EXPECT_EQ(a->getNameProperty(), e->getNameProperty()) << what;
            ASSERT_EQ(e->getParentBoneProperty() == nullptr, a->getParentBoneProperty() == nullptr)
                << what << " parent bone presence";
            if (e->getParentBoneProperty() != nullptr)
            {
                EXPECT_EQ(a->getParentBoneProperty()->getIndexProperty(),
                          e->getParentBoneProperty()->getIndexProperty()) << what;
            }
            EXPECT_FLOAT_EQ(a->getBoundingSphereProperty().Radius,
                            e->getBoundingSphereProperty().Radius) << what;
            EXPECT_EQ(a->getBoundingSphereProperty().Center,
                      e->getBoundingSphereProperty().Center) << what;

            ASSERT_EQ(a->getMeshPartsProperty().getCountProperty(),
                      e->getMeshPartsProperty().getCountProperty()) << what << " part count";
            for (int p = 0; p < e->getMeshPartsProperty().getCountProperty(); ++p)
            {
                const ModelMeshPart* ep = e->getMeshPartsProperty()[p];
                const ModelMeshPart* ap = a->getMeshPartsProperty()[p];
                const std::string pw = what + " part " + std::to_string(p);

                EXPECT_EQ(ap->getNumVerticesProperty(), ep->getNumVerticesProperty()) << pw;
                EXPECT_EQ(ap->getPrimitiveCountProperty(), ep->getPrimitiveCountProperty()) << pw;
                EXPECT_EQ(ap->getPrimitiveTypeEXTProperty(), ep->getPrimitiveTypeEXTProperty())
                    << pw;
                ASSERT_NE(ep->getVertexBufferProperty(), nullptr) << pw;
                ASSERT_NE(ap->getVertexBufferProperty(), nullptr) << pw;
                EXPECT_EQ(ap->getVertexBufferProperty()->getVertexCountProperty(),
                          ep->getVertexBufferProperty()->getVertexCountProperty()) << pw;
                ASSERT_NE(ep->getIndexBufferProperty(), nullptr) << pw;
                ASSERT_NE(ap->getIndexBufferProperty(), nullptr) << pw;
                EXPECT_EQ(ap->getIndexBufferProperty()->getIndexCountProperty(),
                          ep->getIndexBufferProperty()->getIndexCountProperty()) << pw;
                EXPECT_EQ(ap->getIndexBufferProperty()->getIndexElementSizeProperty(),
                          ep->getIndexBufferProperty()->getIndexElementSizeProperty()) << pw;
                // SamplerState has no operator==, so compare the three fields the .cnb schema
                // actually carries rather than adding an equality operator to the XNA API for a
                // test's convenience.
                for (std::size_t slot = 0; slot < 5u; ++slot)
                {
                    const auto& es = ep->getSamplerStatesEXTProperty()[slot];
                    const auto& as = ap->getSamplerStatesEXTProperty()[slot];
                    EXPECT_EQ(as.getFilterProperty(), es.getFilterProperty()) << pw << " sampler " << slot;
                    EXPECT_EQ(as.getAddressUProperty(), es.getAddressUProperty()) << pw << " sampler " << slot;
                    EXPECT_EQ(as.getAddressVProperty(), es.getAddressVProperty()) << pw << " sampler " << slot;
                }
                for (std::size_t slot = 0; slot < 2u; ++slot)
                {
                    const auto& es = ep->getSpecularSamplerStatesEXTProperty()[slot];
                    const auto& as = ap->getSpecularSamplerStatesEXTProperty()[slot];
                    EXPECT_EQ(as.getFilterProperty(), es.getFilterProperty()) << pw << " specular sampler " << slot;
                    EXPECT_EQ(as.getAddressUProperty(), es.getAddressUProperty()) << pw << " specular sampler " << slot;
                    EXPECT_EQ(as.getAddressVProperty(), es.getAddressVProperty()) << pw << " specular sampler " << slot;
                }

                // Effect identity by dynamic type: the two paths must pick the same effect class,
                // which is what decides the shader the part is drawn with.
                ASSERT_NE(ep->getEffectProperty(), nullptr) << pw;
                ASSERT_NE(ap->getEffectProperty(), nullptr) << pw;
                EXPECT_STREQ(typeid(*ap->getEffectProperty()).name(),
                             typeid(*ep->getEffectProperty()).name()) << pw;

                if (auto* eb = dynamic_cast<Microsoft::Xna::Framework::Graphics::BasicEffect*>(
                        ep->getEffectProperty()))
                {
                    auto* ab = dynamic_cast<Microsoft::Xna::Framework::Graphics::BasicEffect*>(
                        ap->getEffectProperty());
                    ASSERT_NE(ab, nullptr) << pw;
                    EXPECT_EQ(ab->getDiffuseColorProperty(), eb->getDiffuseColorProperty()) << pw;
                    EXPECT_FLOAT_EQ(ab->getAlphaProperty(), eb->getAlphaProperty()) << pw;
                    EXPECT_EQ(ab->getTextureEnabledProperty(), eb->getTextureEnabledProperty())
                        << pw;
                    EXPECT_EQ(ab->getLightingEnabledProperty(), eb->getLightingEnabledProperty())
                        << pw;
                    EXPECT_EQ(ab->VertexColorEnabled, eb->VertexColorEnabled) << pw;
                    EXPECT_EQ(ab->getTextureProperty() == nullptr,
                              eb->getTextureProperty() == nullptr) << pw;
                }

                ExpectMorphEquivalent(
                    dynamic_cast<MorphTargetDataEXT*>(ep->getTagProperty()),
                    dynamic_cast<MorphTargetDataEXT*>(ap->getTagProperty()), pw);
            }
        }

        ExpectSkinningEquivalent(dynamic_cast<SkinningData*>(expected.getTagProperty()),
                                 dynamic_cast<SkinningData*>(actual.getTagProperty()));
    }

    /// Converts one corpus fixture with the real glTF tool, compiles the result to .cnb, then
    /// loads it both ways and compares. Returns false when the fixture is not importable at all,
    /// so the caller can distinguish that from a comparison failure.
    void CompareBothPathsForFixture(const std::filesystem::path& gltf, const std::string& tag)
    {
        ScratchDir cnjDir(tag + "_cnj");
        ASSERT_EQ(RunGltfToCnj(gltf.string(), cnjDir.path().string(), "asset"), 0)
            << "converting " << gltf;

        // The tool may emit one .cnj per skin; this comparison uses the primary one.
        const std::filesystem::path cnjPath = cnjDir.path() / "asset.cnj";
        ASSERT_TRUE(std::filesystem::exists(cnjPath)) << "no asset.cnj for " << gltf;

        // Load the reference model from the .cnj, with every sidecar still in place.
        GraphicsDevice device;
        ContentManager cnjManager(nullptr, cnjDir.path().string());
        cnjManager.setGraphicsDevice(device);
        const Model fromCnj = cnjManager.Load<Model>("asset");

        // Compile, then move the compiled file into a directory that holds NOTHING ELSE except the
        // external assets the compiler said stay external. If the .cnb still depended on a
        // sidecar, loading it here would fail.
        const CnjToCnbResult compiled = CompileCnjToCnb(cnjPath.string());
        ScratchDir cnbDir(tag + "_cnb");
        WriteBytes(cnbDir.path() / "asset.cnb", compiled.bytes);
        for (const std::string& external : compiled.externalReferences)
        {
            const std::filesystem::path destination = cnbDir.path() / external;
            std::filesystem::create_directories(destination.parent_path());
            std::error_code ec;
            std::filesystem::copy_file(cnjDir.path() / external, destination, ec);
            ASSERT_FALSE(ec) << "copying external reference '" << external << "': " << ec.message();
        }
        // The consolidation claim, stated as something that can actually fail. The source form is
        // more than one file (otherwise there is nothing to consolidate and this fixture proves
        // nothing), and the compiled directory holds exactly the .cnb plus whatever the compiler
        // said stays external -- no sidecar is present for the load below to fall back on.
        EXPECT_GT(compiled.absorbedFiles.size(), 1u)
            << "the .cnj form of this fixture is a single file; it cannot demonstrate absorption";
        std::size_t filesBesideTheCompiledAsset = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(cnbDir.path()))
        {
            if (entry.is_regular_file()) { ++filesBesideTheCompiledAsset; }
        }
        EXPECT_EQ(filesBesideTheCompiledAsset, 1u + compiled.externalReferences.size())
            << "the compiled asset's directory holds files it should not";

        ContentManager cnbManager(nullptr, cnbDir.path().string());
        cnbManager.setGraphicsDevice(device);
        const Model fromCnb = cnbManager.Load<Model>("asset");

        ExpectModelsEquivalent(fromCnj, fromCnb);
    }
}

TEST(CnbModelEquivalenceTest, ARealSkinnedFixtureLoadsIdenticallyFromCnjAndFromCnb)
{
    const std::filesystem::path corpus = std::filesystem::path("tests") / "assets" / "gltf";
    if (!std::filesystem::exists(corpus))
    {
        GTEST_SKIP() << "the glTF fixture corpus is not present (run from the source root)";
    }
    const std::filesystem::path fixture = corpus / "skin-four-weighted.gltf";
    if (!std::filesystem::exists(fixture))
    {
        GTEST_SKIP() << "the skin-four-weighted fixture is not present";
    }
    CompareBothPathsForFixture(fixture, "skin");
}

TEST(CnbModelEquivalenceTest, ARepresentativeSliceOfTheCorpusLoadsIdenticallyBothWays)
{
    const std::filesystem::path corpus = std::filesystem::path("tests") / "assets" / "gltf";
    if (!std::filesystem::exists(corpus))
    {
        GTEST_SKIP() << "the glTF fixture corpus is not present (run from the source root)";
    }

    // A hand-picked spread rather than the whole corpus: a rigid multi-node scene, a skinned rig,
    // a morphed mesh, a textured PBR material and a multi-primitive mesh -- one fixture per
    // structural feature the Model schema has to carry. The whole corpus would be slower without
    // covering a feature this list misses.
    const std::vector<std::string> wanted = {
        "xf-parent-child",             // a rigid multi-node scene graph
        "xf-shared-mesh",              // one mesh placed at two nodes
        "skin-four-weighted",          // a real skinned rig with a skeleton
        "skin-armature-ancestor",      // the skeleton root-prefix block
        "morph-position-normal",       // morph targets with position and normal deltas
        "morph-position-normal-tangent", // ... and the tangent trailer
        "morph-node-weights-override", // non-zero default morph weights
        "mat-normal-occlusion-scale",  // a PBR material with maps and scalars
        "mat-unlit",                   // the unlit lighting branch
        "tex-dual-texture-stride",     // DualTextureEffect's two-slot layout
        "two-primitives-one-buffer",   // several primitives grouped into one mesh
        "u32-idx",                     // 32-bit index width
        "mode-triangle-strip",         // a non-triangle-list topology
        "anim-two-clips",              // more than one embedded clip
        "skin-vertex-color",           // per-vertex colour on a skinned part
    };

    int compared = 0;
    for (const std::string& id : wanted)
    {
        const std::filesystem::path fixture = corpus / (id + ".gltf");
        if (!std::filesystem::exists(fixture)) { continue; }
        SCOPED_TRACE(id);
        CompareBothPathsForFixture(fixture, id);
        ++compared;
    }

    // A floor, so a renamed or removed fixture group turns this into a failure rather than a
    // silently empty sweep.
    EXPECT_GE(compared, 10) << "fewer corpus fixtures matched than this test needs to be meaningful";
}

// --------------------------------------------------------------------------------------------
// CNBF-084 -- what the v1 Model schema deliberately does not express
// --------------------------------------------------------------------------------------------

TEST(CnbModelEquivalenceTest, TheCompilerRefusesAModelUsingMaterialVariants)
{
    // plans/plan_cnb.md decision D9 puts glTF material variants outside the v1 Model schema. A
    // compiler that quietly dropped them would turn a selectable multi-material asset into a
    // single-material one with no diagnostic, so it refuses by name instead.
    const std::filesystem::path corpus = std::filesystem::path("tests") / "assets" / "gltf";
    const std::filesystem::path fixture = corpus / "mat-material-variants.gltf";
    if (!std::filesystem::exists(fixture))
    {
        GTEST_SKIP() << "the mat-material-variants fixture is not present";
    }

    ScratchDir dir("variants");
    ASSERT_EQ(RunGltfToCnj(fixture.string(), dir.path().string(), "asset"), 0);
    ASSERT_TRUE(std::filesystem::exists(dir.path() / "asset.cnj"));

    try
    {
        (void)CompileCnjToCnb((dir.path() / "asset.cnj").string());
        FAIL() << "expected the compiler to refuse a material-variant model";
    }
    catch (const ContentLoadException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("material variant"), std::string::npos) << message;
        EXPECT_NE(message.find("plans/plan_cnb.md"), std::string::npos) << message;
    }
}

// --------------------------------------------------------------------------------------------
// CNBF-091 -- the measurement the whole format exists for
// --------------------------------------------------------------------------------------------

TEST(CnbModelEquivalenceTest, CompilingAModelReducesItsFileCountAndItsFileOpens)
{
    const std::filesystem::path corpus = std::filesystem::path("tests") / "assets" / "gltf";
    if (!std::filesystem::exists(corpus))
    {
        GTEST_SKIP() << "the glTF fixture corpus is not present (run from the source root)";
    }

    // Swept over assets of different shapes rather than one, because the interesting number is
    // not a single ratio -- it is that the file count falls for every shape while the byte size
    // moves in whichever direction the asset's own mix of geometry and descriptors implies.
    // Printed as well as asserted, so a run of this test is itself the record of the measurement.
    const std::vector<std::string> fixtures = {
        "skin-four-weighted", "skin-73-joints", "morph-eight-targets",
        "two-primitives-one-buffer", "mat-normal-occlusion-scale", "anim-two-clips",
    };

    int measured = 0;
    for (const std::string& id : fixtures)
    {
        const std::filesystem::path fixture = corpus / (id + ".gltf");
        if (!std::filesystem::exists(fixture)) { continue; }
        SCOPED_TRACE(id);

        ScratchDir dir("measure_" + id);
        // A fixture the importer legitimately refuses (over a documented budget, say) is not a
        // measurement failure -- it is simply not an asset to measure. Said out loud rather than
        // skipped in silence, so a converter that started refusing everything would be visible.
        if (RunGltfToCnj(fixture.string(), dir.path().string(), "asset") != 0 ||
            !std::filesystem::exists(dir.path() / "asset.cnj"))
        {
            std::cout << "[ MEASURED ] " << id << ": not importable by the glTF tool, skipped\n";
            continue;
        }

        std::size_t sourceFiles = 0;
        std::uintmax_t sourceBytes = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir.path()))
        {
            if (!entry.is_regular_file()) { continue; }
            ++sourceFiles;
            sourceBytes += entry.file_size();
        }

        const CnjToCnbResult compiled = CompileCnjToCnb((dir.path() / "asset.cnj").string());
        const std::size_t compiledFiles = 1u + compiled.externalReferences.size();

        std::uintmax_t externalBytes = 0;
        for (const std::string& external : compiled.externalReferences)
        {
            std::error_code ec;
            externalBytes += std::filesystem::file_size(dir.path() / external, ec);
        }

        std::cout << "[ MEASURED ] " << id << ": .cnj form = " << sourceFiles << " file(s) / "
                  << sourceBytes << " B; .cnb form = " << compiledFiles << " file(s) / "
                  << (compiled.bytes.size() + externalBytes) << " B (of which the .cnb itself is "
                  << compiled.bytes.size() << " B); file opens per load "
                  << sourceFiles << " -> " << compiledFiles << "\n";

        EXPECT_GT(sourceFiles, compiledFiles) << id << ": compiling did not reduce the file count";
        EXPECT_EQ(compiled.absorbedFiles.size(), sourceFiles - compiled.externalReferences.size())
            << id << ": every source file that is not an external reference should be absorbed";
        ++measured;
    }

    EXPECT_GE(measured, 3) << "too few fixtures were measured for this to mean anything";
}
