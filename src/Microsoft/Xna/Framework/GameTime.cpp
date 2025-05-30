//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/GameTime.h"
namespace Microsoft::Xna::Framework {

    GameTime::GameTime() :
    IMPL_PROP_AUTO(TimeSpan, TotalGameTime),
    IMPL_PROP_AUTO(TimeSpan, ElapsedGameTime)
    {
    }
}
