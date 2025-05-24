//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Content/ContentManager.h"

namespace Microsoft::Xna::Framework::Content {
    template<typename T>
    T ContentManager::Load(const std::string &assetName) {
        std::cout << "Loading asset: " << assetName << std::endl;
        return T(assetName);
    }
}