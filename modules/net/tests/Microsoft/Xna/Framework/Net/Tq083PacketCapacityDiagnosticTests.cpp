// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <typeinfo>
#include "System/AppContext.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"

using namespace Microsoft::Xna::Framework::Net;

TEST(Tq083PacketCapacity, NegativeCapacityUsesTheFrameworkResourceWithoutAnActualValue) {
    // Refresh PacketReader/Writer .ctor(int) IL_0002 delegates to MemoryStream(int).
    // .NET 4 MemoryStream .ctor(int) IL_000a..001e uses resource
    // ArgumentOutOfRange_NegativeCapacity = "Capacity must be positive." (not ThrowIfNegative).
    // Test the existing C++ opt-in Framework diagnostic format. Its line ending is LF;
    // Windows CLR uses Environment.NewLine (CRLF). Default-host suffix parity is downstream.
    struct FrameworkMessages {
        bool old = false;
        FrameworkMessages() {
            System::AppContext::TryGetSwitch("SharpRuntime.UseNetFrameworkArgumentExceptionMessages", old);
            System::AppContext::SetSwitch("SharpRuntime.UseNetFrameworkArgumentExceptionMessages", true);
        }
        ~FrameworkMessages() {
            System::AppContext::SetSwitch("SharpRuntime.UseNetFrameworkArgumentExceptionMessages", old);
        }
    } frameworkMessages;
    const auto expectDiagnostic = [](auto construct) {
        try {
            construct();
            ADD_FAILURE() << "negative capacity was accepted";
        } catch (const System::ArgumentOutOfRangeException& error) {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(error.getParamNameProperty(), "capacity");
            EXPECT_EQ(error.getMessageProperty(), "Capacity must be positive.\nParameter name: capacity");
            EXPECT_TRUE(error.getActualValueProperty().empty());
        }
    };
    for (const int capacity : {-1, -17}) {
        SCOPED_TRACE(capacity);
        expectDiagnostic([capacity] { PacketReader reader(capacity); });
        expectDiagnostic([capacity] { PacketWriter writer(capacity); });
    }
}
