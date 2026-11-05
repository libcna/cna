// SPDX-License-Identifier: MS-PL
#pragma once

#include <gtest/gtest.h>
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/detail/FrameworkCompatibility.hpp"
#include <exception>
#include <string>
#include <type_traits>
#include <typeinfo>

namespace XactContract
{
    // Literal strings from the English Refresh FrameworkResources stream, not CNA output.
    inline constexpr const char* NullText = "This method does not accept null for this parameter.";
    inline constexpr const char* EngineText = "You must pass in a valid audio engine.";
    inline constexpr const char* HeaderText = "XACT could not load the data provided. Make sure you are using the correct version of the XACT tool.";
    inline constexpr const char* ResourceText = "This resource could not be created.";
    inline constexpr const char* VariableText = "The specified variable index is invalid.";
    inline constexpr const char* ArgumentText = "Value does not fall within the expected range.";

    inline std::string CueText(const std::string& name)
    {
        return "An error occurred trying to play the cue named \"" + name + "\". Is the cue name correct?";
    }

    template<class Expected, class Call>
    void Refuses(Call&& call, const std::string& message, const std::string& parameter = {})
    {
        try { call(); ADD_FAILURE() << "Expected " << typeid(Expected).name(); }
        catch (const Expected& error)
        {
            EXPECT_EQ(typeid(error), typeid(Expected));
            EXPECT_EQ(error.getMessageProperty(), message);
            if constexpr (std::is_base_of_v<System::ArgumentException, Expected>)
                EXPECT_EQ(error.getParamNameProperty(), parameter);
        }
        catch (const std::exception& error) { ADD_FAILURE() << "Wrong exception: " << error.what(); }
    }

    template<class Call>
    void Null(Call&& call, const std::string& parameter, const char* text = NullText)
    {
        // SharpRuntime's documented host switch controls only the parameter suffix.
        // Both suffixes are literal .NET formatting contracts; no exception formats its oracle.
        const std::string suffix = System::detail::UseNetFrameworkArgumentExceptionMessages()
            ? "\nParameter name: " + parameter : " (Parameter '" + parameter + "')";
        Refuses<System::ArgumentNullException>(call, std::string(text) + suffix, parameter);
    }
}
