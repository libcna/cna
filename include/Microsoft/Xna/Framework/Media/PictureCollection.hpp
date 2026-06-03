#pragma once

#include <string>
#include <vector>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class Picture;

    /// Collection of pictures.
    /// @note Status: Stub — media library catalog access not implemented
    class PictureCollection final : public System::Object, public System::IDisposable
    {
    public:
        using iterator = std::vector<Picture*>::iterator;
        using const_iterator = std::vector<Picture*>::const_iterator;

        void Dispose() override;

        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;
        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] Picture* operator[](SharpRuntime::intcs index) const;

        [[nodiscard]] iterator begin();
        [[nodiscard]] iterator end();
        [[nodiscard]] const_iterator begin() const;
        [[nodiscard]] const_iterator end() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<Picture*> innerList_;
    };
}
