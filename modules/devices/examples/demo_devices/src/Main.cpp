#include "CNA/Platform/Entrypoint.hpp"

#include "DevicesDemo.hpp"

// plans/plan_devices.md Task DEVICES-0125/0126 originally included a native main shim directly here.
// plans/plan_platform.md PLAT-54 replaced that with CNA/Platform/Entrypoint.hpp, which owns the
// rationale and the platform gating; this file no longer needs to know that Android's entry
// point works by resolving the platform library's renamed main symbol.
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    auto* game = new DevicesDemo();
    game->Run();
    delete game;
    return 0;
}
