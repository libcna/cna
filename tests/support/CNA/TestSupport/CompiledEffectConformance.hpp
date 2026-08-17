// SPDX-License-Identifier: MS-PL
#pragma once

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "System/ArgumentException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @file
 * @brief The shared compiled-effect contract every `CompiledEffects` backend must satisfy.
 *
 * plan_fx.md FX-060: a backend becomes supported only after it passes the same suite, so the
 * assertions live here rather than in one renderer's test file. Each function is one contract
 * section and takes nothing but a ready GraphicsDevice; a new backend's test file supplies its own
 * device setup and calls them. Backend-specific evidence -- golden pixels, native parser
 * diagnostics, fixture provenance, Content Pipeline wiring -- stays in that backend's own tests.
 */
namespace CNA::TestSupport
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector4;
    using Microsoft::Xna::Framework::Graphics::Blend;
    using Microsoft::Xna::Framework::Graphics::BlendFunction;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::CompareFunction;
    using Microsoft::Xna::Framework::Graphics::CullMode;
    using Microsoft::Xna::Framework::Graphics::DepthStencilState;
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::EffectParameter;
    using Microsoft::Xna::Framework::Graphics::EffectParameterClass;
    using Microsoft::Xna::Framework::Graphics::EffectParameterType;
    using Microsoft::Xna::Framework::Graphics::FillMode;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::SamplerState;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
    using Microsoft::Xna::Framework::Graphics::TextureFilter;

    /**
     * @brief Reports whether the device executes compiled Effect Framework bytecode.
     *
     * A backend's test file should `GTEST_SKIP()` on false rather than fail: an explicitly
     * unsupported renderer is a valid state, and the refusal itself is covered by
     * RunCompiledEffectUnsupportedBackendContract().
     */
    [[nodiscard]] inline bool SupportsCompiledEffects(const GraphicsDevice& device)
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects);
    }

    /**
     * @brief Contract: the format boundary accepts only Effect Framework bytecode, by name.
     *
     * @param device Device whose renderer claims CompiledEffects.
     */
    inline void RunCompiledEffectFormatContract(GraphicsDevice& device)
    {
        EXPECT_THROW(Effect(device, {}), System::ArgumentException)
            << "empty bytecode must be rejected as an argument error";

        const std::vector<SharpRuntime::bytecs> mgfx = {'M', 'G', 'F', 'X', 1, 0, 0, 0};
        EXPECT_THROW(Effect(device, mgfx), System::NotSupportedException)
            << "MonoGame's MGFX container must be named, not guessed at";

        const std::vector<SharpRuntime::bytecs> noise(64, 0x5A);
        EXPECT_THROW(Effect(device, noise), System::ArgumentException)
            << "input without an Effect Framework header must be rejected as an argument error";

        const auto valid = BuildSyntheticConformanceEffect({});
        for (std::size_t truncated : {std::size_t{1}, valid.size() / 4, valid.size() / 2,
                                      valid.size() - 4, valid.size() - 1})
        {
            const std::vector<SharpRuntime::bytecs> prefix(valid.begin(),
                                                           valid.begin() + truncated);
            SCOPED_TRACE("truncated to " + std::to_string(truncated) + " bytes");
            try
            {
                Effect effect(device, prefix);
            }
            catch (const std::exception&)
            {
                // Rejecting a truncation is the contract; completing one is only acceptable if
                // the prefix happens to be a self-consistent effect, which the loop tolerates.
            }
        }

        EXPECT_NO_THROW(Effect(device, valid)) << "the synthetic fixture must construct";
    }

    /**
     * @brief Contract: reflected order, names, classes, dimensions, arrays, structs, annotations.
     */
    inline void RunCompiledEffectReflectionContract(GraphicsDevice& device)
    {
        Effect effect(device, BuildSyntheticConformanceEffect({}));

        auto& parameters = effect.getParametersProperty();
        ASSERT_EQ(parameters.getCountProperty(), 5);
        EXPECT_EQ(parameters[0].getNameProperty(), "Gain");
        EXPECT_EQ(parameters[1].getNameProperty(), "Tint");
        EXPECT_EQ(parameters[2].getNameProperty(), "Lighting");
        EXPECT_EQ(parameters[3].getNameProperty(), "Transform");
        EXPECT_EQ(parameters[4].getNameProperty(), "Weights");

        ASSERT_NE(parameters["Gain"], nullptr);
        EXPECT_EQ(parameters["Gain"]->getSemanticProperty(), "SCALAR");
        EXPECT_EQ(parameters["Gain"]->getParameterClassProperty(), EffectParameterClass::Scalar);
        EXPECT_EQ(parameters["Gain"]->getParameterTypeProperty(), EffectParameterType::Single);
        EXPECT_FLOAT_EQ(parameters["Gain"]->GetValueSingle(), 0.25f);
        EXPECT_EQ(parameters["NoSuchParameter"], nullptr)
            << "a name the effect does not declare must be detectable, not fatal";

        ASSERT_NE(parameters["Tint"], nullptr);
        EXPECT_EQ(parameters["Tint"]->getParameterClassProperty(), EffectParameterClass::Vector);
        EXPECT_EQ(parameters["Tint"]->getColumnCountProperty(), 4);

        ASSERT_NE(parameters["Transform"], nullptr);
        EXPECT_EQ(parameters["Transform"]->getParameterClassProperty(),
                  EffectParameterClass::Matrix);
        EXPECT_EQ(parameters["Transform"]->getRowCountProperty(), 4);
        EXPECT_EQ(parameters["Transform"]->getColumnCountProperty(), 4);

        ASSERT_NE(parameters["Weights"], nullptr);
        EXPECT_EQ(parameters["Weights"]->getElementsProperty().getCountProperty(), 2);

        ASSERT_NE(parameters["Lighting"], nullptr);
        EXPECT_EQ(parameters["Lighting"]->getParameterClassProperty(),
                  EffectParameterClass::Struct);
        const auto& members = parameters["Lighting"]->getStructureMembersProperty();
        ASSERT_EQ(members.getCountProperty(), 3);
        EXPECT_EQ(members[0].getNameProperty(), "Intensity");
        EXPECT_EQ(members[1].getNameProperty(), "Direction");
        EXPECT_EQ(members[2].getNameProperty(), "Thresholds");

        const auto& annotations = parameters["Gain"]->getAnnotationsProperty();
        ASSERT_EQ(annotations.getCountProperty(), 1);
        EXPECT_EQ(annotations[0].getNameProperty(), "Visible");
    }

    /**
     * @brief Contract: first technique selected, exact pass identity, technique annotations.
     */
    inline void RunCompiledEffectTechniqueContract(GraphicsDevice& device)
    {
        Effect effect(device, BuildSyntheticConformanceEffect({}));

        auto& techniques = effect.getTechniquesProperty();
        ASSERT_EQ(techniques.getCountProperty(), 2);
        EXPECT_EQ(techniques[0].getNameProperty(), "FirstTechnique");
        EXPECT_EQ(techniques[1].getNameProperty(), "SecondTechnique");
        EXPECT_EQ(effect.getCurrentTechniqueProperty(), &techniques[0])
            << "construction must select the first reflected technique";

        ASSERT_EQ(techniques[0].getAnnotationsProperty().getCountProperty(), 1);
        EXPECT_EQ(techniques[0].getAnnotationsProperty()[0].getNameProperty(), "Quality");

        auto& passes = techniques[0].getPassesProperty();
        ASSERT_EQ(passes.getCountProperty(), 2);
        EXPECT_EQ(passes[0].getNameProperty(), "P0");
        EXPECT_EQ(passes[1].getNameProperty(), "StatePass");
        ASSERT_EQ(techniques[1].getPassesProperty().getCountProperty(), 1);
        EXPECT_EQ(techniques[1].getPassesProperty()[0].getNameProperty(), "P1");

        // Each pass applies itself, not pass zero, and selecting the second technique works.
        EXPECT_NO_THROW(passes[1].Apply());
        effect.setCurrentTechniqueProperty(&techniques[1]);
        EXPECT_NO_THROW(techniques[1].getPassesProperty()[0].Apply());
    }

    /**
     * @brief Contract: a pass publishes its render states through the device's own state objects.
     */
    inline void RunCompiledEffectRenderStateContract(GraphicsDevice& device)
    {
        namespace Fx = EffectFormat;
        const std::vector<SyntheticRenderState> states = {
            {Fx::RsZEnable, Fx::ZBufferFalse},
            {Fx::RsZWriteEnable, 0},
            {Fx::RsZFunc, Fx::CmpGreater},
            {Fx::RsStencilRef, 7},
            {Fx::RsStencilMask, 0x0Fu},
            {Fx::RsCullMode, Fx::CullCw},
            {Fx::RsFillMode, Fx::FillWireframe},
            {Fx::RsSlopeScaleDepthBias, FloatBits(0.5f), true},
            {Fx::RsDepthBias, FloatBits(0.25f), true},
            {Fx::RsSrcBlend, Fx::BlendSrcAlpha},
            {Fx::RsDestBlend, Fx::BlendInvSrcAlpha},
            {Fx::RsBlendOp, Fx::BlendOpRevSubtract},
            {Fx::RsColorWriteEnable, 5},
            {Fx::RsMultiSampleMask, 0x0F0F0F0Fu},
            {Fx::RsBlendFactor, 0x10203040u},
        };

        Effect effect(device, BuildSyntheticConformanceEffect(states));
        effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();

        const DepthStencilState& depth = device.getDepthStencilStateProperty();
        EXPECT_FALSE(depth.getDepthBufferEnableProperty());
        EXPECT_FALSE(depth.getDepthBufferWriteEnableProperty());
        EXPECT_EQ(depth.getDepthBufferFunctionProperty(), CompareFunction::Greater);
        EXPECT_EQ(depth.getReferenceStencilProperty(), 7);
        EXPECT_EQ(depth.getStencilMaskProperty(), 0x0F);

        const RasterizerState& rasterizer = device.getRasterizerStateProperty();
        EXPECT_EQ(rasterizer.getCullModeProperty(), CullMode::CullClockwiseFace);
        EXPECT_EQ(rasterizer.getFillModeProperty(), FillMode::WireFrame);
        EXPECT_FLOAT_EQ(rasterizer.getSlopeScaleDepthBiasProperty(), 0.5f);
        EXPECT_FLOAT_EQ(rasterizer.getDepthBiasProperty(), 0.25f);

        const BlendState& blend = device.getBlendStateProperty();
        EXPECT_EQ(blend.getColorSourceBlendProperty(), Blend::SourceAlpha);
        EXPECT_EQ(blend.getColorDestinationBlendProperty(), Blend::InverseSourceAlpha);
        // Without SEPARATEALPHABLENDENABLE the alpha factors follow the colour ones, but BLENDOP
        // alone never changes the alpha blend function.
        EXPECT_EQ(blend.getAlphaSourceBlendProperty(), Blend::SourceAlpha);
        EXPECT_EQ(blend.getAlphaDestinationBlendProperty(), Blend::InverseSourceAlpha);
        EXPECT_EQ(blend.getColorBlendFunctionProperty(), BlendFunction::ReverseSubtract);
        EXPECT_EQ(blend.getAlphaBlendFunctionProperty(), BlendFunction::Add);
        EXPECT_EQ(static_cast<int>(blend.getColorWriteChannelsProperty()), 5);
        EXPECT_EQ(blend.getMultiSampleMaskProperty(), 0x0F0F0F0F);
        EXPECT_EQ(blend.getBlendFactorProperty(), Color(0x10, 0x20, 0x30, 0x40));
    }

    /**
     * @brief Contract: an unassigned state group survives, and an unknown token is never ignored.
     */
    inline void RunCompiledEffectStatePolicyContract(GraphicsDevice& device)
    {
        namespace Fx = EffectFormat;

        device.setBlendStateProperty(BlendState::NonPremultiplied);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        Effect rasterizerOnly(device, BuildSyntheticConformanceEffect(
            {{Fx::RsFillMode, Fx::FillWireframe}}));
        rasterizerOnly.getTechniquesProperty()[0].getPassesProperty()[1].Apply();

        EXPECT_EQ(device.getBlendStateProperty().getColorSourceBlendProperty(),
                  BlendState::NonPremultiplied.getColorSourceBlendProperty());
        EXPECT_EQ(device.getDepthStencilStateProperty().getDepthBufferEnableProperty(),
                  DepthStencilState::None.getDepthBufferEnableProperty());
        EXPECT_EQ(device.getRasterizerStateProperty().getFillModeProperty(), FillMode::WireFrame);
        EXPECT_EQ(device.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);

        Effect unknown(device, BuildSyntheticConformanceEffect(
            {{Fx::RsUnknownForTesting, 0u}}));
        try
        {
            unknown.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
            ADD_FAILURE() << "an unknown Effect Framework render state must never be ignored";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_NE(std::string(error.what()).find("177"), std::string::npos)
                << "the diagnostic must name the token that was refused";
        }
    }

    /**
     * @brief Contract: sampler assignments reach the device's own sampler slot, per register.
     */
    inline void RunCompiledEffectSamplerContract(GraphicsDevice& device)
    {
        namespace Fx = EffectFormat;
        const auto apply = [&device](const std::vector<SyntheticSamplerState>& states,
                                     std::uint32_t samplerRegister = 0) {
            SyntheticEffectOptions options;
            options.includeSampler = true;
            options.samplerStates = states;
            options.samplerRegister = samplerRegister;
            Effect effect(device, BuildSyntheticEffect(options));
            effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
            return device.getSamplerStatesProperty()[static_cast<int>(samplerRegister)];
        };

        const SamplerState addressing = apply({
            {Fx::SampAddressU, Fx::AddressMirror},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampAddressW, Fx::AddressWrap},
        });
        EXPECT_EQ(addressing.getAddressUProperty(), TextureAddressMode::Mirror);
        EXPECT_EQ(addressing.getAddressVProperty(), TextureAddressMode::Clamp);
        EXPECT_EQ(addressing.getAddressWProperty(), TextureAddressMode::Wrap);

        const SamplerState lod = apply({
            {Fx::SampMipMapLodBias, FloatBits(-1.5f), true},
            {Fx::SampMaxMipLevel, 3},
            {Fx::SampMaxAnisotropy, 8},
        });
        EXPECT_FLOAT_EQ(lod.getMipMapLevelOfDetailBiasProperty(), -1.5f);
        EXPECT_EQ(lod.getMaxMipLevelProperty(), 3);
        EXPECT_EQ(lod.getMaxAnisotropyProperty(), 8);

        // The three Direct3D filter axes collapse into XNA's eight aggregate values, reading an
        // anisotropic component as its linear equivalent.
        struct Expectation
        {
            std::uint32_t mag;
            std::uint32_t min;
            std::uint32_t mip;
            TextureFilter expected;
        };
        const Expectation expectations[] = {
            {Fx::FilterPoint, Fx::FilterPoint, Fx::FilterNone, TextureFilter::Point},
            {Fx::FilterPoint, Fx::FilterPoint, Fx::FilterLinear, TextureFilter::PointMipLinear},
            {Fx::FilterPoint, Fx::FilterLinear, Fx::FilterPoint,
             TextureFilter::MinLinearMagPointMipPoint},
            {Fx::FilterPoint, Fx::FilterAnisotropic, Fx::FilterLinear,
             TextureFilter::MinLinearMagPointMipLinear},
            {Fx::FilterLinear, Fx::FilterPoint, Fx::FilterPoint,
             TextureFilter::MinPointMagLinearMipPoint},
            {Fx::FilterLinear, Fx::FilterPoint, Fx::FilterLinear,
             TextureFilter::MinPointMagLinearMipLinear},
            {Fx::FilterLinear, Fx::FilterLinear, Fx::FilterPoint, TextureFilter::LinearMipPoint},
            {Fx::FilterAnisotropic, Fx::FilterAnisotropic, Fx::FilterAnisotropic,
             TextureFilter::Linear},
        };
        for (const Expectation& expectation : expectations)
        {
            SCOPED_TRACE("mag=" + std::to_string(expectation.mag) +
                         " min=" + std::to_string(expectation.min) +
                         " mip=" + std::to_string(expectation.mip));
            EXPECT_EQ(apply({{Fx::SampMagFilter, expectation.mag},
                             {Fx::SampMinFilter, expectation.min},
                             {Fx::SampMipFilter, expectation.mip}}).getFilterProperty(),
                      expectation.expected);
        }

        // States land on the exact register the shader declared and leave the others alone.
        const SamplerState slotZeroBefore = device.getSamplerStatesProperty()[0];
        const SamplerState slotOneBefore = device.getSamplerStatesProperty()[1];
        const SamplerState onSlotTwo = apply({{Fx::SampAddressU, Fx::AddressMirror}}, 2);
        EXPECT_EQ(onSlotTwo.getAddressUProperty(), TextureAddressMode::Mirror);
        EXPECT_EQ(device.getSamplerStatesProperty()[0].getAddressUProperty(),
                  slotZeroBefore.getAddressUProperty());
        EXPECT_EQ(device.getSamplerStatesProperty()[0].getFilterProperty(),
                  slotZeroBefore.getFilterProperty());
        EXPECT_EQ(device.getSamplerStatesProperty()[1].getAddressUProperty(),
                  slotOneBefore.getAddressUProperty());

        // Unsupported addressing and unknown sampler tokens are refused by name.
        try
        {
            apply({{Fx::SampAddressU, Fx::AddressBorder}});
            ADD_FAILURE() << "Border addressing has no XNA 4.0 SamplerState representation";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_NE(std::string(error.what()).find("Border"), std::string::npos);
        }
        try
        {
            apply({{Fx::SampSrgbTexture, 1}});
            ADD_FAILURE() << "an unknown Effect Framework sampler state must never be ignored";
        }
        catch (const std::runtime_error& error)
        {
            EXPECT_NE(std::string(error.what()).find("15"), std::string::npos);
        }
    }

    /**
     * @brief Contract: a sampler's texture comes from its reflected texture parameter.
     */
    inline void RunCompiledEffectTextureBindingContract(GraphicsDevice& device)
    {
        namespace Fx = EffectFormat;
        SyntheticEffectOptions options;
        options.includeSampler = true;
        options.samplerStates = {{Fx::SampAddressU, Fx::AddressClamp}};
        Effect effect(device, BuildSyntheticEffect(options));

        auto& parameters = effect.getParametersProperty();
        ASSERT_NE(parameters["FxTexture"], nullptr);
        EXPECT_EQ(parameters["FxTexture"]->getParameterTypeProperty(),
                  EffectParameterType::Texture2D);
        EXPECT_EQ(parameters["FxSampler"], nullptr)
            << "sampler and shader objects are not public XNA parameters";

        Texture2D texture(device, 2, 2);
        const Color pixels[4] = {Color::Red, Color::Red, Color::Red, Color::Red};
        texture.SetData(pixels, 4);
        parameters["FxTexture"]->SetValue(&texture);
        effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
        EXPECT_EQ(device.getTexturesProperty()[0], &texture);

        // A pass that assigns no texture leaves the slot the game selected alone.
        Texture2D selected(device, 2, 2);
        selected.SetData(pixels, 4);
        device.getTexturesProperty()(0, &selected);
        SyntheticEffectOptions withoutTexture;
        withoutTexture.includeSampler = true;
        withoutTexture.samplerStates = {{Fx::SampAddressV, Fx::AddressMirror}};
        Effect other(device, BuildSyntheticEffect(withoutTexture));
        other.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
        EXPECT_EQ(device.getTexturesProperty()[0], &selected);
    }

    /**
     * @brief Contract: clones own their values, technique and textures independently.
     */
    inline void RunCompiledEffectCloneContract(GraphicsDevice& device)
    {
        Effect effect(device, BuildSyntheticConformanceEffect({}));
        effect.getParametersProperty()["Gain"]->SetValue(0.75f);
        effect.setCurrentTechniqueProperty(&effect.getTechniquesProperty()[1]);

        std::unique_ptr<Effect> clone(effect.Clone());
        ASSERT_NE(clone, nullptr);
        EXPECT_FLOAT_EQ(clone->getParametersProperty()["Gain"]->GetValueSingle(), 0.75f)
            << "a clone copies the values its source currently holds";
        ASSERT_NE(clone->getCurrentTechniqueProperty(), nullptr);
        EXPECT_EQ(clone->getCurrentTechniqueProperty()->getNameProperty(), "SecondTechnique")
            << "a clone preserves the selected technique by stable index";
        EXPECT_NE(clone->getCurrentTechniqueProperty(), effect.getCurrentTechniqueProperty())
            << "a clone owns its own technique objects";

        clone->getParametersProperty()["Gain"]->SetValue(0.125f);
        EXPECT_FLOAT_EQ(effect.getParametersProperty()["Gain"]->GetValueSingle(), 0.75f)
            << "mutating a clone must not reach its source";

        EXPECT_NO_THROW(clone->getCurrentTechniqueProperty()->getPassesProperty()[0].Apply());
        clone.reset();
        EXPECT_NO_THROW(effect.getCurrentTechniqueProperty()->getPassesProperty()[0].Apply())
            << "disposing a clone must leave its source usable";
    }

    /**
     * @brief Contract: disposal, repeated cycles and post-disposal use are all safe.
     */
    inline void RunCompiledEffectLifecycleContract(GraphicsDevice& device)
    {
        const auto bytes = BuildSyntheticConformanceEffect({});

        for (int cycle = 0; cycle < 8; ++cycle)
        {
            SCOPED_TRACE("cycle " + std::to_string(cycle));
            Effect effect(device, bytes);
            effect.getTechniquesProperty()[0].getPassesProperty()[0].Apply();
            std::unique_ptr<Effect> clone(effect.Clone());
            clone->getTechniquesProperty()[0].getPassesProperty()[0].Apply();
            if ((cycle & 1) == 0) clone.reset();
            effect.Dispose();
        }

        Effect disposed(device, bytes);
        disposed.getTechniquesProperty()[0].getPassesProperty()[0].Apply();
        disposed.Dispose();
        EXPECT_THROW(disposed.Apply(), System::ObjectDisposedException);
        EXPECT_THROW(disposed.getTechniquesProperty()[0].getPassesProperty()[0].Apply(),
                     System::ObjectDisposedException);
        EXPECT_NO_THROW(disposed.Dispose()) << "disposal must be idempotent";

        Effect replacement(device, bytes);
        EXPECT_NO_THROW(replacement.getTechniquesProperty()[0].getPassesProperty()[0].Apply())
            << "the device must still build a fresh effect after a disposal";
    }

    /**
     * @brief Contract for a renderer that does NOT claim CompiledEffects: it refuses explicitly.
     *
     * Never a silent stock-shader fallback: the refusal is what makes a porting bug visible.
     */
    inline void RunCompiledEffectUnsupportedBackendContract(GraphicsDevice& device)
    {
        ASSERT_FALSE(SupportsCompiledEffects(device))
            << "this contract is for renderers that do not claim CompiledEffects";
        EXPECT_THROW(Effect(device, BuildSyntheticConformanceEffect({})),
                     System::NotSupportedException);
    }

    /** @brief Runs every renderer-neutral section of the compiled-effect contract. */
    inline void RunCompiledEffectContract(GraphicsDevice& device)
    {
        RunCompiledEffectFormatContract(device);
        RunCompiledEffectReflectionContract(device);
        RunCompiledEffectTechniqueContract(device);
        RunCompiledEffectRenderStateContract(device);
        RunCompiledEffectStatePolicyContract(device);
        RunCompiledEffectSamplerContract(device);
        RunCompiledEffectTextureBindingContract(device);
        RunCompiledEffectCloneContract(device);
        RunCompiledEffectLifecycleContract(device);
    }
}
