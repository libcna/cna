// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/FileDropEventArgsEXT.hpp"

#include <utility>

namespace Microsoft::Xna::Framework
{
    FileDropEventArgsEXT::FileDropEventArgsEXT(std::vector<SharpRuntime::String> files)
        : files_(std::move(files))
    {
    }

    const std::vector<SharpRuntime::String>& FileDropEventArgsEXT::getFilesProperty() const
    {
        return files_;
    }
}
