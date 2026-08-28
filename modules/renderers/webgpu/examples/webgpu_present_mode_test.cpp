// SPDX-License-Identifier: MS-PL
// WEBGPU-108: unit test for the PresentInterval -> WGPUPresentMode policy. The renderer maps
// PresentInterval to a swap interval, then SelectPresentModeChoiceEXT() picks the present mode from
// what the surface advertises; the renderer only translates that choice onto a concrete
// WGPUPresentMode. This tests the policy (including the fallback when the requested mode is
// unsupported) with no GPU/display -- present mode is not observable through pixel readback.

#include "CNA/Internal/Renderers/WebGPU/WebGPUPresentMode.hpp"

#include <cstdio>
#include <string>

using CNA::Internal::Renderers::WebGPU::PresentModeChoiceEXT;
using CNA::Internal::Renderers::WebGPU::SelectPresentModeChoiceEXT;

namespace
{
    int g_passed = 0;
    int g_total = 0;

    const char* Name(PresentModeChoiceEXT c)
    {
        switch (c)
        {
            case PresentModeChoiceEXT::Fifo:           return "Fifo";
            case PresentModeChoiceEXT::Immediate:      return "Immediate";
            case PresentModeChoiceEXT::Mailbox:        return "Mailbox";
            case PresentModeChoiceEXT::FirstAvailable: return "FirstAvailable";
        }
        return "?";
    }

    void Check(const std::string& label, PresentModeChoiceEXT got, PresentModeChoiceEXT expected)
    {
        ++g_total;
        const bool ok = got == expected;
        if (ok) ++g_passed;
        std::printf("[%s] %s got=%s expected=%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                    Name(got), Name(expected));
    }
}

int main()
{
    // swapInterval == 0 (no v-sync): prefer Immediate, then Mailbox, else Fifo.
    Check("noVSync + Immediate -> Immediate",
          SelectPresentModeChoiceEXT(0, /*imm*/true, /*mail*/true, /*fifo*/true, /*any*/true),
          PresentModeChoiceEXT::Immediate);
    Check("noVSync + Mailbox only -> Mailbox",
          SelectPresentModeChoiceEXT(0, false, true, true, true),
          PresentModeChoiceEXT::Mailbox);
    Check("noVSync + neither Immediate nor Mailbox -> Fifo",
          SelectPresentModeChoiceEXT(0, false, false, true, true),
          PresentModeChoiceEXT::Fifo);
    Check("noVSync + Fifo only -> Fifo",
          SelectPresentModeChoiceEXT(0, false, false, true, true),
          PresentModeChoiceEXT::Fifo);

    // swapInterval != 0 (v-sync): Fifo, which the WebGPU spec always guarantees.
    Check("vSync + Fifo available -> Fifo",
          SelectPresentModeChoiceEXT(1, true, true, true, true),
          PresentModeChoiceEXT::Fifo);
    Check("vSync + Immediate present but Fifo present too -> Fifo",
          SelectPresentModeChoiceEXT(2, true, false, true, true),
          PresentModeChoiceEXT::Fifo);

    // Fallback when the requested (v-sync/Fifo) mode is NOT advertised: use the first available.
    Check("vSync + Fifo UNSUPPORTED, others available -> FirstAvailable",
          SelectPresentModeChoiceEXT(1, /*imm*/true, /*mail*/true, /*fifo*/false, /*any*/true),
          PresentModeChoiceEXT::FirstAvailable);
    // No modes advertised at all: nothing to fall back to -> Fifo (the spec default).
    Check("vSync + no modes advertised -> Fifo",
          SelectPresentModeChoiceEXT(1, false, false, false, /*any*/false),
          PresentModeChoiceEXT::Fifo);
    Check("noVSync + no modes advertised -> Fifo",
          SelectPresentModeChoiceEXT(0, false, false, false, false),
          PresentModeChoiceEXT::Fifo);

    std::printf("=== WEBGPU-108 present-mode mapping: %d/%d PASS ===\n", g_passed, g_total);
    return (g_passed == g_total) ? 0 : 1;
}
