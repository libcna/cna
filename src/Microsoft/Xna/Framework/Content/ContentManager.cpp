//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Content/ContentManager.h"


namespace Microsoft::Xna::Framework::Content {
    std::string ContentManager::RootDirectoryProperty() { return RootDirectoryProperty_; }
    void ContentManager::RootDirectoryProperty(const std::string &v) { RootDirectoryProperty_ = v; };

    template<typename T>
    T ContentManager::Load(const std::string &assetName) {
        std::cout << "Loading asset: " << assetName << std::endl;
        return T(assetName);
    }

    ContentManager::ContentManager() {
    }
}
