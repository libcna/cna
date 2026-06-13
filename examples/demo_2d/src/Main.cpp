#include "Game1.hpp"
#include <iostream>

// Demo project showcasing the use of CNA, an XNA-like wrapper over SDL 3.
// The main goal is to demonstrate a basic game loop and a moving object on the screen.
//
// Description
// cna-demo is a simple demonstration of how CNA can be used to create 2D games in C++.
// The project displays a moving sprite on the screen using SDL 3 as the backend.
//
// Main features:
//
// Game loop with Update and Draw functions
// Moving sprite
// Color-changing background
// Uses CNA to maintain an XNA-like interface

int main(int argc, char* argv[]) {
    std::cout<<"Starting game" <<std::endl;
    auto *game = new Game1();

    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--smoke" && i + 1 < argc)
            game->SetSmokeFrames(std::stoi(argv[++i]));
        else if (std::string(argv[i]) == "--smoke")
            game->SetSmokeFrames(3);
    }

    game->Run();  // Launching of the game.
    delete game;
    return 0;
}
