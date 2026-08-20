// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-317 … GLTF-321: glTF cameras.
//
// `cgltf_camera` had ZERO occurrences in CNA. A file's cameras were dropped entirely, so an asset
// that had been framed by its author arrived with no framing and every viewer had to invent one.
//
// Each fixture states its expected projection in its own manifest, computed from the
// specification's formulas -- so a wrong mapping is a numeric mismatch against the specification
// rather than against a second reading of the same code.

#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "GltfFixtureCorpus.hpp"

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "System/TimeSpan.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"

using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;
using CnaTest::GltfOracle::BoolOr;
using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::Numbers;
using CnaTest::GltfOracle::Path;
using CnaTest::GltfOracle::StringOr;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;

namespace
{
    constexpr float kTolerance = 1e-6f;

    void ExpectColumnMajorNear(const std::vector<double>& expected, const Matrix& actual,
                                const std::string& what)
    {
        ASSERT_EQ(16u, expected.size()) << what << ": manifest matrix is not 4x4";
        float columnMajor[16];
        actual.ToColumnMajor(columnMajor);
        for (std::size_t i = 0; i < 16; ++i)
        {
            EXPECT_NEAR(static_cast<float>(expected[i]), columnMajor[i], kTolerance)
                << what << ", element " << i;
        }
    }

    /// The single camera entry of a one-camera fixture, loaded alongside its manifest expectation.
    struct LoadedCamera
    {
        const JsonValue* expected = nullptr;
        Microsoft::Xna::Framework::Graphics::ModelCameraEXT actual;
        int boneCount = 0;
        std::string boneName;
    };
}

// The whole point of GLTF-317: a camera-bearing node must produce a camera on the model. Asserted
// across all three fixtures, because "cameras reach the model" has to hold for every camera type,
// not only the one that happened to be implemented first.
TEST(GltfCameras, EveryCameraBearingNodeProducesACameraOnTheModel)
{
    for (const std::string& id : {"camera-perspective", "camera-perspective-infinite",
                                  "camera-orthographic"})
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& expected = Path(fixture.Expected(), "l4.cameras");
        ASSERT_EQ(JsonType::Object, expected.type) << "the fixture states no camera expectation";
        const JsonValue& entries = Member(expected, "entries");
        ASSERT_EQ(JsonType::Array, entries.type);
        ASSERT_EQ(1u, entries.arrayValue.size());

        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        const auto& cameras = model.getCamerasEXTProperty();
        ASSERT_EQ(1u, cameras.size()) << "the file's camera did not reach the model";
        const auto& camera = cameras.front();
        const JsonValue& row = entries.arrayValue.front();

        EXPECT_EQ(StringOr(row, "nodeName", "?"),
                  model.getBonesProperty()[camera.SceneNodeIndex]->getNameProperty())
            << "SceneNodeIndex does not resolve to the camera's own node";
        EXPECT_EQ(BoolOr(row, "isPerspective", true), camera.IsPerspective);
        EXPECT_EQ(BoolOr(row, "hasInfiniteFarPlane", false), camera.HasInfiniteFarPlane);
        EXPECT_FALSE(camera.Name.empty()) << "the camera lost its name";
    }
}

// GLTF-318 / GLTF-319 / GLTF-320: the projection itself, against the specification's own formula.
TEST(GltfCameras, EachProjectionMatchesTheSpecificationsOwnFormula)
{
    for (const std::string& id : {"camera-perspective", "camera-perspective-infinite",
                                  "camera-orthographic"})
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& row = Path(fixture.Expected(), "l4.cameras").objectValue.empty()
                                   ? CnaTest::GltfOracle::JsonNull()
                                   : Member(Path(fixture.Expected(), "l4.cameras"), "entries")
                                         .arrayValue.front();

        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);
        ASSERT_EQ(1u, model.getCamerasEXTProperty().size());

        ExpectColumnMajorNear(Numbers(Member(row, "projectionColumnMajor")),
                              model.getCamerasEXTProperty().front().Projection, "projection");
    }
}

// GLTF-319's real content. An infinite far plane is a DIFFERENT matrix, not a large finite zfar:
// substituting a big number perturbs every depth value rather than only the ones near the horizon.
// The two fixtures share every other parameter, so the difference isolates exactly that term.
TEST(GltfCameras, AnInfiniteFarPlaneIsItsOwnMatrixNotALargeZfar)
{
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);

    Model finite = cm.Load<Model>("camera-perspective");
    Model infinite = cm.Load<Model>("camera-perspective-infinite");
    ASSERT_EQ(1u, finite.getCamerasEXTProperty().size());
    ASSERT_EQ(1u, infinite.getCamerasEXTProperty().size());

    const Matrix& f = finite.getCamerasEXTProperty().front().Projection;
    const Matrix& i = infinite.getCamerasEXTProperty().front().Projection;

    EXPECT_FALSE(finite.getCamerasEXTProperty().front().HasInfiniteFarPlane);
    EXPECT_TRUE(infinite.getCamerasEXTProperty().front().HasInfiniteFarPlane);

    // The x/y scales come from yfov and aspect alone, which both fixtures share -- so they must be
    // identical, and any difference would mean the far plane leaked into a term it does not touch.
    EXPECT_NEAR(f.M11, i.M11, kTolerance);
    EXPECT_NEAR(f.M22, i.M22, kTolerance);

    // The two z terms are exactly where they differ, and the infinite ones are the limits.
    EXPECT_NEAR(-1.0f, i.M33, kTolerance) << "M33 is not the zfar -> infinity limit";
    EXPECT_NEAR(-0.25f, i.M43, kTolerance) << "M43 is not -znear";
    EXPECT_GT(std::fabs(f.M33 - i.M33), 1e-4f)
        << "the finite and infinite projections agree on M33 -- one of them is wrong";
}

// GLTF-320's factor of two, on its own, because it is the classic silent error: xmag/ymag are HALF
// extents, so a mapping that used them directly as width and height is wrong by exactly two and
// would still look plausible. The fixture is non-square so a swapped axis is caught as well.
TEST(GltfCameras, OrthographicMagnitudesAreHalfExtents)
{
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("camera-orthographic");
    ASSERT_EQ(1u, model.getCamerasEXTProperty().size());
    const Matrix& p = model.getCamerasEXTProperty().front().Projection;

    // xmag 3, ymag 1.5 -> a 6 x 3 volume -> M11 = 2/6, M22 = 2/3.
    EXPECT_NEAR(2.0f / 6.0f, p.M11, kTolerance)
        << "xmag was used as the full width -- off by exactly two";
    EXPECT_NEAR(2.0f / 3.0f, p.M22, kTolerance)
        << "ymag was used as the full height -- off by exactly two";
    EXPECT_GT(p.M22, p.M11) << "the axes are swapped: this camera is wider than it is tall";
}

// GLTF-321: view = inverse(worldTransform(cameraNode)). A glTF camera looks down its own -Z with
// +Y up, which is XNA's own convention, so no basis change is applied -- and the manifest states
// that, so introducing one would fail here rather than look subtly wrong on screen.
TEST(GltfCameras, TheViewMatrixIsTheInverseOfTheCameraNodesWorldTransform)
{
    for (const std::string& id : {"camera-perspective", "camera-orthographic"})
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& row =
            Member(Path(fixture.Expected(), "l4.cameras"), "entries").arrayValue.front();

        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);
        ASSERT_EQ(1u, model.getCamerasEXTProperty().size());
        const auto& camera = model.getCamerasEXTProperty().front();

        ExpectColumnMajorNear(Numbers(Member(row, "worldTransformColumnMajor")),
                              camera.WorldTransform, "camera node world transform");
        ExpectColumnMajorNear(Numbers(Member(row, "viewMatrixColumnMajor")),
                              Matrix::Invert(camera.WorldTransform), "view matrix");
    }
}

// A model from a file with no cameras must report none rather than inventing a default -- the
// property is empty, and an application can tell "the author framed nothing" from "the author
// framed this". Every other corpus fixture is such a file.
TEST(GltfCameras, AFileWithNoCamerasReportsNoneRatherThanADefault)
{
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("xf-identity");
    EXPECT_TRUE(model.getCamerasEXTProperty().empty());
}

// --- plans/plan_gltf.md GLTF-296: animation of camera (and light) nodes -----------------------------------

// §3.11 animates *nodes*, and a camera node is an ordinary node -- so a channel targeting one needs
// no camera-specific path at all: it is exactly the rigid animation GLTF-293 imports. This proves
// that rather than assuming it.
TEST(GltfCameras, AChannelTargetingACameraNodeIsImportedAsAnOrdinaryRigidClip)
{
    using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;
    using Microsoft::Xna::Framework::Graphics::ModelAnimationsEXT;

    const LoadedFixture fixture("camera-animated-node");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ASSERT_EQ(0u, static_cast<std::size_t>(fixture.Data().skins_count))
        << "this fixture is supposed to have no skin at all";

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("camera-animated-node");

    auto* animations = dynamic_cast<ModelAnimationsEXT*>(model.getTagProperty());
    ASSERT_NE(nullptr, animations) << "the camera node's animation was dropped";
    ASSERT_EQ(1u, animations->Clips.size());
    const auto& clip = animations->Clips.at("CameraSpin");
    EXPECT_EQ(ClipTargetSpaceEXT::SceneNode, clip.TargetSpace);
    ASSERT_EQ(1u, clip.Tracks.size());

    // The track drives the CAMERA's own bone, which is what makes it usable at all.
    ASSERT_EQ(1u, model.getCamerasEXTProperty().size());
    EXPECT_EQ(model.getCamerasEXTProperty().front().SceneNodeIndex, clip.Tracks[0].BoneIndex)
        << "the clip drives a different bone than the one the camera is attached to";
}

// The consequence that is easy to get wrong, and the reason ModelCameraEXT::WorldTransform is
// documented as a snapshot: posing the model moves the camera's BONE, not the stored matrix. A
// consumer reading the stored one every frame would render an animated camera as a stationary one.
TEST(GltfCameras, AnAnimatedCamerasLivePlacementComesFromItsBoneNotItsStoredTransform)
{
    using Microsoft::Xna::Framework::Graphics::ApplyClipToBonesEXT;
    using Microsoft::Xna::Framework::Graphics::ModelAnimationsEXT;

    const LoadedFixture fixture("camera-animated-node");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& entry =
        Member(Path(fixture.Expected(), "l4.cameras"), "entries").arrayValue.front();
    const std::vector<double> expectedAtEnd =
        Numbers(Member(entry, "worldTransformAtEndColumnMajor"));
    ASSERT_EQ(16u, expectedAtEnd.size());

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("camera-animated-node");
    auto* animations = dynamic_cast<ModelAnimationsEXT*>(model.getTagProperty());
    ASSERT_NE(nullptr, animations);

    const auto& camera = model.getCamerasEXTProperty().front();
    const Matrix storedAtImport = camera.WorldTransform;

    // Pose the model at the end of the clip -- a quarter turn about +Y.
    ApplyClipToBonesEXT(model, animations->Clips.at("CameraSpin"),
                        System::TimeSpan::FromSeconds(1.0));
    std::vector<Matrix> absolute(
        static_cast<std::size_t>(model.getBonesProperty().getCountProperty()));
    model.CopyAbsoluteBoneTransformsTo(absolute);
    const Matrix live = absolute[static_cast<std::size_t>(camera.SceneNodeIndex)];

    ExpectColumnMajorNear(expectedAtEnd, live, "camera world transform at t = 1");

    // And the stored matrix did NOT move, which is exactly why it is documented as a snapshot.
    float storedNow[16];
    camera.WorldTransform.ToColumnMajor(storedNow);
    float storedThen[16];
    storedAtImport.ToColumnMajor(storedThen);
    for (std::size_t i = 0; i < 16; ++i)
    {
        EXPECT_FLOAT_EQ(storedThen[i], storedNow[i])
            << "WorldTransform changed under posing -- it is documented as an import-time snapshot";
    }
    EXPECT_GT(std::fabs(live.M11 - camera.WorldTransform.M11), 1e-3f)
        << "the live and stored transforms agree at t = 1, so this test cannot tell them apart";
}

// --- GLTF-322: an absent aspectRatio, and the fact that it was absent -------------------------

TEST(GltfCameras, AnAbsentAspectRatioIsAssumedToBeOneAndTheAssumptionIsRecorded)
{
    // §3.10.3 makes aspectRatio optional and gives its absence a meaning -- "use the viewport's" --
    // that an importer has no viewport to satisfy. One is assumed. The task is that the assumption
    // is RECORDED: without the flag a consumer cannot tell an author who framed a square shot from
    // one who deliberately left the framing to the runtime, and would either stretch the first or
    // letterbox the second. Both read as a projection bug rather than as a missing field.
    const LoadedFixture fixture("camera-perspective-no-aspect");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& row = Path(fixture.Expected(), "l4.cameras").FindMember("entries")
                               ->arrayValue.front();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("camera-perspective-no-aspect");

    const auto& camera = model.getCamerasEXTProperty().front();
    EXPECT_FALSE(camera.HasAuthoredAspectRatio) << "the file declares no aspectRatio";
    EXPECT_NEAR(1.0f, camera.AspectRatio, kTolerance);
    ExpectColumnMajorNear(Numbers(Member(row, "projectionColumnMajor")), camera.Projection,
                          "projection built at the assumed aspect");
}

TEST(GltfCameras, AnAuthoredAspectRatioIsMarkedAsAuthoredAndUsedVerbatim)
{
    // The control. Without it, `HasAuthoredAspectRatio` could be hardwired false and the test
    // above would still pass -- and a consumer would then rebuild every projection, discarding the
    // framing of every camera whose author did state one.
    const LoadedFixture fixture("camera-perspective");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& row = Path(fixture.Expected(), "l4.cameras").FindMember("entries")
                               ->arrayValue.front();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("camera-perspective");

    const auto& camera = model.getCamerasEXTProperty().front();
    EXPECT_TRUE(camera.HasAuthoredAspectRatio);
    EXPECT_NEAR(static_cast<float>(CnaTest::GltfOracle::NumberOr(row, "aspectRatio", -1.0)),
                camera.AspectRatio, kTolerance);
}

TEST(GltfCameras, EveryPerspectiveCameraCarriesTheParametersNeededToRebuildItsProjection)
{
    // The point of GLTF-322 is that a consumer can act on the assumption, not merely detect it.
    // Acting means rebuilding the projection at the real viewport aspect, which requires yfov,
    // znear and zfar -- carried rather than left to be recovered by inverting a matrix, which is
    // work no consumer should do and is impossible for the infinite variant without first knowing
    // it is the infinite variant.
    for (const std::string& id : {"camera-perspective", "camera-perspective-infinite",
                                  "camera-perspective-no-aspect"})
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& row = Path(fixture.Expected(), "l4.cameras").FindMember("entries")
                                   ->arrayValue.front();

        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);
        const auto& camera = model.getCamerasEXTProperty().front();

        EXPECT_NEAR(static_cast<float>(CnaTest::GltfOracle::NumberOr(row, "fieldOfView", -1.0)),
                    camera.FieldOfView, kTolerance);
        EXPECT_NEAR(
            static_cast<float>(CnaTest::GltfOracle::NumberOr(row, "nearPlaneDistance", -1.0)),
            camera.NearPlaneDistance, kTolerance);
        EXPECT_NEAR(
            static_cast<float>(CnaTest::GltfOracle::NumberOr(row, "farPlaneDistance", -1.0)),
            camera.FarPlaneDistance, kTolerance);

        // Rebuilding at the carried aspect must reproduce the projection the importer built, or
        // the parameters describe a different camera from the matrix beside them.
        const Matrix rebuilt = camera.HasInfiniteFarPlane
            ? Microsoft::Xna::Framework::Graphics::CreateInfinitePerspectiveFieldOfViewEXT(
                  camera.FieldOfView, camera.AspectRatio, camera.NearPlaneDistance)
            : Matrix::CreatePerspectiveFieldOfView(camera.FieldOfView, camera.AspectRatio,
                                                   camera.NearPlaneDistance,
                                                   camera.FarPlaneDistance);
        float built[16];
        camera.Projection.ToColumnMajor(built);
        float again[16];
        rebuilt.ToColumnMajor(again);
        for (std::size_t i = 0; i < 16; ++i)
        {
            EXPECT_NEAR(built[i], again[i], kTolerance) << "rebuilt element " << i;
        }
    }
}

TEST(GltfCameras, AnOrthographicCameraLeavesThePerspectiveOnlyParametersAtTheirDefaults)
{
    // xmag/ymag are not a field of view and there is no aspect ratio to assume, so the perspective
    // fields must not be filled with plausible-looking numbers a consumer might act on.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("camera-orthographic");

    const auto& camera = model.getCamerasEXTProperty().front();
    ASSERT_FALSE(camera.IsPerspective);
    EXPECT_FALSE(camera.HasAuthoredAspectRatio);
    EXPECT_FLOAT_EQ(0.0f, camera.FieldOfView) << "an orthographic camera has no field of view";
}
