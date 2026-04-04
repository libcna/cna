//
// Created by robertvokac on 5/24/25.
//

#pragma once
#include <iostream>

#include "CNA/Prop.hpp"

namespace Microsoft::Xna::Framework::Content {
    class ContentManager {
    private:
        std::string RootDirectory_ = "Content";

        DEF_PROP(std::string, RootDirectory, getter1, setter1, member0, static0, constret0, ref1, constmet0)

    public:
        ContentManager();

        template<typename T>
        T Load(const std::string& assetName) {
            std::cout << "Loading asset: " << assetName << std::endl;
            return T(getRootDirectoryProperty() + "/" + assetName);
        }
    };
}

