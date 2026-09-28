// SPDX-License-Identifier: MS-PL
#pragma once
#include <memory>
#include <string>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRendererState.hpp"
#include "Microsoft/Xna/Framework/GamerServices/IAvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/Collections/ObjectModel/ReadOnlyCollection.hpp"
#include "System/IDisposable.hpp"
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    class AvatarDescription;

    /**
     * @brief Provides properties and methods for rendering a standard avatar.
     *
     * A valid description starts Loading while its original CNA assets are assembled in the
     * background, then becomes Ready: the bind pose is available and Draw renders the skinned
     * avatar and its facial expression with the renderer's lights, using the graphics device of
     * the service provider passed to GamerServicesDispatcher::Initialize. A description without
     * an avatar (or one CNA cannot read) is Unavailable and draws nothing.
     */
    class AvatarRenderer : public System::IDisposable
    {
    public:
        /** @brief The number of bones in an avatar's skeleton. */
        static constexpr int BoneCount = 71;

        /**
         * @brief Creates a new instance of AvatarRenderer with the specified description, using the
         * standard loading effect while it loads.
         *
         * @param avatarDescription Description of the avatar to be rendered.
         * @throws System::ArgumentNullException if avatarDescription is null.
         */
        explicit AvatarRenderer(AvatarDescription* avatarDescription);

        /**
         * @brief Creates a new instance of AvatarRenderer with the specified description.
         *
         * @param avatarDescription Description of the avatar to be rendered.
         * @param useLoadingEffect true to draw the animated standard loading effect while the
         * avatar is loading; otherwise nothing is drawn until it is ready.
         * @throws System::ArgumentNullException if avatarDescription is null.
         */
        AvatarRenderer(AvatarDescription* avatarDescription, bool useLoadingEffect);

        /**
         * @brief Destructor.
         *
         * CNAEXT: declared (rather than defaulted inline) so that the GPU resources can be
         * destroyed where their types are complete.
         */
        CNAEXT ~AvatarRenderer();

        /** @brief Gets the world transform matrix. */
        [[nodiscard]] Microsoft::Xna::Framework::Matrix getWorldProperty() const;
        /** @brief Sets the world transform matrix. */
        void setWorldProperty(Microsoft::Xna::Framework::Matrix value);

        /** @brief Gets the view transform matrix. */
        [[nodiscard]] Microsoft::Xna::Framework::Matrix getViewProperty() const;
        /** @brief Sets the view transform matrix. */
        void setViewProperty(Microsoft::Xna::Framework::Matrix value);

        /** @brief Gets the projection transform matrix. */
        [[nodiscard]] Microsoft::Xna::Framework::Matrix getProjectionProperty() const;
        /** @brief Sets the projection transform matrix. */
        void setProjectionProperty(Microsoft::Xna::Framework::Matrix value);

        /**
         * @brief Gets the parent bone index for each of the 71 bones in the avatar's skeleton.
         *
         * @return A read-only collection of 71 parent bone indices (-1 for the root bone).
         */
        [[nodiscard]] System::Collections::ObjectModel::ReadOnlyCollection<int> getParentBonesProperty() const;

        /**
         * @brief Gets the bind pose of each bone, in local space relative to the parent bone.
         *
         * @return A read-only collection of 71 bind pose matrices.
         * @throws System::ObjectDisposedException if this instance has been disposed.
         * @throws System::InvalidOperationException if State is not AvatarRendererState::Ready.
         */
        [[nodiscard]] System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>
        getBindPoseProperty() const;

        /**
         * @brief Gets the state of the avatar.
         *
         * @return Loading while assets are assembled, then Ready; Unavailable when the description
         * holds no avatar CNA can render.
         * @throws System::ObjectDisposedException if this instance has been disposed.
         */
        [[nodiscard]] AvatarRendererState getStateProperty() const;

        /** @brief Gets the light color used to render the avatar. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getLightColorProperty() const;
        /** @brief Sets the light color used to render the avatar. */
        void setLightColorProperty(Microsoft::Xna::Framework::Vector3 value);

        /** @brief Gets the light direction used to render the avatar. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getLightDirectionProperty() const;
        /** @brief Sets the light direction used to render the avatar. */
        void setLightDirectionProperty(Microsoft::Xna::Framework::Vector3 value);

        /** @brief Gets the ambient light color used to render the avatar. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getAmbientLightColorProperty() const;
        /** @brief Sets the ambient light color used to render the avatar. */
        void setAmbientLightColorProperty(Microsoft::Xna::Framework::Vector3 value);

        /** @brief Gets a value indicating whether this instance has been disposed. */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Draws the avatar to the current render target using the specified animation.
         *
         * @param animation The animation providing bone transforms and facial expression.
         * @throws System::ArgumentNullException if animation is null.
         * @throws System::ObjectDisposedException if this instance has been disposed.
         * @throws System::InvalidOperationException if a bone transform is not decomposable, or
         * if no graphics device is available.
         */
        void Draw(IAvatarAnimation* animation);

        /**
         * @brief Draws the avatar to the current render target.
         *
         * Each bone's rotation and scale are applied in local space relative to its parent; the
         * root also takes the supplied translation, while every other bone keeps this avatar's own
         * bind offset, so standard animations fit every avatar's height and build. While loading,
         * the standard loading effect is drawn when requested; an unavailable avatar draws
         * nothing. The model always renders solid and restores the device states it changes.
         *
         * @param bones The bone transform matrices; must contain exactly 71 entries.
         * @param expression Current expression of the avatar's face.
         * @throws System::ObjectDisposedException if this instance has been disposed.
         * @throws System::ArgumentException if bones does not contain exactly 71 entries.
         * @throws System::InvalidOperationException if a bone transform is not decomposable, or
         * if no graphics device is available.
         */
        void Draw(const std::vector<Microsoft::Xna::Framework::Matrix>& bones, AvatarExpression expression);

        /** @brief Releases all resources used by this instance. */
        void Dispose() override;

    protected:
        /**
         * @brief Releases the unmanaged resources used by this instance.
         *
         * @param disposing true to release both managed and unmanaged resources; false to
         * release only unmanaged resources. Idempotent.
         */
        void Dispose(bool disposing);

    private:
        struct Resources;
        void Poll() const;
        void DrawLoadingEffect();
        void DrawAvatar(const std::vector<Microsoft::Xna::Framework::Matrix>& bones, AvatarExpression expression);

        std::unique_ptr<Resources> resources_;
        bool useLoadingEffect_{true};

        Microsoft::Xna::Framework::Matrix world_;
        Microsoft::Xna::Framework::Matrix view_;
        Microsoft::Xna::Framework::Matrix projection_;
        std::vector<int> parentBoneIds_;
        mutable std::vector<Microsoft::Xna::Framework::Matrix> bindPoseArray_;
        mutable AvatarRendererState state_{AvatarRendererState::Unavailable};
        // REMED-GFX-008: sensible non-black defaults. Previously these were value-initialized to
        // (0,0,0), so an avatar drawn without an explicit lighting setup rendered pure black. The
        // ambient (0.35) + head-on key light (0.65) sum to 1.0 on a front-facing surface, so a fully
        // lit part shows at its nominal tint under the FNA-correct SkinnedEffect lighting model.
        Microsoft::Xna::Framework::Vector3 lightColor_{0.65f, 0.65f, 0.65f};
        Microsoft::Xna::Framework::Vector3 lightDirection_{0.0f, 0.0f, -1.0f};
        Microsoft::Xna::Framework::Vector3 ambientLightColor_{0.35f, 0.35f, 0.35f};
        bool isDisposed_{false};
    };
}
