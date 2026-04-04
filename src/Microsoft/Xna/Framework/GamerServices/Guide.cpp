//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
namespace Microsoft::Xna::Framework::GamerServices {
    void Guide::Show(const PlayerIndex& playerIndex) {
        std::cout << "The Market Place should now be shown.";
    }

    IMPL_PROP(bool, IsTrialMode, getter1, setter0, member1, static1, constret1, ref1, constmet0, Guide, false)
}