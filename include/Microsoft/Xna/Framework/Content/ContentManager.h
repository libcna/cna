//
// Created by robertvokac on 5/24/25.
//

#ifndef CONTENTMANAGER_H
#define CONTENTMANAGER_H
#include <iostream>

#include "CNA/Prop.h"

namespace Microsoft::Xna::Framework::Content {
    class ContentManager {
        ddata(std::string, RootDirectory);

    public:
        ContentManager();

        template<typename T>
        T Load(const std::string &assetName);
    };
}


#endif //CONTENTMANAGER_H
