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
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
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

#include <algorithm>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/**
 * @file
 * @brief The shared compiled-effect contract every `CompiledEffects` backend must satisfy.
 *
 * plans/plan_fx.md FX-060: a backend becomes supported only after it passes the same suite, so the
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
    using Microsoft::Xna::Framework::Graphics::SpriteEffects;
    using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
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
    using Microsoft::Xna::Framework::Graphics::CubeMapFace;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::Texture3D;
    using Microsoft::Xna::Framework::Graphics::TextureCube;
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
     * plans/plan_fx.md FX-085. Reflection alone says a parameter is a 4x4 matrix; this says the value a
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

        // plans/plan_fx.md FX-104: and the positive half, on a parameter whose reflected type really is
        // String. Until this the pair was only ever exercised through its rejection path, so
        // "SetValue stores and GetValueString reads it back" was assumed rather than shown.
        Effect stringEffect(device, BuildSyntheticStringParameterEffect());
        auto& stringParameters = stringEffect.getParametersProperty();
        EffectParameter* caption = stringParameters["Caption"];
        ASSERT_NE(caption, nullptr) << "the fixture declares a String parameter named Caption";
        EXPECT_EQ(caption->getParameterTypeProperty(), EffectParameterType::String);
        EXPECT_EQ(caption->getParameterClassProperty(), EffectParameterClass::Object);
        EXPECT_EQ(caption->GetValueString(), "initial caption")
            << "a reflected string parameter reports the value the effect was compiled with";

        caption->SetValue(std::string("assigned"));
        EXPECT_EQ(caption->GetValueString(), "assigned");
        caption->SetValue(std::string());
        EXPECT_EQ(caption->GetValueString(), "")
            << "the empty string is a value, not a request to restore the initial one";
        caption->SetValue(std::string("second"));

        // A clone copies the value its source currently holds and then owns it.
        std::unique_ptr<Effect> stringClone(stringEffect.Clone());
        ASSERT_NE(stringClone, nullptr);
        EffectParameter* clonedCaption = stringClone->getParametersProperty()["Caption"];
        ASSERT_NE(clonedCaption, nullptr);
        EXPECT_EQ(clonedCaption->GetValueString(), "second");
        clonedCaption->SetValue(std::string("clone only"));
        EXPECT_EQ(clonedCaption->GetValueString(), "clone only");
        EXPECT_EQ(caption->GetValueString(), "second")
            << "writing a clone's string must not reach its source";
        caption->SetValue(std::string("source only"));
        EXPECT_EQ(clonedCaption->GetValueString(), "clone only")
            << "and writing the source's string must not reach the clone";

        // plans/plan_fx.md FX-105: the OTHER direction of the type check, which XNA also makes. A
        // compiled effect's object parameters store an effect object-table INDEX at their byte
        // offset, not a number, so a numeric accessor used to read that index back as a float and
        // a numeric setter used to overwrite it -- detaching the parameter from its object. Both
        // were silent.
        EXPECT_THROW((void) caption->GetValueSingle(), System::InvalidCastException);
        EXPECT_THROW((void) caption->GetValueInt32(), System::InvalidCastException);
        EXPECT_THROW((void) caption->GetValueVector4(), System::InvalidCastException);
        EXPECT_THROW((void) caption->GetValueMatrix(), System::InvalidCastException);
        EXPECT_THROW(caption->SetValue(1.0f), System::InvalidCastException);
        EXPECT_THROW(caption->SetValue(1), System::InvalidCastException);
        EXPECT_THROW(caption->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f)),
                     System::InvalidCastException);
        // A texture parameter is an object parameter too, and gets the same treatment.
        ASSERT_NE(parameters["Gain"], nullptr);
        Effect samplerEffect(device, BuildSyntheticSamplingEffect({}));
        EffectParameter* fxTexture = samplerEffect.getParametersProperty()["FxTexture"];
        ASSERT_NE(fxTexture, nullptr);
        EXPECT_THROW((void) fxTexture->GetValueSingle(), System::InvalidCastException);
        EXPECT_THROW(fxTexture->SetValue(2.0f), System::InvalidCastException);
        // ...and the value it really holds still reads back through its own accessor.
        EXPECT_NO_THROW((void) fxTexture->GetValueTexture2D());
        // A numeric parameter is unaffected: the guard rejects the object CLASS, not everything.
        EXPECT_NO_THROW((void) parameters["Gain"]->GetValueSingle());
        EXPECT_NO_THROW((void) parameters["Transform"]->GetValueMatrix());
        EXPECT_NO_THROW((void) parameters["Lighting"]->getStructureMembersProperty()[0]
                                   .GetValueSingle())
            << "a Struct parameter has real numeric storage and must not be caught by this guard";

        // Applying an effect that carries a string parameter must not try to upload it: a string
        // object's value storage is its object-table index, not text.
        EXPECT_NO_THROW(stringEffect.getTechniquesProperty()[0].getPassesProperty()[0].Apply());
    }

    /**
     * @brief Contract: the compiled shader is what actually draws, across the XNA draw matrix.
     *
     * plans/plan_fx.md FX-084/FX-086. Every draw fills the render target with the effect's own `Tint`
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
        // all. plans/plan_fx.md FX-081: GraphicsDevice stages these through the type's own
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
     * plans/plan_fx.md FX-082. Only for backends reporting `MultiStreamVertexInput`. The fixture's
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

        // --- Per-stream offsets that DIFFER, plus a non-zero vertexStart --------------------------
        //
        // plans/plan_fx.md FX-106. Everything above binds both streams at offset zero, so a route that
        // ignored VertexBufferBinding.VertexOffset entirely, or applied one stream's offset to
        // both, or folded the offsets together and lost the per-stream remainder, would pass. Here
        // each buffer carries a DIFFERENT amount of junk in front of its real data and the draw
        // additionally starts at a non-zero vertex, so the address of every attribute is
        // `stream.vertexOffset + vertexStart` records into that stream's own buffer -- and any of
        // those three mistakes reads junk instead.
        {
            constexpr int kPositionPad = 3;
            constexpr int kOffsetPad = 5;
            constexpr int kVertexStart = 2;

            // Junk that would draw nothing (a degenerate quad at the origin) if it were read.
            PositionVertex paddedQuad[kPositionPad + kVertexStart + 6];
            for (auto& vertex : paddedQuad) vertex = PositionVertex{0.0f, 0.0f, 0.0f};
            for (int i = 0; i < 6; ++i)
                paddedQuad[kPositionPad + kVertexStart + i] = quad[i];

            // Junk that would move the geometry the WRONG way if it were read.
            OffsetVertex paddedShift[kOffsetPad + kVertexStart + 6];
            for (auto& vertex : paddedShift) vertex = OffsetVertex{-1.0f, 0.0f, 0.0f, 0.0f};
            for (int i = 0; i < 6; ++i)
                paddedShift[kOffsetPad + kVertexStart + i] = shift[i];

            VertexBuffer paddedPositions(device, positions,
                                         kPositionPad + kVertexStart + 6, BufferUsage::None);
            paddedPositions.SetDataRaw(paddedQuad, kPositionPad + kVertexStart + 6,
                                       static_cast<int>(sizeof(PositionVertex)));
            VertexBuffer paddedOffsets(device, offsets,
                                       kOffsetPad + kVertexStart + 6, BufferUsage::None);
            paddedOffsets.SetDataRaw(paddedShift, kOffsetPad + kVertexStart + 6,
                                     static_cast<int>(sizeof(OffsetVertex)));

            const auto drawOffsetAndRead = [&](int x) -> Color {
                RenderTarget2D target(device, 8, 8);
                device.SetRenderTarget(&target);
                device.Clear(Color(9, 19, 29, 255));
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                device.setDepthStencilStateProperty(DepthStencilState::None);
                device.setBlendStateProperty(BlendState::Opaque);
                parameters["StreamMix"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                pass.Apply();
                device.SetVertexBuffers({
                    VertexBufferBinding(&paddedPositions, kPositionPad),
                    VertexBufferBinding(&paddedOffsets, kOffsetPad),
                });
                device.DrawPrimitives(PrimitiveType::TriangleList, kVertexStart, 2);
                device.SetVertexBuffer(nullptr);
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                Color pixel(0, 0, 0, 0);
                const Rectangle probe(x, 4, 1, 1);
                target.GetData(0, &probe, &pixel, 0, 1);
                return pixel;
            };
            EXPECT_EQ(drawOffsetAndRead(2).getRProperty(), 9)
                << "with both offsets consumed, the left half must be empty";
            EXPECT_NEAR(drawOffsetAndRead(6).getRProperty(), 128, 3)
                << "the right half must be painted, which needs BOTH streams read from their own "
                   "VertexOffset and then advanced by the draw's own vertexStart";
        }
    }

    /**
     * @brief Contract: an instanced draw runs the compiled shader, not a stock one.
     *
     * plans/plan_fx.md FX-082. Only for backends reporting `Instancing` and `MultiStreamVertexInput`.
     * The per-instance stream supplies TEXCOORD0, so instance 0 draws the left half and instance
     * 1 the right; a step rate that never advances, or a stock shader taking over, leaves one of
     * the two halves unpainted.
     */
    inline void RunCompiledEffectInstancingDrawContract(GraphicsDevice& device)
    {
        // plans/plan_fx.md FX-112: gated on `Instancing` ALONE. This shape binds one per-vertex stream
        // and one per-instance stream, which is not what `MultiStreamVertexInput` describes --
        // that capability is about binding SEVERAL streams of the SAME rate, and
        // `GraphicsDevice::SetVertexBuffers` lets one-of-each through without consulting it. Gating
        // on it as well silently excused every renderer that draws instanced primitives perfectly
        // well from one of the two streams-related contracts, which reads as coverage and is not.
        // A renderer that genuinely cannot do this must now say so, and gets skipped for what it
        // actually said.
        if (!device.SupportsCapability(CNA::GraphicsCapability::Instancing))
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
        // plans/plan_fx.md FX-112: a backend whose compiled route cannot draw instanced primitives must
        // REFUSE here rather than fall through to a stock shader, and a named refusal skips the
        // rest -- the same shape the SpriteBatch contracts use. Silence is the one unacceptable
        // answer, and it is what an unwired route produces.
        bool refused = false;
        std::string refusal;
        try
        {
            device.SetVertexBuffers({VertexBufferBinding(&positionBuffer, 0, 0),
                                     VertexBufferBinding(&instanceBuffer, 0, 1)});
            device.setIndicesProperty(&indexBuffer);
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 2, 2);
        }
        catch (const std::exception& error)
        {
            refused = true;
            refusal = error.what();
        }
        device.setIndicesProperty(nullptr);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        if (refused)
        {
            EXPECT_FALSE(refusal.empty())
                << "a refusal must name what is unsupported, so a port can act on it";
            GTEST_SKIP() << "this renderer refuses a compiled instanced draw: " << refusal;
        }

        Color left(0, 0, 0, 0);
        Color right(0, 0, 0, 0);
        const Rectangle leftProbe(2, 4, 1, 1);
        const Rectangle rightProbe(6, 4, 1, 1);
        target.GetData(0, &leftProbe, &left, 0, 1);
        target.GetData(0, &rightProbe, &right, 0, 1);
        EXPECT_NEAR(left.getRProperty(), 128, 3) << "instance 0 must paint the left half";
        EXPECT_NEAR(right.getRProperty(), 128, 3) << "instance 1 must paint the right half";

        // --- Non-zero baseVertex, startIndex and per-instance stream offset ----------------------
        //
        // plans/plan_fx.md FX-107. The draw above uses zero for all three, so a route that dropped any of
        // them would still paint both halves. Here every buffer carries junk in front of its real
        // data: the vertex buffer's junk is a degenerate quad, the index buffer's junk addresses
        // it, and the instance buffer's junk would move both instances the same way. Any of those
        // read instead of the real data changes which halves are painted.
        {
            constexpr int kVertexPad = 3;
            constexpr int kIndexPad = 6;
            constexpr int kInstancePad = 2;

            PositionVertex paddedQuad[kVertexPad + 6];
            for (auto& vertex : paddedQuad) vertex = PositionVertex{0.0f, 0.0f, 0.0f};
            for (int i = 0; i < 6; ++i) paddedQuad[kVertexPad + i] = quad[i];
            const std::uint16_t paddedIndices[kIndexPad + 6] = {0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5};
            OffsetVertex paddedInstances[kInstancePad + 2];
            for (auto& vertex : paddedInstances) vertex = OffsetVertex{0.0f, 0.0f, 0.0f, 0.0f};
            paddedInstances[kInstancePad + 0] = perInstance[0];
            paddedInstances[kInstancePad + 1] = perInstance[1];

            VertexBuffer paddedPositions(device, positions, kVertexPad + 6, BufferUsage::None);
            paddedPositions.SetDataRaw(paddedQuad, kVertexPad + 6,
                                       static_cast<int>(sizeof(PositionVertex)));
            VertexBuffer paddedInstanceBuffer(device, offsets, kInstancePad + 2, BufferUsage::None);
            paddedInstanceBuffer.SetDataRaw(paddedInstances, kInstancePad + 2,
                                            static_cast<int>(sizeof(OffsetVertex)));
            IndexBuffer paddedIndexBuffer(device, IndexElementSize::SixteenBits, kIndexPad + 6,
                                          BufferUsage::None);
            paddedIndexBuffer.SetData(paddedIndices, kIndexPad + 6);

            RenderTarget2D offsetTarget(device, 8, 8);
            device.SetRenderTarget(&offsetTarget);
            device.Clear(Color(9, 19, 29, 255));
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            pass.Apply();
            device.SetVertexBuffers({VertexBufferBinding(&paddedPositions, 0, 0),
                                     VertexBufferBinding(&paddedInstanceBuffer, kInstancePad, 1)});
            device.setIndicesProperty(&paddedIndexBuffer);
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList,
                                           /*baseVertex=*/kVertexPad, /*minVertexIndex=*/0,
                                           /*numVertices=*/6, /*startIndex=*/kIndexPad,
                                           /*primitiveCount=*/2, /*instanceCount=*/2);
            device.setIndicesProperty(nullptr);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            Color offsetLeft(0, 0, 0, 0);
            Color offsetRight(0, 0, 0, 0);
            offsetTarget.GetData(0, &leftProbe, &offsetLeft, 0, 1);
            offsetTarget.GetData(0, &rightProbe, &offsetRight, 0, 1);
            EXPECT_NEAR(offsetLeft.getRProperty(), 128, 3)
                << "instance 0 must still paint the left half with a non-zero baseVertex, "
                   "startIndex and instance-stream VertexOffset";
            EXPECT_NEAR(offsetRight.getRProperty(), 128, 3)
                << "instance 1 must still paint the right half";
        }

        // --- The instance divisor must not survive into a later non-instanced draw ---------------
        //
        // plans/plan_fx.md FX-107. A compiled route that shares one vertex-array object across every
        // compiled draw carries the divisor with it; left at 1, the next ordinary draw advances
        // TEXCOORD0 once per instance instead of once per vertex, so every vertex reads instance
        // 0's offset and the geometry stops depending on the stream at all. With StreamMix on and
        // a per-VERTEX stream whose values differ per vertex, that is visible: only a per-vertex
        // divisor moves the two halves apart.
        //
        // plans/plan_fx.md FX-112: this section, and only this section, binds TWO PER-VERTEX streams, so
        // it is the one part of this contract that genuinely needs MultiStreamVertexInput. It used
        // to gate the whole contract, which excused every instancing-capable renderer without
        // multi-stream input from the two sections above as well.
        if (!device.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput))
        {
            return;
        }
        {
            const OffsetVertex perVertexShift[6] = {
                {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f},
                {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f},
            };
            VertexBuffer perVertexBuffer(device, offsets, 6, BufferUsage::None);
            perVertexBuffer.SetDataRaw(perVertexShift, 6, static_cast<int>(sizeof(OffsetVertex)));

            RenderTarget2D after(device, 8, 8);
            device.SetRenderTarget(&after);
            device.Clear(Color(9, 19, 29, 255));
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            pass.Apply();
            device.SetVertexBuffers({VertexBufferBinding(&positionBuffer),
                                     VertexBufferBinding(&perVertexBuffer)});
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            Color afterLeft(0, 0, 0, 0);
            Color afterRight(0, 0, 0, 0);
            after.GetData(0, &leftProbe, &afterLeft, 0, 1);
            after.GetData(0, &rightProbe, &afterRight, 0, 1);
            EXPECT_EQ(afterLeft.getRProperty(), 9)
                << "an ordinary draw after an instanced one must read its second stream per "
                   "vertex, not per instance";
            EXPECT_NEAR(afterRight.getRProperty(), 128, 3)
                << "the whole quad must have moved right, which only a per-vertex divisor does";
        }
    }

    /**
     * @brief Contract: SpriteBatch draws with the supplied compiled Effect, or refuses by name.
     *
     * plans/plan_fx.md FX-080. A compiled Effect handed to `SpriteBatch.Begin` must either run -- a
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
     * @brief Contract: a multi-pass compiled Effect batches the way XNA's SpriteBatch does.
     *
     * plans/plan_fx.md FX-102. XNA runs a compiled Effect's passes at FLUSH granularity, over a whole
     * contiguous run of same-texture sprites: FNA's `SpriteBatch.FlushBatch` splits the batch into
     * texture runs and `DrawPrimitives` wraps each run's draw in `foreach (pass)`. So for two
     * sprites sharing a texture and two passes the submission order is pass-major -- s0p0, s1p0,
     * s0p1, s1p1 -- not sprite-major.
     *
     * Under an opaque blend the two orders are indistinguishable, because either way the last write
     * to a pixel is the last sprite's last pass. This section therefore uses
     * `BlendState.NonPremultiplied` and two half-transparent, fully overlapping sprites, where each
     * submission's contribution depends on its position in the sequence. The fixture's pass 0
     * writes `Tint.yzxw` and pass 1 writes `Tint`, so with `Tint` = (1, 0, 0, 0.5) the red channel
     * receives 0, 0, 1, 1 in XNA's order and 0, 1, 0, 1 in the sprite-major one -- 192 against 160
     * out of 255, far outside any rounding.
     *
     * @param device Device whose renderer claims CompiledEffects.
     */
    inline void RunCompiledEffectSpriteBatchMultiPassContract(GraphicsDevice& device)
    {
        constexpr int kSize = 8;
        const Color background(0, 0, 0, 255);

        Effect effect(device, BuildSyntheticDrawableEffect());
        auto& parameters = effect.getParametersProperty();
        parameters["Tint"]->SetValue(Vector4(1.0f, 0.0f, 0.0f, 0.5f));
        parameters["Transform"]->SetValue(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, -1.0f, 1.0f));
        ASSERT_EQ(effect.getTechniquesProperty()[0].getPassesProperty().getCountProperty(), 2)
            << "this contract needs a fixture whose current technique has two distinguishable passes";

        Texture2D sprite(device, 1, 1);
        const Color white[1] = {Color::White};
        sprite.SetData(white, 1);

        RenderTarget2D target(device, kSize, kSize);
        device.SetRenderTarget(&target);
        device.Clear(background);

        SpriteBatch batch(device);
        bool refused = false;
        std::string refusal;
        try
        {
            batch.Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, nullptr, nullptr,
                        nullptr, &effect);
            // Two sprites, one texture, exactly overlapping: one run, so XNA's pass loop wraps both.
            batch.Draw(sprite, Rectangle(0, 0, kSize, kSize), Color::White);
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
            EXPECT_FALSE(refusal.empty());
            GTEST_SKIP() << "this renderer refuses a compiled Effect in SpriteBatch: " << refusal;
        }

        Color pixel(0, 0, 0, 0);
        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &centre, &pixel, 0, 1);
        EXPECT_NEAR(pixel.getRProperty(), 192, 4)
            << "XNA draws the whole texture run once per pass; a route that instead draws each "
               "sprite through every pass before moving to the next produces 160 here";

        // The same batch with the sprites APART: both must be painted, and each must show the last
        // pass's own colour, so a route that stops after pass 0 (or runs the passes backwards)
        // fails even without an order-sensitive blend.
        RenderTarget2D pair(device, kSize, kSize);
        device.SetRenderTarget(&pair);
        device.Clear(background);
        parameters["Tint"]->SetValue(Vector4(0.25f, 0.5f, 0.75f, 1.0f));
        SpriteBatch second(device);
        second.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr,
                     &effect);
        second.Draw(sprite, Rectangle(0, 0, kSize / 2, kSize), Color::White);
        second.Draw(sprite, Rectangle(kSize / 2, 0, kSize / 2, kSize), Color::White);
        second.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color left(0, 0, 0, 0);
        Color right(0, 0, 0, 0);
        const Rectangle leftProbe(1, kSize / 2, 1, 1);
        const Rectangle rightProbe(kSize - 2, kSize / 2, 1, 1);
        pair.GetData(0, &leftProbe, &left, 0, 1);
        pair.GetData(0, &rightProbe, &right, 0, 1);
        for (const auto& [probe, label] : {std::pair<Color, const char*>{left, "left sprite"},
                                           std::pair<Color, const char*>{right, "right sprite"}})
        {
            SCOPED_TRACE(label);
            EXPECT_NEAR(probe.getRProperty(), 64, 3)
                << "the last pass of the technique is what stands on every sprite";
            EXPECT_NEAR(probe.getGProperty(), 128, 3);
            EXPECT_NEAR(probe.getBProperty(), 191, 3);
        }
    }

    /**
     * @brief Contract: SpriteBatch gives a compiled Effect the sprite's own texture in slot 0.
     *
     * plans/plan_fx.md FX-103. FNA's `SpriteBatch.DrawPrimitives` sets `GraphicsDevice.Textures[0] =
     * texture` immediately AFTER `pass.Apply()`, with the comment "Set this _after_ Apply,
     * otherwise EffectParameters override it!". So a sprite effect samples the sprite being drawn,
     * not whatever texture the effect's own texture parameter names -- and a backend that binds the
     * effect's texture instead renders a plausible image of entirely the wrong thing.
     *
     * The two textures here are solid and far apart in colour, so which one reached the sampler is
     * the read-back pixel. Source rectangles and `SpriteEffects` are checked in the same batch,
     * since both reach the shader as texture coordinates and are therefore only observable once
     * something actually samples.
     *
     * @param device Device whose renderer claims CompiledEffects.
     */
    inline void RunCompiledEffectSpriteBatchTextureSlotContract(GraphicsDevice& device)
    {
        namespace Fx = EffectFormat;
        constexpr int kSize = 8;
        const Color background(9, 19, 29, 255);

        Effect effect(device, BuildSyntheticSamplingEffect({
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
        }));
        auto& parameters = effect.getParametersProperty();
        parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        parameters["Transform"]->SetValue(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, -1.0f, 1.0f));

        // The effect's own texture parameter names the one that must NOT win.
        Texture2D effectTexture(device, 1, 1);
        const Color effectPixel[1] = {Color(255, 0, 0, 255)};
        effectTexture.SetData(effectPixel, 1);
        parameters["FxTexture"]->SetValue(&effectTexture);

        // The sprite's own texture: a 2x1 pair of columns, so the source rectangle and the
        // horizontal flip are observable as well as the binding itself.
        Texture2D spriteTexture(device, 2, 1);
        const Color spritePixels[2] = {Color(0, 255, 0, 255), Color(0, 0, 255, 255)};
        spriteTexture.SetData(spritePixels, 2);

        const auto drawSprite = [&](const Rectangle& source, SpriteEffects effects) -> Color {
            RenderTarget2D target(device, kSize, kSize);
            device.SetRenderTarget(&target);
            device.Clear(background);
            SpriteBatch batch(device);
            SamplerState pointClamp = SamplerState::PointClamp;
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp,
                        nullptr, nullptr, &effect);
            batch.Draw(spriteTexture, Rectangle(0, 0, kSize, kSize),
                       std::optional<Rectangle>(source), Color::White,
                       0.0f, Vector2::Zero, effects, 0.0f);
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };

        // The whole 2x1 texture across the target: the centre pixel lands in the right-hand column.
        const Color whole = drawSprite(Rectangle(0, 0, 2, 1), SpriteEffects::None);
        EXPECT_NEAR(whole.getRProperty(), 0, 3)
            << "the sprite's own texture must reach slot 0, not the effect's FxTexture (red)";
        EXPECT_NEAR(whole.getBProperty(), 255, 3)
            << "the centre of a full-width 2x1 sprite samples its right-hand texel";

        // Only the LEFT texel: the whole sprite is green.
        const Color leftOnly = drawSprite(Rectangle(0, 0, 1, 1), SpriteEffects::None);
        EXPECT_NEAR(leftOnly.getGProperty(), 255, 3)
            << "a source rectangle must reach the compiled shader as texture coordinates";
        EXPECT_NEAR(leftOnly.getRProperty(), 0, 3);

        // Flipped horizontally: the centre now lands in the left-hand column instead.
        const Color flipped = drawSprite(Rectangle(0, 0, 2, 1), SpriteEffects::FlipHorizontally);
        EXPECT_NEAR(flipped.getGProperty(), 255, 3)
            << "SpriteEffects must reach the compiled shader as texture coordinates";
        EXPECT_NEAR(flipped.getRProperty(), 0, 3);
    }

    /**
     * @brief Contract: SpriteBatch + a compiled Effect samples a `RenderTarget2D` right way up.
     *
     * plans/plan_fx.md FX-118, and the reason it is a section of its own rather than a case inside
     * `RunCompiledEffectRenderTargetSourceContract`: that one drives the compiled route through
     * `DrawUserPrimitives`, where the game supplies its own vertices. SpriteBatch supplies the
     * vertices instead, and a renderer that corrects a rendered source's row order in the sprite's
     * texture coordinates AND again in the bound texture applies the correction twice -- an image
     * that is upside down while every hop of the 3D route is right way up. Found by porting
     * Microsoft's BloomPostprocess sample, whose whole postprocess is exactly this: four
     * fullscreen `SpriteBatch` quads, each one a compiled `.fx` reading the render target the
     * previous quad wrote.
     *
     * The last case is the one that keeps a fix honest in the other direction: an ordinary
     * `Texture2D` drawn the same way must be unaffected by any row-order correction.
     *
     * @param device The graphics device under test.
     */
    inline void RunCompiledEffectSpriteBatchRenderTargetSourceContract(GraphicsDevice& device)
    {
        namespace Fx = EffectFormat;
        constexpr int kSize = 8;
        const Color red(255, 0, 0, 255);
        const Color blue(0, 0, 255, 255);
        const Color background(9, 19, 29, 255);

        Texture2D white(device, 1, 1);
        const Color whitePixel[1] = {Color::White};
        white.SetData(whitePixel, 1);

        // A source with a red TOP half and a blue BOTTOM half, in XNA's own top-down pixel space.
        const auto paintHalves = [&](RenderTarget2D& target) {
            device.SetRenderTarget(&target);
            device.Clear(background);
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            batch.Draw(white, Rectangle(0, 0, kSize, kSize / 2), red);
            batch.Draw(white, Rectangle(0, kSize / 2, kSize, kSize / 2), blue);
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        };
        const auto expectRedOverBlue = [&](RenderTarget2D& target, const char* label) {
            SCOPED_TRACE(label);
            const Rectangle topProbe(kSize / 2, 1, 1, 1);
            const Rectangle bottomProbe(kSize / 2, kSize - 2, 1, 1);
            Color top(0, 0, 0, 0), bottom(0, 0, 0, 0);
            target.GetData(0, &topProbe, &top, 0, 1);
            target.GetData(0, &bottomProbe, &bottom, 0, 1);
            EXPECT_NEAR(top.getRProperty(), 255, 3) << "the top half must be red";
            EXPECT_NEAR(top.getBProperty(), 0, 3) << "the top half must be red";
            EXPECT_NEAR(bottom.getBProperty(), 255, 3) << "the bottom half must be blue";
            EXPECT_NEAR(bottom.getRProperty(), 0, 3) << "the bottom half must be blue";
        };

        Effect effect(device, BuildSyntheticSamplingEffect({
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
        }));
        auto& parameters = effect.getParametersProperty();
        parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        // The fixture's pass assigns its own vertex shader, so it needs SpriteBatch's own
        // projection rather than the stock sprite vertex shader a pixel-only pass inherits.
        parameters["Transform"]->SetValue(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, -1.0f, 1.0f));

        const auto blitThroughEffect = [&](Texture2D& source, RenderTarget2D& destination) {
            device.SetRenderTarget(&destination);
            device.Clear(background);
            SpriteBatch batch(device);
            SamplerState pointClamp = SamplerState::PointClamp;
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp,
                        nullptr, nullptr, &effect);
            batch.Draw(source, Rectangle(0, 0, kSize, kSize), Color::White);
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        };

        RenderTarget2D source(device, kSize, kSize);
        paintHalves(source);
        expectRedOverBlue(source, "the source target itself, read back directly");

        RenderTarget2D firstHop(device, kSize, kSize);
        blitThroughEffect(source, firstHop);
        expectRedOverBlue(firstHop,
                          "RenderTarget2D -> SpriteBatch + compiled Effect -> RenderTarget2D");

        // A second hop, so a renderer that flips once per hop cannot come out right by accident.
        RenderTarget2D secondHop(device, kSize, kSize);
        blitThroughEffect(firstHop, secondHop);
        expectRedOverBlue(secondHop,
                          "the same, twice: a per-hop flip would be visible here");

        // plans/plan_fx.md FX-120: the same SpriteBatch flushed TWICE. The compiled route records its
        // geometry in one long-lived vertex array object, and it used to create and destroy that
        // geometry inside every flush, leaving the array object holding a deleted element buffer
        // for the next draw to read. Desktop GL tolerates that and draws anyway, which is why
        // this case cannot fail here -- it was observable only on WEBGL2, where the draw is
        // refused outright with "glDrawElements: Insufficient buffer size" and every flush after
        // a batch's first produced nothing. It is pinned so a backend that validates its bindings
        // has something to fail on.
        {
            RenderTarget2D first(device, kSize, kSize);
            RenderTarget2D second(device, kSize, kSize);
            SpriteBatch batch(device);
            SamplerState pointClamp = SamplerState::PointClamp;
            for (RenderTarget2D* destination : {&first, &second})
            {
                device.SetRenderTarget(destination);
                device.Clear(background);
                batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp,
                            nullptr, nullptr, &effect);
                batch.Draw(source, Rectangle(0, 0, kSize, kSize), Color::White);
                batch.End();
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            }
            expectRedOverBlue(first, "the first flush of a reused SpriteBatch");
            expectRedOverBlue(second, "the second flush of the SAME SpriteBatch");
        }

        // The other direction: an ordinary Texture2D source must be untouched by any correction.
        {
            Texture2D plain(device, kSize, kSize);
            std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize));
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x)
                    pixels[static_cast<std::size_t>(y * kSize + x)] = y < kSize / 2 ? red : blue;
            plain.SetData(pixels.data(), static_cast<int>(pixels.size()));

            RenderTarget2D destination(device, kSize, kSize);
            blitThroughEffect(plain, destination);
            expectRedOverBlue(destination,
                              "Texture2D -> SpriteBatch + compiled Effect -> RenderTarget2D");
        }
    }

    /**
     * @brief Contract: no truncation of a real effect wedges, crashes, or half-builds a runtime.
     *
     * plans/plan_fx.md FX-112. Every backend parses the effect container through the same pinned
     * MojoShader, so this is shared rather than per-backend. Three outcomes are possible for a
     * truncated input and only two are acceptable. Refusing is the usual one. Parsing successfully
     * is also legitimate -- the last few lengths drop only trailing bytes MojoShader does not need
     * -- but then the runtime has to be WHOLE rather than a half-built object that fails at the
     * next call. Crashing, wedging, or handing back a usable-looking half is the third.
     *
     * Measured while writing this, and worth knowing before reading too much into a pass: going
     * through the public `Effect` constructor, NO truncation of this fixture reaches the assertion
     * inside MojoShader's own parser (`mojoshader_effects.c` `readvalue`) -- CNA's own container
     * validation refuses first. That makes this a contract about the public boundary's robustness,
     * not a proof of the harness assertion policy. The proof of that policy is the sweep that goes
     * at a renderer's compiled-effect runtime DIRECTLY, below the public validation, where the
     * assertion is genuinely reachable (`VulkanCompiledEffectTest.
     * MalformedBytecodeIsRejectedWithoutCrashing`, plans/plan_fx.md FX-111).
     *
     * Sweeping every 4-byte length rather than pinning one keeps it honest across fixture changes,
     * and under a sanitizer the sweep is the real question: a parser walking off the end of a
     * truncated buffer must be caught here rather than in a game.
     *
     * @param device Device whose renderer claims CompiledEffects.
     */
    inline void RunCompiledEffectTruncationContract(GraphicsDevice& device)
    {
        // The COMPILER-PRODUCED fixture, not the synthetic builder's output, and the difference is
        // load-bearing rather than cosmetic: only the real container has the nested value tables
        // whose truncation reaches `readvalue`'s assertion, which is the case the assertion policy
        // exists for. The synthetic effect is refused earlier every time, so a sweep over it would
        // pass without ever exercising the thing being claimed. Falls back to the synthetic effect
        // only if the fixture is missing, and says so rather than silently covering less.
        const std::filesystem::path fixture = std::filesystem::path(__FILE__).parent_path() /
            "../../../../modules/renderers/fna3d/effects/CnaConformanceEffect.fxb";
        std::vector<std::uint8_t> whole;
        {
            std::ifstream input(fixture, std::ios::binary);
            if (input)
            {
                whole.assign(std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>());
            }
        }
        if (whole.empty())
        {
            ADD_FAILURE() << "the committed CnaConformanceEffect.fxb fixture was not found at "
                          << fixture << "; this contract would otherwise silently cover less than "
                             "it claims";
            return;
        }
        ASSERT_GT(whole.size(), 64u) << "the fixture is too small to truncate meaningfully";

        for (std::size_t bytes = 4; bytes < whole.size(); bytes += 4)
        {
            const std::vector<std::uint8_t> truncated(
                whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(bytes));
            std::unique_ptr<Effect> parsed;
            try
            {
                parsed = std::make_unique<Effect>(device, truncated);
            }
            catch (const std::exception&)
            {
                continue;  // refused: the expected outcome for almost every length
            }
            ASSERT_NE(parsed, nullptr) << "truncated to " << bytes << " bytes";
            // Accepted, so it must be usable: reflection present, and the parameter and technique
            // collections reachable without throwing.
            EXPECT_GT(parsed->getTechniquesProperty().getCountProperty(), 0)
                << "truncated to " << bytes << " bytes: accepted, but reflects no technique";
            EXPECT_NO_THROW((void)parsed->getParametersProperty().getCountProperty())
                << "truncated to " << bytes << " bytes";
        }
    }

    /**
     * @brief Contract: many compiled draws in one frame each keep their own uniform values.
     *
     * plans/plan_fx.md FX-112. Every backend has to park each draw's constant values somewhere until the
     * draw is actually submitted, and the natural shape -- one buffer, sliced per draw -- has a
     * fixed capacity. What happens at the end of it is the interesting part: a ring that WRAPS
     * hands two draws in one frame the same slice, so the earlier one silently renders with the
     * later one's constants, and the failure looks like nothing at all until a scene grows past
     * the ring. A compiled sprite effect makes one draw per sprite per pass, so a few hundred
     * compiled draws in a frame is ordinary rather than exceptional.
     *
     * 600 draws, each covering one cell of a 32x32 target with its own `Tint`, then three cells
     * read back. The tint of draw i is deliberately NOT periodic in any power of two: with a
     * `% 256` colour the draw at i and the draw at i + 256 are equal by construction, so a
     * 256-slice ring that wrapped would look correct and this contract would pass against the very
     * defect it exists to catch. Prime moduli make all 600 distinct.
     *
     * @param device Device whose renderer claims CompiledEffects.
     */
    inline void RunCompiledEffectManyDrawsContract(GraphicsDevice& device)
    {
        constexpr int kSize = 32;
        constexpr int kDraws = 600;

        Effect effect(device, BuildSyntheticDrawableEffect());
        auto& parameters = effect.getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        EffectPass& pass = effect.getTechniquesProperty()[0].getPassesProperty()[1];

        struct ClipVertex { float x, y, z; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });

        const auto tintFor = [](int i) {
            return Vector4(static_cast<float>((i * 7) % 251) / 255.0f,
                           static_cast<float>((i * 13) % 241) / 255.0f,
                           static_cast<float>((i * 31) % 239) / 255.0f, 1.0f);
        };

        RenderTarget2D target(device, kSize, kSize);
        device.SetRenderTarget(&target);
        device.Clear(Color(9, 19, 29, 255));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        for (int i = 0; i < kDraws; ++i)
        {
            const int cellX = i % kSize;
            const int cellY = i / kSize;
            const float x0 = -1.0f + 2.0f * static_cast<float>(cellX) / kSize;
            const float x1 = -1.0f + 2.0f * static_cast<float>(cellX + 1) / kSize;
            const float y0 = -1.0f + 2.0f * static_cast<float>(cellY) / kSize;
            const float y1 = -1.0f + 2.0f * static_cast<float>(cellY + 1) / kSize;
            const ClipVertex cell[6] = {
                {x0, y0, 0.0f}, {x0, y1, 0.0f}, {x1, y1, 0.0f},
                {x0, y0, 0.0f}, {x1, y1, 0.0f}, {x1, y0, 0.0f},
            };
            parameters["Tint"]->SetValue(tintFor(i));
            pass.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, cell, 0, 2, declaration);
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // One draw early, and one just past each of the two 256-slice boundaries a ring of that
        // size would have. A backend whose ring is a different size is covered too: 600 distinct
        // colours in one frame cannot survive ANY wrap that reuses a slice these three read.
        for (const int i : {5, 256, 512})
        {
            SCOPED_TRACE("draw " + std::to_string(i));
            const Vector4 tint = tintFor(i);
            Color pixel(0, 0, 0, 0);
            // Clip-space y = -1 is the target's LAST row, so cellY counts up from the bottom.
            const Rectangle cell(i % kSize, kSize - 1 - (i / kSize), 1, 1);
            target.GetData(0, &cell, &pixel, 0, 1);
            const auto channel = [](float value) { return static_cast<int>(value * 255.0f + 0.5f); };
            EXPECT_NEAR(pixel.getRProperty(), channel(tint.X), 2)
                << "this draw rendered with another draw's uniform values";
            EXPECT_NEAR(pixel.getGProperty(), channel(tint.Y), 2);
            EXPECT_NEAR(pixel.getBProperty(), channel(tint.Z), 2);
        }
    }

    /**
     * @brief Contract: switching effects, passes and techniques never leaks state between them.
     *
     * plans/plan_fx.md FX-087/FX-098. Two independent compiled effects draw alternately; each must render
     * its own `Tint`, not the one the other left in the shared register file. A clone drawing in
     * between is exercised for the same reason.
     *
     * The stock draws are the other half, and they are not symmetric with the compiled ones. A
     * backend can share GPU state between the two families in ways that only break in one
     * direction: on EasyGL, MojoShader shadows the bound GL program and re-issues `glUseProgram`
     * only when its OWN idea of the program changes, so a stock draw's program stayed current and
     * every later compiled draw silently ran the stock shader (FX-098). Each stock draw here is
     * therefore followed by a compiled draw whose colour is checked, and a stock draw of its own
     * that must still be correct afterwards.
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

        // --- Stock draws in between, in both directions ----------------------------------------
        //
        // A stock BasicEffect quad and a stock SpriteBatch sprite, each followed by a compiled
        // draw that must still be the compiled effect's own colour. Constructing the SpriteBatch is
        // itself part of the test: on a backend that binds a GL program while building its sprite
        // resources, that alone used to be enough to redirect every later compiled draw.
        BasicEffect stock(device);
        stock.VertexColorEnabled = true;
        stock.setLightingEnabledProperty(false);
        stock.setTextureEnabledProperty(false);
        VertexPositionColor stockQuad[6];
        for (int i = 0; i < 6; ++i)
        {
            stockQuad[i] = VertexPositionColor(Vector3(quad[i].x, quad[i].y, quad[i].z),
                                               Color(0, 255, 0, 255));
        }
        const auto drawStock3D = [&]() -> Color {
            RenderTarget2D target(device, 8, 8);
            device.SetRenderTarget(&target);
            device.Clear(Color(9, 19, 29, 255));
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            stock.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, stockQuad, 0, 2);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(4, 4, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };

        Texture2D white(device, 1, 1);
        const Color whitePixel[1] = {Color::White};
        white.SetData(whitePixel, 1);
        const auto drawStockSprite = [&]() -> Color {
            RenderTarget2D target(device, 8, 8);
            device.SetRenderTarget(&target);
            device.Clear(Color(9, 19, 29, 255));
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            batch.Draw(white, Rectangle(0, 0, 8, 8), Color(0, 0, 255, 255));
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(4, 4, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };

        EXPECT_NEAR(drawStock3D().getGProperty(), 255, 3) << "the stock 3D draw's own colour";
        EXPECT_NEAR(drawWith(first).getRProperty(), 64, 3)
            << "a compiled draw after a stock 3D draw must still run the compiled program";
        EXPECT_NEAR(drawStockSprite().getBProperty(), 255, 3) << "the stock sprite's own colour";
        EXPECT_NEAR(drawWith(second).getRProperty(), 191, 3)
            << "a compiled draw after a stock SpriteBatch must still run the compiled program";
        EXPECT_NEAR(drawStock3D().getGProperty(), 255, 3)
            << "and a stock draw after a compiled one must still run the stock program";
        EXPECT_NEAR(drawWith(first).getRProperty(), 64, 3);
    }

    /**
     * @brief Contract: a compiled effect's geometry lands where a stock effect's would.
     *
     * plans/plan_fx.md FX-088. A compiled backend translates clip space itself -- MojoShader's GLSL, for
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
     * @brief A full-target clip-space quad with a TEXCOORD0 the caller chooses. CNAEXT.
     *
     * plans/plan_fx.md FX-093. Every UV is the SAME value, so the sampled texel is a property of the
     * sampler alone: there is no interpolation across the quad and therefore no dependence on
     * where a rasterizer places a pixel centre. That is what keeps an addressing-mode or filter
     * assertion about a chosen texel rather than about the rounding of a coordinate.
     */
    struct SamplingQuadVertex
    {
        float x, y, z;
        float u, v;
    };

    /** @brief The declaration `SamplingQuadVertex` is streamed with. */
    [[nodiscard]] inline VertexDeclaration SamplingQuadDeclaration()
    {
        return VertexDeclaration(static_cast<int>(sizeof(SamplingQuadVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
    }

    /**
     * @brief Fills a six-vertex full-target quad whose every corner carries @p u / @p v.
     *
     * @param quad Destination, six vertices.
     * @param u Texture coordinate U written to every corner.
     * @param v Texture coordinate V written to every corner.
     */
    inline void FillSamplingQuad(SamplingQuadVertex (&quad)[6], float u, float v)
    {
        const float corners[6][2] = {
            {-1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, -1.0f},
            {-1.0f, 1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
        };
        for (int i = 0; i < 6; ++i)
            quad[i] = SamplingQuadVertex{corners[i][0], corners[i][1], 0.0f, u, v};
    }

    /**
     * @brief The three-component sibling of @ref SamplingQuadVertex. CNAEXT.
     *
     * plans/plan_fx.md FX-110. A cube sampler takes a direction and a volume sampler a 3D coordinate, so
     * the fixture's vertex shader forwards `oT0.xyz` and the stream has to carry three components.
     */
    struct SamplingQuadVertexXYZ
    {
        float x, y, z;
        float u, v, w;
    };

    /** @brief The declaration `SamplingQuadVertexXYZ` is streamed with. */
    [[nodiscard]] inline VertexDeclaration SamplingQuadDeclarationXYZ()
    {
        return VertexDeclaration(static_cast<int>(sizeof(SamplingQuadVertexXYZ)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3,
                          VertexElementUsage::TextureCoordinate, 0),
        });
    }

    /**
     * @brief Fills a six-vertex full-target quad whose every corner carries @p u / @p v / @p w.
     *
     * @param quad Destination, six vertices.
     * @param u Texture coordinate U written to every corner.
     * @param v Texture coordinate V written to every corner.
     * @param w Texture coordinate W written to every corner.
     */
    inline void FillSamplingQuadXYZ(SamplingQuadVertexXYZ (&quad)[6], float u, float v, float w)
    {
        const float corners[6][2] = {
            {-1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, -1.0f},
            {-1.0f, 1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
        };
        for (int i = 0; i < 6; ++i)
            quad[i] = SamplingQuadVertexXYZ{corners[i][0], corners[i][1], 0.0f, u, v, w};
    }

    /**
     * @brief What a backend can represent of XNA's sampler state, for the GPU-visible contract.
     *
     * plans/plan_fx.md FX-093. Some sampler properties have no expression on some backends -- OpenGL ES
     * has no `GL_TEXTURE_LOD_BIAS` at all, for one -- so the contract asks the backend's own test
     * file which subset to prove instead of guessing. A field left false is a documented gap, not
     * a silently skipped assertion: `docs/sampler-state-support.md` carries the same table.
     */
    struct CompiledEffectSamplerContractOptions
    {
        /// Whether `SamplerState.MipMapLevelOfDetailBias` reaches the hardware.
        bool supportsLodBias = true;
        /// Whether `SamplerState.MaxMipLevel` clamps the sampled level.
        bool supportsMaxMipLevel = true;
    };

    /**
     * @brief Contract: the compiled Effect's texture and sampler state reach the GPU.
     *
     * plans/plan_fx.md FX-093. `RunCompiledEffectSamplerContract` proves the Effect Framework's
     * `sampler_state` block is translated onto `GraphicsDevice.SamplerStates` correctly; this
     * proves the translated state is then APPLIED. Every section here binds a real texture,
     * applies a pass, issues a real draw and reads the pixel back, so a backend whose
     * `ApplySamplerState`/`ApplySamplerMipState` never reaches its native sampler -- or whose
     * sampler cache hands back the wrong object -- produces a different colour instead of passing
     * quietly.
     *
     * The fixture's pixel shader is `tex2D(FxSampler, TEXCOORD0) * Tint` with `Tint` at
     * (1,1,1,1), and every vertex carries the same TEXCOORD0, so the read-back pixel IS the texel
     * the sampler chose for that coordinate.
     *
     * @param device Device whose renderer claims CompiledEffects.
     * @param options Which sampler properties this backend can represent.
     */
    inline void RunCompiledEffectSamplerPixelContract(
        GraphicsDevice& device,
        const CompiledEffectSamplerContractOptions& options = {})
    {
        namespace Fx = EffectFormat;
        constexpr int kSize = 8;
        const Color background(9, 19, 29, 255);
        const VertexDeclaration declaration = SamplingQuadDeclaration();

        // An Effect's sampler_state block overrides only the states it names and leaves the rest of
        // GraphicsDevice.SamplerStates[slot] alone -- XNA's own rule, and the reason a section must
        // not depend on what the section before it left behind. Every request here is therefore
        // COMPLETE: all six properties are assigned every time, so each assertion describes only
        // the state it names.
        struct SamplerRequest
        {
            std::uint32_t magFilter = Fx::FilterPoint;
            std::uint32_t minFilter = Fx::FilterPoint;
            std::uint32_t mipFilter = Fx::FilterPoint;
            std::uint32_t addressU = Fx::AddressClamp;
            std::uint32_t addressV = Fx::AddressClamp;
            std::uint32_t maxMipLevel = 0;
            float lodBias = 0.0f;
        };
        const auto statesFor = [](const SamplerRequest& request) {
            return std::vector<SyntheticSamplerState>{
                {Fx::SampMagFilter, request.magFilter},
                {Fx::SampMinFilter, request.minFilter},
                {Fx::SampMipFilter, request.mipFilter},
                {Fx::SampAddressU, request.addressU},
                {Fx::SampAddressV, request.addressV},
                {Fx::SampMaxMipLevel, request.maxMipLevel},
                {Fx::SampMipMapLodBias, FloatBits(request.lodBias), true},
            };
        };

        // Draws one full-target quad at a fixed UV through the effect's sampling pass and returns
        // the centre pixel. `configure` gets the effect before the pass is applied.
        const auto sample = [&](const SamplerRequest& request, float u, float v,
                                const std::function<void(Effect&)>& configure) -> Color {
            Effect effect(device, BuildSyntheticSamplingEffect(statesFor(request)));
            auto& parameters = effect.getParametersProperty();
            parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
            parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
            configure(effect);

            SamplingQuadVertex quad[6];
            FillSamplingQuad(quad, u, v);

            RenderTarget2D target(device, kSize, kSize);
            device.SetRenderTarget(&target);
            device.Clear(background);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };
        const auto expectColor = [](const Color& actual, const Color& expected, const char* label) {
            SCOPED_TRACE(label);
            EXPECT_NEAR(actual.getRProperty(), expected.getRProperty(), 3);
            EXPECT_NEAR(actual.getGProperty(), expected.getGProperty(), 3);
            EXPECT_NEAR(actual.getBProperty(), expected.getBProperty(), 3);
        };

        // --- The texture the sampler samples is the one the effect's texture parameter names ----
        {
            Texture2D red(device, 1, 1);
            Texture2D blue(device, 1, 1);
            const Color redPixel[1] = {Color(255, 0, 0, 255)};
            const Color bluePixel[1] = {Color(0, 0, 255, 255)};
            red.SetData(redPixel, 1);
            blue.SetData(bluePixel, 1);

            expectColor(sample(SamplerRequest{}, 0.5f, 0.5f,
                               [&](Effect& e) {
                                   e.getParametersProperty()["FxTexture"]->SetValue(&red);
                               }),
                        Color(255, 0, 0, 255), "FxTexture = red");
            expectColor(sample(SamplerRequest{}, 0.5f, 0.5f,
                               [&](Effect& e) {
                                   e.getParametersProperty()["FxTexture"]->SetValue(&blue);
                               }),
                        Color(0, 0, 255, 255), "FxTexture = blue");
        }

        // --- One effect, one pass, applied and drawn again and again ----------------------------
        //
        // plans/plan_fx.md FX-101. Every other section here builds a fresh Effect per draw, which is the
        // easy case: a backend reaches its "the applied pass changed" path every time. A game does
        // the opposite -- it holds one Effect for the whole frame and applies the same pass per
        // object -- and a backend that only establishes the pass's sampler binding when its
        // underlying runtime reports a CHANGE silently loses that binding from the second draw on.
        // Same effect, same pass, same texture, three draws: all three must be the texel.
        {
            Texture2D red(device, 1, 1);
            const Color redPixel[1] = {Color(255, 0, 0, 255)};
            red.SetData(redPixel, 1);

            Effect effect(device, BuildSyntheticSamplingEffect(statesFor(SamplerRequest{})));
            auto& parameters = effect.getParametersProperty();
            parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
            parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
            parameters["FxTexture"]->SetValue(&red);
            EffectPass& pass = effect.getTechniquesProperty()[0].getPassesProperty()[1];

            SamplingQuadVertex quad[6];
            FillSamplingQuad(quad, 0.5f, 0.5f);
            for (int repeat = 0; repeat < 3; ++repeat)
            {
                SCOPED_TRACE("repeat " + std::to_string(repeat));
                RenderTarget2D target(device, kSize, kSize);
                device.SetRenderTarget(&target);
                device.Clear(background);
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                device.setDepthStencilStateProperty(DepthStencilState::None);
                device.setBlendStateProperty(BlendState::Opaque);
                pass.Apply();
                device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                          static_cast<const void*>(quad), 0, 2, declaration);
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                Color pixel(0, 0, 0, 0);
                const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
                target.GetData(0, &centre, &pixel, 0, 1);
                expectColor(pixel, Color(255, 0, 0, 255),
                            "re-applying the same pass must keep its sampler binding");
            }

            // And the same effect must still follow a texture change on a later apply.
            Texture2D blue(device, 1, 1);
            const Color bluePixel[1] = {Color(0, 0, 255, 255)};
            blue.SetData(bluePixel, 1);
            parameters["FxTexture"]->SetValue(&blue);
            RenderTarget2D target(device, kSize, kSize);
            device.SetRenderTarget(&target);
            device.Clear(background);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            pass.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            expectColor(pixel, Color(0, 0, 255, 255),
                        "a texture assigned between two applies of one pass must take effect");
        }

        // --- Addressing, on both axes, from a coordinate OUTSIDE [0,1] -------------------------
        //
        // A 2x2 texture whose left column is red and right column is blue, sampled at u = 1.25 and
        // u = 1.75. Texel centres sit at 0.25 and 0.75, so both probes land exactly on one:
        //
        //          u=1.25          u=1.75
        //   Wrap    0.25 -> red     0.75 -> blue
        //   Clamp   1.00 -> blue    1.00 -> blue
        //   Mirror  0.75 -> blue    0.25 -> red
        //
        // Three distinct signatures, so no two modes can be confused for one another, and no probe
        // sits on a texel boundary where the choice of texel would be a rounding question.
        {
            Texture2D columns(device, 2, 2);
            const Color red(255, 0, 0, 255);
            const Color blue(0, 0, 255, 255);
            const Color columnPixels[4] = {red, blue, red, blue};
            columns.SetData(columnPixels, 4);
            const auto bindColumns = [&](Effect& e) {
                e.getParametersProperty()["FxTexture"]->SetValue(&columns);
            };
            const auto withAddressU = [](std::uint32_t mode) {
                SamplerRequest request;
                request.addressU = mode;
                return request;
            };

            expectColor(sample(withAddressU(Fx::AddressWrap), 1.25f, 0.5f, bindColumns),
                        red, "AddressU Wrap at u=1.25");
            expectColor(sample(withAddressU(Fx::AddressWrap), 1.75f, 0.5f, bindColumns),
                        blue, "AddressU Wrap at u=1.75");
            expectColor(sample(withAddressU(Fx::AddressClamp), 1.25f, 0.5f, bindColumns),
                        blue, "AddressU Clamp at u=1.25");
            expectColor(sample(withAddressU(Fx::AddressClamp), 1.75f, 0.5f, bindColumns),
                        blue, "AddressU Clamp at u=1.75");
            expectColor(sample(withAddressU(Fx::AddressMirror), 1.25f, 0.5f, bindColumns),
                        blue, "AddressU Mirror at u=1.25");
            expectColor(sample(withAddressU(Fx::AddressMirror), 1.75f, 0.5f, bindColumns),
                        red, "AddressU Mirror at u=1.75");

            // The V axis, with a texture whose ROWS differ, so a backend that applied the U mode
            // to both axes (or swapped them) fails here rather than passing on symmetry.
            Texture2D rows(device, 2, 2);
            const Color rowPixels[4] = {red, red, blue, blue};
            rows.SetData(rowPixels, 4);
            const auto bindRows = [&](Effect& e) {
                e.getParametersProperty()["FxTexture"]->SetValue(&rows);
            };
            const auto withAddressV = [](std::uint32_t mode) {
                SamplerRequest request;
                request.addressV = mode;
                return request;
            };
            expectColor(sample(withAddressV(Fx::AddressWrap), 0.5f, 1.25f, bindRows),
                        red, "AddressV Wrap at v=1.25");
            expectColor(sample(withAddressV(Fx::AddressWrap), 0.5f, 1.75f, bindRows),
                        blue, "AddressV Wrap at v=1.75");
            expectColor(sample(withAddressV(Fx::AddressClamp), 0.5f, 1.25f, bindRows),
                        blue, "AddressV Clamp at v=1.25");
            expectColor(sample(withAddressV(Fx::AddressMirror), 0.5f, 1.75f, bindRows),
                        red, "AddressV Mirror at v=1.75");
        }

        // --- Filtering: Point picks one texel, Linear blends two ---------------------------------
        //
        // A 2x1-column texture sampled at u = 0.375, a quarter of the way from the left texel's
        // centre (0.25) to the right one's (0.75). Point returns the left texel exactly; Linear
        // returns 3/4 of it plus 1/4 of the right. Deliberately NOT u = 0.5, where Point's answer
        // would depend on how the hardware rounds a coordinate that falls on a texel edge.
        {
            Texture2D columns(device, 2, 2);
            const Color red(255, 0, 0, 255);
            const Color blue(0, 0, 255, 255);
            const Color columnPixels[4] = {red, blue, red, blue};
            columns.SetData(columnPixels, 4);
            const auto bindColumns = [&](Effect& e) {
                e.getParametersProperty()["FxTexture"]->SetValue(&columns);
            };
            SamplerRequest linearFilter;
            linearFilter.magFilter = Fx::FilterLinear;
            linearFilter.minFilter = Fx::FilterLinear;
            linearFilter.mipFilter = Fx::FilterLinear;

            expectColor(sample(SamplerRequest{}, 0.375f, 0.5f, bindColumns),
                        red, "Point filter at u=0.375 returns the left texel whole");
            expectColor(sample(linearFilter, 0.375f, 0.5f, bindColumns),
                        Color(191, 0, 64, 255),
                        "Linear filter at u=0.375 returns three quarters left, one quarter right");
        }

        // --- MaxMipLevel forces the sampled level, whatever the derivatives say -----------------
        //
        // Every vertex carries the same coordinate, so the computed level of detail is 0 and a
        // correct sampler returns level 0 -- unless MaxMipLevel raises the floor, which is exactly
        // what XNA's MaxMipLevel means and what makes it observable here without minified
        // geometry. Level 0 is red and level 1 is green, so "which level" is the read-back colour.
        if (options.supportsMaxMipLevel)
        {
            Texture2D mipped(device, 2, 2, /*mipMap=*/true, SurfaceFormat::Color);
            ASSERT_GE(mipped.getLevelCountProperty(), 2)
                << "a 2x2 mipmapped texture must have two levels for this section to mean anything";
            const Color red(255, 0, 0, 255);
            const Color green(0, 255, 0, 255);
            const Color level0[4] = {red, red, red, red};
            const Color level1[1] = {green};
            const Rectangle wholeLevel0(0, 0, 2, 2);
            const Rectangle wholeLevel1(0, 0, 1, 1);
            mipped.SetData(0, &wholeLevel0, level0, 0, 4);
            mipped.SetData(1, &wholeLevel1, level1, 0, 1);
            const auto bindMipped = [&](Effect& e) {
                e.getParametersProperty()["FxTexture"]->SetValue(&mipped);
            };
            const auto withMaxMipLevel = [](std::uint32_t level) {
                SamplerRequest request;
                request.maxMipLevel = level;
                return request;
            };
            expectColor(sample(withMaxMipLevel(0), 0.5f, 0.5f, bindMipped),
                        red, "MaxMipLevel 0 samples the base level");
            expectColor(sample(withMaxMipLevel(1), 0.5f, 0.5f, bindMipped),
                        green, "MaxMipLevel 1 must clamp the sampler to the smaller level");
        }

        // --- MipMapLevelOfDetailBias shifts the computed level ----------------------------------
        //
        // Unlike the clamp above, a bias only moves a level of detail the hardware has ALREADY
        // computed from the texture coordinate's screen-space derivatives -- and with the constant
        // coordinate every other section uses, that derivative is zero and the resulting level is
        // implementation-defined (it came out 0 on one backend here and log2(0) on another). So
        // this section draws its own quad with a coordinate that varies from 0 to 1 across a target
        // exactly as wide as the texture: one texel per pixel, a computed level of exactly 0, and a
        // bias of +1 therefore selects level 1 on any conforming implementation.
        //
        // Skipped where the backend has no such state at all -- OpenGL ES has no
        // GL_TEXTURE_LOD_BIAS -- rather than asserted and quietly weakened.
        if (options.supportsLodBias)
        {
            constexpr int kMippedSize = 8;
            Texture2D mipped(device, kMippedSize, kMippedSize, /*mipMap=*/true,
                             SurfaceFormat::Color);
            ASSERT_GE(mipped.getLevelCountProperty(), 2);
            const Color levelColors[4] = {
                Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                Color(0, 0, 255, 255), Color(255, 255, 0, 255),
            };
            for (int level = 0; level < mipped.getLevelCountProperty(); ++level)
            {
                const int extent = std::max(1, kMippedSize >> level);
                const Rectangle whole(0, 0, extent, extent);
                std::vector<Color> texels(static_cast<std::size_t>(extent * extent),
                                          levelColors[std::min(level, 3)]);
                mipped.SetData(level, &whole, texels.data(), 0, static_cast<int>(texels.size()));
            }

            // One texel per pixel across the whole target: the computed level of detail is 0.
            const SamplingQuadVertex ramp[6] = {
                {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
                { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
                { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
            };
            const auto sampleRamp = [&](float bias) -> Color {
                SamplerRequest request;
                request.lodBias = bias;
                Effect effect(device, BuildSyntheticSamplingEffect(statesFor(request)));
                auto& parameters = effect.getParametersProperty();
                parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
                parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                parameters["FxTexture"]->SetValue(&mipped);

                RenderTarget2D target(device, kMippedSize, kMippedSize);
                device.SetRenderTarget(&target);
                device.Clear(background);
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                device.setDepthStencilStateProperty(DepthStencilState::None);
                device.setBlendStateProperty(BlendState::Opaque);
                effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
                device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                          static_cast<const void*>(ramp), 0, 2, declaration);
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                Color pixel(0, 0, 0, 0);
                const Rectangle centre(kMippedSize / 2, kMippedSize / 2, 1, 1);
                target.GetData(0, &centre, &pixel, 0, 1);
                return pixel;
            };
            expectColor(sampleRamp(0.0f), levelColors[0],
                        "a zero LOD bias leaves the base level selected");
            expectColor(sampleRamp(1.0f), levelColors[1],
                        "a +1 LOD bias must select the next smaller level");
        }
    }

    /**
     * @brief Contract: a compiled Effect samples a render target the right way up.
     *
     * plans/plan_fx.md FX-099. A renderer whose render targets store their rows in the opposite order to
     * a plain texture has to correct for it somewhere, and every such correction is written for the
     * shaders that renderer authors itself. A compiled Effect's shader is generated by MojoShader
     * from Direct3D 9 bytecode and contains none of it, so the correction has to reach the
     * compiled route by another road -- and if it does not, a compiled Effect sampling a
     * `RenderTarget2D` sees the image upside down while every stock draw of the same target sees
     * it correctly. Nothing about that is visible in a symmetric test image, which is why this
     * section paints an ASYMMETRIC source and compares the compiled result against the stock one
     * rather than against an assumption.
     *
     * Covered: `RenderTarget2D` -> compiled Effect -> `RenderTarget2D`, the same through a second
     * hop so a chain cannot cancel one flip with another, and an ordinary `Texture2D` source,
     * which must NOT be flipped -- a fix that turns the V axis around unconditionally fails there.
     *
     * @param device Device whose renderer claims CompiledEffects.
     */
    inline void RunCompiledEffectRenderTargetSourceContract(GraphicsDevice& device)
    {
        namespace Fx = EffectFormat;
        constexpr int kSize = 8;
        const Color red(255, 0, 0, 255);
        const Color blue(0, 0, 255, 255);
        const Color background(9, 19, 29, 255);

        Texture2D white(device, 1, 1);
        const Color whitePixel[1] = {Color::White};
        white.SetData(whitePixel, 1);

        // A source render target with a red TOP half and a blue BOTTOM half, painted through the
        // stock sprite path in XNA's own top-down pixel space.
        const auto paintHalves = [&](RenderTarget2D& target) {
            device.SetRenderTarget(&target);
            device.Clear(background);
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            batch.Draw(white, Rectangle(0, 0, kSize, kSize / 2), red);
            batch.Draw(white, Rectangle(0, kSize / 2, kSize, kSize / 2), blue);
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        };
        const auto readHalves = [&](RenderTarget2D& target, Color& top, Color& bottom) {
            const Rectangle topProbe(kSize / 2, 1, 1, 1);
            const Rectangle bottomProbe(kSize / 2, kSize - 2, 1, 1);
            target.GetData(0, &topProbe, &top, 0, 1);
            target.GetData(0, &bottomProbe, &bottom, 0, 1);
        };
        const auto expectRedOverBlue = [&](const Color& top, const Color& bottom,
                                           const char* label) {
            SCOPED_TRACE(label);
            EXPECT_NEAR(top.getRProperty(), 255, 3) << "the top half must be red";
            EXPECT_NEAR(top.getBProperty(), 0, 3) << "the top half must be red";
            EXPECT_NEAR(bottom.getBProperty(), 255, 3) << "the bottom half must be blue";
            EXPECT_NEAR(bottom.getRProperty(), 0, 3) << "the bottom half must be blue";
        };

        RenderTarget2D source(device, kSize, kSize);
        paintHalves(source);
        {
            Color top(0, 0, 0, 0), bottom(0, 0, 0, 0);
            readHalves(source, top, bottom);
            expectRedOverBlue(top, bottom, "the source target itself, read back directly");
        }

        // The stock baseline: the same target sampled by the stock sprite program.
        {
            RenderTarget2D destination(device, kSize, kSize);
            device.SetRenderTarget(&destination);
            device.Clear(background);
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            batch.Draw(source, Rectangle(0, 0, kSize, kSize), Color::White);
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color top(0, 0, 0, 0), bottom(0, 0, 0, 0);
            readHalves(destination, top, bottom);
            expectRedOverBlue(top, bottom, "stock sprite sampling of the render target");
        }

        // The compiled route. A quad in clip space with a top-down V, exactly as an XNA sprite
        // effect's own vertices carry: clip y = +1 is the image's first row, which is v = 0.
        Effect effect(device, BuildSyntheticSamplingEffect({
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
        }));
        auto& parameters = effect.getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));

        const VertexDeclaration declaration = SamplingQuadDeclaration();
        const SamplingQuadVertex texturedQuad[6] = {
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
            { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
        };
        const auto sampleThroughEffect = [&](Texture2D& sourceTexture,
                                             RenderTarget2D& destination) {
            parameters["FxTexture"]->SetValue(&sourceTexture);
            device.SetRenderTarget(&destination);
            device.Clear(background);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(texturedQuad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        };

        RenderTarget2D firstHop(device, kSize, kSize);
        sampleThroughEffect(source, firstHop);
        {
            Color top(0, 0, 0, 0), bottom(0, 0, 0, 0);
            readHalves(firstHop, top, bottom);
            expectRedOverBlue(top, bottom,
                              "RenderTarget2D -> compiled Effect -> RenderTarget2D");
        }

        // A second hop: the destination of the first is now the source. A renderer that flips once
        // per hop would come out right here by accident, which is why the first hop is asserted on
        // its own above and this one is asserted as well.
        RenderTarget2D secondHop(device, kSize, kSize);
        sampleThroughEffect(firstHop, secondHop);
        {
            Color top(0, 0, 0, 0), bottom(0, 0, 0, 0);
            readHalves(secondHop, top, bottom);
            expectRedOverBlue(top, bottom,
                              "RenderTarget2D -> compiled Effect -> RenderTarget2D, twice");
        }

        // And an ordinary Texture2D source with the same halves, which must come out the same way.
        // This is the half that fails if a fix mirrors V for every source instead of only for the
        // ones whose storage order needs it.
        {
            Texture2D plain(device, kSize, kSize);
            std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize), red);
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x)
                    pixels[static_cast<std::size_t>(y * kSize + x)] = y < kSize / 2 ? red : blue;
            plain.SetData(pixels.data(), static_cast<int>(pixels.size()));

            RenderTarget2D destination(device, kSize, kSize);
            sampleThroughEffect(plain, destination);
            Color top(0, 0, 0, 0), bottom(0, 0, 0, 0);
            readHalves(destination, top, bottom);
            expectRedOverBlue(top, bottom,
                              "Texture2D -> compiled Effect -> RenderTarget2D must not be flipped");
        }
    }

    /**
     * @brief Contract: a compiled Effect's sampler state does not survive into a stock draw.
     *
     * plans/plan_fx.md FX-092. A renderer that keeps one long-lived native sampler per slot and mutates
     * it in place -- EasyGL's `samplers_[slot]`, FNA3D's `samplerStates_[0]` -- writes only the
     * properties each call describes, so an Effect's own `MaxMipLevel`/`MipMapLevelOfDetailBias`
     * outlives the draw that asked for them. The next stock `SpriteBatch` flush passes filter and
     * addressing alone and inherits the rest, which is a silently wrong mip level on a texture the
     * game never asked to clamp.
     *
     * XNA has no such inheritance: `SpriteBatch`'s flush assigns its whole `SamplerState` to slot
     * zero (FNA `SpriteBatch.PrepRenderState`), so the sprite samples the base level. This draws
     * the compiled effect with the clamp, then the stock sprite without it, then the compiled
     * effect again -- so a leak in either direction fails.
     *
     * @param device Device whose renderer claims CompiledEffects.
     */
    inline void RunCompiledEffectStockDrawIsolationContract(GraphicsDevice& device)
    {
        namespace Fx = EffectFormat;
        constexpr int kSize = 8;
        const Color background(9, 19, 29, 255);
        const Color red(255, 0, 0, 255);
        const Color green(0, 255, 0, 255);

        Texture2D mipped(device, 2, 2, /*mipMap=*/true, SurfaceFormat::Color);
        if (mipped.getLevelCountProperty() < 2)
            GTEST_SKIP() << "this renderer does not allocate a mip chain for a 2x2 texture";
        const Color level0[4] = {red, red, red, red};
        const Color level1[1] = {green};
        const Rectangle wholeLevel0(0, 0, 2, 2);
        const Rectangle wholeLevel1(0, 0, 1, 1);
        mipped.SetData(0, &wholeLevel0, level0, 0, 4);
        mipped.SetData(1, &wholeLevel1, level1, 0, 1);

        // A compiled effect clamped to the smaller level. Its own contract section already proves
        // the clamp reaches the GPU; here it is the state whose escape is being measured.
        const VertexDeclaration declaration = SamplingQuadDeclaration();
        Effect effect(device, BuildSyntheticSamplingEffect({
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampMaxMipLevel, 1},
        }));
        auto& parameters = effect.getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        parameters["FxTexture"]->SetValue(&mipped);

        const auto drawCompiled = [&]() -> Color {
            SamplingQuadVertex quad[6];
            FillSamplingQuad(quad, 0.5f, 0.5f);
            RenderTarget2D target(device, kSize, kSize);
            device.SetRenderTarget(&target);
            device.Clear(background);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            effect.getTechniquesProperty()[0].getPassesProperty()[1].Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };

        // The same texture through the STOCK sprite program, with SpriteBatch's own default
        // SamplerState. The sprite is magnified from 2x2 to the whole target, so a correct sampler
        // reads the base level and the result is red whatever the mip chain holds.
        const auto drawStockSprite = [&]() -> Color {
            RenderTarget2D target(device, kSize, kSize);
            device.SetRenderTarget(&target);
            device.Clear(background);
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            batch.Draw(mipped, Rectangle(0, 0, kSize, kSize), Color::White);
            batch.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };

        const auto expectColor = [](const Color& actual, const Color& expected, const char* label) {
            SCOPED_TRACE(label);
            EXPECT_NEAR(actual.getRProperty(), expected.getRProperty(), 3);
            EXPECT_NEAR(actual.getGProperty(), expected.getGProperty(), 3);
            EXPECT_NEAR(actual.getBProperty(), expected.getBProperty(), 3);
        };

        // A clean baseline FIRST, so the assertion after the compiled draw is a comparison against
        // this renderer's own untainted result rather than against an assumed colour.
        const Color baseline = drawStockSprite();
        expectColor(baseline, red, "a stock sprite magnifying a mipmapped texture reads level 0");

        expectColor(drawCompiled(), green,
                    "the compiled effect's MaxMipLevel must clamp its own draw to level 1");
        expectColor(drawStockSprite(), red,
                    "a stock sprite drawn after a compiled effect must not inherit that effect's "
                    "MaxMipLevel");
        expectColor(drawCompiled(), green,
                    "and the compiled effect must still get its own clamp on the next draw");
    }

    /**
     * @brief Contract: a compiled Effect can sample a cube and a volume texture, or refuse by name.
     *
     * plans/plan_fx.md FX-110. XNA's `TextureCube` and `Texture3D` reach a compiled Effect through the
     * same texture parameter a `Texture2D` does, and MojoShader reflects the shader's own
     * expectation as `MOJOSHADER_SAMPLER_CUBE` or `_VOLUME`. Two things must hold, and neither was
     * observable before this section existed: a backend that resolves those kinds must sample the
     * RIGHT one, and a backend that does not must say so by name instead of binding a texture of
     * the wrong dimension -- which on a GL-shaped API leaves the sampler reading an incomplete
     * target and returning black rather than erroring.
     *
     * A backend that refuses skips rather than fails: refusing a kind it cannot bind is a valid,
     * documented state (`plans/plan_fx.md` section 10.5 classifies it), and the refusal's own text is
     * what this section then asserts on.
     *
     * @param device Device whose renderer claims CompiledEffects.
     */
    inline void RunCompiledEffectCubeAndVolumeSamplerContract(GraphicsDevice& device)
    {
        namespace Fx = EffectFormat;
        constexpr int kSize = 8;
        const Color background(9, 19, 29, 255);
        const VertexDeclaration declaration = SamplingQuadDeclarationXYZ();

        const std::vector<SyntheticSamplerState> pointClamp = {
            {Fx::SampMagFilter, Fx::FilterPoint},
            {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint},
            {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp},
            {Fx::SampAddressW, Fx::AddressClamp},
        };

        // Draws a full-target quad whose every vertex carries the same three-component coordinate,
        // so the sampled texel is a property of the sampler and the coordinate alone.
        const auto sample = [&](SyntheticSamplerKind kind, float u, float v, float w,
                                const std::function<void(Effect&)>& bind,
                                std::string& refusal) -> Color {
            // A backend may refuse at either end: when the effect is CREATED (its shader
            // translation cannot express that sampler dimension) or when a draw BINDS the texture.
            // Both are valid documented states and both are caught here, so a refusal is reported
            // by its message rather than by an escaping exception.
            std::unique_ptr<Effect> effect;
            SamplingQuadVertexXYZ quad[6];
            FillSamplingQuadXYZ(quad, u, v, w);
            RenderTarget2D target(device, kSize, kSize);
            try
            {
                effect = std::make_unique<Effect>(
                    device, BuildSyntheticSamplingEffect(pointClamp, 0, kind));
                auto& parameters = effect->getParametersProperty();
                parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
                parameters["Tint"]->SetValue(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                bind(*effect);
            }
            catch (const std::exception& error)
            {
                refusal = error.what();
                return background;
            }

            device.SetRenderTarget(&target);
            device.Clear(background);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            try
            {
                effect->getTechniquesProperty()[0].getPassesProperty()[1].Apply();
                device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                          static_cast<const void*>(quad), 0, 2, declaration);
            }
            catch (const std::exception& error)
            {
                refusal = error.what();
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                return background;
            }
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };
        const auto expectColor = [](const Color& actual, const Color& expected, const char* label) {
            SCOPED_TRACE(label);
            EXPECT_NEAR(actual.getRProperty(), expected.getRProperty(), 3);
            EXPECT_NEAR(actual.getGProperty(), expected.getGProperty(), 3);
            EXPECT_NEAR(actual.getBProperty(), expected.getBProperty(), 3);
        };

        // --- A cube texture, one solid colour per face ------------------------------------------
        {
            const Color faceColors[6] = {
                Color(255, 0, 0, 255),   // +X
                Color(0, 255, 0, 255),   // -X
                Color(0, 0, 255, 255),   // +Y
                Color(255, 255, 0, 255), // -Y
                Color(255, 0, 255, 255), // +Z
                Color(0, 255, 255, 255), // -Z
            };
            TextureCube cube(device, 2, /*mipMap=*/false, SurfaceFormat::Color);
            for (int face = 0; face < 6; ++face)
            {
                const Color texels[4] = {faceColors[face], faceColors[face],
                                         faceColors[face], faceColors[face]};
                cube.SetData(static_cast<CubeMapFace>(face), texels, 4);
            }
            const auto bindCube = [&](Effect& e) {
                e.getParametersProperty()["FxTexture"]->SetValue(&cube);
            };

            std::string refusal;
            const Color positiveX = sample(SyntheticSamplerKind::SamplerCube,
                                           1.0f, 0.0f, 0.0f, bindCube, refusal);
            if (!refusal.empty())
            {
                EXPECT_FALSE(refusal.empty())
                    << "a refusal must say what it could not do, so a port can act on it";
                GTEST_SKIP() << "this renderer refuses a cube sampler: " << refusal;
            }
            expectColor(positiveX, faceColors[0], "a +X direction samples the +X face");

            std::string ignored;
            expectColor(sample(SyntheticSamplerKind::SamplerCube, -1.0f, 0.0f, 0.0f, bindCube,
                               ignored),
                        faceColors[1], "a -X direction samples the -X face");
            expectColor(sample(SyntheticSamplerKind::SamplerCube, 0.0f, 1.0f, 0.0f, bindCube,
                               ignored),
                        faceColors[2], "a +Y direction samples the +Y face");
            expectColor(sample(SyntheticSamplerKind::SamplerCube, 0.0f, 0.0f, -1.0f, bindCube,
                               ignored),
                        faceColors[5], "a -Z direction samples the -Z face");
        }

        // --- A volume texture, one solid colour per depth slice ---------------------------------
        {
            const Color sliceColors[2] = {Color(255, 0, 0, 255), Color(0, 0, 255, 255)};
            Texture3D volume(device, 2, 2, 2, /*mipMap=*/false, SurfaceFormat::Color);
            const Color texels[8] = {
                sliceColors[0], sliceColors[0], sliceColors[0], sliceColors[0],
                sliceColors[1], sliceColors[1], sliceColors[1], sliceColors[1],
            };
            volume.SetData(texels, 8);
            const auto bindVolume = [&](Effect& e) {
                e.getParametersProperty()["FxTexture"]->SetValue(&volume);
            };

            std::string refusal;
            const Color nearSlice = sample(SyntheticSamplerKind::Sampler3D,
                                           0.5f, 0.5f, 0.25f, bindVolume, refusal);
            if (!refusal.empty())
            {
                EXPECT_FALSE(refusal.empty())
                    << "a refusal must say what it could not do, so a port can act on it";
                GTEST_SKIP() << "this renderer refuses a volume sampler: " << refusal;
            }
            expectColor(nearSlice, sliceColors[0], "w = 0.25 samples the first depth slice");

            std::string ignored;
            expectColor(sample(SyntheticSamplerKind::Sampler3D, 0.5f, 0.5f, 0.75f, bindVolume,
                               ignored),
                        sliceColors[1], "w = 0.75 samples the second depth slice");
        }

        // --- The dimensions must agree ----------------------------------------------------------
        //
        // A cube texture bound where the shader declared sampler2D is not a smaller mistake than
        // an unbound slot: on a GL-shaped API each target is bound separately, so the 2D sampler
        // reads an incomplete target and returns black with no error at all.
        {
            Texture2D flat(device, 1, 1);
            const Color white[1] = {Color::White};
            flat.SetData(white, 1);
            std::string refusal;
            (void) sample(SyntheticSamplerKind::SamplerCube, 1.0f, 0.0f, 0.0f,
                          [&](Effect& e) {
                              e.getParametersProperty()["FxTexture"]->SetValue(&flat);
                          },
                          refusal);
            EXPECT_FALSE(refusal.empty())
                << "a Texture2D bound to a samplerCUBE must be refused, not sampled as black";
        }
    }

    /**
     * @brief Contract: the pass a caller applies is the pass that draws.
     *
     * plans/plan_fx.md FX-094. The drawable fixture's pass `P0` binds a pixel shader that writes
     * `Tint.yzxw` while `StatePass` and `P1` write `Tint` unchanged, so applying the wrong pass --
     * or falling back to "the first pass" when a backend cannot resolve the requested one --
     * rotates the read-back colour's channels instead of producing identical pixels. Before this
     * every pass of a drawable fixture bound the same program pair and no test could have told
     * them apart.
     *
     * @param device Device whose renderer claims CompiledEffects.
     */
    inline void RunCompiledEffectPassSelectionContract(GraphicsDevice& device)
    {
        constexpr int kSize = 8;
        Effect effect(device, BuildSyntheticDrawableEffect());
        auto& parameters = effect.getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        // Three channels that cannot be confused with each other or with their own rotation.
        const Vector4 tint(0.25f, 0.5f, 0.75f, 1.0f);
        parameters["Tint"]->SetValue(tint);

        struct ClipVertex { float x, y, z; };
        const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const ClipVertex quad[6] = {
            {-1.0f,  1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
        };

        const auto drawWithPass = [&](EffectPass& pass) -> Color {
            RenderTarget2D target(device, kSize, kSize);
            device.SetRenderTarget(&target);
            device.Clear(Color(9, 19, 29, 255));
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            pass.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(quad), 0, 2, declaration);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Color pixel(0, 0, 0, 0);
            const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
            target.GetData(0, &centre, &pixel, 0, 1);
            return pixel;
        };

        const Color straight(64, 128, 191, 255);   // Tint
        const Color rotated(128, 191, 64, 255);    // Tint.yzxw
        const auto expectColor = [](const Color& actual, const Color& expected, const char* label) {
            SCOPED_TRACE(label);
            EXPECT_NEAR(actual.getRProperty(), expected.getRProperty(), 3);
            EXPECT_NEAR(actual.getGProperty(), expected.getGProperty(), 3);
            EXPECT_NEAR(actual.getBProperty(), expected.getBProperty(), 3);
        };

        auto& firstTechnique = effect.getTechniquesProperty()[0];
        ASSERT_EQ(firstTechnique.getPassesProperty().getCountProperty(), 2);
        expectColor(drawWithPass(firstTechnique.getPassesProperty()[0]), rotated,
                    "pass 0 must draw with pass 0's own program");
        expectColor(drawWithPass(firstTechnique.getPassesProperty()[1]), straight,
                    "pass 1 must draw with pass 1's own program");
        // Back again: a backend that cached the first pass it ever saw fails here.
        expectColor(drawWithPass(firstTechnique.getPassesProperty()[0]), rotated,
                    "re-selecting pass 0 must go back to pass 0's program");

        // Another technique's pass is a third selection, resolved by name rather than by ordinal.
        effect.setCurrentTechniqueProperty(&effect.getTechniquesProperty()[1]);
        auto& secondTechnique = *effect.getCurrentTechniqueProperty();
        ASSERT_EQ(secondTechnique.getNameProperty(), "SecondTechnique");
        expectColor(drawWithPass(secondTechnique.getPassesProperty()[0]), straight,
                    "SecondTechnique's only pass carries the unrotated program");
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
     * The drawing sections are deliberately **not** called from here. Each is its own `TEST` in a
     * backend's test file, so a skip (a capability the renderer does not advertise) or a failure
     * names the shape that was not satisfied instead of collapsing into one verdict. A backend
     * claiming `CompiledEffects` must run all of them:
     *
     * - `RunCompiledEffectDrawContract` -- the XNA draw matrix, every result read back
     * - `RunCompiledEffectMultiStreamDrawContract` -- several streams, and their own offsets
     * - `RunCompiledEffectInstancingDrawContract` -- instanced draws, offsets and divisor reset
     * - `RunCompiledEffectSpriteBatchContract` -- SpriteBatch runs the effect, or refuses by name
     * - `RunCompiledEffectSpriteBatchMultiPassContract` -- XNA's own batch/pass granularity
     * - `RunCompiledEffectSpriteBatchTextureSlotContract` -- the sprite's texture wins slot 0
     * - `RunCompiledEffectSamplerPixelContract` -- the sampler state reaches the GPU
     * - `RunCompiledEffectPassSelectionContract` -- the applied pass is the pass that draws
     * - `RunCompiledEffectRenderTargetSourceContract` -- a rendered source is sampled right way up
     * - `RunCompiledEffectSpriteBatchRenderTargetSourceContract` -- and through SpriteBatch too
     * - `RunCompiledEffectStockDrawIsolationContract` -- no state leaks into a later stock draw
     * - `RunCompiledEffectOrientationContract` -- compiled geometry lands where stock geometry does
     * - `RunCompiledEffectSwitchingContract` -- effects, clones and stock draws interleave cleanly
     * - `RunCompiledEffectManyDrawsContract` -- 600 draws in one frame keep their own uniforms
     * - `RunCompiledEffectTruncationContract` -- every truncation refuses or parses whole
     *
     * This list is the single place the drawing sections are enumerated; anything that needs to
     * describe the suite's breadth should point here rather than repeat a count.
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
