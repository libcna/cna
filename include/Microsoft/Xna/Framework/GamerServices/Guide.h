//
// Created by robertvokac on 5/28/25.
//

#ifndef GUIDE_H
#define GUIDE_H
#include <iostream>

#include "CNA/Prop.h"
#include "Microsoft/Xna/Framework/PlayerIndex.h"


namespace Microsoft::Xna::Framework::GamerServices {
    class Guide {
    public:
        static void Show(const PlayerIndex& playerIndex);

        dgetterstatic(bool, IsTrialMode)

    };
}


#endif //GUIDE_H
