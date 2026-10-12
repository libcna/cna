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
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToNative;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

[[nodiscard]] CNA_Result QuaternionConjugateInPlace(CNA_Quaternion* const value)
{
    if (value == nullptr) {
        return InvalidArgument("The Quaternion is null.");
    }
    Quaternion native = ToNative(*value);
    native.Conjugate();
    *value = ToC(native);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result QuaternionNormalizeInPlace(CNA_Quaternion* const value)
{
    if (value == nullptr) {
        return InvalidArgument("The Quaternion is null.");
    }
    Quaternion native = ToNative(*value);
    native.Normalize();
    *value = ToC(native);
    return CNA_RESULT_SUCCESS;
}

} // namespace

// The routes generated from modules/c-api/spec/Quaternion.toml.
#include "generated/Quaternion.inc"
