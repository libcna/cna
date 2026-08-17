// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-310 / GLTF-313 / GLTF-315 / GLTF-316: what an animation import does with the
// channels it cannot use, and what it says about it.
//
// Everything asserted here is invisible in a `ClipOut`. A channel whose target lies outside the
// default scene, a channel on a path CNA cannot drive, and a track baked onto the union of two
// channels keyed at different times all produce a clip that plays -- just not the motion the file
// describes. That is the failure class this campaign exists to remove, and the only defence is
// that each of them is counted and named rather than inferred from a silently shorter result.
//
// The suite lives at L4 because every assertion is about the semantic clip (bone indices, key
// times, poses), not about container structure or decoded accessor bytes.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::NumberOr;
using CnaTest::GltfOracle::Path;
using CnaTest::GltfOracle::Strings;

namespace
{
    constexpr float kTolerance = 1e-4f;

    /// Extracts a fixture's rigid clips, returning the report and warnings alongside them.
    struct Extracted
    {
        std::vector<CNA::Internal::GltfImport::ClipOut> clips;
        CNA::Internal::GltfImport::AnimationReportEXT report;
        std::vector<std::string> warnings;
        CNA::Internal::GltfImport::SceneGraphOut scene;
    };

    Extracted ExtractRigid(const LoadedFixture& fixture)
    {
        using namespace CNA::Internal::GltfImport;
        Extracted out;
        out.scene = BuildSceneGraph(&fixture.Data());
        out.clips = ExtractSceneNodeClips(&fixture.Data(), out.scene, 1.0f, out.warnings,
                                           &out.report);
        return out;
    }

    /// Every `anim-*` fixture in the corpus, in the corpus's own stable order.
    std::vector<std::string> AnimationFixtureIds()
    {
        std::vector<std::string> ids;
        for (const std::string& id : CnaTest::GltfOracle::CorpusFixtureIds())
        {
            if (id.rfind("anim-", 0) == 0) { ids.push_back(id); }
        }
        return ids;
    }

    /// True when any warning mentions every one of @p needles.
    bool AnyWarningMentions(const std::vector<std::string>& warnings,
                            const std::vector<std::string>& needles)
    {
        return std::any_of(warnings.begin(), warnings.end(), [&needles](const std::string& w) {
            return std::all_of(needles.begin(), needles.end(), [&w](const std::string& needle) {
                return w.find(needle) != std::string::npos;
            });
        });
    }

    /// The single track of a single-track clip, or nullptr with a gtest failure already recorded.
    const CNA::Internal::GltfImport::TrackOut* SoleTrack(
        const std::vector<CNA::Internal::GltfImport::ClipOut>& clips)
    {
        if (clips.size() != 1u)
        {
            ADD_FAILURE() << "expected exactly one clip, got " << clips.size();
            return nullptr;
        }
        if (clips[0].tracks.size() != 1u)
        {
            ADD_FAILURE() << "expected exactly one track, got " << clips[0].tracks.size();
            return nullptr;
        }
        return &clips[0].tracks[0];
    }
}

// --- GLTF-310: a channel targeting a node outside the default scene -----------------------------

TEST(GltfAnimationRobustness, AChannelTargetingANodeOutsideTheDefaultSceneIsSkippedAndCounted)
{
    // `animations` is a top-level array scoped to nothing, so a channel may name any node in the
    // file -- including one only a non-default scene contains. CNA imports the default scene alone
    // (GLTF-133), so such a channel drives a bone that does not exist. Ignoring it is the only
    // available answer; the whole task is that it is *reported*.
    const LoadedFixture fixture("anim-out-of-scene-target");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const Extracted result = ExtractRigid(fixture);
    const CNA::Internal::JsonValue& expected = Path(fixture.Expected(), "l4.animation");

    ASSERT_EQ(1u, result.clips.size()) << "the in-scene channel still yields its clip";
    EXPECT_EQ("SpinBoth", result.clips[0].name);
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "expectedTrackCount", 0)),
              result.clips[0].tracks.size());

    EXPECT_EQ(static_cast<int>(NumberOr(expected, "channelCount", 0)), result.report.channelCount);
    EXPECT_EQ(static_cast<int>(NumberOr(expected, "skippedOutOfSceneChannels", 0)),
              result.report.skippedOutOfSceneChannels);
    EXPECT_EQ(0, result.report.skippedUnsupportedPathChannels)
        << "the skipped channel's PATH is fine -- only its target is unreachable, and conflating "
           "the two would report the wrong cause";

    // The surviving track drives the in-scene node, not merely "some node": a reader that resolved
    // the out-of-scene target to index 0 would also produce one track, on the synthetic root.
    const cgltf_node* inScene =
        fixture.Data().nodes + static_cast<std::size_t>(NumberOr(
            Member(expected, "animatedInSceneNode"), "node", 0));
    const auto placed = result.scene.indexOfNode.find(inScene);
    ASSERT_NE(result.scene.indexOfNode.end(), placed);
    EXPECT_EQ(placed->second, result.clips[0].tracks[0].boneIndex);
    EXPECT_EQ("InScene", result.scene.nodes[static_cast<std::size_t>(placed->second)].name);

    // The out-of-scene node must genuinely be absent from the graph, or the fixture proves nothing.
    const cgltf_node* offScene =
        fixture.Data().nodes + static_cast<std::size_t>(NumberOr(
            Member(expected, "skippedTarget"), "node", 0));
    EXPECT_EQ(result.scene.indexOfNode.end(), result.scene.indexOfNode.find(offScene))
        << "the decoy node is in the imported graph after all";

    EXPECT_TRUE(AnyWarningMentions(result.warnings, {"SpinBoth", "not in the default scene"}))
        << "the skip must name the clip and the reason; silence here is exactly what D6 was";
}

// --- GLTF-313: sampler input must be ascending --------------------------------------------------

TEST(GltfAnimationRobustness, ADecreasingSamplerInputIsRefusedRatherThanSilentlySorted)
{
    // Sorting would silently re-pair each time with a value the exporter did not write, turning a
    // broken file into a plausible-looking wrong animation that plays. A named failure at import
    // is recoverable; a quietly wrong animation is not.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("bad-animation-input-order");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const SceneGraphOut scene = BuildSceneGraph(&fixture.Data());
    std::vector<std::string> warnings;
    try
    {
        ExtractSceneNodeClips(&fixture.Data(), scene, 1.0f, warnings);
        FAIL() << "a sampler input of 0, 2, 1 was accepted";
    }
    catch (const std::runtime_error& e)
    {
        const std::string what = e.what();
        EXPECT_NE(std::string::npos, what.find("not ascending")) << what;
        EXPECT_NE(std::string::npos, what.find("strictly increasing")) << what;
        // Actionable, not merely correct: the message must say WHICH channel and WHERE, because a
        // real asset has dozens and "an animation is malformed" locates none of them.
        EXPECT_NE(std::string::npos, what.find("Backwards")) << what;
    }
}

TEST(GltfAnimationRobustness, RepeatedInputTimesAreToleratedAndCountedRatherThanRefused)
{
    // The other half of GLTF-313's policy, and the reason it is not simply "reject anything that
    // is not strictly increasing". Equal adjacent times are what an exporter writes for a hard cut;
    // FindBracket's zero-length span already yields the earlier sample, so nothing reads out of
    // range, and refusing them would reject assets that play correctly.
    const LoadedFixture fixture("anim-repeated-time");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const Extracted result = ExtractRigid(fixture);
    const CNA::Internal::JsonValue& expected = Path(fixture.Expected(), "l4.animation");

    ASSERT_EQ(1u, result.clips.size());
    EXPECT_EQ(static_cast<int>(NumberOr(expected, "duplicateAdjacentTimes", 0)),
              result.report.duplicateInputTimeCount)
        << "the tolerance must be visible, or it is indistinguishable from not having looked";

    const CNA::Internal::GltfImport::TrackOut* track = SoleTrack(result.clips);
    ASSERT_NE(nullptr, track);

    // Three keys, not four. A track holds one keyframe per time, so the value in force AT the cut
    // survives and the post-cut value is not representable -- the clip plays a ramp where the file
    // describes a jump. Asserted rather than glossed, because an approximation nobody wrote down
    // is the same thing as a bug.
    const std::vector<CNA::Internal::JsonValue>& keys =
        Member(expected, "expectedKeys").arrayValue;
    ASSERT_EQ(keys.size(), track->keys.size());
    for (std::size_t i = 0; i < track->keys.size(); ++i)
    {
        SCOPED_TRACE("key " + std::to_string(i));
        const CNA::Internal::JsonValue& key = keys[i];
        EXPECT_NEAR(NumberOr(key, "time", -1.0), track->keys[i].time, kTolerance);
        const std::vector<double> t = CnaTest::GltfOracle::Numbers(Member(key, "translation"));
        ASSERT_EQ(3u, t.size());
        EXPECT_NEAR(static_cast<float>(t[0]), track->keys[i].translation.X, kTolerance);
        EXPECT_NEAR(static_cast<float>(t[1]), track->keys[i].translation.Y, kTolerance);
        EXPECT_NEAR(static_cast<float>(t[2]), track->keys[i].translation.Z, kTolerance);
    }
}

// --- GLTF-315: the import report ----------------------------------------------------------------

TEST(GltfAnimationRobustness, AnUnsupportedChannelPathIsSkippedAndCountedWithoutLosingTheClip)
{
    // `weights` is a legal fourth animation path CNA cannot drive: morph weights are applied on
    // the CPU at import, so there is no per-frame weight to key. Skipping it is right; skipping it
    // silently leaves a file that plays its rotation and never morphs, which reads as a broken
    // morph target rather than as an unimported channel.
    const LoadedFixture fixture("anim-weights-path");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const Extracted result = ExtractRigid(fixture);
    const CNA::Internal::JsonValue& expected = Path(fixture.Expected(), "l4.animation");

    EXPECT_EQ(static_cast<int>(NumberOr(expected, "channelCount", 0)), result.report.channelCount);
    EXPECT_EQ(static_cast<int>(NumberOr(expected, "skippedUnsupportedPathChannels", 0)),
              result.report.skippedUnsupportedPathChannels);
    EXPECT_EQ(0, result.report.skippedOutOfSceneChannels)
        << "the weights channel targets an imported node -- only its path is unsupported";

    // The clip survives with its rotation track, so "a channel was skipped" is distinguishable
    // from "the animation was dropped".
    ASSERT_EQ(1u, result.clips.size());
    EXPECT_EQ(1u, result.clips[0].tracks.size());
    EXPECT_EQ(0, result.report.emptyAnimationCount);
    EXPECT_TRUE(AnyWarningMentions(result.warnings, {"SpinAndMorph", "does not import"}));
}

TEST(GltfAnimationRobustness, ChannelsKeyedAtDisjointTimesAreResampledOntoTheirUnionAndCounted)
{
    // A ClipOut track carries one keyframe per time holding all of T/R/S, but glTF keys each path
    // on its own sampler. Reconciling them means baking onto the union of every channel's times --
    // exact at each source key, an interpolation everywhere else, and nothing in the result says
    // it happened.
    const LoadedFixture fixture("anim-translation-scale");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const Extracted result = ExtractRigid(fixture);
    const CNA::Internal::JsonValue& expected = Path(fixture.Expected(), "l4.animation");

    ASSERT_EQ(1u, result.clips.size());
    EXPECT_NEAR(NumberOr(expected, "duration", -1.0), result.clips[0].duration, kTolerance);
    EXPECT_EQ(1, result.report.resampledTrackCount)
        << "four union keys from two channels of two is a resample, and the report must say so";

    const CNA::Internal::GltfImport::TrackOut* track = SoleTrack(result.clips);
    ASSERT_NE(nullptr, track);

    const std::vector<CNA::Internal::JsonValue>& keys =
        Member(expected, "expectedKeys").arrayValue;
    ASSERT_EQ(static_cast<std::size_t>(NumberOr(expected, "expectedTrackKeyCount", 0)), keys.size());
    ASSERT_EQ(keys.size(), track->keys.size())
        << "the union of {0,2} and {1,3} is four times; a reader that used only one channel's "
           "times would produce two";

    const std::vector<double> rest = CnaTest::GltfOracle::Numbers(Member(expected, "restRotation"));
    ASSERT_EQ(4u, rest.size());
    for (std::size_t i = 0; i < track->keys.size(); ++i)
    {
        SCOPED_TRACE("key " + std::to_string(i));
        const CNA::Internal::JsonValue& key = keys[i];
        EXPECT_NEAR(NumberOr(key, "time", -1.0), track->keys[i].time, kTolerance);

        const std::vector<double> t = CnaTest::GltfOracle::Numbers(Member(key, "translation"));
        const std::vector<double> s = CnaTest::GltfOracle::Numbers(Member(key, "scale"));
        ASSERT_EQ(3u, t.size());
        ASSERT_EQ(3u, s.size());
        EXPECT_NEAR(static_cast<float>(t[0]), track->keys[i].translation.X, kTolerance);
        EXPECT_NEAR(static_cast<float>(t[1]), track->keys[i].translation.Y, kTolerance);
        EXPECT_NEAR(static_cast<float>(t[2]), track->keys[i].translation.Z, kTolerance);
        EXPECT_NEAR(static_cast<float>(s[0]), track->keys[i].scale.X, kTolerance);
        EXPECT_NEAR(static_cast<float>(s[1]), track->keys[i].scale.Y, kTolerance);
        EXPECT_NEAR(static_cast<float>(s[2]), track->keys[i].scale.Z, kTolerance);

        // The undriven rotation comes from the node's own rest pose, which the fixture authors as
        // a quarter turn precisely so "bind pose" and "identity" are different answers.
        EXPECT_NEAR(static_cast<float>(rest[2]), track->keys[i].rotation.Z, kTolerance);
        EXPECT_NEAR(static_cast<float>(rest[3]), track->keys[i].rotation.W, kTolerance);
    }
}

TEST(GltfAnimationRobustness, AFixtureWithNoResamplingAndNoSkipsReportsZeroesRatherThanNothing)
{
    // The control for every count above. Without it, a report that returned 0 for everything --
    // because it was never populated at all -- would pass each of the assertions that expect a
    // zero, and only the non-zero cases would be doing any work.
    const LoadedFixture fixture("anim-rigid-node");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const Extracted result = ExtractRigid(fixture);
    EXPECT_EQ(1, result.report.animationCount);
    EXPECT_EQ(1, result.report.clipCount);
    EXPECT_EQ(1, result.report.channelCount);
    EXPECT_EQ(1, result.report.trackCount);
    EXPECT_EQ(0, result.report.emptyAnimationCount);
    EXPECT_EQ(0, result.report.skippedOutOfSceneChannels);
    EXPECT_EQ(0, result.report.skippedUnsupportedPathChannels);
    EXPECT_EQ(0, result.report.resampledTrackCount);
    EXPECT_EQ(0, result.report.duplicateInputTimeCount);
    EXPECT_NEAR(1.0, result.report.longestClipDuration, kTolerance);
    EXPECT_TRUE(result.warnings.empty())
        << "a clean file must warn about nothing, or a warning means nothing";
}

// --- GLTF-316: the animation corpus, swept -------------------------------------------------------

TEST(GltfAnimationRobustness, EveryAnimationFixtureExtractsItsManifestsClipsAndDurations)
{
    // The regression sweep. Each `anim-*` fixture states its own clip names and duration in its
    // manifest, so the assertion is uniform while every fixture discriminates something different:
    // interpolation modes, disjoint key times, two clips, an unnamed clip, a hierarchy, a channel
    // path CNA cannot drive, a target outside the default scene.
    const std::vector<std::string> ids = AnimationFixtureIds();
    ASSERT_FALSE(ids.empty()) << "no anim-* fixtures found -- the sweep proved nothing";

    std::size_t swept = 0;
    for (const std::string& id : ids)
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();

        const CNA::Internal::JsonValue& expected = Path(fixture.Expected(), "l4.animation");
        const std::vector<std::string> names = Strings(Member(expected, "clipNames"));
        if (names.empty()) { continue; } // a morph fixture in the same owning group
        ++swept;

        const Extracted result = ExtractRigid(fixture);
        ASSERT_EQ(names.size(), result.clips.size());
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            EXPECT_EQ(names[i], result.clips[i].name)
                << "clip order and naming are both observable: an application selects a clip BY "
                   "NAME, so a generated name that shifted would break a saved reference";
            EXPECT_EQ(CNA::Internal::GltfImport::ClipTargetSpace::SceneNode,
                      result.clips[i].targetSpace);
        }

        // A file with one clip states its duration directly; one with several states a list.
        const std::vector<double> durations =
            CnaTest::GltfOracle::Numbers(Member(expected, "clipDurations"));
        if (!durations.empty())
        {
            ASSERT_EQ(durations.size(), result.clips.size());
            for (std::size_t i = 0; i < durations.size(); ++i)
            {
                EXPECT_NEAR(durations[i], result.clips[i].duration, kTolerance);
            }
        }
        else
        {
            EXPECT_NEAR(NumberOr(expected, "duration", -1.0), result.clips[0].duration, kTolerance);
        }
    }
    EXPECT_GE(swept, 8u) << "the animation corpus shrank -- a sweep over too few fixtures passes "
                            "for reasons that have nothing to do with the code";
}

TEST(GltfAnimationRobustness, EveryAnimationFixtureAgreesWithItsManifestOnWhichNodesAreDriven)
{
    // The second half of the sweep, and the one that catches an index-space error: a clip whose
    // tracks name the right *count* of bones but the wrong ones plays the wrong geometry, and no
    // assertion on names or durations would notice.
    for (const std::string& id : AnimationFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();

        const Extracted result = ExtractRigid(fixture);
        for (const CNA::Internal::GltfImport::ClipOut& clip : result.clips)
        {
            for (const CNA::Internal::GltfImport::TrackOut& track : clip.tracks)
            {
                ASSERT_GE(track.boneIndex, 0);
                ASSERT_LT(static_cast<std::size_t>(track.boneIndex), result.scene.nodes.size())
                    << "a track's bone index must address the imported graph -- both loaders "
                       "mirror SceneGraphOut::nodes one-for-one onto Model::Bones";
                EXPECT_NE(0, track.boneIndex)
                    << "the synthetic identity root is never a glTF node, so no channel can "
                       "legitimately resolve to it; index 0 is what an unresolved target "
                       "degrades into";
                EXPECT_FALSE(track.keys.empty()) << "a track with no keys is not a track";
                for (std::size_t k = 1; k < track.keys.size(); ++k)
                {
                    EXPECT_LT(track.keys[k - 1].time, track.keys[k].time)
                        << "keys must be strictly ascending after resampling, whatever the source "
                           "channels did";
                }
            }
        }
    }
}
