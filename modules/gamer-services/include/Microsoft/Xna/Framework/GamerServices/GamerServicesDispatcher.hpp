// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IServiceProvider.hpp"
#include <cstddef>

namespace CNA::Internal::GamerServices { struct LocalGameDefaults; }

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Provides the entry points that drive GamerServices background processing.
     */
    class GamerServicesDispatcher
    {
    public:
        /** @brief Deleted; all members are static. */
        GamerServicesDispatcher() = delete;

        /**
         * @brief Gets whether GamerServices has been initialized.
         *
         * @return true if Initialize() has been called.
         */
        [[nodiscard]] static bool getIsInitializedProperty();

        /**
         * @brief Gets the native window handle used by GamerServices.
         *
         * @return The window handle.
         */
        [[nodiscard]] static SharpRuntime::IntPtr getWindowHandleProperty();

        /**
         * @brief Sets the native window handle used by GamerServices.
         *
         * @param value The window handle.
         */
        static void setWindowHandleProperty(SharpRuntime::IntPtr value);

        /** @brief Raised when a title update is being installed. Never raised in this platform's implementation. */
        static System::EventHandler<System::EventArgs> InstallingTitleUpdate;

        /**
         * @brief Initializes GamerServices with the given service provider.
         *
         * @param serviceProvider The game's service provider.
         */
        static void Initialize(System::IServiceProvider& serviceProvider);

        /** @brief Processes pending GamerServices work for the current frame. */
        static void Update();

        /**
         * @brief Processes one iteration of pending GamerServices asynchronous work.
         *
         * @return true if GamerServices is initialized; otherwise false.
         */
        static bool UpdateAsync();

        /**
         * @brief Gets the number of previously released dispatcher identities for tests.
         *
         * Retired identities are retained until shutdown, so this value remains zero.
         * @return Number of released identities.
         */
        CNAEXT [[nodiscard]] static std::size_t GetFreedGamerCountForTesting();

    private:
        // Copies a local profile's stored game settings into a signed-in gamer's GameDefaults.
        static void ApplyLocalGameDefaults(class GameDefaults& target, const CNA::Internal::GamerServices::LocalGameDefaults& source);

        static bool isInitialized_;
        static SharpRuntime::IntPtr windowHandle_;
        static std::size_t freedGamerCount_;
    };
}
