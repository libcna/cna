// SPDX-License-Identifier: MS-PL
#pragma once

#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief Specifies the target format of a processed texture.
     */
    enum class TextureProcessorOutputFormat : SharpRuntime::intcs
    {
        /** @brief The texture keeps the format it was imported in. */
        NoChange = 0,
        /** @brief The texture is converted to the Color format. */
        Color = 1,
        /** @brief The texture is compressed to the DXT format that suits it. */
        DxtCompressed = 2
    };

    /**
     * @brief Specifies the effect a material processor builds when the material names none.
     */
    enum class MaterialProcessorDefaultEffect : SharpRuntime::intcs
    {
        /** @brief BasicEffect. */
        BasicEffect = 0,
        /** @brief SkinnedEffect. */
        SkinnedEffect = 1,
        /** @brief EnvironmentMapEffect. */
        EnvironmentMapEffect = 2,
        /** @brief DualTextureEffect. */
        DualTextureEffect = 3,
        /** @brief AlphaTestEffect. */
        AlphaTestEffect = 4
    };

    /**
     * @brief Specifies how an effect processor compiles: for debugging, for speed, or as the build
     *        configuration says.
     */
    enum class EffectProcessorDebugMode : SharpRuntime::intcs
    {
        /** @brief Debug information is emitted for a Debug build and optimizations for a Release one. */
        Auto = 0,
        /** @brief Debug information is emitted and optimizations are disabled. */
        Debug = 1,
        /** @brief Optimizations are enabled and no debug information is emitted. */
        Optimize = 2
    };
}

CNA_XNA_CONTENT_ENUM(Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessorOutputFormat,
                     "Microsoft.Xna.Framework.Content.Pipeline.Processors.TextureProcessorOutputFormat", false,
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessorOutputFormat::NoChange,
                      "NoChange"},
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessorOutputFormat::Color,
                      "Color"},
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessorOutputFormat::
                          DxtCompressed,
                      "DxtCompressed"});

CNA_XNA_CONTENT_ENUM(Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessorDefaultEffect,
                     "Microsoft.Xna.Framework.Content.Pipeline.Processors.MaterialProcessorDefaultEffect", false,
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessorDefaultEffect::
                          BasicEffect,
                      "BasicEffect"},
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessorDefaultEffect::
                          SkinnedEffect,
                      "SkinnedEffect"},
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessorDefaultEffect::
                          EnvironmentMapEffect,
                      "EnvironmentMapEffect"},
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessorDefaultEffect::
                          DualTextureEffect,
                      "DualTextureEffect"},
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessorDefaultEffect::
                          AlphaTestEffect,
                      "AlphaTestEffect"});

CNA_XNA_CONTENT_ENUM(Microsoft::Xna::Framework::Content::Pipeline::Processors::EffectProcessorDebugMode,
                     "Microsoft.Xna.Framework.Content.Pipeline.Processors.EffectProcessorDebugMode", false,
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::EffectProcessorDebugMode::Auto,
                      "Auto"},
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::EffectProcessorDebugMode::Debug,
                      "Debug"},
                     {Microsoft::Xna::Framework::Content::Pipeline::Processors::EffectProcessorDebugMode::Optimize,
                      "Optimize"});
