// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ProcessorParameter.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/FormatException.hpp"
#include "System/InvalidCastException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    ProcessorParameter::ProcessorParameter(std::string propertyName, std::string propertyType,
                                           std::string displayName, std::string description,
                                           ContentObject defaultValue,
                                           std::vector<std::string> possibleEnumValues)
        : propertyName_(std::move(propertyName))
        , propertyType_(std::move(propertyType))
        , displayName_(std::move(displayName))
        , description_(std::move(description))
        , defaultValue_(std::move(defaultValue))
        , possibleEnumValues_(std::move(possibleEnumValues))
    {
        if (displayName_.empty()) { displayName_ = propertyName_; }
    }

    const ContentObject& ProcessorParameter::getDefaultValueProperty() const noexcept { return defaultValue_; }
    const std::string& ProcessorParameter::getDescriptionProperty() const noexcept { return description_; }
    const std::string& ProcessorParameter::getDisplayNameProperty() const noexcept { return displayName_; }
    bool ProcessorParameter::getIsEnumProperty() const noexcept { return !possibleEnumValues_.empty(); }

    System::Collections::ObjectModel::ReadOnlyCollection<std::string>
    ProcessorParameter::getPossibleEnumValuesProperty() const
    {
        return System::Collections::ObjectModel::ReadOnlyCollection<std::string>(possibleEnumValues_);
    }

    const std::string& ProcessorParameter::getPropertyNameProperty() const noexcept { return propertyName_; }
    const std::string& ProcessorParameter::getPropertyTypeProperty() const noexcept { return propertyType_; }

    bool ProcessorParameter::operator==(const ProcessorParameter& other) const noexcept
    {
        return propertyName_ == other.propertyName_ && propertyType_ == other.propertyType_ &&
               displayName_ == other.displayName_ && description_ == other.description_ &&
               possibleEnumValues_ == other.possibleEnumValues_;
    }

    ProcessorParameterCollection::ProcessorParameterCollection(std::vector<ProcessorParameter> parameters)
        : System::Collections::ObjectModel::ReadOnlyCollection<ProcessorParameter>(std::move(parameters))
    {
    }

    const ProcessorParameter* ProcessorParameterCollection::Find(const std::string& propertyName) const noexcept
    {
        for (SharpRuntime::intcs i = 0; i < getCountProperty(); ++i)
        {
            const ProcessorParameter& parameter = (*this)[i];
            if (parameter.getPropertyNameProperty() == propertyName) { return &parameter; }
        }
        return nullptr;
    }

    namespace
    {
        std::string Trim(std::string text)
        {
            const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
            text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
            return text;
        }

        std::string Lower(std::string text)
        {
            for (char& c : text) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            return text;
        }

        [[noreturn]] void ThrowFormat(const std::string& text, const char* type)
        {
            throw System::FormatException("'" + text + "' is not a valid " + type + " processor parameter value.");
        }

        template<typename T>
        T ParseInteger(const std::string& raw, const char* type)
        {
            const std::string text = Trim(raw);
            T value{};
            const char* begin = text.data();
            const char* end = begin + text.size();
            if (!text.empty() && text.front() == '+') { ++begin; }
            const auto result = std::from_chars(begin, end, value, 10);
            if (result.ec != std::errc{} || result.ptr != end || text.empty()) { ThrowFormat(raw, type); }
            return value;
        }

        double ParseDouble(const std::string& raw, const char* type)
        {
            const std::string text = Trim(raw);
            if (text.empty()) { ThrowFormat(raw, type); }
            std::istringstream stream(text);
            stream.imbue(std::locale::classic());
            double value = 0.0;
            stream >> value;
            if (stream.fail() || !stream.eof()) { ThrowFormat(raw, type); }
            return value;
        }

        std::vector<float> ParseComponents(const std::string& raw, std::size_t minimum, std::size_t maximum,
                                           const char* type)
        {
            // Accepts "1, 2, 3" and "{X:1 Y:2 Z:3}" / "{R:255 G:0 B:255 A:255}".
            std::string text = Trim(raw);
            if (!text.empty() && text.front() == '{' && text.back() == '}')
            {
                text = text.substr(1, text.size() - 2);
            }
            for (char& c : text)
            {
                if (c == ',' || c == ';' || c == '\t' || c == '\n') { c = ' '; }
            }
            std::vector<float> components;
            std::istringstream stream(text);
            stream.imbue(std::locale::classic());
            std::string token;
            while (stream >> token)
            {
                const std::size_t colon = token.find(':');
                if (colon != std::string::npos) { token = token.substr(colon + 1); }
                if (token.empty()) { continue; }
                try { components.push_back(static_cast<float>(ParseDouble(token, type))); }
                catch (const System::FormatException&) { ThrowFormat(raw, type); }
            }
            if (components.size() < minimum || components.size() > maximum) { ThrowFormat(raw, type); }
            return components;
        }

        template<typename T>
        T Narrow(std::int64_t value, const std::string& propertyName)
        {
            if (value < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
                value > static_cast<std::int64_t>(std::numeric_limits<T>::max()))
            {
                throw System::InvalidCastException("Processor parameter '" + propertyName +
                                                   "' cannot hold the value " + std::to_string(value) + ".");
            }
            return static_cast<T>(value);
        }

        template<typename T>
        T NarrowUnsigned(std::uint64_t value, const std::string& propertyName)
        {
            if (value > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
            {
                throw System::InvalidCastException("Processor parameter '" + propertyName +
                                                   "' cannot hold the value " + std::to_string(value) + ".");
            }
            return static_cast<T>(value);
        }

        template<typename T>
        T FromNumericBox(const ContentObject& value, const std::string& propertyName, bool& handled)
        {
            handled = true;
            if (Holds<std::int64_t>(value)) { return Narrow<T>(Unbox<std::int64_t>(value), propertyName); }
            if (Holds<std::int32_t>(value)) { return Narrow<T>(Unbox<std::int32_t>(value), propertyName); }
            if (Holds<std::uint64_t>(value)) { return NarrowUnsigned<T>(Unbox<std::uint64_t>(value), propertyName); }
            if (Holds<std::uint32_t>(value)) { return NarrowUnsigned<T>(Unbox<std::uint32_t>(value), propertyName); }
            handled = false;
            return T{};
        }
    }

    template<>
    bool ParseProcessorParameterText<bool>(const std::string& text)
    {
        const std::string folded = Lower(Trim(text));
        if (folded == "true") { return true; }
        if (folded == "false") { return false; }
        ThrowFormat(text, "Boolean");
    }

    template<>
    std::int32_t ParseProcessorParameterText<std::int32_t>(const std::string& text)
    {
        return ParseInteger<std::int32_t>(text, "Int32");
    }

    template<>
    std::uint32_t ParseProcessorParameterText<std::uint32_t>(const std::string& text)
    {
        return ParseInteger<std::uint32_t>(text, "UInt32");
    }

    template<>
    std::int64_t ParseProcessorParameterText<std::int64_t>(const std::string& text)
    {
        return ParseInteger<std::int64_t>(text, "Int64");
    }

    template<>
    float ParseProcessorParameterText<float>(const std::string& text)
    {
        return static_cast<float>(ParseDouble(text, "Single"));
    }

    template<>
    double ParseProcessorParameterText<double>(const std::string& text)
    {
        return ParseDouble(text, "Double");
    }

    template<>
    std::string ParseProcessorParameterText<std::string>(const std::string& text)
    {
        return text;
    }

    template<>
    char16_t ParseProcessorParameterText<char16_t>(const std::string& text)
    {
        // A .NET Char parameter is one UTF-16 code unit; the text is UTF-8.
        if (text.empty()) { ThrowFormat(text, "Char"); }
        const unsigned char lead = static_cast<unsigned char>(text[0]);
        std::uint32_t codePoint = 0;
        std::size_t length = 0;
        if (lead < 0x80) { codePoint = lead; length = 1; }
        else if ((lead & 0xE0) == 0xC0) { codePoint = lead & 0x1F; length = 2; }
        else if ((lead & 0xF0) == 0xE0) { codePoint = lead & 0x0F; length = 3; }
        else { ThrowFormat(text, "Char"); }
        if (text.size() != length) { ThrowFormat(text, "Char"); }
        for (std::size_t i = 1; i < length; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if ((c & 0xC0) != 0x80) { ThrowFormat(text, "Char"); }
            codePoint = (codePoint << 6) | (c & 0x3F);
        }
        if (codePoint > 0xFFFF) { ThrowFormat(text, "Char"); }
        return static_cast<char16_t>(codePoint);
    }

    template<>
    Microsoft::Xna::Framework::Color ParseProcessorParameterText<Microsoft::Xna::Framework::Color>(const std::string& text)
    {
        const std::vector<float> c = ParseComponents(text, 3, 4, "Color");
        const auto channel = [&text](float v) {
            if (v < 0.0f || v > 255.0f || v != std::floor(v)) { ThrowFormat(text, "Color"); }
            return static_cast<SharpRuntime::intcs>(v);
        };
        return c.size() == 3
                   ? Microsoft::Xna::Framework::Color(channel(c[0]), channel(c[1]), channel(c[2]))
                   : Microsoft::Xna::Framework::Color(channel(c[0]), channel(c[1]), channel(c[2]), channel(c[3]));
    }

    template<>
    Microsoft::Xna::Framework::Vector2 ParseProcessorParameterText<Microsoft::Xna::Framework::Vector2>(const std::string& text)
    {
        const std::vector<float> c = ParseComponents(text, 2, 2, "Vector2");
        return Microsoft::Xna::Framework::Vector2(c[0], c[1]);
    }

    template<>
    Microsoft::Xna::Framework::Vector3 ParseProcessorParameterText<Microsoft::Xna::Framework::Vector3>(const std::string& text)
    {
        const std::vector<float> c = ParseComponents(text, 3, 3, "Vector3");
        return Microsoft::Xna::Framework::Vector3(c[0], c[1], c[2]);
    }

    template<>
    Microsoft::Xna::Framework::Vector4 ParseProcessorParameterText<Microsoft::Xna::Framework::Vector4>(const std::string& text)
    {
        const std::vector<float> c = ParseComponents(text, 4, 4, "Vector4");
        return Microsoft::Xna::Framework::Vector4(c[0], c[1], c[2], c[3]);
    }

    namespace
    {
        template<typename T>
        T ConvertExactOrText(const ContentObject& value, const std::string& propertyName)
        {
            if (Holds<T>(value)) { return Unbox<T>(value); }
            if (Holds<std::string>(value))
            {
                try { return ParseProcessorParameterText<T>(Unbox<std::string>(value)); }
                catch (const System::FormatException& error)
                {
                    throw System::InvalidCastException("Processor parameter '" + propertyName + "': " +
                                                       error.getMessageProperty());
                }
            }
            throw System::InvalidCastException("Processor parameter '" + propertyName + "' expects '" +
                                               ContentTypeName<T>::Name() + "', not '" + value.StableType() + "'.");
        }
    }

    template<>
    bool ConvertProcessorParameterObject<bool>(const ContentObject& value, const std::string& propertyName)
    {
        return ConvertExactOrText<bool>(value, propertyName);
    }

    template<>
    std::int32_t ConvertProcessorParameterObject<std::int32_t>(const ContentObject& value, const std::string& propertyName)
    {
        bool handled = false;
        const std::int32_t numeric = FromNumericBox<std::int32_t>(value, propertyName, handled);
        return handled ? numeric : ConvertExactOrText<std::int32_t>(value, propertyName);
    }

    template<>
    std::uint32_t ConvertProcessorParameterObject<std::uint32_t>(const ContentObject& value, const std::string& propertyName)
    {
        bool handled = false;
        const std::uint32_t numeric = FromNumericBox<std::uint32_t>(value, propertyName, handled);
        return handled ? numeric : ConvertExactOrText<std::uint32_t>(value, propertyName);
    }

    template<>
    std::int64_t ConvertProcessorParameterObject<std::int64_t>(const ContentObject& value, const std::string& propertyName)
    {
        bool handled = false;
        const std::int64_t numeric = FromNumericBox<std::int64_t>(value, propertyName, handled);
        return handled ? numeric : ConvertExactOrText<std::int64_t>(value, propertyName);
    }

    template<>
    float ConvertProcessorParameterObject<float>(const ContentObject& value, const std::string& propertyName)
    {
        if (Holds<double>(value)) { return static_cast<float>(Unbox<double>(value)); }
        bool handled = false;
        const std::int64_t numeric = FromNumericBox<std::int64_t>(value, propertyName, handled);
        return handled ? static_cast<float>(numeric) : ConvertExactOrText<float>(value, propertyName);
    }

    template<>
    double ConvertProcessorParameterObject<double>(const ContentObject& value, const std::string& propertyName)
    {
        if (Holds<float>(value)) { return Unbox<float>(value); }
        bool handled = false;
        const std::int64_t numeric = FromNumericBox<std::int64_t>(value, propertyName, handled);
        return handled ? static_cast<double>(numeric) : ConvertExactOrText<double>(value, propertyName);
    }

    template<>
    std::string ConvertProcessorParameterObject<std::string>(const ContentObject& value, const std::string& propertyName)
    {
        return ConvertExactOrText<std::string>(value, propertyName);
    }

    template<>
    char16_t ConvertProcessorParameterObject<char16_t>(const ContentObject& value, const std::string& propertyName)
    {
        return ConvertExactOrText<char16_t>(value, propertyName);
    }

    template<>
    Microsoft::Xna::Framework::Color ConvertProcessorParameterObject<Microsoft::Xna::Framework::Color>(
        const ContentObject& value, const std::string& propertyName)
    {
        return ConvertExactOrText<Microsoft::Xna::Framework::Color>(value, propertyName);
    }

    template<>
    Microsoft::Xna::Framework::Vector2 ConvertProcessorParameterObject<Microsoft::Xna::Framework::Vector2>(
        const ContentObject& value, const std::string& propertyName)
    {
        return ConvertExactOrText<Microsoft::Xna::Framework::Vector2>(value, propertyName);
    }

    template<>
    Microsoft::Xna::Framework::Vector3 ConvertProcessorParameterObject<Microsoft::Xna::Framework::Vector3>(
        const ContentObject& value, const std::string& propertyName)
    {
        return ConvertExactOrText<Microsoft::Xna::Framework::Vector3>(value, propertyName);
    }

    template<>
    Microsoft::Xna::Framework::Vector4 ConvertProcessorParameterObject<Microsoft::Xna::Framework::Vector4>(
        const ContentObject& value, const std::string& propertyName)
    {
        return ConvertExactOrText<Microsoft::Xna::Framework::Vector4>(value, propertyName);
    }
}
