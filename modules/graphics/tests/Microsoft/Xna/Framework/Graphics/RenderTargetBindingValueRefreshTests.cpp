// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "System/ArgumentNullException.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

TEST(RenderTargetBindingValueRefreshTest, TypedNullConstructorsNameTheirArgument)
{
    // Refresh RTBinding::.ctor(2D) and .ctor(cube,face), IL_0003-0012:
    // both throw ArgumentNullException with ParamName="renderTarget".
    for (const bool cube : {false, true})
    {
        try
        {
            if (cube) (void)RenderTargetBinding(static_cast<RenderTargetCube*>(nullptr), CubeMapFace::NegativeZ);
            else (void)RenderTargetBinding(static_cast<RenderTarget2D*>(nullptr));
            ADD_FAILURE() << "null constructor accepted";
        }
        catch (const System::ArgumentNullException& error)
        {
            EXPECT_EQ(error.getParamNameProperty(), "renderTarget");
        }
    }
}
