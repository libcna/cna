// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::DevicesExt (plans/MODULARIZATION_PLAN.md §11): the CNA-specific
// device extensions must be usable WITHOUT the XNA device base (dependency direction is
// devices-ext -> runtime/graphics/core/math, never devices-ext <-> devices). The extension
// surface itself is option-gated (CNA_DEVICES, default OFF), so the probe uses real symbols
// when the option is on and stays a pure link-closure consumer otherwise; the paired
// ModuleLinkClosure_probe_devices_ext ctest asserts the base devices archive stays out.
#include "CNA/Devices/FileDialogFilter.hpp"
#include "CNA/Devices/PowerState.hpp"

#include <cstdio>

int main()
{
#ifdef CNA_DEVICES
    CNA::Devices::FileDialogFilter filter;
    filter.Name = "All files";
    filter.Extensions = "*";
    std::printf("devices-ext probe: filter '%s' power=%d\n", filter.Name.c_str(),
                static_cast<int>(CNA::Devices::PowerState::OnBattery));
#else
    std::printf("devices-ext probe: extension surface disabled (CNA_DEVICES off); "
                "link closure still verified\n");
#endif
    return 0;
}
