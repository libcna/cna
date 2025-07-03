//
// Created by robertvokac on 5/24/25.
//

#include "Microsoft/Xna/Framework/Content/ContentManager.h"


namespace Microsoft::Xna::Framework::Content {
    std::string &ContentManager::getRootDirectoryProperty() { return RootDirectory_; }
    void ContentManager::setRootDirectoryProperty(const std::string &v) { RootDirectory_ = v; };

    ContentManager::ContentManager() {
    }
}
