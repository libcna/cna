#pragma once

#include <SDL3/SDL.h>

namespace CNA::Internal::Input {
    /**
     * @brief Bridge mezi SDL3 eventy a interním vstupním stavem CNA.
     *
     * Tento bridge zná SDL typy, ale vystavuje je pouze interně.
     *
     * @note Status: PARTIAL (zatím implementována pouze myš)
     */
    class SdlInputBridge {
    public:
        /**
         * @brief Zpracuje jeden SDL event a promítne relevantní změny do InputManageru.
         */
        static void ProcessEvent(const SDL_Event& event);
    };
}
