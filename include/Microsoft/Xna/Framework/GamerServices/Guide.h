//
// Created by robertvokac on 5/28/25.
//

#ifndef GUIDE_H
#define GUIDE_H
#include <iostream>

#include "Microsoft/Xna/Framework/PlayerIndex.h"


namespace Microsoft::Xna::Framework::GamerServices {
    class Guide {
    public:
        static void Show(PlayerIndex playerIndex) {
            std::cout << "The Market Place should now be shown.";
        }

        static bool IsTrialMode() {
            return false; //todo
        };
    };
}


#endif //GUIDE_H
