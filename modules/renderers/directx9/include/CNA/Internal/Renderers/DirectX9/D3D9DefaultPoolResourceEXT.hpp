#pragma once

// plans/plan_dx9.md D9-34/D9-40: CNAEXT internal machinery, D3D9-only. D3DPOOL_DEFAULT resources (dynamic
// vertex/index buffers, D9-40/D9-41; later render targets, D9-53) do not survive a device Reset()
// the way D3DPOOL_MANAGED resources do (D9-4's own spike already confirmed MANAGED survives
// untouched) -- they must be explicitly released before Reset() and are recreated lazily on next
// use. This is real, authentic D3D9/XNA behavior, not a CNA limitation: a real XNA game using a
// DYNAMIC VertexBuffer is expected to re-fill it after DeviceReset fires.
//
// DirectX9Renderer keeps a small registry (raw, non-owning pointers) of every live
// D3DPOOL_DEFAULT resource; PerformResetRecovery() calls ReleaseDefaultPoolResourceEXT() on each
// before Reset(). Each resource re-creates its own D3DPOOL_DEFAULT object lazily (its own
// EnsureCapacity()-style check already handles "buffer_ is null" the same way it handles
// "buffer_ is too small") the next time it is actually used -- no separate "OnDeviceReset" step is
// needed.

namespace CNA::Internal::Renderers::DirectX9
{
    /// Implemented by every D3D9 renderer object that owns a D3DPOOL_DEFAULT resource.
    class ID3D9DefaultPoolResourceEXT
    {
    public:
        virtual ~ID3D9DefaultPoolResourceEXT() = default;
        /// Releases the underlying D3DPOOL_DEFAULT COM object. Called by
        /// DirectX9Renderer::PerformResetRecovery() right before IDirect3DDevice9::Reset() --
        /// using a D3DPOOL_DEFAULT resource across Reset() without releasing it first is undefined
        /// behavior in D3D9, not just stale data.
        virtual void ReleaseDefaultPoolResourceEXT() = 0;
    };
}
