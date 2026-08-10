// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp"
#include "System/ArgumentException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    std::string AvatarBodyTypeToContentNameEXT(AvatarBodyType bodyType)
    {
        switch (bodyType)
        {
            case AvatarBodyType::Female: return "avatar/female/avatar";
            case AvatarBodyType::Male: return "avatar/male/avatar";
        }
        throw System::ArgumentException("Unrecognized AvatarBodyType value.", "bodyType");
    }
}
