// SPDX-License-Identifier: MS-PL
#pragma once
#include <memory>
#include <string>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAppearanceEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRendererState.hpp"
#include "Microsoft/Xna/Framework/GamerServices/IAvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/Collections/ObjectModel/ReadOnlyCollection.hpp"
#include "System/IDisposable.hpp"
#include "System/TimeSpan.hpp"
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class SkinnedEffect;
    class SkinnedModelEXT;
}

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

        /**
         * @brief Destructor.
         *
         * NOXNA: declared (rather than defaulted inline) so that the real-rendering
         * extension's std::unique_ptr<Graphics::SkinnedEffect> member can be destroyed
         * where Graphics::SkinnedEffect is a complete type.
         */
        NOXNA ~AvatarRenderer();

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
         * @throws System::ArgumentNullException if animation is null.
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

        /**
         * @brief Opts this instance into real (non-XNA-spec) GPU-skinned mesh rendering.
         *
         * NOXNA — CNA extension. The faithful Draw() overloads above remain permanent no-ops
         * regardless of this call; only DrawRealEXT() renders real geometry, and only after
         * this method has been called. Not part of the real XNA 4.0 Avatar API — the real
         * implementation never renders anything off-Xbox (see the class remarks).
         *
         * @param device Graphics device used for the real render path.
         * @param model  A loaded skinned mesh + skeleton + animation clip set.
         * @throws System::ArgumentNullException if model is null.
         * @throws System::ObjectDisposedException if this instance has been disposed.
         */
        NOXNA void EnableRealRenderingEXT(Graphics::GraphicsDevice& device,
                                           std::shared_ptr<Graphics::SkinnedModelEXT> model);

        /**
         * @brief Gets whether EnableRealRenderingEXT has been called.
         *
         * @note NOXNA — CNA extension.
         * @return true if real rendering is enabled.
         */
        NOXNA [[nodiscard]] bool IsRealRenderingEnabledEXT() const;

        /**
         * @brief Sets the skin/hair/clothing tint used by DrawRealEXT.
         *
         * @note NOXNA — CNA extension.
         * @param appearance The appearance to apply on subsequent DrawRealEXT calls.
         */
        NOXNA void SetAppearanceEXT(const AvatarAppearanceEXT& appearance);

        /**
         * @brief Really renders the avatar's mesh using GPU skinning, using World/View/Projection
         * as already set via setWorldProperty/setViewProperty/setProjectionProperty.
         *
         * NOXNA — CNA extension; not part of the real XNA 4.0 Avatar API (which never renders
         * anything off-Xbox). Rendering happens through the standard GraphicsDevice/SkinnedEffect
         * path, so on backends without 3D support this throws whatever error that backend already
         * raises for any 3D draw call.
         *
         * @param animationClipName Name of a clip present in the enabled SkinnedModelEXT's Clips.
         * @param position          Playback position within the clip.
         * @param loop              Whether to loop playback at the clip's end.
         * @throws System::ObjectDisposedException if this instance has been disposed.
         * @throws System::InvalidOperationException if real rendering has not been enabled.
         */
        NOXNA void DrawRealEXT(const std::string& animationClipName,
                               System::TimeSpan position, bool loop);

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
        /**
         * @brief Resolves the AvatarAppearanceEXT tint for a SkinnedModelEXT part, by
         * substring match against the part's name (e.g. "CNAAvatarShirt").
         *
         * @note NOXNA — CNA extension helper for DrawRealEXT. Substring match, not exact
         * equality: part names are Blender object names baked through by the content
         * pipeline, not a fixed vocabulary CNA itself defines.
         * @param partName The SkinnedModelEXT part's name.
         * @return The matching hair/shirt/pants/shoes tint, or the skin tint if no
         * garment-slot keyword is found in @p partName.
         */
        NOXNA [[nodiscard]] Microsoft::Xna::Framework::Color PartTintEXT(const std::string& partName) const;

        // Task 13.1: PartTintEXT's substring-match routing has no non-GPU-dependent test access
        // otherwise - the only existing coverage goes through DrawRealEXT + real pixel readback,
        // which only ever exercised Hair/Shirt. Grants direct access for thorough Pants/Shoes/
        // skin-fallback/case-sensitivity/substring-collision coverage without needing a GPU.
        NOXNA friend struct AvatarRendererTestAccess;

        Microsoft::Xna::Framework::Matrix world_;
        Microsoft::Xna::Framework::Matrix view_;
        Microsoft::Xna::Framework::Matrix projection_;
        std::vector<int> parentBoneIds_;
        std::vector<Microsoft::Xna::Framework::Matrix> bindPoseArray_;
        mutable AvatarRendererState state_{AvatarRendererState::Unavailable};
        // REMED-GFX-008: sensible non-black defaults. Previously these were value-initialized to
        // (0,0,0), so an avatar drawn without an explicit lighting setup rendered pure black. The
        // ambient (0.35) + head-on key light (0.65) sum to 1.0 on a front-facing surface, so a fully
        // lit part shows at its nominal tint under the FNA-correct SkinnedEffect lighting model.
        Microsoft::Xna::Framework::Vector3 lightColor_{0.65f, 0.65f, 0.65f};
        Microsoft::Xna::Framework::Vector3 lightDirection_{0.0f, 0.0f, -1.0f};
        Microsoft::Xna::Framework::Vector3 ambientLightColor_{0.35f, 0.35f, 0.35f};
        bool isDisposed_{false};

        // --- Real-rendering extension state (NOXNA) ---
        Graphics::GraphicsDevice* realDevice_ = nullptr;
        std::shared_ptr<Graphics::SkinnedModelEXT> realModel_;
        std::unique_ptr<Graphics::SkinnedEffect> realEffect_;
        AvatarAppearanceEXT appearance_;
    };
}
