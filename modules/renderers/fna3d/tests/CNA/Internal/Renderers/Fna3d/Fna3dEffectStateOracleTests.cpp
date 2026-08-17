// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-005, state half: compare the device state CNA installs when a compiled pass is
// applied against the state FNA installs for the same pass on the same binary.
//
// The reflection half of the oracle answers "does CNA read the same object graph as FNA". This
// answers the harder question: does CNA *act* on it the same way. FNA's
// Effect.INTERNAL_applyEffect folds MojoShader's reported state changes through PipelineCache and
// assigns the results to the public GraphicsDevice.BlendState / DepthStencilState /
// RasterizerState / SamplerStates properties; CNA does the same in Effect::ApplyCompiledPassState.
// Those properties are therefore directly comparable.
//
// The oracle is regenerated with:
//   mono tools/fna-reference/bin/Debug/FnaReference.exe --effect-states \
//        modules/renderers/fna3d/effects tests/fixtures/compiled-effects/fna-effect-states.json
//
// A pass that assigns no state of its own must leave the game's selection alone, so every pass
// starts from the same device state the generator used. That starting point is part of the
// comparison, not incidental to it: "unchanged" is as much a result as "replaced".

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Json.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using CNA::Internal::JsonValue;
    using namespace Microsoft::Xna::Framework::Graphics;

    /// Sampler slots the generator records, kept in step with its own constant.
    constexpr int kRecordedSamplerSlots = 4;

    std::filesystem::path FixtureRoot()
    {
        return std::filesystem::path(__FILE__).parent_path() /
            "../../../../../../../../tests/fixtures/compiled-effects";
    }

    std::filesystem::path EffectRoot()
    {
        return std::filesystem::path(__FILE__).parent_path() /
            "../../../../../effects";
    }

    /** @brief Reads FNA's checked-in pass-state oracle. */
    JsonValue LoadOracle()
    {
        std::ifstream input(FixtureRoot() / "fna-effect-states.json");
        if (!input) return {};
        std::ostringstream text;
        text << input.rdbuf();
        return CNA::Internal::ParseJson(text.str());
    }

    std::vector<std::uint8_t> ReadEffect(const std::string& file)
    {
        std::ifstream input(EffectRoot() / file, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    int OracleInt(const JsonValue& object, const char* key)
    {
        const JsonValue* member = object.FindMember(key);
        return member != nullptr ? static_cast<int>(member->numberValue) : -1;
    }

    bool OracleBool(const JsonValue& object, const char* key)
    {
        const JsonValue* member = object.FindMember(key);
        return member != nullptr && member->boolValue;
    }

    /// Restores the exact starting state the oracle generator used before each pass.
    void ResetDeviceState(GraphicsDevice& device)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        for (int slot = 0; slot < kRecordedSamplerSlots; ++slot)
        {
            device.getSamplerStatesProperty()[slot] = SamplerState::LinearWrap;
        }
    }

    void ExpectBlendMatches(const GraphicsDevice& device, const JsonValue& oracle,
                            const std::string& where)
    {
        const BlendState& blend = device.getBlendStateProperty();
        EXPECT_EQ(static_cast<int>(blend.getColorSourceBlendProperty()),
                  OracleInt(oracle, "ColorSourceBlend")) << where << " ColorSourceBlend";
        EXPECT_EQ(static_cast<int>(blend.getColorDestinationBlendProperty()),
                  OracleInt(oracle, "ColorDestinationBlend")) << where << " ColorDestinationBlend";
        EXPECT_EQ(static_cast<int>(blend.getAlphaSourceBlendProperty()),
                  OracleInt(oracle, "AlphaSourceBlend")) << where << " AlphaSourceBlend";
        EXPECT_EQ(static_cast<int>(blend.getAlphaDestinationBlendProperty()),
                  OracleInt(oracle, "AlphaDestinationBlend")) << where << " AlphaDestinationBlend";
        EXPECT_EQ(static_cast<int>(blend.getColorBlendFunctionProperty()),
                  OracleInt(oracle, "ColorBlendFunction")) << where << " ColorBlendFunction";
        EXPECT_EQ(static_cast<int>(blend.getAlphaBlendFunctionProperty()),
                  OracleInt(oracle, "AlphaBlendFunction")) << where << " AlphaBlendFunction";
        EXPECT_EQ(static_cast<int>(blend.getColorWriteChannelsProperty()),
                  OracleInt(oracle, "ColorWriteChannels")) << where << " ColorWriteChannels";
    }

    void ExpectDepthStencilMatches(const GraphicsDevice& device, const JsonValue& oracle,
                                   const std::string& where)
    {
        const DepthStencilState& depth = device.getDepthStencilStateProperty();
        EXPECT_EQ(depth.getDepthBufferEnableProperty(),
                  OracleBool(oracle, "DepthBufferEnable")) << where << " DepthBufferEnable";
        EXPECT_EQ(depth.getDepthBufferWriteEnableProperty(),
                  OracleBool(oracle, "DepthBufferWriteEnable"))
            << where << " DepthBufferWriteEnable";
        EXPECT_EQ(static_cast<int>(depth.getDepthBufferFunctionProperty()),
                  OracleInt(oracle, "DepthBufferFunction")) << where << " DepthBufferFunction";
        EXPECT_EQ(depth.getStencilEnableProperty(),
                  OracleBool(oracle, "StencilEnable")) << where << " StencilEnable";
        EXPECT_EQ(static_cast<int>(depth.getStencilFunctionProperty()),
                  OracleInt(oracle, "StencilFunction")) << where << " StencilFunction";
        EXPECT_EQ(depth.getReferenceStencilProperty(),
                  OracleInt(oracle, "ReferenceStencil")) << where << " ReferenceStencil";
    }

    void ExpectRasterizerMatches(const GraphicsDevice& device, const JsonValue& oracle,
                                 const std::string& where)
    {
        const RasterizerState& raster = device.getRasterizerStateProperty();
        EXPECT_EQ(static_cast<int>(raster.getCullModeProperty()),
                  OracleInt(oracle, "CullMode")) << where << " CullMode";
        EXPECT_EQ(static_cast<int>(raster.getFillModeProperty()),
                  OracleInt(oracle, "FillMode")) << where << " FillMode";
        EXPECT_EQ(raster.getScissorTestEnableProperty(),
                  OracleBool(oracle, "ScissorTestEnable")) << where << " ScissorTestEnable";
    }

    void ExpectSamplersMatch(GraphicsDevice& device, const JsonValue& oracle,
                             const std::string& where)
    {
        for (int slot = 0; slot < kRecordedSamplerSlots; ++slot)
        {
            const JsonValue* expected = oracle.FindMember(std::to_string(slot).c_str());
            ASSERT_NE(expected, nullptr) << where << " slot " << slot << " missing from oracle";
            const SamplerState& sampler = device.getSamplerStatesProperty()[slot];
            const std::string at = where + " slot " + std::to_string(slot);
            EXPECT_EQ(static_cast<int>(sampler.getFilterProperty()),
                      OracleInt(*expected, "Filter")) << at << " Filter";
            EXPECT_EQ(static_cast<int>(sampler.getAddressUProperty()),
                      OracleInt(*expected, "AddressU")) << at << " AddressU";
            EXPECT_EQ(static_cast<int>(sampler.getAddressVProperty()),
                      OracleInt(*expected, "AddressV")) << at << " AddressV";
            EXPECT_EQ(static_cast<int>(sampler.getAddressWProperty()),
                      OracleInt(*expected, "AddressW")) << at << " AddressW";
            EXPECT_EQ(sampler.getMaxAnisotropyProperty(),
                      OracleInt(*expected, "MaxAnisotropy")) << at << " MaxAnisotropy";
            EXPECT_EQ(sampler.getMaxMipLevelProperty(),
                      OracleInt(*expected, "MaxMipLevel")) << at << " MaxMipLevel";
        }
    }
}

TEST(Fna3dEffectStateOracleTest, EveryPassInstallsTheStateFnaInstalls)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "selected renderer does not execute XNA Effect Framework bytecode";

    const JsonValue oracle = LoadOracle();
    ASSERT_EQ(OracleInt(oracle, "recordedSamplerSlots"), kRecordedSamplerSlots)
        << "the oracle records a different number of sampler slots than this test compares";

    int comparedPasses = 0;
    for (const auto& entry : oracle.objectValue)
    {
        if (entry.key.size() < 4 || entry.key.compare(entry.key.size() - 4, 4, ".fxb") != 0)
        {
            continue;
        }
        const std::vector<std::uint8_t> bytes = ReadEffect(entry.key);
        ASSERT_FALSE(bytes.empty()) << "cannot read fixture " << entry.key;

        Effect effect(device, std::vector<SharpRuntime::bytecs>(bytes.begin(), bytes.end()));
        const JsonValue* techniques = entry.value.FindMember("techniques");
        ASSERT_NE(techniques, nullptr) << entry.key << " has no techniques in the oracle";

        for (const auto& techniqueEntry : techniques->objectValue)
        {
            EffectTechnique* technique =
                effect.getTechniquesProperty()[techniqueEntry.key.c_str()];
            ASSERT_NE(technique, nullptr)
                << entry.key << " has no technique named " << techniqueEntry.key;
            effect.setCurrentTechniqueProperty(technique);

            for (const auto& passEntry : techniqueEntry.value.objectValue)
            {
                const std::string where = entry.key + " " + techniqueEntry.key + "/" +
                    passEntry.key;
                ASSERT_EQ(passEntry.value.FindMember("error"), nullptr)
                    << where << " failed inside FNA, so there is nothing to compare against";

                EffectPass* pass = technique->getPassesProperty()[passEntry.key.c_str()];
                ASSERT_NE(pass, nullptr) << where << " does not exist in CNA";

                ResetDeviceState(device);
                pass->Apply();

                const JsonValue* blend = passEntry.value.FindMember("BlendState");
                const JsonValue* depth = passEntry.value.FindMember("DepthStencilState");
                const JsonValue* raster = passEntry.value.FindMember("RasterizerState");
                const JsonValue* samplers = passEntry.value.FindMember("SamplerStates");
                ASSERT_NE(blend, nullptr) << where;
                ASSERT_NE(depth, nullptr) << where;
                ASSERT_NE(raster, nullptr) << where;
                ASSERT_NE(samplers, nullptr) << where;

                ExpectBlendMatches(device, *blend, where);
                ExpectDepthStencilMatches(device, *depth, where);
                ExpectRasterizerMatches(device, *raster, where);
                ExpectSamplersMatch(device, *samplers, where);
                ++comparedPasses;
            }
        }
    }

    // A silently empty oracle would make every assertion above vacuous.
    EXPECT_GE(comparedPasses, 8) << "the oracle covered suspiciously few passes";
}
