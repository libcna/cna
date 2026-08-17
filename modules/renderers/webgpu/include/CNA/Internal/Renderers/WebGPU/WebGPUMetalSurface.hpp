// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/NativeWindowHandle.hpp"

namespace CNA::Internal::Renderers::WebGPU
{
    /** Creates a layer-backed child view and returns its borrowed CAMetalLayer. Apple only. */
    void* CreateWebGPUMetalLayer(const CNA::Platform::NativeWindowHandle& handle,
                                 int width, int height, float displayScale, void*& owner);
    /** Updates the layer's physical drawable size and density. Apple only. */
    void ResizeWebGPUMetalLayer(void* owner, int width, int height, float displayScale);
    /** Removes and releases the child view created by CreateWebGPUMetalLayer. Apple only. */
    void DestroyWebGPUMetalLayer(void*& owner);
}
