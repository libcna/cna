//
// Created by robertvokac on 5/24/25.
//

#ifndef SPRITESORTMODE_H
#define SPRITESORTMODE_H


namespace Microsoft::Xna::Framework::Graphics {
    enum SpriteSortMode {
        Deferred,

        Immediate,
        Texture,
        BackToFront,
        FrontToBack,
    };
}


#endif //SPRITESORTMODE_H
