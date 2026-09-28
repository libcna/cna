// SPDX-License-Identifier: MS-PL
#include "../../../../../src/Internal/CredentialStore.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>

namespace {
using CNA::Internal::GamerServices::CredentialStore;
using CNA::Internal::GamerServices::StoredCredential;
class CredentialStoreTest : public testing::Test {
protected:
    void SetUp() override {
        if(const auto* value=std::getenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR"))previous_=value;
        root_=std::filesystem::temp_directory_path()/("cna-credentials-unit-"+std::to_string(getpid()));
        std::filesystem::remove_all(root_);setenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR",root_.c_str(),1);
    }
    void TearDown() override {
        if(previous_)setenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR",previous_->c_str(),1);
        else unsetenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR");
        std::filesystem::remove_all(root_);
    }
    std::filesystem::path record() const {
        for(const auto& entry:std::filesystem::directory_iterator(root_))if(entry.path().extension()==".json")return entry.path();
        return {};
    }
    CNA::GamerServices::Configuration config_{"https://credentials.test/cna/v1","one","",false};
    StoredCredential fixture_{std::string(64,'a'),2000000000};
    std::filesystem::path root_;
    std::optional<std::string> previous_;
};
TEST_F(CredentialStoreTest, AtomicPrivateRoundTripAndRemoval) {
    {
        CredentialStore store(config_);EXPECT_FALSE(store.load(0));ASSERT_TRUE(store.save(0,fixture_));
        const auto loaded=store.load(0);ASSERT_TRUE(loaded);EXPECT_TRUE(loaded->refreshToken==fixture_.refreshToken);EXPECT_EQ(loaded->expires,fixture_.expires);
        struct stat info{};ASSERT_EQ(stat(root_.c_str(),&info),0);EXPECT_EQ(info.st_mode&0777,0700u);
        ASSERT_EQ(stat(record().c_str(),&info),0);EXPECT_EQ(info.st_mode&0777,0600u);
        fixture_.refreshToken=std::string(64,'b');ASSERT_TRUE(store.save(0,fixture_));EXPECT_TRUE(store.load(0)->refreshToken==fixture_.refreshToken);
    }
    CredentialStore reopened(config_);ASSERT_TRUE(reopened.load(0));reopened.remove(0);EXPECT_FALSE(reopened.load(0));reopened.remove(0);
}
TEST_F(CredentialStoreTest, IsolatesEndpointTitleAndAllFourSlots) {
    CredentialStore store(config_);
    for(int i=0;i<4;++i){auto value=fixture_;value.expires+=i;ASSERT_TRUE(store.save(i,value));}
    for(int i=0;i<4;++i)EXPECT_EQ(store.load(i)->expires,fixture_.expires+i);
    auto other=config_;other.gameId="two";CredentialStore title(other);EXPECT_FALSE(title.load(0));
    other=config_;other.endpoint="https://other.test/cna/v1";CredentialStore endpoint(other);EXPECT_FALSE(endpoint.load(0));
    EXPECT_FALSE(store.load(-1));EXPECT_FALSE(store.load(4));EXPECT_FALSE(store.save(4,fixture_));
}
TEST_F(CredentialStoreTest, ConcurrentLeaseUsesEphemeralBehavior) {
    CredentialStore owner(config_);ASSERT_TRUE(owner.save(0,fixture_));
    CredentialStore concurrent(config_);EXPECT_FALSE(concurrent.load(0));EXPECT_FALSE(concurrent.save(0,fixture_));
    concurrent.remove(0);EXPECT_TRUE(owner.load(0));
}
TEST_F(CredentialStoreTest, RefusesPublicPermissionsAndLinkedFiles) {
    CredentialStore store(config_);ASSERT_TRUE(store.save(0,fixture_));const auto path=record();
    ASSERT_EQ(chmod(path.c_str(),0644),0);EXPECT_FALSE(store.load(0));ASSERT_EQ(chmod(path.c_str(),0600),0);
    const auto link=root_/"hardlink";std::filesystem::create_hard_link(path,link);EXPECT_FALSE(store.load(0));std::filesystem::remove(link);
    const auto target=root_/"target";std::filesystem::rename(path,target);std::filesystem::create_symlink(target,path);EXPECT_FALSE(store.load(0));
    // Atomic replacement replaces the link itself and leaves the target's contents untouched.
    auto changed=fixture_;changed.expires++;ASSERT_TRUE(store.save(0,changed));EXPECT_FALSE(std::filesystem::is_symlink(path));EXPECT_EQ(store.load(0)->expires,changed.expires);
}
TEST_F(CredentialStoreTest, RefusesInsecureOrSymlinkStorageDirectories) {
    std::filesystem::create_directory(root_);ASSERT_EQ(chmod(root_.c_str(),0755),0);
    {CredentialStore insecure(config_);EXPECT_FALSE(insecure.save(0,fixture_));}
    std::filesystem::remove(root_);const auto target=root_.string()+"-target";std::filesystem::create_directory(target);
    std::filesystem::create_directory_symlink(target,root_);{CredentialStore linked(config_);EXPECT_FALSE(linked.save(0,fixture_));}
    std::filesystem::remove(root_);std::filesystem::remove(target);
}
TEST_F(CredentialStoreTest, RefusesCorruptionAndMalformedAuthorityWithoutLoggingIt) {
    CredentialStore store(config_);ASSERT_TRUE(store.save(0,fixture_));const auto path=record();
    for(const auto& bytes:{std::string("{"),std::string(16385,'x'),std::string("{\"v\":2,\"refreshToken\":\"bad\",\"expires\":1}"),std::string("{\"v\":1,\"refreshToken\":\"bad\",\"expires\":-1}")}) {
        std::ofstream file(path,std::ios::trunc);file<<bytes;file.close();EXPECT_FALSE(store.load(0));
    }
    auto bad=fixture_;bad.refreshToken="malformed";EXPECT_FALSE(store.save(0,bad));
    bad=fixture_;bad.expires=-1;EXPECT_FALSE(store.save(0,bad));
}
TEST_F(CredentialStoreTest, ExplicitDisableCreatesNoUserFiles) {
    setenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR","0",1);CredentialStore store(config_);
    EXPECT_FALSE(store.save(0,fixture_));EXPECT_FALSE(store.load(0));EXPECT_FALSE(std::filesystem::exists(root_));
}
}
#endif
