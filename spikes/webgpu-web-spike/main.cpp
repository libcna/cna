// Existence-gate spike (WEBGPU-119): proves the Emscripten branch of cna_configure_webgpu()
// links the emdawnwebgpu port and that the canvas-selector surface path this renderer uses
// compiles AND links into a real wasm module. Mirrors WebGPURenderer::CreateSurface() exactly.
#include <webgpu/webgpu.h>
#include <cstdio>
int main() {
    WGPUInstance instance = wgpuCreateInstance(nullptr);
    WGPUSurfaceDescriptor descriptor{};
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source{};
    source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    source.selector = WGPUStringView{"#canvas", WGPU_STRLEN};
    descriptor.nextInChain = &source.chain;
    WGPUSurface surface = wgpuInstanceCreateSurface(instance, &descriptor);
    std::printf("instance=%p surface=%p\n", (void*)instance, (void*)surface);
    return 0;
}
