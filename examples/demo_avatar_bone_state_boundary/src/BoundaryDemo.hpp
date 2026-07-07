#pragma once

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"

// Task 15.20: cna_demo_avatar_bone_state_boundary. Documents the real, verified
// AvatarRenderer skeleton-API boundary via console output, contrasted against the working
// SkinnedModelEXT EXT path demo_avatar actually uses. Minimal window (no meaningful rendering);
// all the actual content is printed to console during Initialize(), then the demo exits itself
// after a few frames.
class BoundaryDemo : public Microsoft::Xna::Framework::Game
{
public:
    BoundaryDemo() = default;

    void Initialize() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

private:
    int framesBeforeExit_ = 30;
};
