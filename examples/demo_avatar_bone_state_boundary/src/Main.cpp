#include "BoundaryDemo.hpp"

#include <cstdio>

// Task 15.20: cna_demo_avatar_bone_state_boundary. Single process, minimal window - all real
// content is printed to console during Initialize(); exits itself after a few frames.
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    auto* game = new BoundaryDemo();
    game->Run();
    delete game;
    return 0;
}
