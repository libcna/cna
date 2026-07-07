#include "AvatarDemo.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    // Task 11.12: --gender male|female selects which procedurally-generated body
    // AvatarDemo loads, via AvatarBodyTypeToContentNameEXT. Defaults to Male.
    //
    // Task 14.3: previously silently accepted any value other than exactly "female" (including a
    // typo like "Female") as Male, with no warning. Rejects anything other than "male"/"female"
    // with a friendly usage error instead.
    Microsoft::Xna::Framework::GamerServices::AvatarBodyType ParseGenderArg(int argc, char* argv[])
    {
        using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (std::strcmp(argv[i], "--gender") == 0)
            {
                if (std::strcmp(argv[i + 1], "female") == 0) { return AvatarBodyType::Female; }
                if (std::strcmp(argv[i + 1], "male") == 0) { return AvatarBodyType::Male; }
                std::fprintf(stderr, "cna_demo_avatar: unrecognized --gender value \"%s\" "
                                      "(expected \"male\" or \"female\")\n", argv[i + 1]);
                std::exit(64);
            }
        }
        return AvatarBodyType::Male;
    }

    // Task 11.22: --wardrobe-hair <Style> swaps the baked-in hair for a standalone
    // wardrobe piece (Content/wardrobe/hair_<Style>/) via SkinnedModelEXT::AttachPartEXT
    // (Task 11.21), at load time -- proving the runtime attach path, not just that it
    // compiles. Empty (the default) means "keep the baked-in hair, don't attach anything".
    //
    // Task 14.3: previously did zero validation against the two known styles - a bogus style
    // threw a raw, unfriendly ContentLoadException from deep inside ContentManager instead of a
    // clear usage error. Validates against the two shipped styles up front instead.
    std::string ParseWardrobeHairArg(int argc, char* argv[])
    {
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (std::strcmp(argv[i], "--wardrobe-hair") == 0)
            {
                const char* style = argv[i + 1];
                if (std::strcmp(style, "Cap") == 0 || std::strcmp(style, "Ponytail") == 0)
                {
                    return style;
                }
                std::fprintf(stderr, "cna_demo_avatar: unrecognized --wardrobe-hair value \"%s\" "
                                      "(expected \"Cap\" or \"Ponytail\")\n", style);
                std::exit(64);
            }
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
