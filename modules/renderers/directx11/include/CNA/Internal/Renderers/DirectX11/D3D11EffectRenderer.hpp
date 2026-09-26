#pragma once

// plans/plan_dx.md Phase DIRECTX8 (DX-58): custom ShaderEffect -- runtime D3DCompile() of arbitrary HLSL
// vertex+fragment source, separate from the offline stock-shader pipeline (design decision 5).
//
// DX-223 replaces the original SpriteBatch-only fixed-slot convention with shared D3D reflection.
// Constant-buffer variables are addressed by their HLSL names and offsets, texture/sampler
// registers retain their declared slots, and input layouts are created lazily against the caller's
// real vertex declaration. The fixed Sprite2DVertex layout remains one cache entry, not the
// definition of the whole ShaderEffect feature.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DProgramReflection.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DVertexFormatHelper.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    class D3D11EffectRenderer final : public IEffectRenderer
    {
    public:
        D3D11EffectRenderer(ID3D11Device* device, ID3D11DeviceContext* context);

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        void Bind() override;
        void Unbind() override;
        [[nodiscard]] bool IsValid() const override { return valid_; }
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }

        void SetUniformMat4(const char* name, const float* matrix) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;
        void SetUniformFloatArray(const char* name, const float* values, int count) override;
        void SetUniformVec2Array(const char* name, const float* values, int count) override;
        void SetUniformVec3Array(const char* name, const float* values, int count) override;
        void SetUniformMat4Array(const char* name, const float* matrices, int count) override;
        void BindTexture(int unit, ITextureRenderer* texture) override;
        void BindTextureCube(int unit, ITextureCubeRenderer* texture) override;
        void BindTexture3D(int unit, ITexture3DRenderer* texture) override;
        /**
         * @brief Binds a sampled array while retaining its renderer-owned storage.
         * @param unit Pixel-shader texture register.
         * @param texture Array record, or null to clear the register.
         * @return True when the slot and native device match.
         */
        [[nodiscard]] bool BindTexture2DArrayEXT(
            int unit, std::shared_ptr<ITexture2DArrayRenderer> texture) override;
        /**
         * @brief Binds a storage texture through a sampled pixel-shader slot.
         * @param unit Pixel-shader texture register.
         * @param texture Storage record, or null to clear the register.
         * @return True when the texture has a native sampled view on this device.
         */
        [[nodiscard]] bool BindStorageTexture2DEXT(
            int unit, std::shared_ptr<IStorageTexture2DRenderer> texture) override;

        /** @brief Writes the reflected viewport-size aliases used by SpriteBatch shaders. */
        void SetViewportSizeEXT(float width, float height);

        /** @brief Binds the completed effect parameters and SpriteBatch vertex layout. */
        [[nodiscard]] bool BindSpriteEXT();

        /**
         * @brief Binds this effect for a non-sprite draw using the caller's complete input layout.
         *
         * @param declaration Combined per-vertex declaration used for shader semantic selection.
         * @param inputElements Explicit slotted elements for multi-stream input, or empty.
         * @return True when a native input layout matching this effect's vertex shader exists.
         */
        [[nodiscard]] bool BindForDrawEXT(
            const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaration,
            const std::vector<D3DCommon::D3DVertexInputElement>& inputElements);

        /**
         * @brief Binds a vertex variant that reads the absolute logical instance ID from the first private input slot.
         * @param declaration Combined per-vertex declaration.
         * @param inputElements Explicit slotted input elements.
         * @param usesLogicalIdStream Set when the vertex shader consumes the private ID stream.
         * @return True when the required input layout and shader variant are valid.
         */
        [[nodiscard]] bool BindForBaseInstanceDrawEXT(
            const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaration,
            const std::vector<D3DCommon::D3DVertexInputElement>& inputElements,
            bool& usesLogicalIdStream);

    private:
        enum class TextureKind
        {
            None,
            Texture2D,
            TextureCube,
            Texture3D,
            Texture2DArray,
            StorageTexture2D,
        };

        struct TextureBinding
        {
            TextureKind kind = TextureKind::None;
            void* texture = nullptr;
            bool explicitlySet = false;
            std::shared_ptr<ITexture2DArrayRenderer> retainedArray;
            std::shared_ptr<IStorageTexture2DRenderer> retainedStorage;
        };

        using InputLayoutKey = std::tuple<
            D3DCommon::D3DVertexDeclarationKey,
            D3DCommon::D3DVertexInputLayoutKey>;

        void BindProgramEXT(bool preserveImplicitTexture0);
        [[nodiscard]] ComPtr<ID3D11InputLayout> GetOrCreateInputLayoutEXT(
            const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaration,
            const std::vector<D3DCommon::D3DVertexInputElement>& inputElements,
            bool logicalInstanceId = false);
        [[nodiscard]] bool EnsureBaseInstanceVertexShaderEXT();
        [[nodiscard]] ID3D11ShaderResourceView* ResolveTextureSrvEXT(int slot) const;

        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<ID3D11VertexShader> vs_;
        ComPtr<ID3D11VertexShader> baseInstanceVs_;
        ComPtr<ID3D11PixelShader> ps_;
        ComPtr<ID3DBlob> vsBytecode_;
        ComPtr<ID3DBlob> baseInstanceVsBytecode_;
        ComPtr<ID3DBlob> psBytecode_;
        std::map<InputLayoutKey, ComPtr<ID3D11InputLayout>> inputLayouts_;
        std::map<InputLayoutKey, ComPtr<ID3D11InputLayout>> baseInstanceInputLayouts_;
        std::string vertexSource_;
        bool hasInstanceIdInput_ = false;
        D3DCommon::D3DProgramReflection reflection_;
        std::array<ComPtr<ID3D11Buffer>,
                   D3DCommon::D3DProgramReflection::kMaxConstantBuffers> constantBuffers_{};
        std::array<TextureBinding,
                   D3DCommon::D3DProgramReflection::kMaxShaderResources> textures_{};
        std::string compileError_;
        bool valid_ = false;
    };
}
