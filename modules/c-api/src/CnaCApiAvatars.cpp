// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAppearanceEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarExpression.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/EventHandler.hpp"
#include "System/TimeSpan.hpp"

#include <any>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::BorrowAnyGamer;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedSkinnedModelValue;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::GamerServices::AvatarAnimation;
using Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset;
using Microsoft::Xna::Framework::GamerServices::AvatarAppearanceEXT;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::AvatarDescription;
using Microsoft::Xna::Framework::GamerServices::AvatarExpression;
using Microsoft::Xna::Framework::GamerServices::AvatarEye;
using Microsoft::Xna::Framework::GamerServices::AvatarEyebrow;
using Microsoft::Xna::Framework::GamerServices::AvatarMouth;
using Microsoft::Xna::Framework::GamerServices::AvatarRenderer;
using Microsoft::Xna::Framework::GamerServices::Gamer;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct AvatarDescriptionResource final {
    std::shared_ptr<AvatarDescription> value;
};

struct AvatarAnimationResource final {
    std::shared_ptr<AvatarAnimation> value;
};

// A renderer holds a raw description pointer, so the handle keeps the description resource alive for
// as long as the renderer draws it.
struct AvatarRendererResource final {
    std::shared_ptr<AvatarRenderer> value;
    std::shared_ptr<AvatarDescriptionResource> description;
    bool realRenderingEnabled = false;
};

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result BorrowDescription(
    const CNA_Handle handle,
    std::shared_ptr<AvatarDescriptionResource>* const outDescription)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::AvatarDescription, outDescription);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned AvatarDescription handle is invalid for this call.");
}

[[nodiscard]] CNA_Result BorrowAnimation(
    const CNA_Handle handle,
    std::shared_ptr<AvatarAnimationResource>* const outAnimation)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::AvatarAnimation, outAnimation);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned AvatarAnimation handle is invalid for this call.");
}

[[nodiscard]] CNA_Result BorrowRenderer(
    const CNA_Handle handle,
    std::shared_ptr<AvatarRendererResource>* const outRenderer)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::AvatarRenderer, outRenderer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned AvatarRenderer handle is invalid for this call.");
}

[[nodiscard]] CNA_Matrix ToC(const Matrix& value) noexcept
{
    CNA_Matrix out;
    out.m11 = value.M11; out.m12 = value.M12; out.m13 = value.M13; out.m14 = value.M14;
    out.m21 = value.M21; out.m22 = value.M22; out.m23 = value.M23; out.m24 = value.M24;
    out.m31 = value.M31; out.m32 = value.M32; out.m33 = value.M33; out.m34 = value.M34;
    out.m41 = value.M41; out.m42 = value.M42; out.m43 = value.M43; out.m44 = value.M44;
    return out;
}

[[nodiscard]] Matrix ToNative(const CNA_Matrix& value) noexcept
{
    return Matrix(
        value.m11, value.m12, value.m13, value.m14,
        value.m21, value.m22, value.m23, value.m24,
        value.m31, value.m32, value.m33, value.m34,
        value.m41, value.m42, value.m43, value.m44);
}

[[nodiscard]] bool IsFinite(const CNA_Vector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] CNA_AvatarExpression ToC(const AvatarExpression& value) noexcept
{
    const CNA_AvatarExpression out = {
        sizeof(CNA_AvatarExpression),
        StructureVersion,
        static_cast<CNA_AvatarMouth>(value.getMouthProperty()),
        static_cast<CNA_AvatarEye>(value.getLeftEyeProperty()),
        static_cast<CNA_AvatarEye>(value.getRightEyeProperty()),
        static_cast<CNA_AvatarEyebrow>(value.getLeftEyebrowProperty()),
        static_cast<CNA_AvatarEyebrow>(value.getRightEyebrowProperty())
    };
    return out;
}

[[nodiscard]] CNA_Result ToNativeExpression(
    const CNA_AvatarExpression* const expression,
    AvatarExpression* const outExpression)
{
    if (expression == nullptr || expression->struct_size < sizeof(CNA_AvatarExpression) ||
        expression->struct_version != StructureVersion) {
        return InvalidInput("The AvatarExpression structure is invalid.");
    }
    if (expression->mouth > CNA_AVATAR_MOUTH_MAXIMUM ||
        expression->left_eye > CNA_AVATAR_EYE_MAXIMUM ||
        expression->right_eye > CNA_AVATAR_EYE_MAXIMUM ||
        expression->left_eyebrow > CNA_AVATAR_EYEBROW_MAXIMUM ||
        expression->right_eyebrow > CNA_AVATAR_EYEBROW_MAXIMUM) {
        return InvalidInput("The AvatarExpression holds an undefined identity.");
    }
    outExpression->setMouthProperty(static_cast<AvatarMouth>(expression->mouth));
    outExpression->setLeftEyeProperty(static_cast<AvatarEye>(expression->left_eye));
    outExpression->setRightEyeProperty(static_cast<AvatarEye>(expression->right_eye));
    outExpression->setLeftEyebrowProperty(static_cast<AvatarEyebrow>(expression->left_eyebrow));
    outExpression->setRightEyebrowProperty(static_cast<AvatarEyebrow>(expression->right_eyebrow));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Color ToC(const Color& value) noexcept
{
    CNA_Color out;
    out.r = value.getRProperty();
    out.g = value.getGProperty();
    out.b = value.getBProperty();
    out.a = value.getAProperty();
    return out;
}

[[nodiscard]] Color ToNative(const CNA_Color value) noexcept
{
    return Color(value.r, value.g, value.b, value.a);
}

[[nodiscard]] CNA_Result PublishDescription(
    AvatarDescription value,
    CNA_Handle* const outDescription)
{
    const auto resource = std::make_shared<AvatarDescriptionResource>();
    resource->value = std::make_shared<AvatarDescription>(std::move(value));
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::AvatarDescription, resource, outDescription);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned AvatarDescription handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_avatar_expression_init(CNA_AvatarExpression* const outExpression)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outExpression == nullptr) {
            return InvalidInput("The AvatarExpression output is null.");
        }
        *outExpression = ToC(AvatarExpression{});
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_appearance_init_ext(CNA_AvatarAppearanceEXT* const outAppearance)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAppearance == nullptr) {
            return InvalidInput("The AvatarAppearance output is null.");
        }
        const AvatarAppearanceEXT appearance;
        const CNA_AvatarAppearanceEXT value = {
            sizeof(CNA_AvatarAppearanceEXT),
            StructureVersion,
            ToC(appearance.getSkinColorProperty()),
            ToC(appearance.getHairColorProperty()),
            ToC(appearance.getShirtColorProperty()),
            ToC(appearance.getPantsColorProperty()),
            ToC(appearance.getShoesColorProperty())
        };
        *outAppearance = value;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_description_create(
    const uint8_t* const description,
    const uint64_t byteCount,
    CNA_AvatarDescriptionHandle* const outDescription)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDescription == nullptr) {
            return InvalidInput("The AvatarDescription output handle is null.");
        }
        *outDescription = CNA_INVALID_HANDLE;
        if (description == nullptr && byteCount != UINT64_C(0)) {
            return InvalidInput("The description byte array is null.");
        }
        const std::vector<SharpRuntime::bytecs> bytes(
            description,
            description + static_cast<std::size_t>(byteCount));
        return PublishDescription(AvatarDescription(bytes), outDescription);
    });
}

CNA_Result cna_avatar_description_create_random(CNA_AvatarDescriptionHandle* const outDescription)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDescription == nullptr) {
            return InvalidInput("The AvatarDescription output handle is null.");
        }
        *outDescription = CNA_INVALID_HANDLE;
        return PublishDescription(AvatarDescription::CreateRandom(), outDescription);
    });
}

CNA_Result cna_avatar_description_create_random_for_body_type(
    const CNA_AvatarBodyType bodyType,
    CNA_AvatarDescriptionHandle* const outDescription)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDescription == nullptr) {
            return InvalidInput("The AvatarDescription output handle is null.");
        }
        *outDescription = CNA_INVALID_HANDLE;
        if (bodyType > CNA_AVATAR_BODY_TYPE_MAXIMUM) {
            return InvalidInput("The avatar body type is not a defined identity.");
        }
        return PublishDescription(
            AvatarDescription::CreateRandom(static_cast<AvatarBodyType>(bodyType)),
            outDescription);
    });
}

CNA_Result cna_avatar_description_get_from_gamer(
    const CNA_GamerHandle gamerHandle,
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_AvatarDescriptionHandle* const outDescription)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDescription == nullptr) {
            return InvalidInput("The AvatarDescription output handle is null.");
        }
        *outDescription = CNA_INVALID_HANDLE;
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowAnyGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical read completes before its own begin route returns, so the operation is
        // released here rather than handed to a caller that has nothing to do with it.
        const std::unique_ptr<System::IAsyncResult> action(
            AvatarDescription::BeginGetFromGamer(gamer, System::AsyncCallback{}, std::any{}));
        if (const CNA_Result result =
                PublishDescription(AvatarDescription::EndGetFromGamer(action.get()), outDescription);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_description_destroy(const CNA_AvatarDescriptionHandle descriptionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvatarDescriptionResource> description;
        if (const CNA_Result result = BorrowDescription(descriptionHandle, &description);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(descriptionHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned AvatarDescription handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_description_get_info(
    const CNA_AvatarDescriptionHandle descriptionHandle,
    CNA_AvatarDescriptionInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_AvatarDescriptionInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidInput("The AvatarDescription info output structure is invalid.");
        }
        std::shared_ptr<AvatarDescriptionResource> description;
        if (const CNA_Result result = BorrowDescription(descriptionHandle, &description);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_AvatarDescriptionInfo info = {
            sizeof(CNA_AvatarDescriptionInfo),
            StructureVersion,
            static_cast<CNA_AvatarBodyType>(description->value->getBodyTypeProperty()),
            description->value->getHeightProperty(),
            static_cast<uint64_t>(description->value->getDescriptionProperty().size()),
            description->value->getIsValidProperty() ? CNA_TRUE : CNA_FALSE,
            {0U, 0U, 0U, 0U, 0U, 0U, 0U}
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_description_copy_description(
    const CNA_AvatarDescriptionHandle descriptionHandle,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The description output buffer is invalid.");
        }
        std::shared_ptr<AvatarDescriptionResource> description;
        if (const CNA_Result result = BorrowDescription(descriptionHandle, &description);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<SharpRuntime::bytecs> bytes = description->value->getDescriptionProperty();
        *outBytes = static_cast<uint64_t>(bytes.size());
        if (capacity < bytes.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the avatar description.");
        }
        if (!bytes.empty()) {
            std::memcpy(destination, bytes.data(), bytes.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_description_subscribe_changed_ext(
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The avatar event callback is null.");
        }
        auto* const source = &AvatarDescription::Changed;
        const auto token = source->Add(
            [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::GamerEventRegistration,
            std::static_pointer_cast<CNA::C::Detail::GamerRegistrationBase>(
                std::make_shared<CNA::C::Detail::GamerRegistration<System::EventArgs>>(
                    source,
                    token)),
            outRegistration);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The avatar registration could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_create(
    const CNA_AvatarAnimationPreset preset,
    CNA_AvatarAnimationHandle* const outAnimation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAnimation == nullptr) {
            return InvalidInput("The AvatarAnimation output handle is null.");
        }
        *outAnimation = CNA_INVALID_HANDLE;
        if (preset > CNA_AVATAR_ANIMATION_PRESET_MAXIMUM) {
            return InvalidInput("The avatar animation preset is not a defined identity.");
        }
        const auto resource = std::make_shared<AvatarAnimationResource>();
        resource->value =
            std::make_shared<AvatarAnimation>(static_cast<AvatarAnimationPreset>(preset));
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::AvatarAnimation, resource, outAnimation);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned AvatarAnimation handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_destroy(const CNA_AvatarAnimationHandle animationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvatarAnimationResource> animation;
        if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        animation->value->Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(animationHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned AvatarAnimation handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_get_info(
    const CNA_AvatarAnimationHandle animationHandle,
    CNA_AvatarAnimationInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_AvatarAnimationInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidInput("The AvatarAnimation info output structure is invalid.");
        }
        std::shared_ptr<AvatarAnimationResource> animation;
        if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_AvatarAnimationInfo info = {
            sizeof(CNA_AvatarAnimationInfo),
            StructureVersion,
            static_cast<int32_t>(animation->value->getBoneTransformsProperty().getCountProperty()),
            animation->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE,
            {0U, 0U, 0U},
            static_cast<int64_t>(animation->value->getCurrentPositionProperty().getTicksProperty()),
            static_cast<int64_t>(animation->value->getLengthProperty().getTicksProperty())
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_set_current_position(
    const CNA_AvatarAnimationHandle animationHandle,
    const int64_t positionTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvatarAnimationResource> animation;
        if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        animation->value->setCurrentPositionProperty(System::TimeSpan(positionTicks));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_get_expression(
    const CNA_AvatarAnimationHandle animationHandle,
    CNA_AvatarExpression* const outExpression)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outExpression == nullptr ||
            outExpression->struct_size < sizeof(CNA_AvatarExpression) ||
            outExpression->struct_version != StructureVersion) {
            return InvalidInput("The AvatarExpression output structure is invalid.");
        }
        std::shared_ptr<AvatarAnimationResource> animation;
        if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outExpression = ToC(animation->value->getExpressionProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_update(
    const CNA_AvatarAnimationHandle animationHandle,
    const int64_t elapsedTicks,
    const CNA_Bool loop)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (loop != CNA_FALSE && loop != CNA_TRUE) {
            return InvalidInput("The animation loop flag is invalid.");
        }
        std::shared_ptr<AvatarAnimationResource> animation;
        if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        animation->value->Update(System::TimeSpan(elapsedTicks), loop == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_get_bone_transform_at(
    const CNA_AvatarAnimationHandle animationHandle,
    const int32_t index,
    CNA_Matrix* const outTransform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTransform == nullptr) {
            return InvalidInput("The bone transform output is null.");
        }
        std::shared_ptr<AvatarAnimationResource> animation;
        if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto transforms = animation->value->getBoneTransformsProperty();
        if (index < 0 || index >= transforms.getCountProperty()) {
            return InvalidInput("The bone index is outside the animation's skeleton.");
        }
        *outTransform = ToC(transforms[static_cast<int>(index)]);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_get_real_clip_name_size_ext(
    const CNA_AvatarAnimationHandle animationHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The clip-name size output is null.");
        }
        std::shared_ptr<AvatarAnimationResource> animation;
        if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = animation->value->GetRealClipNameEXT().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_copy_real_clip_name_ext(
    const CNA_AvatarAnimationHandle animationHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The clip-name output buffer is invalid.");
        }
        std::shared_ptr<AvatarAnimationResource> animation;
        if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::string& name = animation->value->GetRealClipNameEXT();
        *outBytes = name.size();
        if (capacity < name.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the clip name.");
        }
        if (!name.empty()) {
            std::memcpy(destination, name.data(), name.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_animation_set_real_clip_name_ext(
    const CNA_AvatarAnimationHandle animationHandle,
    const CNA_StringView clipName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (clipName.data == nullptr && clipName.byte_length != UINT64_C(0)) {
            return InvalidInput("The clip name is invalid.");
        }
        std::shared_ptr<AvatarAnimationResource> animation;
        if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        animation->value->SetRealClipNameEXT(std::string(
            clipName.data == nullptr ? "" : clipName.data,
            static_cast<std::size_t>(clipName.byte_length)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_create(
    const CNA_AvatarDescriptionHandle descriptionHandle,
    const CNA_Bool useLoadingEffect,
    CNA_AvatarRendererHandle* const outRenderer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRenderer == nullptr) {
            return InvalidInput("The AvatarRenderer output handle is null.");
        }
        *outRenderer = CNA_INVALID_HANDLE;
        if (useLoadingEffect != CNA_FALSE && useLoadingEffect != CNA_TRUE) {
            return InvalidInput("The loading-effect flag is invalid.");
        }
        std::shared_ptr<AvatarDescriptionResource> description;
        if (const CNA_Result result = BorrowDescription(descriptionHandle, &description);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<AvatarRendererResource>();
        resource->description = description;
        resource->value = std::make_shared<AvatarRenderer>(
            description->value.get(),
            useLoadingEffect == CNA_TRUE);
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::AvatarRenderer, resource, outRenderer);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned AvatarRenderer handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_destroy(const CNA_AvatarRendererHandle rendererHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        renderer->value->Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(rendererHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned AvatarRenderer handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_get_info(
    const CNA_AvatarRendererHandle rendererHandle,
    CNA_AvatarRendererInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_AvatarRendererInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidInput("The AvatarRenderer info output structure is invalid.");
        }
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_AvatarRendererInfo info = {
            sizeof(CNA_AvatarRendererInfo),
            StructureVersion,
            static_cast<CNA_AvatarRendererState>(renderer->value->getStateProperty()),
            renderer->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE,
            renderer->value->IsRealRenderingEnabledEXT() ? CNA_TRUE : CNA_FALSE,
            {0U, 0U}
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_get_transforms(
    const CNA_AvatarRendererHandle rendererHandle,
    CNA_Matrix* const outWorld,
    CNA_Matrix* const outView,
    CNA_Matrix* const outProjection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outWorld != nullptr) {
            *outWorld = ToC(renderer->value->getWorldProperty());
        }
        if (outView != nullptr) {
            *outView = ToC(renderer->value->getViewProperty());
        }
        if (outProjection != nullptr) {
            *outProjection = ToC(renderer->value->getProjectionProperty());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_set_transforms(
    const CNA_AvatarRendererHandle rendererHandle,
    const CNA_Matrix* const world,
    const CNA_Matrix* const view,
    const CNA_Matrix* const projection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (world != nullptr) {
            renderer->value->setWorldProperty(ToNative(*world));
        }
        if (view != nullptr) {
            renderer->value->setViewProperty(ToNative(*view));
        }
        if (projection != nullptr) {
            renderer->value->setProjectionProperty(ToNative(*projection));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_get_lighting(
    const CNA_AvatarRendererHandle rendererHandle,
    CNA_Vector3* const outLightColor,
    CNA_Vector3* const outLightDirection,
    CNA_Vector3* const outAmbientLightColor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (outLightColor != nullptr) {
            const Vector3 value = renderer->value->getLightColorProperty();
            *outLightColor = CNA_Vector3{value.X, value.Y, value.Z};
        }
        if (outLightDirection != nullptr) {
            const Vector3 value = renderer->value->getLightDirectionProperty();
            *outLightDirection = CNA_Vector3{value.X, value.Y, value.Z};
        }
        if (outAmbientLightColor != nullptr) {
            const Vector3 value = renderer->value->getAmbientLightColorProperty();
            *outAmbientLightColor = CNA_Vector3{value.X, value.Y, value.Z};
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_set_lighting(
    const CNA_AvatarRendererHandle rendererHandle,
    const CNA_Vector3* const lightColor,
    const CNA_Vector3* const lightDirection,
    const CNA_Vector3* const ambientLightColor)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if ((lightColor != nullptr && !IsFinite(*lightColor)) ||
            (lightDirection != nullptr && !IsFinite(*lightDirection)) ||
            (ambientLightColor != nullptr && !IsFinite(*ambientLightColor))) {
            return InvalidInput("The lighting vectors must be finite.");
        }
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (lightColor != nullptr) {
            renderer->value->setLightColorProperty(
                Vector3{lightColor->x, lightColor->y, lightColor->z});
        }
        if (lightDirection != nullptr) {
            renderer->value->setLightDirectionProperty(
                Vector3{lightDirection->x, lightDirection->y, lightDirection->z});
        }
        if (ambientLightColor != nullptr) {
            renderer->value->setAmbientLightColorProperty(
                Vector3{ambientLightColor->x, ambientLightColor->y, ambientLightColor->z});
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_get_parent_bone_at(
    const CNA_AvatarRendererHandle rendererHandle,
    const int32_t index,
    int32_t* const outParentIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outParentIndex == nullptr) {
            return InvalidInput("The parent-bone output is null.");
        }
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto parents = renderer->value->getParentBonesProperty();
        if (index < 0 || index >= parents.getCountProperty()) {
            return InvalidInput("The bone index is outside the avatar skeleton.");
        }
        *outParentIndex = static_cast<int32_t>(parents[static_cast<int>(index)]);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_get_bind_pose_at(
    const CNA_AvatarRendererHandle rendererHandle,
    const int32_t index,
    CNA_Matrix* const outTransform)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTransform == nullptr) {
            return InvalidInput("The bind-pose output is null.");
        }
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto bindPose = renderer->value->getBindPoseProperty();
        if (index < 0 || index >= bindPose.getCountProperty()) {
            return InvalidInput("The bone index is outside the avatar skeleton.");
        }
        *outTransform = ToC(bindPose[static_cast<int>(index)]);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_draw_animation(
    const CNA_AvatarRendererHandle rendererHandle,
    const CNA_AvatarAnimationHandle animationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvatarRendererResource> renderer;
        std::shared_ptr<AvatarAnimationResource> animation;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical parameter is the animation interface, and this ABI's animation handle is the
        // only thing that implements it -- a C caller cannot supply an implementation of its own.
        renderer->value->Draw(animation->value.get());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_draw_bones(
    const CNA_AvatarRendererHandle rendererHandle,
    const CNA_Matrix* const bones,
    const uint64_t boneCount,
    const CNA_AvatarExpression* const expression)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (bones == nullptr && boneCount != UINT64_C(0)) {
            return InvalidInput("The bone array is null.");
        }
        AvatarExpression nativeExpression;
        if (const CNA_Result result = ToNativeExpression(expression, &nativeExpression);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Matrix> nativeBones;
        nativeBones.reserve(static_cast<std::size_t>(boneCount));
        for (uint64_t index = UINT64_C(0); index < boneCount; ++index) {
            nativeBones.push_back(ToNative(bones[index]));
        }
        renderer->value->Draw(nativeBones, nativeExpression);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_enable_real_rendering_ext(
    const CNA_AvatarRendererHandle rendererHandle,
    const CNA_Handle deviceHandle,
    const CNA_Handle modelHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> device;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(deviceHandle, &device);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<Microsoft::Xna::Framework::Graphics::SkinnedModelEXT> model;
        if (const CNA_Result result = GetOwnedSkinnedModelValue(modelHandle, &model);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        renderer->value->EnableRealRenderingEXT(*device->value, std::move(model));
        renderer->realRenderingEnabled = true;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_set_appearance_ext(
    const CNA_AvatarRendererHandle rendererHandle,
    const CNA_AvatarAppearanceEXT* const appearance)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (appearance == nullptr || appearance->struct_size < sizeof(CNA_AvatarAppearanceEXT) ||
            appearance->struct_version != StructureVersion) {
            return InvalidInput("The AvatarAppearance structure is invalid.");
        }
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        AvatarAppearanceEXT native;
        native.setSkinColorProperty(ToNative(appearance->skin_color));
        native.setHairColorProperty(ToNative(appearance->hair_color));
        native.setShirtColorProperty(ToNative(appearance->shirt_color));
        native.setPantsColorProperty(ToNative(appearance->pants_color));
        native.setShoesColorProperty(ToNative(appearance->shoes_color));
        renderer->value->SetAppearanceEXT(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_avatar_renderer_draw_real_ext(
    const CNA_AvatarRendererHandle rendererHandle,
    const CNA_StringView animationClipName,
    const int64_t positionTicks,
    const CNA_Bool loop)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (animationClipName.data == nullptr && animationClipName.byte_length != UINT64_C(0)) {
            return InvalidInput("The animation clip name is invalid.");
        }
        if (loop != CNA_FALSE && loop != CNA_TRUE) {
            return InvalidInput("The animation loop flag is invalid.");
        }
        std::shared_ptr<AvatarRendererResource> renderer;
        if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        renderer->value->DrawRealEXT(
            std::string(
                animationClipName.data == nullptr ? "" : animationClipName.data,
                static_cast<std::size_t>(animationClipName.byte_length)),
            System::TimeSpan(positionTicks),
            loop == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}
