//
// Created by robertvokac on 5/24/25.
//

#ifndef CONTENTMANAGER_H
#define CONTENTMANAGER_H
#include <iostream>
#include "NeoSdk/Property.h"

namespace Microsoft::Xna::Framework::Content {
    class ContentManager {
        ContentManager();
    public:
        template <typename T>
     T Load(const std::string& assetName);

        DEF_PROP_AUTO(std::string, RootDirectory, "Content");
    };


}


#endif //CONTENTMANAGER_H
