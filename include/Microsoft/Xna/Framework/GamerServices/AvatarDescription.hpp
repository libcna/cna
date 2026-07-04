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
     * The real XNA implementation's random-generation methods (CreateRandom(),
     * CreateRandom(AvatarBodyType), EndGetFromGamer()) never actually produce randomized or
     * populated data — every one of them returns an all-zero, 1021-byte description whose
     * IsValid property is false. That surprising but verified behavior is preserved here
     * exactly, not "fixed."
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
         * @brief Gets the height of the avatar, in meters.
         *
         * Lazily initializes to 0 on first access if never explicitly set, matching the real
         * XNA implementation.
         *
         * @return The avatar's height.
         */
        [[nodiscard]] SharpRuntime::Single getHeightProperty() const;

        /**
         * @brief Gets the body type of the avatar.
         *
         * Lazily initializes to AvatarBodyType::Female on first access if never explicitly set,
         * matching the real XNA implementation.
         *
         * @return The avatar's body type.
         */
        [[nodiscard]] AvatarBodyType getBodyTypeProperty() const;

        /**
         * @brief Creates an AvatarDescription with an all-zero, invalid description.
         *
         * Despite the name, the real XNA implementation does not actually randomize anything —
         * it always returns an all-zero (invalid) description. Preserved exactly.
         *
         * @return A new, invalid AvatarDescription.
         */
        [[nodiscard]] static AvatarDescription CreateRandom();

        /**
         * @brief Creates an AvatarDescription with an all-zero, invalid description.
         *
         * Despite the name, the real XNA implementation does not actually randomize anything,
         * and the @p bodyType argument is not read at all — it always returns an all-zero
         * (invalid) description. Preserved exactly.
         *
         * @param bodyType The requested body type (not read).
         * @return A new, invalid AvatarDescription.
         * @throws System::ArgumentOutOfRangeException if bodyType is not a valid AvatarBodyType value.
         */
        [[nodiscard]] static AvatarDescription CreateRandom(AvatarBodyType bodyType);

        /**
         * @brief Begins retrieving the avatar description for the specified gamer.
         *
         * Invokes @p callback synchronously before returning, matching the real XNA
         * implementation (a fully synchronous fake-async operation).
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
         * Always returns an all-zero, invalid AvatarDescription, matching the real XNA
         * implementation.
         *
         * @param result The IAsyncResult returned by BeginGetFromGamer.
         * @return An all-zero, invalid AvatarDescription.
         * @throws System::ArgumentException if result was not returned by BeginGetFromGamer.
         */
        [[nodiscard]] static AvatarDescription EndGetFromGamer(System::IAsyncResult* result);

        /** @brief Raised when the avatar description changes. */
        static System::EventHandler<System::EventArgs> Changed;

    private:
        AvatarDescription(std::vector<SharpRuntime::bytecs> data, bool makeCopy);

        /** @brief The exact byte length every AvatarDescription's raw data must have. */
        static constexpr int DescriptionSize = 1021;

        std::vector<SharpRuntime::bytecs> description_;
        mutable std::optional<SharpRuntime::Single> height_;
        mutable std::optional<AvatarBodyType> bodyType_;
    };
}
