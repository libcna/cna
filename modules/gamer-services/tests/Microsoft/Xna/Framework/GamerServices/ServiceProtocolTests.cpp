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

TEST(ServiceProtocolTest, FullPolicyResponsesRetainRequestSizeDepthAndDuplicateGuards) {
    using namespace CnaService;
    auto rows=Json::array();for(int i=0;i<1024;++i)rows.push_back("Blocked");
    const auto response=Json{{"blocked",rows}}.dump();
    EXPECT_THROW((void)parse(response),Error);
    EXPECT_EQ(1024u,parse(response,1024)["blocked"].size());
    rows.push_back("Overflow");EXPECT_THROW((void)parse(Json{{"blocked",rows}}.dump(),1024),Error);
    EXPECT_THROW((void)parse("{\"blocked\":[],\"blocked\":[]}",1024),Error);
    EXPECT_THROW((void)parse(std::string(MaxMessageBytes+1,' '),1024),Error);
    EXPECT_THROW((void)parse(std::string(18,'[')+"0"+std::string(18,']'),1024),Error);
    auto fields=Json::object();for(int i=0;i<257;++i)fields[std::to_string(i)]=0;
    EXPECT_THROW((void)parse(fields.dump(),1024),Error);
}
