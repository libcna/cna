// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <typeinfo>

namespace Microsoft::Xna::Framework::Content { class ContentReader; }

namespace CNA::Internal::Xnb
{
    // Native diagnostic spelling: primitive XNA names, then a registered canonical target,
    // then RTTI for a native type with no managed-name association. Never construct a reader.
    [[nodiscard]] std::string ContentReaderTypeName(const std::type_info& type);

    [[noreturn]] void ThrowContentReaderTypeMismatch(
        const Microsoft::Xna::Framework::Content::ContentReader& input,
        const std::string& contained, const std::string& requested);
}
