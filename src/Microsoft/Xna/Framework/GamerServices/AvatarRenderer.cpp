// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
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

    AvatarRenderer::~AvatarRenderer() = default;

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
        // Task 1.5: every sibling method on this class throws consistently on invalid input
        // (ObjectDisposedException for a disposed instance) - a null animation used to
        // unconditionally dereference here instead, undefined behavior rather than a catchable
        // exception.
        if (animation == nullptr)
        {
            throw System::ArgumentNullException("animation");
        }
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

    void AvatarRenderer::EnableRealRenderingEXT(Graphics::GraphicsDevice& device,
                                                 std::shared_ptr<Graphics::SkinnedModelEXT> model)
    {
        // Task 11.6: unlike DrawRealEXT/Draw/getStateProperty/getBindPoseProperty (which all
        // throw ObjectDisposedException), this used to silently succeed after Dispose() - even
        // re-populating realDevice_/realModel_/realEffect_, effectively "undisposing" the object.
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("AvatarRenderer");
        }
        // Task 1.6: without this, a null/empty model silently succeeded here and only surfaced
        // later, inside DrawRealEXT, as InvalidOperationException("real rendering is disabled") -
        // a poor and misleading contract, since that exception says nothing about the null model
        // actually passed to this call. Reject it at the call site instead, matching the
        // ArgumentNullException convention used elsewhere in this codebase.
        if (model == nullptr)
        {
            throw System::ArgumentNullException("model");
        }
        realDevice_ = &device;
        realModel_ = std::move(model);
        realEffect_ = std::make_unique<Graphics::SkinnedEffect>(device);
    }

    bool AvatarRenderer::IsRealRenderingEnabledEXT() const
    {
        return realModel_ != nullptr;
    }

    void AvatarRenderer::SetAppearanceEXT(const AvatarAppearanceEXT& appearance)
    {
        // Task 11.6: same consistency fix as EnableRealRenderingEXT above.
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("AvatarRenderer");
        }
        appearance_ = appearance;
    }

    Microsoft::Xna::Framework::Color AvatarRenderer::PartTintEXT(const std::string& partName) const
    {
        if (partName.find("Hair") != std::string::npos) { return appearance_.getHairColorProperty(); }
        if (partName.find("Shirt") != std::string::npos) { return appearance_.getShirtColorProperty(); }
        if (partName.find("Pants") != std::string::npos) { return appearance_.getPantsColorProperty(); }
        if (partName.find("Shoes") != std::string::npos) { return appearance_.getShoesColorProperty(); }
        return appearance_.getSkinColorProperty();
    }

    void AvatarRenderer::DrawRealEXT(const std::string& animationClipName,
                                      System::TimeSpan position, bool loop)
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("AvatarRenderer");
        }
        if (!IsRealRenderingEnabledEXT())
        {
            throw System::InvalidOperationException(
                "Real rendering has not been enabled; call EnableRealRenderingEXT first.");
        }

        std::vector<Microsoft::Xna::Framework::Matrix> boneTransforms;
        realModel_->ComputeBoneTransformsEXT(animationClipName, position, loop, boneTransforms);

        realEffect_->setWorldProperty(world_);
        realEffect_->setViewProperty(view_);
        realEffect_->setProjectionProperty(projection_);
        realEffect_->SetBoneTransforms(boneTransforms);
        // REMED-GFX-008: XNA's AvatarRenderer exposes exactly ONE directional light
        // (LightDirection/LightColor) plus AmbientLightColor -- not the three-light BasicEffect rig.
        // Configure DirectionalLight0 as that single key light and DISABLE DirectionalLight1/2 so the
        // two exposed properties genuinely control avatar lighting. Previously this called
        // EnableDefaultLighting() (which turns on all three lights + XNA's dim default ambient) and
        // overrode only Light0, leaving XNA's generic fill/back lights 1 & 2 leaking into every
        // avatar -- an infidelity that was only invisible in tests because those extra lights happen
        // to back-face the front-facing test quads. Ambient is set last (no EnableDefaultLighting to
        // clobber it). Specular is left off: the avatar model exposes only a diffuse key light +
        // ambient, and emissive is unused, so both are zeroed to keep the lit result a clean
        // (ambient + keyLight*N.L) * tint under the FNA-correct SkinnedEffect lighting model.
        realEffect_->getDirectionalLight0Property().setEnabledProperty(true);
        realEffect_->getDirectionalLight0Property().setDirectionProperty(lightDirection_);
        realEffect_->getDirectionalLight0Property().setDiffuseColorProperty(lightColor_);
        realEffect_->getDirectionalLight0Property().setSpecularColorProperty(
            Microsoft::Xna::Framework::Vector3::Zero);
        realEffect_->getDirectionalLight1Property().setEnabledProperty(false);
        realEffect_->getDirectionalLight2Property().setEnabledProperty(false);
        realEffect_->setSpecularColorProperty(Microsoft::Xna::Framework::Vector3::Zero);
        realEffect_->setEmissiveColorProperty(Microsoft::Xna::Framework::Vector3::Zero);
        realEffect_->setAmbientLightColorProperty(ambientLightColor_);

        for (const auto& part : realModel_->Parts)
        {
            realEffect_->setDiffuseColorProperty(PartTintEXT(part.Name).ToVector3());
            realEffect_->setTextureProperty(part.Texture);
            realEffect_->Apply();

            realDevice_->SetVertexBuffer(part.Part->getVertexBufferProperty());
            realDevice_->SetIndexBuffer(part.Part->getIndexBufferProperty());
            realDevice_->DrawIndexedPrimitives(
                Graphics::PrimitiveType::TriangleList,
                part.Part->getVertexOffsetProperty(),
                0,
                part.Part->getNumVerticesProperty(),
                part.Part->getStartIndexProperty(),
                part.Part->getPrimitiveCountProperty());
        }
    }

    void AvatarRenderer::Dispose()
    {
        Dispose(true);
    }

    void AvatarRenderer::Dispose(bool /*disposing*/)
    {
        isDisposed_ = true;
        realEffect_.reset();
        realModel_.reset();
        realDevice_ = nullptr;
    }
}
