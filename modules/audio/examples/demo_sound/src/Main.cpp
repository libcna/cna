#include "SoundDemo.hpp"
#include <iostream>

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "CNA Audio Demo\n"
              << "Controls:\n"
              << "  [1]         - Play click (fire-and-forget)\n"
              << "  [2]         - Toggle looping tone (Play/Pause)\n"
              << "  [3]         - Stop looping tone\n"
              << "  [4]         - Play beep with current vol/pitch/pan\n"
              << "  [5]         - Play 3D-positioned beep at emitter location\n"
              << "  [6]         - Toggle DynamicSoundEffectInstance (sine generator)\n"
              << "  Up/Down     - Volume +/-\n"
              << "  W/S         - Pitch +/- (semitone steps)\n"
              << "  Left/Right  - Pan left/right\n"
              << "  A/D         - Move 3D emitter left/right\n"
              << "  Z/X         - DynamicSEI frequency down/up\n"
              << "  Escape      - Exit\n\n";

    auto* game = new SoundDemo();
    game->Run();
    delete game;
    return 0;
}
