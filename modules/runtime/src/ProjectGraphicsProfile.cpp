// SPDX-License-Identifier: MS-PL
#include "CNA/ProjectGraphicsProfile.hpp"

namespace CNA
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::GraphicsProfile;

        GraphicsProfile& storage()
        {
            static GraphicsProfile profile = GraphicsProfile::Reach;
            return profile;
        }
    }

    void SetProjectGraphicsProfileEXT(
        Microsoft::Xna::Framework::Graphics::GraphicsProfile profile)
    {
        storage() = profile;
    }

    Microsoft::Xna::Framework::Graphics::GraphicsProfile
    GetProjectGraphicsProfileEXT()
    {
        return storage();
    }

    ProjectGraphicsProfileEXT::ProjectGraphicsProfileEXT(
        Microsoft::Xna::Framework::Graphics::GraphicsProfile profile)
    {
        SetProjectGraphicsProfileEXT(profile);
    }
}
