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

    // Task 11.22: --wardrobe-hair <Style> swaps the baked-in hair for a standalone
    // wardrobe piece (Content/wardrobe/hair_<Style>/) via SkinnedModelEXT::AttachPartEXT
    // (Task 11.21), at load time -- proving the runtime attach path, not just that it
    // compiles. Empty (the default) means "keep the baked-in hair, don't attach anything".
    std::string ParseWardrobeHairArg(int argc, char* argv[])
    {
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (std::strcmp(argv[i], "--wardrobe-hair") == 0) { return argv[i + 1]; }
        }
        return "";
    }
}

int main(int argc, char* argv[])
{
    auto* game = new AvatarDemo(ParseGenderArg(argc, argv), ParseWardrobeHairArg(argc, argv));
    game->Run();
    delete game;
    return 0;
}
