// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    namespace
    {
        // Parent bone index for each of the 71 avatar skeleton bones (-1 = root/no parent).
        // Exact values decoded from the real XNA reference assembly; not derived or guessed.
        const std::vector<int> kParentBoneIds = {
            -1, 0, 0, 0, 0, 1, 2, 2, 3, 3, 1, 6, 5, 6, 5, 8, 5, 8, 5, 14, 12, 11, 16, 15, 14, 20, 20, 20, 22, 22, 22,
            25, 25, 25, 28, 28, 28, 33, 33, 33, 33, 33, 33, 33, 36, 36, 36, 36, 36, 36, 36, 37, 38, 39, 40, 43, 44,
            45, 46, 47, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60
        };
    }

    // The real XNA implementation never reads either constructor's arguments - every instance
    // ends up with identical, permanently AvatarRendererState::Unavailable state. Preserved
    // exactly, not "fixed."
    AvatarRenderer::AvatarRenderer(AvatarDescription* /*avatarDescription*/)
        : AvatarRenderer(nullptr, true)
    {
    }

    AvatarRenderer::AvatarRenderer(AvatarDescription* /*avatarDescription*/, bool /*useLoadingEffect*/)
        : world_(Microsoft::Xna::Framework::Matrix::getIdentityProperty())
        , view_(Microsoft::Xna::Framework::Matrix::getIdentityProperty())
        , projection_(Microsoft::Xna::Framework::Matrix::getIdentityProperty())
        , parentBoneIds_(kParentBoneIds)
        , bindPoseArray_(BoneCount)
    {
    }

    Microsoft::Xna::Framework::Matrix AvatarRenderer::getWorldProperty() const { return world_; }
    void AvatarRenderer::setWorldProperty(Microsoft::Xna::Framework::Matrix value) { world_ = value; }

    Microsoft::Xna::Framework::Matrix AvatarRenderer::getViewProperty() const { return view_; }
    void AvatarRenderer::setViewProperty(Microsoft::Xna::Framework::Matrix value) { view_ = value; }

    Microsoft::Xna::Framework::Matrix AvatarRenderer::getProjectionProperty() const { return projection_; }
    void AvatarRenderer::setProjectionProperty(Microsoft::Xna::Framework::Matrix value) { projection_ = value; }

    System::Collections::ObjectModel::ReadOnlyCollection<int> AvatarRenderer::getParentBonesProperty() const
    {
        return System::Collections::ObjectModel::ReadOnlyCollection<int>(parentBoneIds_);
    }

    System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>
    AvatarRenderer::getBindPoseProperty() const
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("AvatarRenderer");
        }
        // Checks the raw state field directly, not getStateProperty() (which always forces
        // itself to Unavailable) - matching the real implementation exactly. Since nothing
        // anywhere ever sets state_ to Ready, this throws in every practical case.
        if (state_ != AvatarRendererState::Ready)
        {
            throw System::InvalidOperationException("The avatar's bind pose is not available.");
        }
        return System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>(bindPoseArray_);
    }

    AvatarRendererState AvatarRenderer::getStateProperty() const
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("AvatarRenderer");
        }
        // Forces itself to Unavailable on every single read, matching the real implementation
        // exactly (see the class remarks) - not a one-time initial value.
        state_ = AvatarRendererState::Unavailable;
        return state_;
    }

    Microsoft::Xna::Framework::Vector3 AvatarRenderer::getLightColorProperty() const { return lightColor_; }
    void AvatarRenderer::setLightColorProperty(Microsoft::Xna::Framework::Vector3 value) { lightColor_ = value; }

    Microsoft::Xna::Framework::Vector3 AvatarRenderer::getLightDirectionProperty() const { return lightDirection_; }
    void AvatarRenderer::setLightDirectionProperty(Microsoft::Xna::Framework::Vector3 value) { lightDirection_ = value; }

    Microsoft::Xna::Framework::Vector3 AvatarRenderer::getAmbientLightColorProperty() const { return ambientLightColor_; }
    void AvatarRenderer::setAmbientLightColorProperty(Microsoft::Xna::Framework::Vector3 value) { ambientLightColor_ = value; }

    bool AvatarRenderer::getIsDisposedProperty() const { return isDisposed_; }

    void AvatarRenderer::Draw(IAvatarAnimation* animation)
    {
        auto boneTransforms = animation->getBoneTransformsProperty();
        std::vector<Microsoft::Xna::Framework::Matrix> bones(boneTransforms.begin(), boneTransforms.end());
        Draw(bones, animation->getExpressionProperty());
    }

    void AvatarRenderer::Draw(const std::vector<Microsoft::Xna::Framework::Matrix>& bones, AvatarExpression /*expression*/)
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("AvatarRenderer");
        }
        if (static_cast<int>(bones.size()) != BoneCount)
        {
            throw System::ArgumentException("bones must contain exactly 71 entries.", "bones");
        }
        // Genuinely a no-op once validated, matching the real implementation.
    }

    void AvatarRenderer::Dispose()
    {
        Dispose(true);
    }

    void AvatarRenderer::Dispose(bool /*disposing*/)
    {
        isDisposed_ = true;
    }
}
