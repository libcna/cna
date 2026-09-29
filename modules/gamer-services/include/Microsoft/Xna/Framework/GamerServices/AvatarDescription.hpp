// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/GamerServices/AvatarBodyType.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/AsyncCallback.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IAsyncResult.hpp"
#include <optional>
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    class Gamer;

    /**
     * @brief Describes the physical characteristics of an avatar.
     *
     * The 1021-byte buffer is opaque to games. CNA stores its own versioned avatar encoding in
     * it (body type, height, build, colors and wardrobe catalog items, protected by a checksum);
     * it does not read or produce Xbox LIVE avatar data. A buffer from another source keeps the
     * reference IsValid rule but reads as height 0 / Female and renders as unavailable.
     */
    class AvatarDescription
    {
    public:
        /**
         * @brief Initializes a new instance of AvatarDescription from raw description data.
         *
         * @param data The raw description bytes; must be exactly 1021 bytes.
         * @throws System::ArgumentException if data is not exactly 1021 bytes long.
         */
        explicit AvatarDescription(const std::vector<SharpRuntime::bytecs>& data);

        /**
         * @brief Gets a value indicating whether this description holds valid avatar data.
         *
         * @return true if the description is exactly 1021 bytes and its first byte is nonzero.
         */
        [[nodiscard]] bool getIsValidProperty() const;

        /**
         * @brief Gets a copy of the raw description data.
         *
         * @return A 1021-byte copy of the description.
         */
        [[nodiscard]] std::vector<SharpRuntime::bytecs> getDescriptionProperty() const;

        /**
         * @brief Gets the height of the avatar, from the feet to the top of the head.
         *
         * @return Height in meters; 0 when the buffer is not a CNA avatar description.
         */
        [[nodiscard]] SharpRuntime::Single getHeightProperty() const;

        /**
         * @brief Gets the body type of the avatar based on the description data.
         *
         * @return The avatar's body type; Female when the buffer is not a CNA avatar description.
         */
        [[nodiscard]] AvatarBodyType getBodyTypeProperty() const;

        /**
         * @brief Creates an avatar of random body type, features and clothing.
         *
         * @return A new, valid AvatarDescription drawn from the built-in avatar catalog.
         */
        [[nodiscard]] static AvatarDescription CreateRandom();

        /**
         * @brief Creates an avatar of the specified body type with random features and clothing.
         *
         * @param bodyType Body type of the randomly-created avatar.
         * @return A new, valid AvatarDescription of that body type.
         * @throws System::ArgumentOutOfRangeException if bodyType is not a valid AvatarBodyType value.
         */
        [[nodiscard]] static AvatarDescription CreateRandom(AvatarBodyType bodyType);

        /**
         * @brief Begins retrieving the avatar description for the specified gamer.
         *
         * A gamer with a CNA service identity (signed in, or met in an online session) has its
         * description read from the service; the operation completes during
         * GamerServicesDispatcher.Update. Any other gamer completes at once, synchronously.
         *
         * @param gamer The gamer to retrieve the avatar description for.
         * @param callback The method to call when the operation completes.
         * @param state A user-defined object to pass to the callback.
         * @return An IAsyncResult representing the asynchronous operation.
         * @throws System::ArgumentNullException if gamer is nullptr.
         * @throws System::ObjectDisposedException if gamer has been disposed.
         */
        [[nodiscard]] static System::IAsyncResult* BeginGetFromGamer(
            Gamer* gamer,
            System::AsyncCallback callback,
            std::any state
        );

        /**
         * @brief Ends a pending request to retrieve an avatar description started with
         * BeginGetFromGamer.
         *
         * @param result The IAsyncResult returned by BeginGetFromGamer.
         * @return The gamer's avatar description; an invalid (all-zero) one when the gamer has no
         * avatar, has no service identity, or the service cannot be reached.
         * @throws System::ArgumentException if result was not returned by BeginGetFromGamer.
         */
        [[nodiscard]] static AvatarDescription EndGetFromGamer(System::IAsyncResult* result);

        /**
         * @brief Raised when this gamer's avatar changes.
         *
         * A per-instance event, as Microsoft declares it (`public event EventHandler<EventArgs>
         * Changed;`): XNA keeps one cached AvatarDescription per signed-in player and raises the
         * event on the description of the player whose avatar changed, so subscribers of one
         * description never hear about another. Nothing in this runtime raises it -- the live
         * avatar service it reported was an Xbox 360 one -- but the ownership and delivery shape
         * is the documented one, and a copy of a description carries the subscribers it had when
         * it was copied.
         */
        System::EventHandler<System::EventArgs> Changed;

    private:
        AvatarDescription(std::vector<SharpRuntime::bytecs> data, bool makeCopy);

        static AvatarDescription CreateRandom(std::optional<int> bodyType);

        /** @brief The exact byte length every AvatarDescription's raw data must have. */
        static constexpr int DescriptionSize = 1021;

        std::vector<SharpRuntime::bytecs> description_;
        mutable std::optional<SharpRuntime::Single> height_;
        mutable std::optional<AvatarBodyType> bodyType_;
    };
}
