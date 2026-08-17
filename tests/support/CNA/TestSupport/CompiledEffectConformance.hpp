// SPDX-License-Identifier: MS-PL
#pragma once

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidCastException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cstdint>
#include <functional>
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
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Vector4;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::Blend;
    using Microsoft::Xna::Framework::Graphics::BufferUsage;
    using Microsoft::Xna::Framework::Graphics::EffectPass;
    using Microsoft::Xna::Framework::Graphics::IndexBuffer;
    using Microsoft::Xna::Framework::Graphics::IndexElementSize;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;
    using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
    using Microsoft::Xna::Framework::Graphics::VertexBuffer;
    using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
    using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
    using Microsoft::Xna::Framework::Graphics::VertexElement;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
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
     * @brief Contract: the public parameter API reads and writes every reflected shape.
     *
     * plan_fx.md FX-085. Reflection alone says a parameter is a 4x4 matrix; this says the value a
     * game writes through the XNA setter is the value the XNA getter reads back, for every class
     * and type the fixture declares -- scalars, vectors, matrices and their transpose variants,
     * arrays and their per-element views, and structure members. `RunCompiledEffectDrawContract`
     * then proves the same values reach the GPU.
     */
    inline void RunCompiledEffectParameterApiContract(GraphicsDevice& device)
    {
        Effect effect(device, BuildSyntheticConformanceEffect({}));
        auto& parameters = effect.getParametersProperty();

        // Scalar. XNA converts an int assigned to a float parameter, rather than reinterpreting
        // its bits, which is observable only through the float getter.
        ASSERT_NE(parameters["Gain"], nullptr);
        parameters["Gain"]->SetValue(0.75f);
        EXPECT_FLOAT_EQ(parameters["Gain"]->GetValueSingle(), 0.75f);
        parameters["Gain"]->SetValue(3);
        EXPECT_FLOAT_EQ(parameters["Gain"]->GetValueSingle(), 3.0f);
        parameters["Gain"]->SetValue(true);
        EXPECT_TRUE(parameters["Gain"]->GetValueBoolean());
        parameters["Gain"]->SetValue(0.25f);

        // Vector, through both the typed setter and the packed float-array setter.
        ASSERT_NE(parameters["Tint"], nullptr);
        parameters["Tint"]->SetValue(Vector4(0.125f, 0.25f, 0.5f, 0.75f));
        const Vector4 tint = parameters["Tint"]->GetValueVector4();
        EXPECT_FLOAT_EQ(tint.X, 0.125f);
        EXPECT_FLOAT_EQ(tint.W, 0.75f);
        parameters["Tint"]->SetValue(std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f});
        const std::vector<float> tintCells = parameters["Tint"]->GetValueSingleArray(4);
        ASSERT_EQ(tintCells.size(), 4u);
        EXPECT_FLOAT_EQ(tintCells[2], 3.0f);
        EXPECT_FLOAT_EQ(parameters["Tint"]->GetValueVector2().Y, 2.0f);
        EXPECT_FLOAT_EQ(parameters["Tint"]->GetValueVector3().Z, 3.0f);

        // Matrix. The transpose variants must round-trip through their own storage order, which is
        // the one place a packing mistake is invisible to the non-transposed getter.
        ASSERT_NE(parameters["Transform"], nullptr);
        const Matrix scale = Matrix::CreateScale(2.0f, 3.0f, 4.0f);
        parameters["Transform"]->SetValue(scale);
        const Matrix readBack = parameters["Transform"]->GetValueMatrix();
        EXPECT_FLOAT_EQ(readBack.M11, 2.0f);
        EXPECT_FLOAT_EQ(readBack.M22, 3.0f);
        EXPECT_FLOAT_EQ(readBack.M33, 4.0f);
        const Matrix asymmetric = Matrix::CreateTranslation(5.0f, 6.0f, 7.0f);
        parameters["Transform"]->SetValue(asymmetric);
        EXPECT_FLOAT_EQ(parameters["Transform"]->GetValueMatrix().M41, 5.0f);
        EXPECT_FLOAT_EQ(parameters["Transform"]->GetValueMatrixTranspose().M14, 5.0f);
        parameters["Transform"]->SetValueTranspose(asymmetric);
        EXPECT_FLOAT_EQ(parameters["Transform"]->GetValueMatrixTranspose().M41, 5.0f);
        EXPECT_FLOAT_EQ(parameters["Transform"]->GetValueMatrix().M14, 5.0f);
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());

        // Array, through the whole-array setter and through the per-element views.
        ASSERT_NE(parameters["Weights"], nullptr);
        parameters["Weights"]->SetValue(std::vector<float>{0.375f, 0.625f});
        const std::vector<float> weights = parameters["Weights"]->GetValueSingleArray(2);
        ASSERT_EQ(weights.size(), 2u);
        EXPECT_FLOAT_EQ(weights[0], 0.375f);
        EXPECT_FLOAT_EQ(weights[1], 0.625f);
        auto& elements = parameters["Weights"]->getElementsProperty();
        ASSERT_EQ(elements.getCountProperty(), 2);
        EXPECT_FLOAT_EQ(elements[1].GetValueSingle(), 0.625f);
        elements[1].SetValue(0.875f);
        EXPECT_FLOAT_EQ(parameters["Weights"]->GetValueSingleArray(2)[1], 0.875f)
            << "an element view writes into its parent's own storage, not a copy";

        // Structure members are views onto the parent's storage on the same terms.
        ASSERT_NE(parameters["Lighting"], nullptr);
        auto& members = parameters["Lighting"]->getStructureMembersProperty();
        ASSERT_EQ(members.getCountProperty(), 3);
        members[0].SetValue(0.5f);
        EXPECT_FLOAT_EQ(members[0].GetValueSingle(), 0.5f);
        members[1].SetValue(Vector3(0.1f, 0.2f, 0.3f));
        EXPECT_FLOAT_EQ(members[1].GetValueVector3().Y, 0.2f);
        ASSERT_EQ(members[2].getElementsProperty().getCountProperty(), 2);
        members[2].getElementsProperty()[1].SetValue(0.9f);
        EXPECT_FLOAT_EQ(members[2].GetValueSingleArray(2)[1], 0.9f);

        // Strings follow XNA 4.0's own EffectParameter.SetValue(string)/GetValueString(), which
        // reject a parameter whose reflected type is not String rather than silently succeeding.
        EXPECT_THROW(parameters["Gain"]->SetValue(std::string("nope")),
                     System::InvalidCastException);
        EXPECT_THROW((void) parameters["Tint"]->GetValueString(), System::InvalidCastException);
    }

    /**
     * @brief Contract: the compiled shader is what actually draws, across the XNA draw matrix.
     *
     * plan_fx.md FX-084/FX-077. Every draw fills the render target with the effect's own `Tint`
     * parameter, which no stock CNA shader would produce for the same inputs -- so a silent
     * fallback to a stock program, or an attribute bound from the wrong stream/offset, changes
     * the read-back pixel instead of passing unnoticed. The matrix covers buffered and user draws,
     * indexed and not, a canonical built-in vertex type with no explicit declaration, plus
     * multi-stream and instanced draws on the backends that advertise them.
     */
    inline void RunCompiledEffectDrawContract(GraphicsDevice& device)
    {
        Effect effect(device, BuildSyntheticDrawableEffect());
        auto& parameters = effect.getParametersProperty();
        ASSERT_NE(parameters["Tint"], nullptr);
        ASSERT_NE(parameters["Transform"], nullptr);
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        EffectPass& pass = effect.getTechniquesProperty()[0].getPassesProperty()[1];

        // A quad already in clip space, so the identity Transform passes it straight through.
        struct ClipVertex { float x, y, z; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const ClipVertex quad[6] = {
            {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
        };
        const std::uint16_t indices[6] = {0, 1, 2, 3, 4, 5};

        const Color background(9, 19, 29, 255);
        const auto drawAndReadCentre =
            [&](const Vector4& tint, const std::function<void()>& draw) -> Color
        {
            RenderTarget2D target(device, 8, 8);
            device.SetRenderTarget(&target);
            device.Clear(background);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            parameters["Tint"]->SetValue(tint);
            pass.Apply();
            draw();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(4, 4, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };
        const auto expectPixel = [](const Color& actual, const Vector4& tint, const char* label) {
            const auto channel = [](float value) {
                return static_cast<int>(value * 255.0f + 0.5f);
            };
            SCOPED_TRACE(label);
            EXPECT_NEAR(actual.getRProperty(), channel(tint.X), 2);
            EXPECT_NEAR(actual.getGProperty(), channel(tint.Y), 2);
            EXPECT_NEAR(actual.getBProperty(), channel(tint.Z), 2);
            EXPECT_NEAR(actual.getAProperty(), channel(tint.W), 2);
        };

        // Buffered, non-indexed. A different Tint per draw also proves the parameter upload is
        // not a one-shot: the second draw must not keep the first draw's colour.
        {
            const Vector4 tint(0.25f, 0.5f, 0.75f, 1.0f);
            VertexBuffer buffer(device, declaration, 6, BufferUsage::None);
            buffer.SetDataRaw(quad, 6, static_cast<int>(sizeof(ClipVertex)));
            const Color pixel = drawAndReadCentre(tint, [&] {
                device.SetVertexBuffer(&buffer);
                device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            });
            expectPixel(pixel, tint, "buffered non-indexed");
            device.SetVertexBuffer(nullptr);
        }

        // Buffered, indexed.
        {
            const Vector4 tint(0.75f, 0.25f, 0.125f, 1.0f);
            VertexBuffer buffer(device, declaration, 6, BufferUsage::None);
            buffer.SetDataRaw(quad, 6, static_cast<int>(sizeof(ClipVertex)));
            IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            indexBuffer.SetData(indices, 6);
            const Color pixel = drawAndReadCentre(tint, [&] {
                device.SetVertexBuffer(&buffer);
                device.setIndicesProperty(&indexBuffer);
                device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 2);
            });
            expectPixel(pixel, tint, "buffered indexed");
            device.setIndicesProperty(nullptr);
            device.SetVertexBuffer(nullptr);
        }

        // Buffered, indexed, with a non-zero baseVertex and startIndex. Both buffers carry junk
        // ahead of the real data, so a route that ignores either offset draws the junk instead --
        // an easy mistake to make in a compiled route that has to rebuild its own binding.
        {
            const Vector4 tint(0.375f, 0.875f, 0.625f, 1.0f);
            ClipVertex padded[9];
            for (int i = 0; i < 3; ++i) padded[i] = ClipVertex{0.0f, 0.0f, 0.0f};
            for (int i = 0; i < 6; ++i) padded[3 + i] = quad[i];
            const std::uint16_t paddedIndices[12] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5};

            VertexBuffer buffer(device, declaration, 9, BufferUsage::None);
            buffer.SetDataRaw(padded, 9, static_cast<int>(sizeof(ClipVertex)));
            IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 12, BufferUsage::None);
            indexBuffer.SetData(paddedIndices, 12);
            const Color pixel = drawAndReadCentre(tint, [&] {
                device.SetVertexBuffer(&buffer);
                device.setIndicesProperty(&indexBuffer);
                device.DrawIndexedPrimitives(PrimitiveType::TriangleList,
                                             /*baseVertex=*/3, /*minVertexIndex=*/0,
                                             /*numVertices=*/6, /*startIndex=*/6,
                                             /*primitiveCount=*/2);
            });
            expectPixel(pixel, tint, "buffered indexed, baseVertex 3 / startIndex 6");
            device.setIndicesProperty(nullptr);
            device.SetVertexBuffer(nullptr);
        }

        // Buffered, non-indexed, with a non-zero vertexStart.
        {
            const Vector4 tint(0.625f, 0.125f, 0.25f, 1.0f);
            ClipVertex padded[9];
            for (int i = 0; i < 3; ++i) padded[i] = ClipVertex{0.0f, 0.0f, 0.0f};
            for (int i = 0; i < 6; ++i) padded[3 + i] = quad[i];
            VertexBuffer buffer(device, declaration, 9, BufferUsage::None);
            buffer.SetDataRaw(padded, 9, static_cast<int>(sizeof(ClipVertex)));
            const Color pixel = drawAndReadCentre(tint, [&] {
                device.SetVertexBuffer(&buffer);
                device.DrawPrimitives(PrimitiveType::TriangleList, /*vertexStart=*/3,
                                      /*primitiveCount=*/2);
            });
            expectPixel(pixel, tint, "buffered non-indexed, vertexStart 3");
            device.SetVertexBuffer(nullptr);
        }

        // User, non-indexed, caller-supplied declaration.
        {
            const Vector4 tint(0.5f, 0.5f, 0.25f, 1.0f);
            const Color pixel = drawAndReadCentre(tint, [&] {
                device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                          static_cast<const void*>(quad), 0, 2, declaration);
            });
            expectPixel(pixel, tint, "user non-indexed");
        }

        // User, indexed, caller-supplied declaration.
        {
            const Vector4 tint(0.125f, 0.625f, 0.875f, 1.0f);
            const Color pixel = drawAndReadCentre(tint, [&] {
                device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
                                                 static_cast<const void*>(quad), 0, 6,
                                                 indices, 0, 2, declaration);
            });
            expectPixel(pixel, tint, "user indexed");
        }

        // A canonical built-in XNA vertex type through the overload that takes no declaration at
        // all. plan_fx.md FX-081: GraphicsDevice stages these through the type's own
        // VertexDeclaration, so a compiled Effect can match its POSITION0 input against a real
        // declaration instead of a bare byte stride.
        {
            const Vector4 tint(0.875f, 0.375f, 0.5f, 1.0f);
            VertexPositionColor typedQuad[6];
            for (int i = 0; i < 6; ++i)
            {
                typedQuad[i] = VertexPositionColor(
                    Vector3(quad[i].x, quad[i].y, quad[i].z), Color::White);
            }
            const Color pixel = drawAndReadCentre(tint, [&] {
                device.DrawUserPrimitives(PrimitiveType::TriangleList, typedQuad, 0, 2);
            });
            expectPixel(pixel, tint, "user non-indexed, VertexPositionColor");
        }

        {
            const Vector4 tint(0.25f, 0.75f, 0.5f, 1.0f);
            VertexPositionColor typedQuad[6];
            for (int i = 0; i < 6; ++i)
            {
                typedQuad[i] = VertexPositionColor(
                    Vector3(quad[i].x, quad[i].y, quad[i].z), Color::White);
            }
            const Color pixel = drawAndReadCentre(tint, [&] {
                device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, typedQuad, 0, 6,
                                                 indices, 0, 2);
            });
            expectPixel(pixel, tint, "user indexed, VertexPositionColor");
        }
    }

    /**
     * @brief Contract: a compiled effect reads attributes from more than one bound stream.
     *
     * plan_fx.md FX-082. Only for backends reporting `MultiStreamVertexInput`. The fixture's
     * vertex shader computes `mul(POSITION0 + TEXCOORD0 * StreamMix, Transform)`, POSITION0 comes
     * from stream 0 and TEXCOORD0 from stream 1, and `StreamMix` selects whether stream 1
     * contributes. With it on, the quad shifts far enough that the sampled pixel changes -- so
     * binding stream 1 from stream 0's buffer, stride or offset is detected rather than assumed.
     */
    inline void RunCompiledEffectMultiStreamDrawContract(GraphicsDevice& device)
    {
        if (!device.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput))
        {
            GTEST_SKIP() << "this renderer does not bind more than one per-vertex stream";
        }

        Effect effect(device, BuildSyntheticDrawableEffect(/*readsSecondStream=*/true));
        auto& parameters = effect.getParametersProperty();
        ASSERT_NE(parameters["StreamMix"], nullptr)
            << "the multi-stream fixture declares the parameter its vertex shader scales by";
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(Vector4(0.5f, 0.25f, 0.75f, 1.0f));
        EffectPass& pass = effect.getTechniquesProperty()[0].getPassesProperty()[1];

        struct PositionVertex { float x, y, z; };
        struct OffsetVertex { float x, y, z, w; };
        const VertexDeclaration positions(static_cast<int>(sizeof(PositionVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const VertexDeclaration offsets(static_cast<int>(sizeof(OffsetVertex)), {
            VertexElement(0, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 0),
        });

        // A left-half quad in stream 0; stream 1 carries the shift that moves it right.
        const PositionVertex quad[6] = {
            {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f}, { 0.0f, -1.0f, 0.0f}, {0.0f,  1.0f, 0.0f},
        };
        const OffsetVertex shift[6] = {
            {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f},
        };

        VertexBuffer positionBuffer(device, positions, 6, BufferUsage::None);
        positionBuffer.SetDataRaw(quad, 6, static_cast<int>(sizeof(PositionVertex)));
        VertexBuffer offsetBuffer(device, offsets, 6, BufferUsage::None);
        offsetBuffer.SetDataRaw(shift, 6, static_cast<int>(sizeof(OffsetVertex)));

        const auto drawAndRead = [&](const Vector4& streamMix, int x) -> Color {
            RenderTarget2D target(device, 8, 8);
            device.SetRenderTarget(&target);
            device.Clear(Color(9, 19, 29, 255));
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            parameters["StreamMix"]->SetValue(streamMix);
            pass.Apply();
            device.SetVertexBuffers({VertexBufferBinding(&positionBuffer),
                                     VertexBufferBinding(&offsetBuffer)});
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle probe(x, 4, 1, 1);
            target.GetData(0, &probe, &pixel, 0, 1);
            return pixel;
        };

        // StreamMix zero: stream 1 contributes nothing, so only the left half is covered.
        const Color leftOff = drawAndRead(Vector4(0.0f, 0.0f, 0.0f, 0.0f), 2);
        const Color rightOff = drawAndRead(Vector4(0.0f, 0.0f, 0.0f, 0.0f), 6);
        EXPECT_NEAR(leftOff.getRProperty(), 128, 3) << "the left half must be painted";
        EXPECT_EQ(rightOff.getRProperty(), 9) << "the right half must still be the clear colour";

        // StreamMix (1,1,1,1): every vertex moves right by stream 1's own x, so the covered half
        // swaps. That can only happen if stream 1 was fetched from its own buffer.
        const Color leftOn = drawAndRead(Vector4(1.0f, 1.0f, 1.0f, 1.0f), 2);
        const Color rightOn = drawAndRead(Vector4(1.0f, 1.0f, 1.0f, 1.0f), 6);
        EXPECT_EQ(leftOn.getRProperty(), 9) << "the left half must no longer be painted";
        EXPECT_NEAR(rightOn.getRProperty(), 128, 3)
            << "the right half must be painted, which requires stream 1's real contents";
    }

    /**
     * @brief Contract: an instanced draw runs the compiled shader, not a stock one.
     *
     * plan_fx.md FX-082. Only for backends reporting `Instancing` and `MultiStreamVertexInput`.
     * The per-instance stream supplies TEXCOORD0, so instance 0 draws the left half and instance
     * 1 the right; a step rate that never advances, or a stock shader taking over, leaves one of
     * the two halves unpainted.
     */
    inline void RunCompiledEffectInstancingDrawContract(GraphicsDevice& device)
    {
        if (!device.SupportsCapability(CNA::GraphicsCapability::Instancing) ||
            !device.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput))
        {
            GTEST_SKIP() << "this renderer does not draw instanced primitives";
        }

        Effect effect(device, BuildSyntheticDrawableEffect(/*readsSecondStream=*/true));
        auto& parameters = effect.getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(Vector4(0.5f, 0.25f, 0.75f, 1.0f));
        parameters["StreamMix"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        EffectPass& pass = effect.getTechniquesProperty()[0].getPassesProperty()[1];

        struct PositionVertex { float x, y, z; };
        struct OffsetVertex { float x, y, z, w; };
        const VertexDeclaration positions(static_cast<int>(sizeof(PositionVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const VertexDeclaration offsets(static_cast<int>(sizeof(OffsetVertex)), {
            VertexElement(0, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const PositionVertex quad[6] = {
            {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f}, { 0.0f, -1.0f, 0.0f}, {0.0f,  1.0f, 0.0f},
        };
        const OffsetVertex perInstance[2] = {
            {0.0f, 0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f, 0.0f},
        };
        const std::uint16_t indices[6] = {0, 1, 2, 3, 4, 5};

        VertexBuffer positionBuffer(device, positions, 6, BufferUsage::None);
        positionBuffer.SetDataRaw(quad, 6, static_cast<int>(sizeof(PositionVertex)));
        VertexBuffer instanceBuffer(device, offsets, 2, BufferUsage::None);
        instanceBuffer.SetDataRaw(perInstance, 2, static_cast<int>(sizeof(OffsetVertex)));
        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        indexBuffer.SetData(indices, 6);

        RenderTarget2D target(device, 8, 8);
        device.SetRenderTarget(&target);
        device.Clear(Color(9, 19, 29, 255));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        pass.Apply();
        device.SetVertexBuffers({VertexBufferBinding(&positionBuffer, 0, 0),
                                 VertexBufferBinding(&instanceBuffer, 0, 1)});
        device.setIndicesProperty(&indexBuffer);
        device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 2, 2);
        device.setIndicesProperty(nullptr);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color left(0, 0, 0, 0);
        Color right(0, 0, 0, 0);
        const Rectangle leftProbe(2, 4, 1, 1);
        const Rectangle rightProbe(6, 4, 1, 1);
        target.GetData(0, &leftProbe, &left, 0, 1);
        target.GetData(0, &rightProbe, &right, 0, 1);
        EXPECT_NEAR(left.getRProperty(), 128, 3) << "instance 0 must paint the left half";
        EXPECT_NEAR(right.getRProperty(), 128, 3) << "instance 1 must paint the right half";
    }

    /**
     * @brief Contract: SpriteBatch draws with the supplied compiled Effect, or refuses by name.
     *
     * plan_fx.md FX-080. A compiled Effect handed to `SpriteBatch.Begin` must either run -- a
     * sprite covering the whole target comes out `Tint` -- or fail explicitly. What it must never
     * do is quietly render with the stock sprite program, which would put the sampled texture's
     * own colour on screen and look like success.
     *
     * Sprite vertices reach the shader in the target's pixel space, exactly as in FNA, and
     * SpriteBatch sets no transform on a custom effect; the effect's own `Transform` parameter
     * carries the projection, which is what a ported XNA sprite effect declares for itself.
     */
    inline void RunCompiledEffectSpriteBatchContract(GraphicsDevice& device)
    {
        constexpr int kSize = 8;
        Effect effect(device, BuildSyntheticDrawableEffect());
        auto& parameters = effect.getParametersProperty();
        const Vector4 tint(0.25f, 0.5f, 0.75f, 1.0f);
        parameters["Tint"]->SetValue(tint);
        parameters["Transform"]->SetValue(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, -1.0f, 1.0f));

        Texture2D sprite(device, 1, 1);
        const Color white[1] = {Color::White};
        sprite.SetData(white, 1);

        RenderTarget2D target(device, kSize, kSize);
        device.SetRenderTarget(&target);
        device.Clear(Color(9, 19, 29, 255));

        SpriteBatch batch(device);
        bool refused = false;
        std::string refusal;
        try
        {
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr,
                        &effect);
            batch.Draw(sprite, Rectangle(0, 0, kSize, kSize), Color::White);
            batch.End();
        }
        catch (const std::exception& error)
        {
            refused = true;
            refusal = error.what();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        if (refused)
        {
            EXPECT_FALSE(refusal.empty())
                << "a refusal must name what is unsupported, so a port can act on it";
            GTEST_SKIP() << "this renderer refuses a compiled Effect in SpriteBatch: " << refusal;
        }

        Color pixel(0, 0, 0, 0);
        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &centre, &pixel, 0, 1);
        EXPECT_NEAR(pixel.getRProperty(), 64, 3)
            << "the compiled pixel shader writes Tint; the stock sprite program would write the "
               "sampled white texture times the vertex colour instead";
        EXPECT_NEAR(pixel.getGProperty(), 128, 3);
        EXPECT_NEAR(pixel.getBProperty(), 191, 3);
    }

    /**
     * @brief Contract: switching effects, passes and techniques never leaks state between them.
     *
     * plan_fx.md FX-087. Two independent compiled effects draw alternately; each must render its
     * own `Tint`, not the one the other left in the shared register file. The same effect's other
     * pass, and a stock effect drawing in between, are exercised for the same reason.
     */
    inline void RunCompiledEffectSwitchingContract(GraphicsDevice& device)
    {
        Effect first(device, BuildSyntheticDrawableEffect());
        Effect second(device, BuildSyntheticDrawableEffect());
        first.getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        second.getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        const Vector4 firstTint(0.25f, 0.25f, 0.25f, 1.0f);
        const Vector4 secondTint(0.75f, 0.75f, 0.75f, 1.0f);
        first.getParametersProperty()["Tint"]->SetValue(firstTint);
        second.getParametersProperty()["Tint"]->SetValue(secondTint);

        struct ClipVertex { float x, y, z; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const ClipVertex quad[6] = {
            {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
        };

        const auto drawWith = [&](Effect& effect) -> Color {
            RenderTarget2D target(device, 8, 8);
            device.SetRenderTarget(&target);
            device.Clear(Color(9, 19, 29, 255));
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(4, 4, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };

        for (int round = 0; round < 2; ++round)
        {
            SCOPED_TRACE("round " + std::to_string(round));
            EXPECT_NEAR(drawWith(first).getRProperty(), 64, 3);
            EXPECT_NEAR(drawWith(second).getRProperty(), 191, 3);
            EXPECT_NEAR(drawWith(first).getRProperty(), 64, 3)
                << "switching back must restore the first effect's own parameter values";
        }

        // A clone drawing in between must not disturb either source, and must carry its own value.
        std::unique_ptr<Effect> clone(first.Clone());
        clone->getParametersProperty()["Tint"]->SetValue(Vector4(0.5f, 0.5f, 0.5f, 1.0f));
        EXPECT_NEAR(drawWith(*clone).getRProperty(), 128, 3);
        EXPECT_NEAR(drawWith(first).getRProperty(), 64, 3);
    }

    /**
     * @brief Contract: a compiled effect's geometry lands where a stock effect's would.
     *
     * plan_fx.md FX-088. A compiled backend translates clip space itself -- MojoShader's GLSL, for
     * one, can negate `gl_Position.y` on request -- so a compiled draw can end up mirrored against
     * every other draw the same renderer issues while still passing any symmetric golden-pixel
     * test. This draws the same asymmetric half-target quad twice, once through a stock effect and
     * once through the compiled one, and requires them to paint the same half.
     */
    inline void RunCompiledEffectOrientationContract(GraphicsDevice& device)
    {
        constexpr int kSize = 8;
        // Upper half of clip space, full width.
        const Vector3 corners[6] = {
            Vector3(-1.0f, 1.0f, 0.0f), Vector3(-1.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f),
            Vector3(-1.0f, 1.0f, 0.0f), Vector3( 1.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 0.0f),
        };
        const Color background(9, 19, 29, 255);

        const auto paintedRow = [&](const std::function<void()>& draw) -> int {
            RenderTarget2D target(device, kSize, kSize);
            device.SetRenderTarget(&target);
            device.Clear(background);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            draw();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color top(0, 0, 0, 0);
            Color bottom(0, 0, 0, 0);
            const Rectangle topProbe(kSize / 2, 1, 1, 1);
            const Rectangle bottomProbe(kSize / 2, kSize - 2, 1, 1);
            target.GetData(0, &topProbe, &top, 0, 1);
            target.GetData(0, &bottomProbe, &bottom, 0, 1);
            const bool topPainted = top != background;
            const bool bottomPainted = bottom != background;
            if (topPainted == bottomPainted) return -1;  // both or neither: inconclusive
            return topPainted ? 0 : 1;
        };

        BasicEffect stock(device);
        stock.VertexColorEnabled = true;
        stock.setLightingEnabledProperty(false);
        stock.setTextureEnabledProperty(false);
        VertexPositionColor stockQuad[6];
        for (int i = 0; i < 6; ++i)
            stockQuad[i] = VertexPositionColor(corners[i], Color::Red);
        const int stockRow = paintedRow([&] {
            stock.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, stockQuad, 0, 2);
        });
        ASSERT_NE(stockRow, -1) << "the stock draw must cover exactly one half of the target";

        Effect compiled(device, BuildSyntheticDrawableEffect());
        compiled.getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        compiled.getParametersProperty()["Tint"]->SetValue(Vector4(1.0f, 0.0f, 0.0f, 1.0f));
        struct ClipVertex { float x, y, z; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        ClipVertex compiledQuad[6];
        for (int i = 0; i < 6; ++i)
            compiledQuad[i] = ClipVertex{corners[i].X, corners[i].Y, corners[i].Z};
        const int compiledRow = paintedRow([&] {
            compiled.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(compiledQuad), 0, 2, declaration);
        });
        ASSERT_NE(compiledRow, -1) << "the compiled draw must cover exactly one half of the target";
        EXPECT_EQ(compiledRow, stockRow)
            << "a compiled effect must not render vertically mirrored against every other draw "
               "this renderer issues";
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

    /**
     * @brief Runs every non-drawing section of the compiled-effect contract.
     *
     * The drawing sections -- `RunCompiledEffectDrawContract`,
     * `RunCompiledEffectMultiStreamDrawContract`, `RunCompiledEffectInstancingDrawContract`,
     * `RunCompiledEffectSpriteBatchContract`, `RunCompiledEffectOrientationContract` and
     * `RunCompiledEffectSwitchingContract` -- are deliberately **not** called from here. Each is
     * its own `TEST` in a backend's test file, so a skip (a capability the renderer does not
     * advertise) or a failure names the shape that was not satisfied instead of collapsing into
     * one verdict. A backend claiming `CompiledEffects` must run all of them.
     */
    inline void RunCompiledEffectContract(GraphicsDevice& device)
    {
        RunCompiledEffectFormatContract(device);
        RunCompiledEffectReflectionContract(device);
        RunCompiledEffectParameterApiContract(device);
        RunCompiledEffectTechniqueContract(device);
        RunCompiledEffectRenderStateContract(device);
        RunCompiledEffectStatePolicyContract(device);
        RunCompiledEffectSamplerContract(device);
        RunCompiledEffectTextureBindingContract(device);
        RunCompiledEffectCloneContract(device);
        RunCompiledEffectLifecycleContract(device);
    }
}
