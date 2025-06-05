//
// Created by robertvokac on 5/28/25.
//

#include "Microsoft/Xna/Framework/GamerServices/Guide.h"
namespace Microsoft::Xna::Framework::GamerServices {
    void Guide::Show(const PlayerIndex& playerIndex) {
        std::cout << "The Market Place should now be shown.";
    }

    igetterstatic(bool, IsTrialMode, Guide ,false);
}