// SPDX-License-Identifier: MS-PL

#include "CNA/C/quaternion.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstring>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToNative;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

} // namespace

CNA_Result cna_quaternion_conjugate_in_place(CNA_Quaternion* const value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Quaternion is null.");
        }
        Quaternion native = ToNative(*value);
        native.Conjugate();
        *value = ToC(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_quaternion_normalize_in_place(CNA_Quaternion* const value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Quaternion is null.");
        }
        Quaternion native = ToNative(*value);
        native.Normalize();
        *value = ToC(native);
        return CNA_RESULT_SUCCESS;
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/Quaternion.toml.
#include "generated/Quaternion.inc"
