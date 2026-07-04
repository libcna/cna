// SPDX-License-Identifier: MS-PL
#pragma once
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
     * @brief Renders a 3D avatar model.
     *
     * The real XNA implementation's constructors never actually read their AvatarDescription
     * (or, for the 2-argument overload, useLoadingEffect) arguments — every instance ends up in
     * an identical, permanently AvatarRendererState::Unavailable state, since State's getter
     * unconditionally forces itself to Unavailable on every single read, and nothing anywhere
     * in the class ever assigns Ready or Loading. That surprising but verified behavior is
     * preserved here exactly, not "fixed."
     */
    class AvatarRenderer : public System::IDisposable
    {
    public:
        /** @brief The number of bones in an avatar's skeleton. */
        static constexpr int BoneCount = 71;

        /**
         * @brief Initializes a new instance of AvatarRenderer for the specified avatar description.
         *
         * @param avatarDescription The avatar description (not read by the real XNA
         * implementation; every instance behaves identically regardless of this argument).
         */
        explicit AvatarRenderer(AvatarDescription* avatarDescription);

        /**
         * @brief Initializes a new instance of AvatarRenderer for the specified avatar description.
         *
         * @param avatarDescription The avatar description (not read by the real XNA
         * implementation; every instance behaves identically regardless of this argument).
         * @param useLoadingEffect Whether to use a loading effect (not read by the real XNA
         * implementation).
         */
        AvatarRenderer(AvatarDescription* avatarDescription, bool useLoadingEffect);

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
         * @brief Gets the bind pose transform matrices for the avatar's skeleton.
         *
         * @return A read-only collection of 71 bind pose matrices.
         * @throws System::ObjectDisposedException if this instance has been disposed.
         * @throws System::InvalidOperationException if State is not AvatarRendererState::Ready
         * (always the case in practice, since State never becomes Ready — see the class
         * remarks).
         */
        [[nodiscard]] System::Collections::ObjectModel::ReadOnlyCollection<Microsoft::Xna::Framework::Matrix>
        getBindPoseProperty() const;

        /**
         * @brief Gets the current loading state of the avatar.
         *
         * Forces itself to AvatarRendererState::Unavailable on every single read, matching the
         * real XNA implementation (see the class remarks).
         *
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
         * @brief Draws the avatar using the specified animation.
         *
         * @param animation The animation providing bone transforms and facial expression.
         * @throws System::ObjectDisposedException if this instance has been disposed.
         */
        void Draw(IAvatarAnimation* animation);

        /**
         * @brief Draws the avatar using the specified bone transforms and facial expression.
         *
         * @param bones The bone transform matrices; must contain exactly 71 entries.
         * @param expression The facial expression to render.
         * @throws System::ObjectDisposedException if this instance has been disposed.
         * @throws System::ArgumentException if bones does not contain exactly 71 entries.
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
        Microsoft::Xna::Framework::Matrix world_;
        Microsoft::Xna::Framework::Matrix view_;
        Microsoft::Xna::Framework::Matrix projection_;
        std::vector<int> parentBoneIds_;
        std::vector<Microsoft::Xna::Framework::Matrix> bindPoseArray_;
        mutable AvatarRendererState state_{AvatarRendererState::Unavailable};
        Microsoft::Xna::Framework::Vector3 lightColor_;
        Microsoft::Xna::Framework::Vector3 lightDirection_;
        Microsoft::Xna::Framework::Vector3 ambientLightColor_;
        bool isDisposed_{false};
    };
}
