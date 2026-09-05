// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "System/Exception.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Thrown when errors are encountered during a content pipeline build that are not
     *        problems in the content itself -- a misconfigured component, a missing processor,
     *        a host failure.
     */
    class PipelineException : public System::Exception
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.PipelineException";

        /** @brief Initializes an exception with a generic message. */
        PipelineException();

        /**
         * @brief Initializes an exception with a message.
         *
         * @param message Description of the problem.
         */
        explicit PipelineException(const std::string& message);

        /**
         * @brief Initializes an exception with a message and an inner exception.
         *
         * @param message Description of the problem.
         * @param innerException The exception that caused this one.
         */
        PipelineException(const std::string& message, std::exception_ptr innerException);

        /**
         * @brief Initializes an exception with a composite-format message and its arguments --
         *        the C# `(string message, params object[] messageArgs)` constructor.
         *
         * Both `{0}`-style indexed and `{}`-style sequential placeholders are accepted.
         *
         * @tparam Args Argument types formattable by `std::format`.
         * @param message Format string.
         * @param messageArgs Values substituted into @p message.
         */
        template<typename... Args>
            requires(sizeof...(Args) > 0)
        PipelineException(std::string_view message, Args&&... messageArgs)
            : System::Exception(std::vformat(message, std::make_format_args(messageArgs...)))
        {
        }
    };
}
