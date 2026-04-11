//
// Created by robertvokac on 5/24/25.
//

#pragma once
#include <iostream>
#include <SDL3/SDL_log.h>

#include "CNA/Logger.hpp"
#include "CNA/Prop.hpp"

namespace Microsoft::Xna::Framework::Content {
    using log = CNA::Logger;
    class ContentManager {
    private:
        std::string RootDirectory_ = "Content";

        DEF_PROP(std::string, RootDirectory, getter1, setter1, member0, static0, constret0, ref1, constmet0)

    public:
        ContentManager();

        template<typename T>
        T Load(const std::string& assetName) {
            log::Debug(std::string("Loading asset: ") + assetName);
            return T(getRootDirectoryProperty() + "/" + assetName);
        }
    };
}

