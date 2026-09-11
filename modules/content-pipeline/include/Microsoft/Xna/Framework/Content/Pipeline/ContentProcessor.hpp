// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <type_traits>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/IContentProcessor.hpp"
#include "System/Object.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Provides a base class to use when developing custom processor components. All
     *        processors must derive from this class.
     *
     * A reference-typed @p TInput arrives as `std::shared_ptr<TInput>` and may be mutated in
     * place, exactly as an XNA processor mutates the object graph it is handed; a value type
     * arrives by value. `System::Object` on either side means the untyped @ref ContentObject,
     * which is how `PassThroughProcessor` is declared.
     *
     * Configurable properties are declared through `ProcessorParameterBindings` (see
     * `ProcessorParameter.hpp`) so the host can set them from a `.contentproj` and the component
     * scanner can list them without reflection.
     *
     * @tparam TInput Type of the input content.
     * @tparam TOutput Type of the output content.
     */
    template<typename TInput, typename TOutput>
    class ContentProcessor : public IContentProcessor
    {
    public:
        /** @brief The carrier `Process` receives. */
        using InputCarrier = std::conditional_t<std::is_same_v<TInput, System::Object>, ContentObject, Carrier<TInput>>;

        /** @brief The carrier `Process` returns. */
        using OutputCarrier = std::conditional_t<std::is_same_v<TOutput, System::Object>, ContentObject, Carrier<TOutput>>;

        /**
         * @brief Processes the specified input data and returns the result.
         *
         * @param input Existing content object being processed.
         * @param context Contains any required custom process parameters.
         * @return A single object representing the processed input.
         */
        [[nodiscard]] virtual OutputCarrier Process(const InputCarrier& input,
                                                    ContentProcessorContext& context) = 0;

        /** @brief Gets the expected object type of the input parameter to `Process`. */
        [[nodiscard]] System::Type getInputTypeProperty() const override
        {
            return System::Type::From<TInput>();
        }

        /** @brief Gets the object type returned by `Process`. */
        [[nodiscard]] System::Type getOutputTypeProperty() const override
        {
            return System::Type::From<TOutput>();
        }

        /** @brief Gets the .NET full name of the input type. */
        [[nodiscard]] std::string getInputTypeNameProperty() const override
        {
            return ContentTypeName<TInput>::Name();
        }

        /** @brief Gets the .NET full name of the output type. */
        [[nodiscard]] std::string getOutputTypeNameProperty() const override
        {
            return ContentTypeName<TOutput>::Name();
        }

    protected:
        /** @brief Initializes a processor. */
        ContentProcessor() = default;

        /**
         * @brief Unboxes the input, runs the typed `Process`, and boxes the result.
         *
         * @param input Existing content object being processed, boxed.
         * @param context Contains any required custom process parameters.
         * @return The processed content, boxed under `ContentTypeName<TOutput>::Name()`.
         */
        [[nodiscard]] ContentObject ProcessObject(const ContentObject& input,
                                                  ContentProcessorContext& context) override
        {
            if constexpr (std::is_same_v<TInput, System::Object>)
            {
                if constexpr (std::is_same_v<TOutput, System::Object>) { return Process(input, context); }
                else { return Box<TOutput>(Process(input, context)); }
            }
            else
            {
                if constexpr (std::is_same_v<TOutput, System::Object>)
                {
                    return Process(Unbox<TInput>(input), context);
                }
                else
                {
                    return Box<TOutput>(Process(Unbox<TInput>(input), context));
                }
            }
        }
    };
}
