// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBone.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarSpace.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::Matrix;

namespace {
using System::TimeSpan;

TimeSpan Seconds(double seconds) { return TimeSpan::FromTicks(static_cast<long long>(seconds * 1.0e7)); }

bool Near(const Matrix& a, const Matrix& b, float tolerance = 1e-4f) {
    const float* x = &a.M11;
    const float* y = &b.M11;
    for (int i = 0; i < 16; ++i) {
        if (std::fabs(x[i] - y[i]) > tolerance) return false;
    }
    return true;
}

Matrix Bone(const AvatarAnimation& animation, int index) {
    const auto bones = animation.getBoneTransformsProperty();
    return bones[index];
}
}

TEST(AvatarAnimationTest, EveryPresetHasARealLengthAndDecomposableBones) {
    for (int preset = 0; preset <= static_cast<int>(AvatarAnimationPreset::MaleYawn); ++preset) {
        AvatarAnimation animation(static_cast<AvatarAnimationPreset>(preset));
        EXPECT_GE(animation.getLengthProperty(), Seconds(2.5)) << preset;
        EXPECT_LE(animation.getLengthProperty(), Seconds(10.0)) << preset;
        for (double t : {0.0, 0.37, 1.1, 2.2}) {
            animation.setCurrentPositionProperty(Seconds(t));
            const auto bones = animation.getBoneTransformsProperty();
            ASSERT_EQ(bones.getCountProperty(), 71);
            for (int bone = 0; bone < 71; ++bone) {
                Microsoft::Xna::Framework::Vector3 scale, translation;
                Microsoft::Xna::Framework::Quaternion rotation;
                ASSERT_TRUE(bones[bone].Decompose(scale, rotation, translation)) << preset << " bone " << bone;
                EXPECT_NEAR(scale.X, 1.0f, 1e-3f);
            }
        }
    }
}

TEST(AvatarAnimationTest, PresetsAreDistinctMotions) {
    AvatarAnimation stand(AvatarAnimationPreset::Stand0);
    AvatarAnimation wave(AvatarAnimationPreset::Wave);
    EXPECT_NE(stand.getLengthProperty(), wave.getLengthProperty());
    wave.setCurrentPositionProperty(Seconds(1.2));
    stand.setCurrentPositionProperty(Seconds(1.2));
    EXPECT_FALSE(Near(Bone(wave, static_cast<int>(AvatarBone::ShoulderRight)), Bone(stand, static_cast<int>(AvatarBone::ShoulderRight))));
}

TEST(AvatarAnimationTest, BonesMoveOverTime) {
    AvatarAnimation wave(AvatarAnimationPreset::Wave);
    const Matrix rest = Bone(wave, static_cast<int>(AvatarBone::ElbowRight));
    wave.Update(Seconds(1.2), false);
    EXPECT_FALSE(Near(rest, Bone(wave, static_cast<int>(AvatarBone::ElbowRight)), 1e-2f));
}

TEST(AvatarAnimationTest, BoneTransformsAreLocalToTheParent) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand0);
    const Matrix head = Bone(animation, static_cast<int>(AvatarBone::Head));
    // Non-root animation matrices are deltas; BindPose supplies the joint offsets.
    EXPECT_EQ(head.getTranslationProperty(), Microsoft::Xna::Framework::Vector3::Zero);
    // The root carries the whole avatar at the floor.
    EXPECT_LT(Bone(animation, 0).getTranslationProperty().Length(), 0.05f);
}

TEST(AvatarAnimationTest, CurrentPositionIsClampedIntoTheLength) {
    AvatarAnimation animation(AvatarAnimationPreset::Clap);
    const auto length = animation.getLengthProperty();
    animation.setCurrentPositionProperty(length + Seconds(1.0));
    EXPECT_EQ(animation.getCurrentPositionProperty(), length);
    animation.setCurrentPositionProperty(Seconds(-1.0));
    EXPECT_EQ(animation.getCurrentPositionProperty(), TimeSpan::Zero);
    animation.setCurrentPositionProperty(Seconds(1.5));
    EXPECT_EQ(animation.getCurrentPositionProperty(), Seconds(1.5));
}

TEST(AvatarAnimationTest, UpdateAdvancesAndClampsWithoutLoop) {
    AvatarAnimation animation(AvatarAnimationPreset::Wave);
    animation.Update(Seconds(1.0), false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), Seconds(1.0));
    animation.Update(Seconds(100.0), false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), animation.getLengthProperty());
    animation.Update(Seconds(-200.0), false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), TimeSpan::Zero);
}

TEST(AvatarAnimationTest, LoopingWrapsByTheRemainderOfTheLength) {
    AvatarAnimation animation(AvatarAnimationPreset::Wave);
    const long long length = animation.getLengthProperty().getTicksProperty();
    animation.Update(TimeSpan::FromTicks(length * 2 + 12345), true);
    EXPECT_EQ(animation.getCurrentPositionProperty().getTicksProperty(), 12345);
    // Backward past zero continues from the end.
    animation.Update(TimeSpan::FromTicks(-12345 - 1000), true);
    EXPECT_EQ(animation.getCurrentPositionProperty().getTicksProperty(), length - 1000);
    // Exactly Length is kept, not wrapped to zero.
    animation.setCurrentPositionProperty(animation.getLengthProperty());
    animation.Update(TimeSpan::Zero, true);
    EXPECT_EQ(animation.getCurrentPositionProperty().getTicksProperty(), length);
}

TEST(AvatarAnimationTest, OverflowingUpdateClampsToTheEnd) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand3);
    animation.Update(Seconds(1.0), false);
    animation.Update(TimeSpan::MaxValue, false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), animation.getLengthProperty());
    animation.Update(TimeSpan::MinValue, false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), TimeSpan::Zero);
}

TEST(AvatarAnimationTest, IdlesLoopSeamlessly) {
    for (auto preset : {AvatarAnimationPreset::Stand0, AvatarAnimationPreset::Stand4, AvatarAnimationPreset::Stand7}) {
        AvatarAnimation animation(preset);
        const auto start = animation.getBoneTransformsProperty();
        animation.setCurrentPositionProperty(animation.getLengthProperty());
        const auto end = animation.getBoneTransformsProperty();
        for (int bone = 0; bone < 71; ++bone) {
            EXPECT_TRUE(Near(start[bone], end[bone], 2e-3f)) << static_cast<int>(preset) << " bone " << bone;
        }
    }
}

TEST(AvatarAnimationTest, ExpressionFollowsTheClip) {
    AvatarAnimation celebrate(AvatarAnimationPreset::Celebrate);
    EXPECT_EQ(celebrate.getExpressionProperty().getMouthProperty(), AvatarMouth::Happy);
    celebrate.setCurrentPositionProperty(Seconds(1.0));
    EXPECT_EQ(celebrate.getExpressionProperty().getMouthProperty(), AvatarMouth::Laughing);
    EXPECT_EQ(celebrate.getExpressionProperty().getLeftEyeProperty(), AvatarEye::Laughing);
    EXPECT_EQ(celebrate.getExpressionProperty().getLeftEyebrowProperty(), AvatarEyebrow::Raised);

    AvatarAnimation idle(AvatarAnimationPreset::Stand0);
    idle.setCurrentPositionProperty(Seconds(1.45));
    EXPECT_EQ(idle.getExpressionProperty().getLeftEyeProperty(), AvatarEye::Blink);
    EXPECT_EQ(idle.getExpressionProperty().getRightEyeProperty(), AvatarEye::Blink);
    idle.setCurrentPositionProperty(Seconds(1.7));
    EXPECT_EQ(idle.getExpressionProperty().getLeftEyeProperty(), AvatarEye::Neutral);

    AvatarAnimation confused(AvatarAnimationPreset::FemaleConfused);
    confused.setCurrentPositionProperty(Seconds(1.0));
    EXPECT_EQ(confused.getExpressionProperty().getLeftEyebrowProperty(), AvatarEyebrow::Confused);
    EXPECT_EQ(confused.getExpressionProperty().getMouthProperty(), AvatarMouth::Confused);
}

TEST(AvatarAnimationTest, AnUndefinedPresetIsAZeroLengthBindPose) {
    AvatarAnimation animation(static_cast<AvatarAnimationPreset>(999));
    EXPECT_EQ(animation.getLengthProperty(), TimeSpan::Zero);
    animation.Update(Seconds(3.0), true);
    EXPECT_EQ(animation.getCurrentPositionProperty(), TimeSpan::Zero);
    AvatarAnimation stand(AvatarAnimationPreset::Stand0);
    EXPECT_EQ(Bone(animation, static_cast<int>(AvatarBone::Head)).getTranslationProperty(),
              Bone(stand, static_cast<int>(AvatarBone::Head)).getTranslationProperty());
    EXPECT_EQ(animation.getExpressionProperty().getMouthProperty(), AvatarMouth::Neutral);
}

TEST(AvatarAnimationTest, IsDisposedDefaultsFalse) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand5);
    EXPECT_FALSE(animation.getIsDisposedProperty());
}

TEST(AvatarAnimationTest, DisposeSetsIsDisposed) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand6);
    animation.Dispose();
    EXPECT_TRUE(animation.getIsDisposedProperty());
}

TEST(AvatarAnimationTest, DisposeIsIdempotent) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand7);
    animation.Dispose();
    EXPECT_NO_THROW(animation.Dispose());
    EXPECT_TRUE(animation.getIsDisposedProperty());
}

TEST(AvatarAnimationTest, UpdateThrowsAfterDispose) {
    AvatarAnimation animation(AvatarAnimationPreset::FemaleLaugh);
    animation.Dispose();
    EXPECT_THROW(animation.Update(System::TimeSpan::Zero, false), System::ObjectDisposedException);
}

TEST(AvatarAnimationTest, ImplementsIAvatarAnimationInterface) {
    AvatarAnimation animation(AvatarAnimationPreset::MaleLaugh);
    IAvatarAnimation& asInterface = animation;
    EXPECT_EQ(asInterface.getLengthProperty(), animation.getLengthProperty());
    EXPECT_GT(asInterface.getLengthProperty(), System::TimeSpan::Zero);
    EXPECT_EQ(asInterface.getBoneTransformsProperty().getCountProperty(), 71);
}

namespace {
using Microsoft::Xna::Framework::Vector3;

// World positions of the 71 joints for a set of XNA avatar local bone transforms.
std::array<Vector3, 71> JointPositions(const AvatarAnimation& animation) {
    static const int parents[71] = {
        -1, 0, 0, 0, 0, 1, 2, 2, 3, 3, 1, 6, 5, 6, 5, 8, 5, 8, 5, 14, 12, 11, 16, 15, 14, 20, 20, 20, 22, 22, 22,
        25, 25, 25, 28, 28, 28, 33, 33, 33, 33, 33, 33, 33, 36, 36, 36, 36, 36, 36, 36, 37, 38, 39, 40, 43, 44,
        45, 46, 47, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60};
    const auto bones = animation.getBoneTransformsProperty();
    std::array<Matrix, 71> world;
    std::array<Vector3, 71> out;
    for (int bone = 0; bone < 71; ++bone) {
        const auto offset = CNA::Internal::GamerServices::Avatars::clipLibrary().bindTranslations[bone];
        const Matrix local = bones[bone] * CNA::Internal::GamerServices::Avatars::changeAvatarSpace(Matrix::CreateTranslation(offset));
        world[bone] = parents[bone] < 0 ? local : local * world[parents[bone]];
        out[bone] = world[bone].getTranslationProperty();
    }
    return out;
}

float SegmentDistance(const Vector3& p, const Vector3& a, const Vector3& b) {
    const Vector3 ab = b - a;
    const float t = std::clamp(Vector3::Dot(p - a, ab) / Vector3::Dot(ab, ab), 0.0f, 1.0f);
    return Vector3::Distance(p, a + ab * t);
}

constexpr int Presets = static_cast<int>(AvatarAnimationPreset::MaleYawn) + 1;
}

TEST(AvatarAnimationTest, LoopingIdlesJoinSeamlessly) {
    // The Stand presets loop: their last frame is their first, with no jump in motion either.
    for (int preset = 0; preset <= static_cast<int>(AvatarAnimationPreset::Stand7); ++preset) {
        AvatarAnimation animation(static_cast<AvatarAnimationPreset>(preset));
        const auto length = animation.getLengthProperty();
        const auto first = animation.getBoneTransformsProperty();
        std::vector<Matrix> start(first.begin(), first.end());
        animation.setCurrentPositionProperty(length);
        for (int bone = 0; bone < 71; ++bone) {
            EXPECT_TRUE(Near(Bone(animation, bone), start[bone], 2e-3f)) << "preset " << preset << " bone " << bone;
        }
        // The step across the seam is no larger than a step inside the clip.
        const auto step = Seconds(1.0 / 60.0);
        animation.setCurrentPositionProperty(length - step);
        const auto before = JointPositions(animation);
        animation.Update(Seconds(2.0 / 60.0), true);
        const auto after = JointPositions(animation);
        EXPECT_LT(Vector3::Distance(before[36], after[36]), 0.02f) << "preset " << preset;
    }
}

TEST(AvatarAnimationTest, FeetStayOnTheGroundAndHandsOutOfTheBody) {
    AvatarAnimation bind(static_cast<AvatarAnimationPreset>(Presets + 5));
    const auto rest = JointPositions(bind);
    constexpr int ankles[2] = {11, 15}, toes[2] = {21, 23};
    constexpr int hands[] = {33, 36, 52, 57, 61, 66};
    for (int preset = 0; preset < Presets; ++preset) {
        AvatarAnimation animation(static_cast<AvatarAnimationPreset>(preset));
        const double length = static_cast<double>(animation.getLengthProperty().getTicksProperty()) / 1.0e7;
        float lowest = 1.0f, closest = 1.0f;
        for (double t = 0.0; t <= length; t += 1.0 / 30.0) {
            animation.setCurrentPositionProperty(Seconds(t));
            const auto joints = JointPositions(animation);
            for (int side = 0; side < 2; ++side) {
                lowest = std::min({lowest, joints[ankles[side]].Y - rest[ankles[side]].Y, joints[toes[side]].Y - rest[toes[side]].Y});
            }
            for (int hand : hands) {
                closest = std::min({closest, SegmentDistance(joints[hand], joints[1], joints[5]),
                                    SegmentDistance(joints[hand], joints[5], joints[14])});
            }
        }
        // Planted feet never sink into the floor, and no hand passes through the spine.
        EXPECT_GT(lowest, -0.012f) << "preset " << preset;
        EXPECT_GT(closest, 0.05f) << "preset " << preset;
    }
}

TEST(AvatarAnimationTest, PresetsMoveWithoutPops) {
    for (int preset = 0; preset < Presets; ++preset) {
        AvatarAnimation animation(static_cast<AvatarAnimationPreset>(preset));
        const double length = static_cast<double>(animation.getLengthProperty().getTicksProperty()) / 1.0e7;
        auto previous = JointPositions(animation);
        float fastest = 0.0f;
        for (double t = 1.0 / 60.0; t <= length; t += 1.0 / 60.0) {
            animation.setCurrentPositionProperty(Seconds(t));
            const auto joints = JointPositions(animation);
            for (int bone = 0; bone < 71; ++bone) {
                fastest = std::max(fastest, Vector3::Distance(joints[bone], previous[bone]));
            }
            previous = joints;
        }
        // 12 cm in a sixtieth of a second is 7.2 m/s, far past anything the presets intend.
        EXPECT_LT(fastest, 0.12f) << "preset " << preset;
    }
}

TEST(AvatarAnimationTest, PublicBonesUseXnaFacingMinusZCoordinates) {
    namespace Assets = CNA::Internal::GamerServices::Avatars;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Quaternion;
    const auto& library = Assets::clipLibrary();
    for (int preset = 0; preset < static_cast<int>(library.clips.size()); ++preset) {
        AvatarAnimation animation(static_cast<AvatarAnimationPreset>(preset));
        for (double t : {0.0, 0.37, 1.1, 2.2}) {
            animation.setCurrentPositionProperty(Seconds(t));
            std::array<Quaternion, 71> rotations;
            Vector3 root;
            Assets::sampleClip(library.clips[preset], t, rotations, root);
            const auto publicBones = animation.getBoneTransformsProperty();
            for (int bone = 0; bone < 71; ++bone) {
                const auto offset = bone == 0 ? root : Vector3::Zero;
                const Matrix asset = Matrix::CreateFromQuaternion(rotations[bone]) * Matrix::CreateTranslation(offset);
                // A point and its direction must describe the same movement after a Y half-turn.
                for (const Vector3 p : {Vector3::Zero, Vector3(0.13f, -0.27f, 0.41f)}) {
                    const auto expectedAsset = Vector3::Transform(p, asset);
                    const auto actualXna = Vector3::Transform(Vector3(-p.X, p.Y, -p.Z), publicBones[bone]);
                    EXPECT_NEAR(actualXna.X, -expectedAsset.X, 1e-5f) << preset << " bone " << bone;
                    EXPECT_NEAR(actualXna.Y, expectedAsset.Y, 1e-5f);
                    EXPECT_NEAR(actualXna.Z, -expectedAsset.Z, 1e-5f);
                }
            }
        }
    }
}
