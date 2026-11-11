// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/TestSupport/TestScratch.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <typeinfo>
#include <type_traits>
#include <vector>
#include "CNA/Internal/PathUtf8.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "System/ArgumentException.hpp"
#include "System/Environment.hpp"
#include "System/ObjectDisposedException.hpp"

// Desktop storage honors XDG_DATA_HOME. No test fixture writes to the real save root.
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
namespace Tq042
{
namespace fs = std::filesystem;
using Microsoft::Xna::Framework::Storage::StorageDevice;
using Microsoft::Xna::Framework::Storage::StorageContainer;
class StorageSecondPassTest : public ::testing::Test
{
protected:
    fs::path scratch, app, root;
    std::optional<std::string> previous;
    bool owned = false;
    std::unique_ptr<StorageDevice> device;
    std::unique_ptr<StorageContainer> container;
    void SetEnvironment(const char* value)
    {
        System::Environment::SetEnvironmentVariable(
            "XDG_DATA_HOME", value ? std::optional<std::string>(value) : std::nullopt);
    }
    void SetUp() override
    {
        if (const char* value = std::getenv("XDG_DATA_HOME")) previous = value;
        std::random_device entropy;
        scratch = CNA::TestSupport::FixtureRoot() / ("CnaTq042-" + std::to_string(entropy()) + "-" + std::to_string(entropy()));
        ASSERT_TRUE(fs::create_directory(scratch));
        owned = true;
        ASSERT_NO_FATAL_FAILURE(SetEnvironment(CNA::Internal::PathToUtf8(scratch).c_str()));
        StorageDevice::SetAppNameEXT("app");
        app = scratch / "app";
        root = app / "saves" / "AllPlayers";
        auto selected = StorageDevice::BeginShowSelector(nullptr, nullptr);
        device = StorageDevice::EndShowSelector(selected.get());
        auto opened = device->BeginOpenContainer("saves", nullptr, nullptr);
        container = device->EndOpenContainer(opened.get());
        ASSERT_TRUE(fs::is_directory(root));
        { std::ofstream seed(root / "seed.bin", std::ios::binary); seed << "seed"; ASSERT_TRUE(seed.good()); }
        ASSERT_TRUE(fs::create_directory(root / "child"));
    }
    void TearDown() override
    {
        container.reset();
        device.reset();
        SetEnvironment(previous ? previous->c_str() : nullptr);
        StorageDevice::SetAppNameEXT("");
        if (owned) { std::error_code error; fs::remove_all(scratch, error); }
    }
    template<class Error, class Action>
    static void Exact(Action action, const char* parameter = nullptr)
    {
        try { action(); ADD_FAILURE() << "expected " << typeid(Error).name(); }
        catch (const Error& error)
        {
            EXPECT_EQ(typeid(error), typeid(Error));
            if constexpr (std::is_base_of_v<System::ArgumentException, Error>)
                if (parameter) EXPECT_EQ(error.getParamNameProperty(), parameter);
            if constexpr (std::is_same_v<System::ObjectDisposedException, Error>)
                EXPECT_EQ(error.getObjectNameProperty(), "StorageContainer");
        }
        catch (const std::exception& error) { ADD_FAILURE() << typeid(error).name() << ": " << error.what(); }
    }
    void SeedUnchanged()
    {
        std::ifstream seed(root / "seed.bin", std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(seed), {}), "seed");
    }
};
}
#endif
