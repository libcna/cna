#pragma once

// plan_dx9.md Phase D9-5 (D9-55): real D3D9 occlusion query backend.

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <d3d9.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D9
{
    using Microsoft::WRL::ComPtr;

    /// Real D3D9 occlusion query backend (D9-55), `IDirect3DQuery9` with `D3DQUERYTYPE_OCCLUSION`.
    /// `Begin()`/`End()` map to `Issue(D3DISSUE_BEGIN)`/`Issue(D3DISSUE_END)`; `PixelCount()` reads
    /// back an exact visible-sample count (unlike EasyGL's GLES3 `GL_ANY_SAMPLES_PASSED`
    /// boolean-only result), matching real D3D9/XNA behavior.
    class D3D9OcclusionQueryBackend final : public IOcclusionQueryBackend
    {
    public:
        explicit D3D9OcclusionQueryBackend(IDirect3DDevice9* device);

        void Begin() override;
        void End() override;
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int PixelCount() const override;

    private:
        ComPtr<IDirect3DDevice9> device_;
        ComPtr<IDirect3DQuery9> query_;
    };
}
