#include "CNA/Internal/Backends/D3D9/D3D9ShaderCache.hpp"
#include "CNA/Internal/Backends/D3D9/shaders/d3d9_shaders.hpp"

#include <cstdio>
#include <stdexcept>

namespace CNA::Internal::Backends::D3D9
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        const Shaders::D3D9EmbeddedShader* FindEmbeddedShader(const std::string& name, bool wantVertexShader)
        {
            for (const auto& s : Shaders::kAllShaders)
            {
                if (s.isVertexShader == wantVertexShader && name == s.name)
                    return &s;
            }
            return nullptr;
        }
    }

    D3D9ShaderCache::D3D9ShaderCache(IDirect3DDevice9* device)
        : device_(device)
    {
    }

    IDirect3DVertexShader9* D3D9ShaderCache::GetVertexShader(const std::string& name)
    {
        auto it = vertexShaders_.find(name);
        if (it != vertexShaders_.end()) return it->second.Get();

        const auto* found = FindEmbeddedShader(name, true);
        if (!found)
            throw std::runtime_error("D3D9ShaderCache::GetVertexShader: unknown vertex shader name '" + name + "'");

        ComPtr<IDirect3DVertexShader9> shader;
        const HRESULT hr = device_->CreateVertexShader(
            reinterpret_cast<const DWORD*>(found->bytecode), shader.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D9ShaderCache::GetVertexShader: CreateVertexShader failed for '" + name + "', hr=" + FormatHr(hr));

        IDirect3DVertexShader9* raw = shader.Get();
        vertexShaders_.emplace(name, std::move(shader));
        return raw;
    }

    IDirect3DPixelShader9* D3D9ShaderCache::GetPixelShader(const std::string& name)
    {
        auto it = pixelShaders_.find(name);
        if (it != pixelShaders_.end()) return it->second.Get();

        const auto* found = FindEmbeddedShader(name, false);
        if (!found)
            throw std::runtime_error("D3D9ShaderCache::GetPixelShader: unknown pixel shader name '" + name + "'");

        ComPtr<IDirect3DPixelShader9> shader;
        const HRESULT hr = device_->CreatePixelShader(
            reinterpret_cast<const DWORD*>(found->bytecode), shader.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D9ShaderCache::GetPixelShader: CreatePixelShader failed for '" + name + "', hr=" + FormatHr(hr));

        IDirect3DPixelShader9* raw = shader.Get();
        pixelShaders_.emplace(name, std::move(shader));
        return raw;
    }

    void D3D9ShaderCache::CreateAllEXT()
    {
        for (const auto& s : Shaders::kAllShaders)
        {
            if (s.isVertexShader) (void)GetVertexShader(s.name);
            else (void)GetPixelShader(s.name);
        }
    }
}
