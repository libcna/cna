// plan_dx.md Phase DIRECTX3 (DX-15-embed).
#include "CNA/Internal/Renderers/D3DCommon/D3DShaderCache.hpp"

#include "shaders/hlsl_shaders.hpp"

namespace CNA::Internal::Renderers::D3DCommon
{
    namespace
    {
        struct Bytecode
        {
            const uint8_t* bytes;
            std::size_t size;
        };

        Bytecode VertexBytecodeFor(D3DShaderVariant variant)
        {
            using namespace CNA::Internal::Renderers::D3DCommon::Shaders;
            switch (variant)
            {
                case D3DShaderVariant::Colored3d:         return {kColored3dVertDxbc, kColored3dVertDxbc_size};
                case D3DShaderVariant::Textured3d:        return {kTextured3dVertDxbc, kTextured3dVertDxbc_size};
                case D3DShaderVariant::ColoredTextured3d: return {kColoredTextured3dVertDxbc, kColoredTextured3dVertDxbc_size};
                case D3DShaderVariant::LitTextured3d:     return {kLitTextured3dVertDxbc, kLitTextured3dVertDxbc_size};
                case D3DShaderVariant::AlphaTest3d:       return {kAlphaTest3dVertDxbc, kAlphaTest3dVertDxbc_size};
                case D3DShaderVariant::DualTexture3d:     return {kDualTexture3dVertDxbc, kDualTexture3dVertDxbc_size};
                case D3DShaderVariant::EnvMap3d:          return {kEnvMap3dVertDxbc, kEnvMap3dVertDxbc_size};
                case D3DShaderVariant::Skinned3d:         return {kSkinned3dVertDxbc, kSkinned3dVertDxbc_size};
                case D3DShaderVariant::Sprite2d:          return {kSprite2dVertDxbc, kSprite2dVertDxbc_size};
                case D3DShaderVariant::Instanced3d:       return {kInstanced3dVertDxbc, kInstanced3dVertDxbc_size};
                case D3DShaderVariant::AlphaTestColored3d: return {kAlphaTestColored3dVertDxbc, kAlphaTestColored3dVertDxbc_size};
                case D3DShaderVariant::LitTextured3dVertexLit: return {kLitTextured3dVertexLitVertDxbc, kLitTextured3dVertexLitVertDxbc_size};
                case D3DShaderVariant::Skinned3dVertexLit:     return {kSkinned3dVertexLitVertDxbc, kSkinned3dVertexLitVertDxbc_size};
                case D3DShaderVariant::Pbr3d:                  return {kPbr3dVertDxbc, kPbr3dVertDxbc_size};
                case D3DShaderVariant::Pbr3dDualUv:            return {kPbr3dDualUvVertDxbc, kPbr3dDualUvVertDxbc_size};
                case D3DShaderVariant::PbrSkinned3d:           return {kPbrSkinned3dVertDxbc, kPbrSkinned3dVertDxbc_size};
                case D3DShaderVariant::PbrSkinned3dDualUv:     return {kPbrSkinned3dDualUvVertDxbc, kPbrSkinned3dDualUvVertDxbc_size};
                case D3DShaderVariant::PbrSkinned3dDualUvColor: return {kPbrSkinned3dDualUvColorVertDxbc, kPbrSkinned3dDualUvColorVertDxbc_size};
                case D3DShaderVariant::Skinned3dColored:       return {kSkinned3dColoredVertDxbc, kSkinned3dColoredVertDxbc_size};
                case D3DShaderVariant::Skinned3dVertexLitColored: return {kSkinned3dVertexLitColoredVertDxbc, kSkinned3dVertexLitColoredVertDxbc_size};
            }
            return {nullptr, 0};
        }

        Bytecode PixelBytecodeFor(D3DShaderVariant variant)
        {
            using namespace CNA::Internal::Renderers::D3DCommon::Shaders;
            switch (variant)
            {
                case D3DShaderVariant::Colored3d:         return {kColored3dFragDxbc, kColored3dFragDxbc_size};
                case D3DShaderVariant::Textured3d:        return {kTextured3dFragDxbc, kTextured3dFragDxbc_size};
                case D3DShaderVariant::ColoredTextured3d: return {kColoredTextured3dFragDxbc, kColoredTextured3dFragDxbc_size};
                case D3DShaderVariant::LitTextured3d:     return {kLitTextured3dFragDxbc, kLitTextured3dFragDxbc_size};
                case D3DShaderVariant::AlphaTest3d:       return {kAlphaTest3dFragDxbc, kAlphaTest3dFragDxbc_size};
                case D3DShaderVariant::DualTexture3d:     return {kDualTexture3dFragDxbc, kDualTexture3dFragDxbc_size};
                case D3DShaderVariant::EnvMap3d:          return {kEnvMap3dFragDxbc, kEnvMap3dFragDxbc_size};
                case D3DShaderVariant::Skinned3d:         return {kSkinned3dFragDxbc, kSkinned3dFragDxbc_size};
                case D3DShaderVariant::Sprite2d:          return {kSprite2dFragDxbc, kSprite2dFragDxbc_size};
                case D3DShaderVariant::Instanced3d:       return {kInstanced3dFragDxbc, kInstanced3dFragDxbc_size};
                case D3DShaderVariant::AlphaTestColored3d: return {kAlphaTestColored3dFragDxbc, kAlphaTestColored3dFragDxbc_size};
                case D3DShaderVariant::LitTextured3dVertexLit: return {kLitTextured3dVertexLitFragDxbc, kLitTextured3dVertexLitFragDxbc_size};
                case D3DShaderVariant::Skinned3dVertexLit:     return {kSkinned3dVertexLitFragDxbc, kSkinned3dVertexLitFragDxbc_size};
                case D3DShaderVariant::Pbr3d:                  return {kPbr3dFragDxbc, kPbr3dFragDxbc_size};
                case D3DShaderVariant::Pbr3dDualUv:            return {kPbr3dDualUvFragDxbc, kPbr3dDualUvFragDxbc_size};
                case D3DShaderVariant::PbrSkinned3d:           return {kPbrSkinned3dFragDxbc, kPbrSkinned3dFragDxbc_size};
                case D3DShaderVariant::PbrSkinned3dDualUv:     return {kPbrSkinned3dDualUvFragDxbc, kPbrSkinned3dDualUvFragDxbc_size};
                case D3DShaderVariant::PbrSkinned3dDualUvColor: return {kPbrSkinned3dDualUvColorFragDxbc, kPbrSkinned3dDualUvColorFragDxbc_size};
                case D3DShaderVariant::Skinned3dColored:       return {kSkinned3dColoredFragDxbc, kSkinned3dColoredFragDxbc_size};
                case D3DShaderVariant::Skinned3dVertexLitColored: return {kSkinned3dVertexLitColoredFragDxbc, kSkinned3dVertexLitColoredFragDxbc_size};
            }
            return {nullptr, 0};
        }
    }

    void GetVertexShaderBytecode(D3DShaderVariant variant, const uint8_t*& bytes, std::size_t& size)
    {
        const Bytecode bc = VertexBytecodeFor(variant);
        bytes = bc.bytes;
        size = bc.size;
    }

    void GetPixelShaderBytecode(D3DShaderVariant variant, const uint8_t*& bytes, std::size_t& size)
    {
        const Bytecode bc = PixelBytecodeFor(variant);
        bytes = bc.bytes;
        size = bc.size;
    }

    ComPtr<ID3D11VertexShader> CreateVertexShaderForVariant(ID3D11Device* device, D3DShaderVariant variant)
    {
        const Bytecode bc = VertexBytecodeFor(variant);
        ComPtr<ID3D11VertexShader> shader;
        if (bc.bytes == nullptr || device == nullptr)
            return shader;
        device->CreateVertexShader(bc.bytes, bc.size, nullptr, shader.GetAddressOf());
        return shader;
    }

    ComPtr<ID3D11PixelShader> CreatePixelShaderForVariant(ID3D11Device* device, D3DShaderVariant variant)
    {
        const Bytecode bc = PixelBytecodeFor(variant);
        ComPtr<ID3D11PixelShader> shader;
        if (bc.bytes == nullptr || device == nullptr)
            return shader;
        device->CreatePixelShader(bc.bytes, bc.size, nullptr, shader.GetAddressOf());
        return shader;
    }
}
