#pragma once

// plans/plan_dx9.md Phase D9-5 (D9-55): real D3D9 occlusion query renderer.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d3d9.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::WRL::ComPtr;

    /// Real D3D9 occlusion query renderer (D9-55), `IDirect3DQuery9` with `D3DQUERYTYPE_OCCLUSION`.
    /// `Begin()`/`End()` map to `Issue(D3DISSUE_BEGIN)`/`Issue(D3DISSUE_END)`; `PixelCount()` reads
    /// back an exact visible-sample count (unlike EasyGL's GLES3 `GL_ANY_SAMPLES_PASSED`
    /// boolean-only result), matching real D3D9/XNA behavior.
    class D3D9OcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        explicit D3D9OcclusionQueryRenderer(IDirect3DDevice9* device);

        void Begin() override;
        void End() override;
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int PixelCount() const override;

    private:
        ComPtr<IDirect3DDevice9> device_;
        ComPtr<IDirect3DQuery9> query_;
    };
}
