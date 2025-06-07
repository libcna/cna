#include "gtest/gtest.h"
#include <string>
#include "System/EventHandler.h"

using System::EventHandler;

TEST(EventHandlerTest, SingleHandlerIsCalled) {
    EventHandler<int> handler;

    int result = 0;
    handler += [&result](const int& value) {
        result = value;
    };

    handler.RaiseCurrentValueChanged(42);
    EXPECT_EQ(result, 42);
}

TEST(EventHandlerTest, MultipleHandlersAreCalled) {
    EventHandler<std::string> handler;

    std::string result1;
    std::string result2;

    handler += [&result1](const std::string& value) {
        result1 = value;
    };
    handler += [&result2](const std::string& value) {
        result2 = value + "!";
    };

    handler.RaiseCurrentValueChanged("Test");

    EXPECT_EQ(result1, "Test");
    EXPECT_EQ(result2, "Test!");
}

TEST(EventHandlerTest, HandlersAreCalledInOrder) {
    EventHandler<int> handler;
    std::vector<int> callOrder;

    handler += [&callOrder](const int& value) {
        callOrder.push_back(value + 1);
    };
    handler += [&callOrder](const int& value) {
        callOrder.push_back(value + 2);
    };

    handler.RaiseCurrentValueChanged(10);

    ASSERT_EQ(callOrder.size(), 2);
    EXPECT_EQ(callOrder[0], 11);
    EXPECT_EQ(callOrder[1], 12);
}
