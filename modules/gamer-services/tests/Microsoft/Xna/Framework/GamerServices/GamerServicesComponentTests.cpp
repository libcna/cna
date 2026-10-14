// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp"

using Microsoft::Xna::Framework::Game;
using Microsoft::Xna::Framework::GamerServices::GamerServicesComponent;

// Every XNA type CNA ports reports its own fully qualified name; a derived component that forgot
// the override would answer with its base class's name.
TEST(GamerServicesComponentTest, GetTypeNameNamesTheDerivedType)
{
    Game game;
    GamerServicesComponent component(game);
    EXPECT_EQ(component.GetTypeName(),
              "Microsoft.Xna.Framework.GamerServices.GamerServicesComponent");
}
