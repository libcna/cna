// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief Passes content through the build unchanged, which is what a game asks for when its
     *        importer already produced the object the runtime wants.
     */
    class PassThroughProcessor : public ContentProcessor<System::Object, System::Object>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.PassThroughProcessor";

        /** @brief Initializes a new instance of PassThroughProcessor. */
        PassThroughProcessor() = default;

        /**
         * @brief Returns the input unchanged.
         *
         * @param input The content to pass through.
         * @param context The processor context, unused.
         * @return The input.
         */
        [[nodiscard]] ContentObject Process(const ContentObject& input, ContentProcessorContext& context) override;

        /**
         * @brief Returns the type's stable name.
         *
         * @return The .NET full name of this processor.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;
    };
}
