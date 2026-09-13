#!/usr/bin/env bash
# plans/plan_sdlgpu.md SDLGPU-104: isolated cross-compiled SDL GPU/D3D12 CTest transport.
#
# Usage: scripts/run-wine-vkd3d-headless.sh <path-to.exe> [args...]
#
# A separate Xvfb server is created for every invocation. Wine/vkd3d needs that virtual X server
# to enumerate the Vulkan-backed DXGI adapter even though SDL itself uses the dummy video driver
# and CNA deliberately creates no swapchain. The renderer's test-only forced-headless guard also
# rejects this mode unless SDL confirms dummy/offscreen video, so this wrapper cannot accidentally
# present on the host display.
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to.exe> [args...]" >&2
    exit 2
fi

scriptDir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "error: xvfb-run is required for isolated SDL GPU D3D12 tests" >&2
    exit 1
fi

exec xvfb-run -a -s '-screen 0 1024x768x24' \
    env WAYLAND_DISPLAY= SDL_VIDEODRIVER=dummy SDL_GPU_DRIVER=direct3d12 \
        CNA_SDLGPU_TEST_FORCE_HEADLESS=1 \
        bash "${scriptDir}/run-wine-vkd3d.sh" "$@"
