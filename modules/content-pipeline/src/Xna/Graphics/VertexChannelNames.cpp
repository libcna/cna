// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"

#include <array>
#include <cctype>
#include <utility>

#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        /** @brief The usage names XNA encodes and decodes, in value order. */
        constexpr std::array<std::pair<VertexElementUsage, std::string_view>, 13> UsageNames{
            {{VertexElementUsage::Position, "Position"},
             {VertexElementUsage::Color, "Color"},
             {VertexElementUsage::TextureCoordinate, "TextureCoordinate"},
             {VertexElementUsage::Normal, "Normal"},
             {VertexElementUsage::Binormal, "Binormal"},
             {VertexElementUsage::Tangent, "Tangent"},
             {VertexElementUsage::BlendIndices, "BlendIndices"},
             {VertexElementUsage::BlendWeight, "BlendWeight"},
             {VertexElementUsage::Depth, "Depth"},
             {VertexElementUsage::Fog, "Fog"},
             {VertexElementUsage::PointSize, "PointSize"},
             {VertexElementUsage::Sample, "Sample"},
             {VertexElementUsage::TessellateFactor, "TessellateFactor"}}};

        /** @brief The refusal for a name C# would pass as null; a std::string has only empty. */
        void RequireName(const std::string& value, const char* parameter)
        {
            if (value.empty())
            {
                throw System::ArgumentNullException(parameter);
            }
        }

        void RequireUsageIndex(SharpRuntime::intcs usageIndex)
        {
            if (usageIndex < 0)
            {
                throw System::ArgumentOutOfRangeException("usageIndex");
            }
        }

        /** @brief The index of the first trailing digit, or the length when there is none. */
        std::size_t DigitStart(const std::string& encodedName)
        {
            std::size_t start = encodedName.size();
            while (start > 0 && std::isdigit(static_cast<unsigned char>(encodedName[start - 1])) != 0)
            {
                --start;
            }
            return start;
        }
    }

    std::string VertexChannelNames::Binormal(SharpRuntime::intcs usageIndex)
    {
        return EncodeName(VertexElementUsage::Binormal, usageIndex);
    }

    std::string VertexChannelNames::Color(SharpRuntime::intcs usageIndex)
    {
        return EncodeName(VertexElementUsage::Color, usageIndex);
    }

    std::string VertexChannelNames::DecodeBaseName(const std::string& encodedName)
    {
        RequireName(encodedName, "encodedName");
        return encodedName.substr(0, DigitStart(encodedName));
    }

    SharpRuntime::intcs VertexChannelNames::DecodeUsageIndex(const std::string& encodedName)
    {
        RequireName(encodedName, "encodedName");
        const std::size_t start = DigitStart(encodedName);
        if (start == encodedName.size())
        {
            // A name with no trailing digits is usage index zero (measured, vertexnames/decode).
            return 0;
        }
        return static_cast<SharpRuntime::intcs>(std::stol(encodedName.substr(start)));
    }

    std::string VertexChannelNames::EncodeName(Microsoft::Xna::Framework::Graphics::VertexElementUsage usage,
                                               SharpRuntime::intcs usageIndex)
    {
        for (const auto& [value, name] : UsageNames)
        {
            if (value == usage)
            {
                return EncodeName(std::string(name), usageIndex);
            }
        }
        return EncodeName(std::to_string(static_cast<SharpRuntime::intcs>(usage)), usageIndex);
    }

    std::string VertexChannelNames::EncodeName(const std::string& baseName, SharpRuntime::intcs usageIndex)
    {
        RequireName(baseName, "baseName");
        RequireUsageIndex(usageIndex);
        return baseName + std::to_string(usageIndex);
    }

    std::string VertexChannelNames::Normal() { return Normal(0); }

    std::string VertexChannelNames::Normal(SharpRuntime::intcs usageIndex)
    {
        return EncodeName(VertexElementUsage::Normal, usageIndex);
    }

    std::string VertexChannelNames::Tangent(SharpRuntime::intcs usageIndex)
    {
        return EncodeName(VertexElementUsage::Tangent, usageIndex);
    }

    std::string VertexChannelNames::TextureCoordinate(SharpRuntime::intcs usageIndex)
    {
        return EncodeName(VertexElementUsage::TextureCoordinate, usageIndex);
    }

    bool VertexChannelNames::TryDecodeUsage(const std::string& encodedName,
                                            Microsoft::Xna::Framework::Graphics::VertexElementUsage& usage)
    {
        RequireName(encodedName, "encodedName");
        const std::string baseName = DecodeBaseName(encodedName);
        for (const auto& [value, name] : UsageNames)
        {
            if (baseName == name)
            {
                usage = value;
                return true;
            }
        }
        // The out parameter keeps the default the runtime leaves it at (measured,
        // vertexnames/try_decode: an unknown base name answers false with Position).
        usage = VertexElementUsage::Position;
        return false;
    }

    std::string VertexChannelNames::Weights() { return Weights(0); }

    std::string VertexChannelNames::Weights(SharpRuntime::intcs usageIndex)
    {
        // The blend weights channel is spelled "Weights", not by its VertexElementUsage name
        // (measured, vertexnames/standard).
        return EncodeName(std::string("Weights"), usageIndex);
    }
}
