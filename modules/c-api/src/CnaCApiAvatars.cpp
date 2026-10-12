// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarExpression.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
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
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::CreateResourceHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::IsFinite;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::ValidateVersionedStructure;

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::GamerServices::AvatarAnimation;
using Microsoft::Xna::Framework::GamerServices::AvatarAnimationPreset;
using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
using Microsoft::Xna::Framework::GamerServices::AvatarDescription;
using Microsoft::Xna::Framework::GamerServices::AvatarExpression;
using Microsoft::Xna::Framework::GamerServices::AvatarEye;
using Microsoft::Xna::Framework::GamerServices::AvatarEyebrow;
using Microsoft::Xna::Framework::GamerServices::AvatarMouth;
using Microsoft::Xna::Framework::GamerServices::AvatarRenderer;
using Microsoft::Xna::Framework::GamerServices::Gamer;

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
};

[[nodiscard]] CNA_Result BorrowDescription(
    const CNA_Handle handle,
    std::shared_ptr<AvatarDescriptionResource>* const outDescription)
{
    return LookupHandle(handle, ObjectKind::AvatarDescription, outDescription);
}

[[nodiscard]] CNA_Result BorrowAnimation(
    const CNA_Handle handle,
    std::shared_ptr<AvatarAnimationResource>* const outAnimation)
{
    return LookupHandle(handle, ObjectKind::AvatarAnimation, outAnimation);
}

[[nodiscard]] CNA_Result BorrowRenderer(
    const CNA_Handle handle,
    std::shared_ptr<AvatarRendererResource>* const outRenderer)
{
    return LookupHandle(handle, ObjectKind::AvatarRenderer, outRenderer);
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
    if (const CNA_Result result =
            ValidateVersionedStructure(expression, "The AvatarExpression structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (expression->mouth > CNA_AVATAR_MOUTH_MAXIMUM ||
        expression->left_eye > CNA_AVATAR_EYE_MAXIMUM ||
        expression->right_eye > CNA_AVATAR_EYE_MAXIMUM ||
        expression->left_eyebrow > CNA_AVATAR_EYEBROW_MAXIMUM ||
        expression->right_eyebrow > CNA_AVATAR_EYEBROW_MAXIMUM) {
        return InvalidArgument("The AvatarExpression holds an undefined identity.");
    }
    outExpression->setMouthProperty(static_cast<AvatarMouth>(expression->mouth));
    outExpression->setLeftEyeProperty(static_cast<AvatarEye>(expression->left_eye));
    outExpression->setRightEyeProperty(static_cast<AvatarEye>(expression->right_eye));
    outExpression->setLeftEyebrowProperty(static_cast<AvatarEyebrow>(expression->left_eyebrow));
    outExpression->setRightEyebrowProperty(static_cast<AvatarEyebrow>(expression->right_eyebrow));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PublishDescription(
    AvatarDescription value,
    CNA_Handle* const outDescription)
{
    return CreateResourceHandle<AvatarDescriptionResource>(
        ObjectKind::AvatarDescription,
        std::make_shared<AvatarDescription>(std::move(value)),
        outDescription);
}

[[nodiscard]] CNA_Result AvatarDescriptionCreate(
    const uint8_t* const description,
    const uint64_t byteCount,
    CNA_AvatarDescriptionHandle* const outDescription)
{
    if (description == nullptr && byteCount != UINT64_C(0)) {
        return InvalidArgument("The description byte array is null.");
    }
    const std::vector<SharpRuntime::bytecs> bytes(
        description,
        description + static_cast<std::size_t>(byteCount));
    return PublishDescription(AvatarDescription(bytes), outDescription);
}

[[nodiscard]] CNA_Result AvatarDescriptionGetFromGamer(
    const CNA_GamerHandle gamerHandle,
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_AvatarDescriptionHandle* const outDescription)
{
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
}

[[nodiscard]] CNA_Result AvatarDescriptionSubscribeChangedExt(
    const CNA_AvatarDescriptionHandle descriptionHandle,
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    if (callback == nullptr) {
        return InvalidArgument("The avatar event callback is null.");
    }
    // AvatarDescription.Changed is an instance event, so the subscription names one
    // description rather than a process-wide source.
    std::shared_ptr<AvatarDescriptionResource> description;
    if (const CNA_Result result = BorrowDescription(descriptionHandle, &description);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const source = &description->value->Changed;
    const auto token = source->Add(
        [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
    return CreateHandle(ObjectKind::GamerEventRegistration,
        std::static_pointer_cast<CNA::C::Detail::GamerRegistrationBase>(
            std::make_shared<CNA::C::Detail::GamerRegistration<System::EventArgs>>(
                source,
                token)),
        outRegistration);
}

[[nodiscard]] CNA_Result AvatarAnimationGetBoneTransformAt(
    const CNA_AvatarAnimationHandle animationHandle,
    const int32_t index,
    CNA_Matrix* const outTransform)
{
    std::shared_ptr<AvatarAnimationResource> animation;
    if (const CNA_Result result = BorrowAnimation(animationHandle, &animation);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto transforms = animation->value->getBoneTransformsProperty();
    if (index < 0 || index >= transforms.getCountProperty()) {
        return InvalidArgument("The bone index is outside the animation's skeleton.");
    }
    *outTransform = ToC(transforms[static_cast<int>(index)]);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AvatarRendererCreate(
    const CNA_AvatarDescriptionHandle descriptionHandle,
    const CNA_Bool useLoadingEffect,
    CNA_AvatarRendererHandle* const outRenderer)
{
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
    return CreateHandle(ObjectKind::AvatarRenderer, resource, outRenderer);
}

[[nodiscard]] CNA_Result AvatarRendererGetTransforms(
    const CNA_AvatarRendererHandle rendererHandle,
    CNA_Matrix* const outWorld,
    CNA_Matrix* const outView,
    CNA_Matrix* const outProjection)
{
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
}

[[nodiscard]] CNA_Result AvatarRendererSetTransforms(
    const CNA_AvatarRendererHandle rendererHandle,
    const CNA_Matrix* const world,
    const CNA_Matrix* const view,
    const CNA_Matrix* const projection)
{
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
}

[[nodiscard]] CNA_Result AvatarRendererGetLighting(
    const CNA_AvatarRendererHandle rendererHandle,
    CNA_Vector3* const outLightColor,
    CNA_Vector3* const outLightDirection,
    CNA_Vector3* const outAmbientLightColor)
{
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
}

[[nodiscard]] CNA_Result AvatarRendererSetLighting(
    const CNA_AvatarRendererHandle rendererHandle,
    const CNA_Vector3* const lightColor,
    const CNA_Vector3* const lightDirection,
    const CNA_Vector3* const ambientLightColor)
{
    if ((lightColor != nullptr && !IsFinite(*lightColor)) ||
        (lightDirection != nullptr && !IsFinite(*lightDirection)) ||
        (ambientLightColor != nullptr && !IsFinite(*ambientLightColor))) {
        return InvalidArgument("The lighting vectors must be finite.");
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
}

[[nodiscard]] CNA_Result AvatarRendererGetParentBoneAt(
    const CNA_AvatarRendererHandle rendererHandle,
    const int32_t index,
    int32_t* const outParentIndex)
{
    std::shared_ptr<AvatarRendererResource> renderer;
    if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto parents = renderer->value->getParentBonesProperty();
    if (index < 0 || index >= parents.getCountProperty()) {
        return InvalidArgument("The bone index is outside the avatar skeleton.");
    }
    *outParentIndex = static_cast<int32_t>(parents[static_cast<int>(index)]);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AvatarRendererGetBindPoseAt(
    const CNA_AvatarRendererHandle rendererHandle,
    const int32_t index,
    CNA_Matrix* const outTransform)
{
    std::shared_ptr<AvatarRendererResource> renderer;
    if (const CNA_Result result = BorrowRenderer(rendererHandle, &renderer);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto bindPose = renderer->value->getBindPoseProperty();
    if (index < 0 || index >= bindPose.getCountProperty()) {
        return InvalidArgument("The bone index is outside the avatar skeleton.");
    }
    *outTransform = ToC(bindPose[static_cast<int>(index)]);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AvatarRendererDrawBones(
    const CNA_AvatarRendererHandle rendererHandle,
    const CNA_Matrix* const bones,
    const uint64_t boneCount,
    const CNA_AvatarExpression* const expression)
{
    if (bones == nullptr && boneCount != UINT64_C(0)) {
        return InvalidArgument("The bone array is null.");
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
}

} // namespace

// The routes generated from modules/c-api/spec/Avatars.toml.
#include "generated/Avatars.inc"
