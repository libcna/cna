// SPDX-License-Identifier: MS-PL
#include "../../../../../src/Internal/Protocol/CnaService/Protocol.hpp"
#include <gtest/gtest.h>

TEST(ServiceProtocolTest, AllEightNullableSignedPropertySlotsValidate) {
    CnaService::Json properties=CnaService::Json::array({nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr});
    EXPECT_EQ(8U,CnaService::SessionPropertyCount);
    EXPECT_EQ(31,CnaService::MaxSessionGamers);
    for(std::size_t index=0;index<CnaService::SessionPropertyCount;++index) {
        for(const auto& value:{CnaService::Json(-2147483648LL),CnaService::Json(2147483647LL),CnaService::Json(nullptr)}) {
            properties[index]=value;
            EXPECT_NO_THROW(CnaService::validateSessionProperties(properties));
        }
    }
}
TEST(ServiceProtocolTest, PropertySchemaRejectsWidthsOverflowBooleansAndFloats) {
    using CnaService::Json;
    for(const auto& invalid:{Json::array(),Json::array({nullptr}),Json::object(),
        Json::array({1,2,3,4,5,6,7,8,9}),Json::array({true,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr}),
        Json::array({1.5,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr}),
        Json::array({-2147483649LL,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr}),
        Json::array({2147483648LL,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr}),
        Json::array({18446744073709551615ULL,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr})}) {
        EXPECT_THROW(CnaService::validateSessionProperties(invalid),CnaService::Error);
    }
}
