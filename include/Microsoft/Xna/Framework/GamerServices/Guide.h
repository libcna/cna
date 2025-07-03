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
        DEF_PROP(bool, IsTrialMode, getter1, setter0, member0, static1, constret1, ref1, constmet0)

    };
}


#endif //GUIDE_H
