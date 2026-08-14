// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"

using namespace CNA::Internal::GltfImport;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const GltfImportDiagnosticEXT* FindDiagnostic(
        const GltfImportReportEXT& report, const std::string& code)
    {
        const auto found = std::find_if(
            report.Diagnostics.begin(), report.Diagnostics.end(),
            [&](const GltfImportDiagnosticEXT& diagnostic) { return diagnostic.Code == code; });
        return found != report.Diagnostics.end() ? &*found : nullptr;
    }
}

TEST(GltfImportReport, AModelFromAnotherContentPathHasAnEmptyReport)
{
    const Model model;
    const GltfImportReportEXT& report = model.getGltfImportReportEXTProperty();
    EXPECT_TRUE(report.Diagnostics.empty());
    EXPECT_EQ(0u, report.NodeCount);
    EXPECT_FALSE(report.AnythingLost());
    EXPECT_EQ(0u, report.getWarningCountProperty());
    EXPECT_EQ(0u, report.getDroppedFeatureCountProperty());
    EXPECT_EQ(0u, report.getApproximationCountProperty());
}

TEST(GltfImportReport, EveryDropNamedByGltf035HasAStructuredEntry)
{
    GltfImportReportEXT report;

    NodeGraphReportEXT graph;
    graph.nodeCount = 4;
    graph.meshInstanceCount = 2;
    graph.distinctMeshCount = 1;
    graph.gpuInstancedNodeCount = 1;
    AppendGltfNodeGraphReportEXT(report, graph);
    AppendGltfValidationWarningsEXT(
        report,
        {"'/machine/private/asset.gltf' uses extension 'KHR_materials_clearcoat', which CNA "
         "does not fully implement."});

    MeshOut mesh;
    mesh.uvSetMismatchedMapsEXT = {"normalTexture", "occlusionTexture"};
    mesh.extraColorSetsEXT = 2;
    mesh.ignoredCustomAttributesEXT = {"_BATCHID"};
    AppendGltfMeshReportEXT(report, mesh, "Body");

    LightReportEXT lights;
    lights.droppedLightCount = 3;
    lights.approximatedPointLightCount = 1;
    AppendGltfLightReportEXT(report, lights, 3);

    AnimationReportEXT animations;
    animations.animationCount = 1;
    animations.clipCount = 1;
    animations.skippedUnsupportedPathChannels = 2;
    AppendGltfAnimationReportEXT(report, animations);

    ASSERT_NE(nullptr, FindDiagnostic(report, "gpu-instancing-dropped"));
    const auto* ignoredExtension = FindDiagnostic(report, "ignored-extension");
    ASSERT_NE(nullptr, ignoredExtension);
    EXPECT_EQ(std::string::npos, ignoredExtension->Message.find("/machine/private"))
        << "a serialized import report must not leak or pin the converter's absolute source path";
    const auto* uv = FindDiagnostic(report, "uv-set-mismatch");
    ASSERT_NE(nullptr, uv);
    EXPECT_EQ(2u, uv->Count);
    EXPECT_EQ(GltfImportDiagnosticKindEXT::DroppedData, uv->Kind);
    ASSERT_NE(nullptr, FindDiagnostic(report, "color-sets-dropped"));
    ASSERT_NE(nullptr, FindDiagnostic(report, "custom-attributes-ignored"));
    const auto* droppedLights = FindDiagnostic(report, "lights-dropped");
    ASSERT_NE(nullptr, droppedLights);
    EXPECT_EQ(3u, droppedLights->Count);
    ASSERT_NE(nullptr, FindDiagnostic(report, "point-lights-approximated"));
    const auto* animationPaths = FindDiagnostic(report, "animation-paths-unsupported");
    ASSERT_NE(nullptr, animationPaths);
    EXPECT_EQ(2u, animationPaths->Count);

    EXPECT_TRUE(report.AnythingLost());
    EXPECT_GT(report.getDroppedFeatureCountProperty(), 0u);
    EXPECT_GT(report.getApproximationCountProperty(), 0u);
    EXPECT_EQ(4u, report.NodeCount);
    EXPECT_EQ(1u, report.PrimitiveCount);
    EXPECT_EQ(1u, report.AnimationCount);
    EXPECT_EQ(1u, report.ClipCount);
}

TEST(GltfImportReport, ExactTopologyConversionIsInformationNotLoss)
{
    MeshOut mesh;
    mesh.sourceTopology = PrimitiveTopology::TriangleStrip;
    mesh.topology = PrimitiveTopology::Triangles;
    GltfImportReportEXT report;
    AppendGltfMeshReportEXT(report, mesh, "Strip");

    const auto* conversion = FindDiagnostic(report, "topology-converted");
    ASSERT_NE(nullptr, conversion);
    EXPECT_EQ(GltfImportDiagnosticSeverityEXT::Information, conversion->Severity);
    EXPECT_EQ(GltfImportDiagnosticKindEXT::Information, conversion->Kind);
    EXPECT_FALSE(report.AnythingLost());
}

TEST(GltfImportReport, InvalidAccessorBoundsAreAWarningButNotAnApproximation)
{
    GltfImportReportEXT report;
    AppendGltfValidationWarningsEXT(
        report, {"asset.gltf accessor 2 decoded maximum differs from accessor.max."});

    const auto* warning = FindDiagnostic(report, "accessor-bounds-mismatch");
    ASSERT_NE(nullptr, warning);
    EXPECT_EQ(GltfImportDiagnosticSeverityEXT::Warning, warning->Severity);
    EXPECT_EQ(GltfImportDiagnosticKindEXT::InvalidSourceData, warning->Kind);
    EXPECT_TRUE(report.AnythingLost());
    EXPECT_EQ(1u, report.getWarningCountProperty());
    EXPECT_EQ(0u, report.getDroppedFeatureCountProperty());
    EXPECT_EQ(0u, report.getApproximationCountProperty());
}

TEST(GltfImportReport, SkinnedModelsReportOnlySceneTracksTheirPalettesDoNotCarry)
{
    cgltf_node sourceNodes[2]{};
    SceneGraphOut scene;
    scene.nodes.resize(3);
    scene.nodes[1].node = &sourceNodes[0];
    scene.nodes[2].node = &sourceNodes[1];

    SkeletonResult skin;
    skin.nodeToNewIndex.emplace(&sourceNodes[0], 0);

    ClipOut sceneClip;
    sceneClip.name = "Mixed";
    sceneClip.tracks.resize(3);
    sceneClip.tracks[0].boneIndex = 1;  // Carried by the skin palette.
    sceneClip.tracks[1].boneIndex = 2;  // Rigid scene node: Tag has nowhere to keep it.
    sceneClip.tracks[2].boneIndex = -1; // Defensive invalid target: also not carried.

    const std::size_t dropped =
        CountGltfRigidAnimationDropsEXT(sceneClip, scene, {&skin});
    EXPECT_EQ(2u, dropped);

    GltfImportReportEXT report;
    AppendGltfRigidAnimationDropEXT(report, sceneClip.name, dropped);
    const auto* diagnostic =
        FindDiagnostic(report, "rigid-animation-dropped-on-skinned-model");
    ASSERT_NE(nullptr, diagnostic);
    EXPECT_EQ(2u, diagnostic->Count);
    EXPECT_EQ("Mixed", diagnostic->Subject);
    EXPECT_EQ(GltfImportDiagnosticKindEXT::DroppedData, diagnostic->Kind);
}
