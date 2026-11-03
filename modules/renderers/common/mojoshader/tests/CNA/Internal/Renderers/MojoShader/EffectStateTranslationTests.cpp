// SPDX-License-Identifier: MS-PL
//
// MTH-SAFE-007: render- and sampler-state values in a compiled effect are file content. MojoShader
// stores each one as the 32 bits the file holds, behind a pointer typed as its D3D9 enumeration
// (`MOJOSHADER_blendMode*` and so on), and stores a sampler state's identifier unvalidated. The
// long fuzz campaign found the translation loading such a value through the enum type --
// `load of value 2114238391, which is not a valid value for type 'MOJOSHADER_blendMode'`, undefined
// behaviour -- on a mutated CnaConformanceEffect.fxb. A value outside its enumeration must be
// refused like any other unsupported state, without ever being loaded as the enum.
//
// The bit patterns are written with memcpy, as the parser writes them; the tests never form an
// out-of-range enum value themselves. Under UBSan a regression aborts the test binary.

#if __has_include(<mojoshader.h>)

#include "CNA/Internal/Renderers/MojoShader/EffectTranslation.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    using CNA::Internal::Renderers::CompiledEffectDeviceState;
    using CNA::Internal::Renderers::CompiledEffectPassStateChanges;
    namespace Translation = CNA::Internal::Renderers::MojoShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Blend;
    using Microsoft::Xna::Framework::Graphics::Texture;
    using Microsoft::Xna::Framework::Graphics::TextureAddressMode;

    // The value the campaign found; no D3D9 enumeration comes near it.
    constexpr std::int32_t kForeignValue = 2114238391;

    template<typename TEnum>
    void StoreBits(TEnum& field, std::int32_t bits)
    {
        static_assert(sizeof(field) == sizeof(bits));
        std::memcpy(&field, &bits, sizeof(bits));
    }

    /** Translates one render state whose single value cell holds @p bits. */
    std::string TranslateOneRenderState(MOJOSHADER_renderStateType type, std::int32_t bits,
                                        CompiledEffectPassStateChanges& changes)
    {
        std::int32_t storage = bits;
        MOJOSHADER_effectState state{};
        state.type = type;
        state.value.value_count = 1;
        state.value.valuesI = &storage;
        MOJOSHADER_effectStateChanges native{};
        native.render_state_change_count = 1;
        native.render_state_changes = &state;
        try
        {
            Translation::TranslateRenderStates(native, CompiledEffectDeviceState{}, changes);
        }
        catch (const std::runtime_error& error)
        {
            return error.what();
        }
        return {};
    }

    std::string TranslateOneSamplerState(std::int32_t typeBits, std::int32_t valueBits,
                                         CompiledEffectPassStateChanges& changes)
    {
        std::int32_t storage = valueBits;
        MOJOSHADER_effectSamplerState state{};
        StoreBits(state.type, typeBits);
        state.value.value_count = 1;
        state.value.valuesI = &storage;
        MOJOSHADER_samplerStateRegister registerChange{};
        registerChange.sampler_name = "s0";
        registerChange.sampler_register = 0;
        registerChange.sampler_state_count = 1;
        registerChange.sampler_states = &state;
        const std::unordered_map<std::string, std::uint32_t> samplerTextureParameters;
        const std::vector<Texture*> textures;
        try
        {
            Translation::TranslateSamplers(&registerChange, 1, false, 16, samplerTextureParameters,
                                           textures, CompiledEffectDeviceState{}, changes);
        }
        catch (const std::runtime_error& error)
        {
            return error.what();
        }
        return {};
    }
}

TEST(MojoShaderEffectStateTranslation, AValidEnumeratedRenderStateStillTranslates)
{
    CompiledEffectPassStateChanges changes;
    EXPECT_EQ(TranslateOneRenderState(MOJOSHADER_RS_SRCBLEND, MOJOSHADER_BLEND_SRCALPHA, changes), "");
    EXPECT_TRUE(changes.blendChanged);
    EXPECT_EQ(changes.blend.getColorSourceBlendProperty(), Blend::SourceAlpha);
}

TEST(MojoShaderEffectStateTranslation, ARenderStateValueOutsideItsEnumerationIsRefused)
{
    // Expected messages are the translation's existing refusals for an unsupported value.
    const struct
    {
        MOJOSHADER_renderStateType type;
        const char* refusal;
    } cases[] = {
        {MOJOSHADER_RS_SRCBLEND, "unsupported D3D9 blend mode 2114238391."},
        {MOJOSHADER_RS_DESTBLEND, "unsupported D3D9 blend mode 2114238391."},
        {MOJOSHADER_RS_SRCBLENDALPHA, "unsupported D3D9 blend mode 2114238391."},
        {MOJOSHADER_RS_DESTBLENDALPHA, "unsupported D3D9 blend mode 2114238391."},
        {MOJOSHADER_RS_BLENDOP, "unsupported D3D9 blend operation."},
        {MOJOSHADER_RS_BLENDOPALPHA, "unsupported D3D9 blend operation."},
        {MOJOSHADER_RS_CULLMODE, "unsupported cull mode."},
        {MOJOSHADER_RS_ZFUNC, "unsupported D3D9 comparison function."},
        {MOJOSHADER_RS_STENCILFUNC, "unsupported D3D9 comparison function."},
        {MOJOSHADER_RS_CCW_STENCILFUNC, "unsupported D3D9 comparison function."},
        {MOJOSHADER_RS_STENCILFAIL, "unsupported D3D9 stencil operation."},
        {MOJOSHADER_RS_STENCILZFAIL, "unsupported D3D9 stencil operation."},
        {MOJOSHADER_RS_STENCILPASS, "unsupported D3D9 stencil operation."},
        {MOJOSHADER_RS_CCW_STENCILFAIL, "unsupported D3D9 stencil operation."},
        {MOJOSHADER_RS_CCW_STENCILZFAIL, "unsupported D3D9 stencil operation."},
        {MOJOSHADER_RS_CCW_STENCILPASS, "unsupported D3D9 stencil operation."},
        {MOJOSHADER_RS_FILLMODE, "point fill mode is unsupported."},
    };
    for (const auto& item : cases)
    {
        CompiledEffectPassStateChanges changes;
        const std::string refusal = TranslateOneRenderState(item.type, kForeignValue, changes);
        EXPECT_NE(refusal.find(item.refusal), std::string::npos)
            << "state " << static_cast<int>(item.type) << ": " << refusal;
        // Transactional: a refused token publishes no state group.
        EXPECT_FALSE(changes.blendChanged || changes.depthStencilChanged || changes.rasterizerChanged)
            << "state " << static_cast<int>(item.type);
    }

    // D3DZB_TRUE is the only value that enables depth; any other bit pattern disables it.
    CompiledEffectPassStateChanges changes;
    EXPECT_EQ(TranslateOneRenderState(MOJOSHADER_RS_ZENABLE, kForeignValue, changes), "");
    EXPECT_TRUE(changes.depthStencilChanged);
    EXPECT_FALSE(changes.depthStencil.getDepthBufferEnableProperty());
}

TEST(MojoShaderEffectStateTranslation, ASamplerStateIdentifierOrValueOutsideItsEnumerationIsRefused)
{
    CompiledEffectPassStateChanges valid;
    EXPECT_EQ(TranslateOneSamplerState(MOJOSHADER_SAMP_ADDRESSU, MOJOSHADER_TADDRESS_MIRROR, valid), "");
    ASSERT_EQ(valid.samplers.size(), 1u);
    EXPECT_TRUE(valid.samplers[0].samplerChanged);
    EXPECT_EQ(valid.samplers[0].sampler.getAddressUProperty(), TextureAddressMode::Mirror);

    CompiledEffectPassStateChanges changes;
    EXPECT_EQ(TranslateOneSamplerState(kForeignValue, 0, changes),
              "Compiled effect: unsupported sampler state 2114238391.");
    EXPECT_NE(TranslateOneSamplerState(MOJOSHADER_SAMP_ADDRESSU, kForeignValue, changes)
                  .find("not representable by XNA 4.0 SamplerState"),
              std::string::npos);
    for (const auto filter : {MOJOSHADER_SAMP_MAGFILTER, MOJOSHADER_SAMP_MINFILTER, MOJOSHADER_SAMP_MIPFILTER})
    {
        EXPECT_EQ(TranslateOneSamplerState(filter, kForeignValue, changes),
                  "Compiled effect: unsupported sampler filter component.")
            << "sampler state " << static_cast<int>(filter);
    }
    EXPECT_TRUE(changes.samplers.empty());
}

#endif
