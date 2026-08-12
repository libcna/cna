// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-307: how `AnimationPlayer` treats a time outside the clip.
//
// Every frame of every animated model goes through this, and both branches have a failure mode that
// is easy to miss and unpleasant when it lands. Clamping that lets the position run past the
// duration reads keyframes off the end of the track. Looping implemented as repeated subtraction
// costs time proportional to how long the session has been running, so a short clip in a
// long-running game gets slower the longer it plays -- and looping implemented as a plain `%`
// returns a NEGATIVE position for a negative time, which then indexes backwards.
//
// The clip here is deliberately short (100 ticks) against times far outside it, so a wraparound
// that is off by one period is off by a lot.

#include <gtest/gtest.h>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "System/TimeSpan.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr long long kDurationTicks = 100;

    /// The minimum a player needs: one bone, and a clip with a duration. No keyframes are needed --
    /// what is under test is the position arithmetic, not what is sampled at that position.
    SkinningData MakeSkinningData()
    {
        SkinningData data;
        data.BindPose = {Microsoft::Xna::Framework::Matrix::getIdentityProperty()};
        data.InverseBindPose = {Microsoft::Xna::Framework::Matrix::getIdentityProperty()};
        data.SkeletonHierarchy = {-1};
        data.BoneCount = 1;
        return data;
    }

    AnimationClip MakeClip()
    {
        AnimationClip clip;
        clip.Duration = System::TimeSpan::FromTicks(kDurationTicks);
        clip.Tracks = {BoneTrackEXT{}};
        return clip;
    }
}

TEST(AnimationPlayerClampAndLoop, WithoutLoopingThePositionStopsAtTheDurationRatherThanRunningPast)
{
    // Reading past the end of a track is the failure this prevents, and it is not a crash: the
    // bracket search clamps to the last keyframe, so the pose looks right while the *position*
    // keeps growing -- so any code that compares the position against the duration to decide the
    // clip has finished never fires.
    const SkinningData data = MakeSkinningData();
    AnimationPlayer player(data);
    const AnimationClip clip = MakeClip();
    player.StartClip(clip);

    player.Update(System::TimeSpan::FromTicks(kDurationTicks * 5), false, false);
    EXPECT_EQ(kDurationTicks, player.getCurrentPositionProperty().getTicksProperty())
        << "the position ran past the clip's duration";

    player.Update(System::TimeSpan::FromTicks(-kDurationTicks * 5), false, false);
    EXPECT_EQ(0LL, player.getCurrentPositionProperty().getTicksProperty())
        << "a negative time must clamp at zero, not run backwards off the front";
}

TEST(AnimationPlayerClampAndLoop, LoopingWrapsAPositiveTimeIntoTheClip)
{
    const SkinningData data = MakeSkinningData();
    AnimationPlayer player(data);
    const AnimationClip clip = MakeClip();
    player.StartClip(clip);

    // 5 whole periods plus 37 ticks. An implementation that subtracted one duration and stopped
    // would report 437; one that wrapped correctly reports 37.
    player.Update(System::TimeSpan::FromTicks(kDurationTicks * 5 + 37), false, true);
    EXPECT_EQ(37LL, player.getCurrentPositionProperty().getTicksProperty());

    // Exactly on a period boundary is the case a `<` / `<=` mix-up lands on: it must wrap to 0,
    // not sit at the duration, or a looping clip freezes for one frame every period.
    player.Update(System::TimeSpan::FromTicks(kDurationTicks * 3), false, true);
    EXPECT_EQ(0LL, player.getCurrentPositionProperty().getTicksProperty());
}

TEST(AnimationPlayerClampAndLoop, LoopingANegativeTimeWrapsForwardRatherThanStayingNegative)
{
    // C++'s `%` follows the sign of the dividend, so a plain `pos % duration` gives a NEGATIVE
    // position for a negative time -- which then indexes a track backwards. Playing an animation in
    // reverse is an ordinary thing to ask for, so this is not a hypothetical input.
    const SkinningData data = MakeSkinningData();
    AnimationPlayer player(data);
    const AnimationClip clip = MakeClip();
    player.StartClip(clip);

    player.Update(System::TimeSpan::FromTicks(-37), false, true);
    EXPECT_EQ(kDurationTicks - 37, player.getCurrentPositionProperty().getTicksProperty())
        << "a negative looping time stayed negative; C++'s % follows the dividend's sign";

    player.Update(System::TimeSpan::FromTicks(-(kDurationTicks * 4 + 37)), false, true);
    EXPECT_EQ(kDurationTicks - 37, player.getCurrentPositionProperty().getTicksProperty())
        << "several whole periods of negative time must land in the same place as one";
}

TEST(AnimationPlayerClampAndLoop, RelativeUpdatesAccumulateFromTheCurrentPosition)
{
    // The flag every game loop uses: `Update(elapsed, true)` advances from wherever the clip is.
    // Treating it as absolute restarts the clip every frame, which renders as an animation frozen
    // on its first pose -- a symptom easily mistaken for a missing track.
    const SkinningData data = MakeSkinningData();
    AnimationPlayer player(data);
    const AnimationClip clip = MakeClip();
    player.StartClip(clip);

    for (int i = 0; i < 3; ++i) { player.Update(System::TimeSpan::FromTicks(10), true, false); }
    EXPECT_EQ(30LL, player.getCurrentPositionProperty().getTicksProperty())
        << "relative updates did not accumulate; 10 would mean each Update restarted the clip";
}

TEST(AnimationPlayerClampAndLoop, AZeroDurationClipHoldsAtZeroRatherThanDividingByIt)
{
    // A clip whose duration is zero is a legal degenerate -- a single-keyframe pose exported as an
    // animation -- and it is the input that turns the looping branch's modulo into a division by
    // zero. Holding at zero is the only answer that is both defined and meaningful.
    const SkinningData data = MakeSkinningData();
    AnimationPlayer player(data);
    AnimationClip clip;
    clip.Duration = System::TimeSpan::Zero;
    clip.Tracks = {BoneTrackEXT{}};
    player.StartClip(clip);

    EXPECT_NO_THROW(player.Update(System::TimeSpan::FromTicks(500), false, true));
    EXPECT_EQ(0LL, player.getCurrentPositionProperty().getTicksProperty());
    EXPECT_NO_THROW(player.Update(System::TimeSpan::FromTicks(500), false, false));
    EXPECT_EQ(0LL, player.getCurrentPositionProperty().getTicksProperty());
}
