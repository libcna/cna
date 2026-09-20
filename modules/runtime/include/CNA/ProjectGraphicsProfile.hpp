// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"

namespace CNA
{
    /**
     * @brief Declares the XNA graphics profile selected by the application's project.
     *
     * XNA's project profile is embedded in the executable rather than assigned by game code.
     * CNA defaults to Reach when no project profile is declared.
     *
     * @param profile The profile selected by the original XNA project.
     */
    CNAEXT void SetProjectGraphicsProfileEXT(
        Microsoft::Xna::Framework::Graphics::GraphicsProfile profile);

    /**
     * @brief Gets the graphics profile declared for this application.
     * @return The declared profile, or Reach when none was declared.
     */
    CNAEXT [[nodiscard]] Microsoft::Xna::Framework::Graphics::GraphicsProfile
    GetProjectGraphicsProfileEXT();

    /**
     * @brief Declares the XNA project's graphics profile before the game starts.
     */
    CNAEXT struct ProjectGraphicsProfileEXT
    {
        /**
         * @brief Registers the profile embedded by the original XNA project build.
         * @param profile The project's graphics profile.
         */
        explicit ProjectGraphicsProfileEXT(
            Microsoft::Xna::Framework::Graphics::GraphicsProfile profile);
    };
}
