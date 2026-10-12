// SPDX-License-Identifier: MS-PL

#include "CNA/C/matrix.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::StoreOutput;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Plane;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

[[nodiscard]] std::optional<Vector3> ToNativeOptional(const CNA_Vector3* const value)
{
    if (value == nullptr) {
        return std::nullopt;
    }
    return ToNative(*value);
}

template<typename TCallable>
[[nodiscard]] CNA_Result MutateMatrix(
    CNA_Matrix* const value,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return InvalidArgument("The Matrix is null.");
        }
        Matrix native = ToNative(*value);
        std::forward<TCallable>(callable)(native);
        *value = ToC(native);
        return CNA_RESULT_SUCCESS;
    });
}

[[nodiscard]] CNA_Result MatrixSetBackward(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setBackwardProperty(ToNative(direction));
    });
}

[[nodiscard]] CNA_Result MatrixSetDown(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setDownProperty(ToNative(direction));
    });
}

[[nodiscard]] CNA_Result MatrixSetForward(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setForwardProperty(ToNative(direction));
    });
}

[[nodiscard]] CNA_Result MatrixSetLeft(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setLeftProperty(ToNative(direction));
    });
}

[[nodiscard]] CNA_Result MatrixSetRight(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setRightProperty(ToNative(direction));
    });
}

[[nodiscard]] CNA_Result MatrixSetTranslation(
    CNA_Matrix* const value,
    const CNA_Vector3 translation)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setTranslationProperty(ToNative(translation));
    });
}

[[nodiscard]] CNA_Result MatrixSetUp(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setUpProperty(ToNative(direction));
    });
}

[[nodiscard]] CNA_Result MatrixDecompose(
    const CNA_Matrix value,
    CNA_Vector3* const outScale,
    CNA_Quaternion* const outRotation,
    CNA_Vector3* const outTranslation,
    CNA_Bool* const outDecomposed)
{
    Vector3 scale;
    Quaternion rotation = Quaternion::Identity;
    Vector3 translation;
    const bool decomposed = ToNative(value).Decompose(scale, rotation, translation);
    *outScale = ToC(scale);
    *outRotation = ToC(rotation);
    *outTranslation = ToC(translation);
    *outDecomposed = ToCBool(decomposed);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result MatrixCreateBillboard(
    const CNA_Vector3 objectPosition,
    const CNA_Vector3 cameraPosition,
    const CNA_Vector3 cameraUp,
    const CNA_Vector3* const cameraForward,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateBillboard(
            ToNative(objectPosition),
            ToNative(cameraPosition),
            ToNative(cameraUp),
            ToNativeOptional(cameraForward)));
    });
}

[[nodiscard]] CNA_Result MatrixCreateConstrainedBillboard(
    const CNA_Vector3 objectPosition,
    const CNA_Vector3 cameraPosition,
    const CNA_Vector3 rotateAxis,
    const CNA_Vector3* const cameraForward,
    const CNA_Vector3* const objectForward,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateConstrainedBillboard(
            ToNative(objectPosition),
            ToNative(cameraPosition),
            ToNative(rotateAxis),
            ToNativeOptional(cameraForward),
            ToNativeOptional(objectForward)));
    });
}

} // namespace

// The routes generated from modules/c-api/spec/Matrix.toml.
#include "generated/Matrix.inc"
