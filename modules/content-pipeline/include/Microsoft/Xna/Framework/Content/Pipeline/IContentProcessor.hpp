// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    class ContentProcessorContext;

    /**
     * @brief Provides methods and properties for accessing a statically typed
     *        `ContentProcessor<TInput,TOutput>` through a common interface that works with
     *        untyped `object` payloads.
     *
     * `Process` here is non-virtual for the reason `IContentImporter::Import` is: it is the C++
     * spelling of the explicit interface implementation, and `ContentProcessor<TInput,TOutput>::Process`
     * is the typed virtual a component overrides.
     */
    class IContentProcessor
    {
    public:
        /** @brief Destroys the processor. */
        virtual ~IContentProcessor() = default;

        /**
         * @brief Gets the expected object type of the input parameter to `Process`.
         *
         * @return The input type.
         */
        [[nodiscard]] virtual System::Type getInputTypeProperty() const = 0;

        /**
         * @brief Gets the object type returned by `Process`.
         *
         * @return The output type.
         */
        [[nodiscard]] virtual System::Type getOutputTypeProperty() const = 0;

        /**
         * @brief Gets the .NET full name of the input type -- what the registry selects on.
         *
         * @return The stable input type name.
         */
        CNAEXT [[nodiscard]] virtual std::string getInputTypeNameProperty() const = 0;

        /**
         * @brief Gets the .NET full name of the output type.
         *
         * @return The stable output type name.
         */
        CNAEXT [[nodiscard]] virtual std::string getOutputTypeNameProperty() const = 0;

        /**
         * @brief Processes the specified input data and returns the result.
         *
         * @param input Existing content object being processed, boxed.
         * @param context Contains any required custom process parameters.
         * @return The processed content, boxed under its pipeline type name.
         */
        [[nodiscard]] ContentObject Process(const ContentObject& input, ContentProcessorContext& context)
        {
            return ProcessObject(input, context);
        }

    protected:
        /**
         * @brief The customization point behind the interface's `Process`;
         *        `ContentProcessor<TInput,TOutput>` implements it by unboxing and boxing.
         *
         * @param input Existing content object being processed, boxed.
         * @param context Contains any required custom process parameters.
         * @return The processed content, boxed under its pipeline type name.
         */
        CNAEXT [[nodiscard]] virtual ContentObject ProcessObject(const ContentObject& input,
                                                                 ContentProcessorContext& context) = 0;
    };
}
