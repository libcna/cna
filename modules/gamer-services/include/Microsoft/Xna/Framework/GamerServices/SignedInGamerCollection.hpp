// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerCollection.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    class SignedInGamer;

    /**
     * @brief A read-only collection of SignedInGamer objects, indexed by PlayerIndex.
     */
    class SignedInGamerCollection : public GamerCollection<SignedInGamer>
    {
    public:
        /**
         * @brief Brings GamerCollection<SignedInGamer>::operator[](int) into scope.
         *
         * CNAEXT: without this, declaring the PlayerIndex overload below would hide the
         * inherited int overload per C++ name-hiding rules (C# has no equivalent hiding for
         * overloads with new parameter types) — FNA code such as
         * `Gamer.SignedInGamers[i]` with an int index relies on that inherited overload.
         */
        CNAEXT using GamerCollection<SignedInGamer>::operator[];

        /**
         * @brief Gets the signed-in gamer for the specified player index.
         *
         * @param index The player index to look up.
         * @return Pointer to the SignedInGamer, or nullptr if no gamer is at that index.
         */
        [[nodiscard]] SignedInGamer* operator[](Microsoft::Xna::Framework::PlayerIndex index) const;

        /** @brief Creates a SignedInGamerCollection for CNA internal use. */
        CNAEXT static SignedInGamerCollection CreateInternal(std::vector<SignedInGamer*> gamers);

    private:
        explicit SignedInGamerCollection(std::vector<SignedInGamer*> gamers);
    };
}
