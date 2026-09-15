// SPDX-License-Identifier: MS-PL

#pragma once

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework
{
    /**
     * @brief Event arguments of `GameWindow::TextDropEXT`: text dropped on the window.
     *
     * @note CNAEXT — CNA extension, not XNA API. XNA had no drag and drop.
     */
    CNAEXT class TextDropEventArgsEXT : public System::EventArgs
    {
    public:
        /**
         * @brief Creates the arguments for one dropped text.
         *
         * @param text The text, UTF-8, whole.
         */
        explicit TextDropEventArgsEXT(SharpRuntime::String text);

        /**
         * @brief Gets the dropped text.
         *
         * @return The text, UTF-8, whole -- a link dragged out of a browser is its URL.
         */
        [[nodiscard]] const SharpRuntime::String& getTextProperty() const;

    private:
        SharpRuntime::String text_;
    };
}
