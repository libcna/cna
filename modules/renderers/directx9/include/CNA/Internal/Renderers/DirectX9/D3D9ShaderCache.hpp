#pragma once

// plans/plan_dx9.md Phase D9-7 (D9-74): creates real IDirect3DVertexShader9/IDirect3DPixelShader9
// objects from D9-71's embedded bytecode (d3d9_shaders.hpp), through a live D3D9 device.
//
// Lookup is by name ("<EffectName>_<EntryPointName>", e.g. "BasicEffect_VSBasic" -- matches
// Shaders::kAllShaders' own name field, D9-71's own manifest table) rather than a hand-written
// enum: with 66 distinct entry points across 6 effects, a name-keyed cache mirrors how a future
// XNA Effect-dispatch consumer will naturally select a shader (by which effect + which feature
// combination is active), not an arbitrary ordinal.

#include <d3d9.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::WRL::ComPtr;

    /// Real D3D9 vertex/pixel shader object cache (D9-74). Creates lazily on first request,
    /// caches for the lifetime of this object -- mirrors this project's other GPU-object caches
    /// (e.g. D3D11's own sampler-state cache) in shape, adapted to D3D9's register-based shader
    /// objects (no state-object equivalent exists for shaders themselves; `IDirect3DVertexShader9`/
    /// `IDirect3DPixelShader9` already are the compiled, cacheable GPU object).
    class D3D9ShaderCache
    {
    public:
        explicit D3D9ShaderCache(IDirect3DDevice9* device);

        /// Returns the cached vertex shader for `name`, creating it on first request. Throws
        /// `std::runtime_error` if `name` is not a known vertex shader (unknown name, or a real
        /// pixel-shader name passed by mistake) or if `CreateVertexShader` fails.
        [[nodiscard]] IDirect3DVertexShader9* GetVertexShader(const std::string& name);
        /// Returns the cached pixel shader for `name`, creating it on first request. Same error
        /// behavior as `GetVertexShader` for an unknown/wrong-stage name or a real device failure.
        [[nodiscard]] IDirect3DPixelShader9* GetPixelShader(const std::string& name);

        /// D9-74: eagerly creates every one of the 66 embedded shaders through the live device --
        /// the real per-task verification this row's own plan text asks for ("test creates all 66
        /// through a live device"), and a legitimate warm-cache convenience for a future consumer
        /// that would rather pay shader-creation cost once up front than on first use. CNAEXT.
        void CreateAllEXT();

        /// Number of distinct vertex shaders created so far (CNAEXT diagnostics/tests).
        [[nodiscard]] size_t GetCachedVertexShaderCountEXT() const { return vertexShaders_.size(); }
        /// Number of distinct pixel shaders created so far (CNAEXT diagnostics/tests).
        [[nodiscard]] size_t GetCachedPixelShaderCountEXT() const { return pixelShaders_.size(); }

    private:
        ComPtr<IDirect3DDevice9> device_;
        std::unordered_map<std::string, ComPtr<IDirect3DVertexShader9>> vertexShaders_;
        std::unordered_map<std::string, ComPtr<IDirect3DPixelShader9>> pixelShaders_;
    };
}
