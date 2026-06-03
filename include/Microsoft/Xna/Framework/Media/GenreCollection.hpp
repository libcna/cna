#pragma once

#include <string>
#include <vector>

#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Media
{
    class Genre;

    /// Collection of genres.
    /// @note Status: Stub — media library catalog access not implemented
    class GenreCollection final : public System::Object, public System::IDisposable
    {
    public:
        using iterator = std::vector<Genre*>::iterator;
        using const_iterator = std::vector<Genre*>::const_iterator;

        void Dispose() override;

        [[nodiscard]] SharpRuntime::intcs getCountProperty() const;
        [[nodiscard]] bool getIsDisposedProperty() const;
        [[nodiscard]] Genre* operator[](SharpRuntime::intcs index) const;

        [[nodiscard]] iterator begin();
        [[nodiscard]] iterator end();
        [[nodiscard]] const_iterator begin() const;
        [[nodiscard]] const_iterator end() const;

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<Genre*> innerList_;
    };
}
