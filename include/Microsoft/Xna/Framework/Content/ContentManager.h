//
// Created by robertvokac on 5/24/25.
//

#ifndef CONTENTMANAGER_H
#define CONTENTMANAGER_H
#include <iostream>

#include "CNA/Prop.h"

namespace Microsoft::Xna::Framework::Content {
    class ContentManager {
    private:
        std::string RootDirectory_ = "Content";

    public:
        [[nodiscard]] std::string getRootDirectoryProperty() const;

    public:
        void setRootDirectoryProperty(const std::string &v);;

    public:
        ContentManager();

        template<typename T>
        T Load(const std::string& assetName) {
            std::cout << "Loading asset: " << assetName << std::endl;
            return T(getRootDirectoryProperty() + "/" + assetName);
        }
    };
}


#endif //CONTENTMANAGER_H
