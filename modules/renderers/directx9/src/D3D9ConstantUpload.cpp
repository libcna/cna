#include "CNA/Internal/Renderers/DirectX9/D3D9ConstantUpload.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::DirectX9
{
    namespace
    {
        const Shaders::D3D9ShaderConstantSlot* FindSlot(const Shaders::D3D9ShaderConstantSlot* table,
                                                         int count, const char* name)
        {
            for (int i = 0; i < count; ++i)
            {
                if (std::strcmp(table[i].name, name) == 0)
                    return &table[i];
            }
            return nullptr;
        }
    }

    void UploadVertexShaderConstantEXT(IDirect3DDevice9* device,
                                       const Shaders::D3D9ShaderConstantSlot* table, int count,
                                       const char* name, const float* data)
    {
        if (!table || count == 0) return; // this shader variant has no named constants at all
        const Shaders::D3D9ShaderConstantSlot* slot = FindSlot(table, count, name);
        if (!slot)
        {
            throw std::runtime_error(
                std::string("UploadVertexShaderConstantEXT: constant '") + name +
                "' not present in this shader's register table (plans/plan_dx9.md D9-82)");
        }
        device->SetVertexShaderConstantF(slot->registerIndex, data, slot->registerCount);
    }

    void UploadPixelShaderConstantEXT(IDirect3DDevice9* device,
                                      const Shaders::D3D9ShaderConstantSlot* table, int count,
                                      const char* name, const float* data)
    {
        if (!table || count == 0) return;
        const Shaders::D3D9ShaderConstantSlot* slot = FindSlot(table, count, name);
        if (!slot)
        {
            throw std::runtime_error(
                std::string("UploadPixelShaderConstantEXT: constant '") + name +
                "' not present in this shader's register table (plans/plan_dx9.md D9-82)");
        }
        device->SetPixelShaderConstantF(slot->registerIndex, data, slot->registerCount);
    }

    bool TryUploadVertexShaderConstantEXT(IDirect3DDevice9* device,
                                          const Shaders::D3D9ShaderConstantSlot* table, int count,
                                          const char* name, const float* data)
    {
        if (!table || count == 0) return false;
        const Shaders::D3D9ShaderConstantSlot* slot = FindSlot(table, count, name);
        if (!slot) return false;
        device->SetVertexShaderConstantF(slot->registerIndex, data, slot->registerCount);
        return true;
    }

    bool TryUploadPixelShaderConstantEXT(IDirect3DDevice9* device,
                                         const Shaders::D3D9ShaderConstantSlot* table, int count,
                                         const char* name, const float* data)
    {
        if (!table || count == 0) return false;
        const Shaders::D3D9ShaderConstantSlot* slot = FindSlot(table, count, name);
        if (!slot) return false;
        device->SetPixelShaderConstantF(slot->registerIndex, data, slot->registerCount);
        return true;
    }
}
