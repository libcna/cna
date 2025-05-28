//
// Created by robertvokac on 5/28/25.
//

#ifndef GUIDE_H
#define GUIDE_H


namespace Microsoft::Xna::Framework::GamerServices {
    class Guide {
    public:
        static void Show(PlayerIndex playerIndex) {
            Debug.Write("The Market Place should now be shown.");
        }

        static bool IsTrialMode() {
            return false; //todo
        };
    };
}


#endif //GUIDE_H
