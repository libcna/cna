// SPDX-License-Identifier: MS-PL
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "System/ArgumentException.hpp"
#include <cstdlib>

namespace
{
    using namespace CNA::TestSupport;
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    std::uint32_t Word(const std::vector<std::uint8_t>& bytes, std::size_t at)
    {
        return std::uint32_t(bytes.at(at)) | (std::uint32_t(bytes.at(at + 1)) << 8) |
               (std::uint32_t(bytes.at(at + 2)) << 16) | (std::uint32_t(bytes.at(at + 3)) << 24);
    }

    // A generic sampler is legal in a legacy effect. Remove the fixture's redundant sampler
    // dimension from CTAB and sampler_state; only FxTexture's parameter declaration retains it.
    std::vector<std::uint8_t> TypedTextureWithGenericSampler(SyntheticSamplerKind kind,
                                                          bool signedSource,
                                                          bool samplerFirst = false)
    {
        namespace Fx = EffectFormat;
        SyntheticEffectOptions options;
        options.includeDrawableProgram = options.includeSampler = true;
        options.samplerRegister = 1;
        options.samplerKind = kind;
        options.samplerParameterPrecedesTexture = samplerFirst;
        options.pixelShaderLegacyDependentTexture = SyntheticLegacyDependentTexture::RegisterRgb;
        options.initializeLegacyTextureSource = options.sampleLegacyDependentTextureSource = true;
        options.signedLegacyDependentTextureSource = signedSource;
        options.samplerStates = {
            {Fx::SampMagFilter, Fx::FilterPoint}, {Fx::SampMinFilter, Fx::FilterPoint},
            {Fx::SampMipFilter, Fx::FilterPoint}, {Fx::SampAddressU, Fx::AddressClamp},
            {Fx::SampAddressV, Fx::AddressClamp}, {Fx::SampAddressW, Fx::AddressClamp},
        };
        auto bytes = BuildSyntheticEffect(options);
        constexpr std::uint32_t genericSampler = 10; // D3DXPT_SAMPLER / MOJOSHADER_SYMTYPE_SAMPLER
        constexpr std::uint32_t genericTexture = 5;  // D3DXPT_TEXTURE
        constexpr std::size_t base = 8;
        const auto structure = base + Word(bytes, 4);
        auto parameter = structure + 16;
        for (std::uint32_t i = 0; i < Word(bytes, structure); ++i)
        {
            const auto type = base + Word(bytes, parameter);
            const auto value = base + Word(bytes, parameter + 4);
            const auto annotations = Word(bytes, parameter + 12);
            const auto declared = Word(bytes, type);
            if (declared >= 10 && declared <= 14)
            {
                PatchUInt32(bytes, type, genericSampler);
                const auto stateCount = Word(bytes, value);
                for (std::uint32_t state = 0; state < stateCount; ++state)
                {
                    const auto record = value + 4 + state * 16;
                    if ((Word(bytes, record) & ~0xA0u) == Fx::SampTexture)
                        PatchUInt32(bytes, base + Word(bytes, record + 8), genericTexture);
                }
            }
            parameter += 16 + annotations * 8;
        }
        for (std::size_t at = structure; at + 36 < bytes.size(); at += 4)
        {
            if (Word(bytes, at) != 0x42415443u) continue;
            const auto ctab = at + 4;
            const auto constants = Word(bytes, ctab + 12);
            const auto info = ctab + Word(bytes, ctab + 16);
            for (std::uint32_t i = 0; i < constants; ++i)
            {
                const auto entry = info + i * 20;
                if ((Word(bytes, entry + 4) & 0xFFFFu) != 3) continue;
                const auto type = ctab + Word(bytes, entry + 12);
                PatchUInt32(bytes, type, (Word(bytes, type) & 0xFFFFu) | (genericSampler << 16));
            }
        }
        return bytes;
    }
}

TEST(CompiledEffectSamplerInferenceTest, TypedTextureDeclarationControlsEveryLegacyPixel)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(64, 64));
    if (!SupportsCompiledEffects(device))
    {
        ASSERT_EQ(std::getenv("CNA_GAP13_REQUIRE_CORPUS"), nullptr)
            << "enabled inference qualification requires compiled Effects";
        for (const auto kind : {SyntheticSamplerKind::Sampler2D,
                                SyntheticSamplerKind::SamplerCube, SyntheticSamplerKind::Sampler3D})
        {
            SCOPED_TRACE(static_cast<int>(kind));
            for (const bool signedSource : {false, true})
            {
                SCOPED_TRACE(signedSource);
                try
                {
                    Effect effect(device, TypedTextureWithGenericSampler(kind, signedSource));
                    FAIL() << "disabled compiled Effects must refuse";
                }
                catch (const System::NotSupportedException& error)
                {
                    EXPECT_NE(std::string(error.what()).find("GraphicsCapability::CompiledEffects"),
                              std::string::npos) << error.what();
                }
            }
        }
        return;
    }
    Texture2D source(device, 1, 1), flat(device, 4, 4);
    TextureCube cube(device, 4, false, SurfaceFormat::Color);
    Texture3D volume(device, 4, 4, 4, false, SurfaceFormat::Color);
    const Color sourcePixel(32, 96, 160, 224);
    source.SetData(&sourcePixel, 1);
    Color grid[64];
    for (int z = 0; z < 4; ++z) for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
        grid[z * 16 + y * 4 + x] = Color(40 * (x + 1), 40 * (y + 1),
                                        7 * (4 * x + y + 1) + 40 * z, 255);
    flat.SetData(grid, 16);
    volume.SetData(grid, 64);
    for (int face = 0; face < 6; ++face)
    {
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
            grid[y * 4 + x] = Color(40 * (x + 1), 40 * (y + 1),
                                   30 * face + 4 * x + y + 36, 255);
        cube.SetData(static_cast<CubeMapFace>(face), grid, 16);
    }
    struct Vertex { float x, y, z, u, v, w, q; };
    const Vertex quad[] = {
        {-1,1,0,.5f,.5f,0,1}, {-1,-1,0,.5f,.5f,0,1}, {1,-1,0,.5f,.5f,0,1},
        {-1,1,0,.5f,.5f,0,1}, {1,-1,0,.5f,.5f,0,1}, {1,1,0,.5f,.5f,0,1},
    };
    const VertexDeclaration declaration(sizeof(Vertex), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 0),
    });
    // Hand oracle: floor(4*RGB/255)=(0,1,2); signed RGB=(-191,-63,65)/255
    // clamps to (0,0,1). Cube +Z: u=.6,v=.2 -> (2,0), face=4.
    // Cube -X: u=(65/191+1)/2,v=(63/191+1)/2 -> (2,2), face=1.
    // Substitution into the independently authored grids above yields these six literals.
    const Color expected[3][2] = {
        {Color(40,80,14,255), Color(40,40,7,255)},
        {Color(120,40,164,255), Color(120,120,76,255)},
        {Color(40,80,94,255), Color(40,40,47,255)},
    };
    const SyntheticSamplerKind kinds[] = {SyntheticSamplerKind::Sampler2D,
        SyntheticSamplerKind::SamplerCube, SyntheticSamplerKind::Sampler3D};
    const EffectParameterType parameterTypes[] = {EffectParameterType::Texture2D,
        EffectParameterType::TextureCube, EffectParameterType::Texture3D};
    Texture* textures[] = {&flat, &cube, &volume};
    for (int kind = 0; kind < 3; ++kind) for (int signedSource = 0; signedSource < 2; ++signedSource)
    {
        SCOPED_TRACE(kind);
        SCOPED_TRACE(signedSource);
        Effect effect(device, TypedTextureWithGenericSampler(kinds[kind], signedSource != 0));
        auto* textureParameter = effect.getParametersProperty()["FxTexture"];
        ASSERT_NE(textureParameter, nullptr);
        EXPECT_EQ(textureParameter->getParameterTypeProperty(), parameterTypes[kind]);
        effect.getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
        textureParameter->SetValue(textures[kind]);
        RenderTarget2D target(device, 8, 8);
        device.SetRenderTarget(&target);
        device.Clear(Color(51,102,153,204));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
        effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        // The Effect Texture state binds its typed parameter to s1; source s0 is an ordinary
        // GraphicsDevice.Textures binding. No private compile-time sampler map participates.
        EXPECT_EQ(device.getTexturesProperty()[1], textures[kind]);
        device.getTexturesProperty()(0, &source);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad),
                                  0, 2, declaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color pixels[64];
        target.GetData(pixels, 64);
        for (int pixel = 0; pixel < 64; ++pixel)
            EXPECT_EQ(pixels[pixel], expected[kind][signedSource]) << "pixel " << pixel;
    }
}

TEST(CompiledEffectSamplerInferenceFixtureTest, OnlyTextureParameterRetainsDimension)
{
    // D3DXPT declarations in the authored Effect binary are the oracle here. The fork's
    // inference must follow Texture's object/name link; a redundant typed sampler would hide it.
    const SyntheticSamplerKind kinds[] = {SyntheticSamplerKind::Sampler2D,
        SyntheticSamplerKind::SamplerCube, SyntheticSamplerKind::Sampler3D};
    const std::uint32_t expected[] = {7, 9, 8}; // D3DXPT_TEXTURE2D/CUBE/3D
    for (int kind = 0; kind < 3; ++kind)
    {
        auto bytes = TypedTextureWithGenericSampler(kinds[kind], false);
        const auto structure = 8 + Word(bytes, 4);
        auto parameter = structure + 16;
        int typedTextures = 0, genericSamplers = 0, genericTextureStates = 0, ctabSamplers = 0;
        for (std::uint32_t i = 0; i < Word(bytes, structure); ++i)
        {
            const auto type = 8 + Word(bytes, parameter);
            const auto declared = Word(bytes, type);
            if (declared >= 5 && declared <= 9)
            {
                EXPECT_EQ(declared, expected[kind]);
                ++typedTextures;
            }
            if (declared >= 10 && declared <= 14)
            {
                EXPECT_EQ(declared, 10u);
                ++genericSamplers;
                const auto value = 8 + Word(bytes, parameter + 4);
                for (std::uint32_t state = 0; state < Word(bytes, value); ++state)
                {
                    const auto record = value + 4 + state * 16;
                    if ((Word(bytes, record) & ~0xA0u) != EffectFormat::SampTexture) continue;
                    EXPECT_EQ(Word(bytes, 8 + Word(bytes, record + 8)), 5u); // D3DXPT_TEXTURE
                    ++genericTextureStates;
                }
            }
            parameter += 16 + Word(bytes, parameter + 12) * 8;
        }
        for (std::size_t at = structure; at + 36 < bytes.size(); at += 4)
        {
            if (Word(bytes, at) != 0x42415443u) continue;
            const auto ctab = at + 4, info = ctab + Word(bytes, ctab + 16);
            for (std::uint32_t i = 0; i < Word(bytes, ctab + 12); ++i)
            {
                const auto entry = info + i * 20;
                if ((Word(bytes, entry + 4) & 0xFFFFu) != 3) continue;
                EXPECT_EQ(Word(bytes, ctab + Word(bytes, entry + 12)) >> 16, 10u);
                ++ctabSamplers;
            }
        }
        EXPECT_EQ(typedTextures, 1);
        EXPECT_EQ(genericSamplers, 1);
        EXPECT_EQ(genericTextureStates, 1);
        EXPECT_EQ(ctabSamplers, 1);
    }
}

TEST(CompiledEffectSamplerInferencePolicyTest, GenericAndTypedObjectAliasesReachCapabilityOrTranslation)
{
    GraphicsDevice device;
    // Effect Framework texture-state values may be generic while their referenced parameter
    // is typed. FNA Effect.INTERNAL_parseEffectStruct resolves the object mapping's name for
    // every TEXTURE..TEXTURECUBE value; parameter and state do not need identical declarations.
    for (const auto kind : {SyntheticSamplerKind::Sampler2D,
                            SyntheticSamplerKind::SamplerCube, SyntheticSamplerKind::Sampler3D})
    {
        SCOPED_TRACE(static_cast<int>(kind));
        for (const bool samplerFirst : {false, true})
        {
            SCOPED_TRACE(samplerFirst);
            const auto bytes = TypedTextureWithGenericSampler(kind, false, samplerFirst);
            if (SupportsCompiledEffects(device))
            {
                EXPECT_NO_THROW({ Effect effect(device, bytes); });
            }
            else
            {
                try { Effect effect(device, bytes); FAIL() << "disabled capability must refuse"; }
                catch (const System::NotSupportedException& error)
                {
                    EXPECT_NE(std::string(error.what()).find("GraphicsCapability::CompiledEffects"),
                              std::string::npos) << error.what();
                }
            }
        }
    }
}

TEST(CompiledEffectSamplerInferencePolicyTest, IncompatibleTypedObjectAliasesRemainMalformed)
{
    GraphicsDevice device;
    for (const bool samplerFirst : {false, true})
    {
        SCOPED_TRACE(samplerFirst);
        auto bytes = TypedTextureWithGenericSampler(SyntheticSamplerKind::Sampler2D, false, samplerFirst);
        const auto structure = 8 + Word(bytes, 4);
        auto parameter = structure + 16;
        for (std::uint32_t i = 0; i < Word(bytes, structure); ++i)
        {
            const auto type = 8 + Word(bytes, parameter);
            if (Word(bytes, type) == 10u)
            {
                const auto value = 8 + Word(bytes, parameter + 4);
                const auto textureState = value + 4; // Builder always puts Texture first.
                ASSERT_EQ(Word(bytes, textureState), EffectFormat::SampTexture);
                // A single object cannot be both specifically Texture2D (7) and TextureCube (9).
                PatchUInt32(bytes, 8 + Word(bytes, textureState + 8), 9u);
            }
            parameter += 16 + Word(bytes, parameter + 12) * 8;
        }
        try { Effect effect(device, bytes); FAIL() << "conflicting typed object aliases must refuse"; }
        catch (const System::ArgumentException& error)
        {
            EXPECT_NE(std::string(error.what()).find("structurally valid"), std::string::npos)
                << error.what();
        }
    }
}
