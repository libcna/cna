#include "AvatarDemo.hpp"

#include <cstring>

namespace
{
    // Task 11.12: --gender male|female selects which procedurally-generated body
    // AvatarDemo loads, via AvatarBodyTypeToContentNameEXT. Defaults to Male.
    Microsoft::Xna::Framework::GamerServices::AvatarBodyType ParseGenderArg(int argc, char* argv[])
    {
        using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (std::strcmp(argv[i], "--gender") == 0)
            {
                if (std::strcmp(argv[i + 1], "female") == 0) { return AvatarBodyType::Female; }
                return AvatarBodyType::Male;
            }
        }
        return AvatarBodyType::Male;
    }
}

int main(int argc, char* argv[])
{
    auto* game = new AvatarDemo(ParseGenderArg(argc, argv));
    game->Run();
    delete game;
    return 0;
}
