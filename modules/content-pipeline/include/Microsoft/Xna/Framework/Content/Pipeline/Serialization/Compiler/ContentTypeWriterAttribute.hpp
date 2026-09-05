// SPDX-License-Identifier: MS-PL
#pragma once

#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "System/Attribute.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler
{
    /**
     * @brief Identifies the components of a content type writer.
     *
     * XNA applies this marker to a `ContentTypeWriter<T>` class so the content compiler can find
     * it by reflection. C++ has no attributes, so the marker is a descriptor object handed to
     * `ContentCompiler::AddTypeWriter<TWriter>()` (docs/xna-content-pipeline-compat-api.md §2).
     */
    class ContentTypeWriterAttribute final : public System::Attribute
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentTypeWriterAttribute";

        /** @brief Initializes the marker. */
        ContentTypeWriterAttribute() = default;
    };
}
