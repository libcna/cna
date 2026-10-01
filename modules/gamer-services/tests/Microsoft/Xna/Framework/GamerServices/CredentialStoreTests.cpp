// SPDX-License-Identifier: MS-PL
#include "../../../../../src/Internal/CredentialStore.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
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
TEST_F(CredentialStoreTest, ANamedDirectoryIsPrivateFilesNeverTheKeyring) {
    CredentialStore store(config_);
    EXPECT_STREQ("private-file",store.protection());
    ASSERT_TRUE(store.save(0,fixture_));EXPECT_FALSE(record().empty());
}
#if defined(__linux__)
// GSH-07: in the default location, and with a desktop keyring answering, a record lives in the
// freedesktop Secret Service rather than a file; a file from before moves into it. The test runs
// itself again inside a private D-Bus session with a throwaway gnome-keyring, so the owner's
// keyring is never touched.
namespace {
// Runs one keyring case of this binary in a private D-Bus session: with a gnome-keyring unlocked
// before the test ("unlocked"), or one D-Bus starts on demand with no unlocked collection
// ("locked"), or on a bus that never answers ("silent"). Reports whether the case left its marker.
bool RunInPrivateKeyringSession(const std::string& test,const std::string& mode) {
    const auto root=std::filesystem::temp_directory_path()/("cna-keyring-unit-"+std::to_string(getpid()));
    std::filesystem::remove_all(root);
    const auto runtime=root/"run";
    for(const auto& sub:{root/"data",root/"home",root/"state",runtime})std::filesystem::create_directories(sub);
    std::filesystem::permissions(runtime,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
    const auto self=std::filesystem::read_symlink("/proc/self/exe").string();
    const auto start=mode=="unlocked"?"printf fixture | gnome-keyring-daemon --unlock --components=secrets >/dev/null 2>&1; ":"";
    const auto environment="env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS -u CNA_GAMER_SERVICES_CREDENTIALS_DIR "
        "-u CNA_GAMER_SERVICES_KEYRING HOME='"+(root/"home").string()+"' XDG_DATA_HOME='"+(root/"data").string()+
        "' XDG_RUNTIME_DIR='"+runtime.string()+"' XDG_STATE_HOME='"+(root/"state").string()+"' CNA_TEST_PRIVATE_KEYRING='"+
        (root/"passed").string()+"' ";
    std::string command;int silent=-1;
    if(mode=="silent") {
        // A bus that accepts the connection and never says anything.
        silent=socket(AF_UNIX,SOCK_STREAM,0);sockaddr_un address{};address.sun_family=AF_UNIX;
        const auto path=(root/"bus").string();std::snprintf(address.sun_path,sizeof(address.sun_path),"%s",path.c_str());
        if(bind(silent,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0||listen(silent,4)!=0)return false;
        command=environment+"DBUS_SESSION_BUS_ADDRESS='unix:path="+path+"' timeout 90 '"+self+"' --gtest_filter="+test+" >/dev/null 2>&1";
    } else {
        command=environment+"timeout 90 dbus-run-session -- sh -c '"+start+"exec \"$0\" --gtest_filter="+test+"' '"+self+"' >/dev/null 2>&1";
    }
    const int status=std::system(command.c_str());
    if(silent>=0)close(silent);
    const bool passed=status==0&&std::filesystem::exists(root/"passed");
    std::filesystem::remove_all(root);
    return passed;
}
bool KeyringToolsPresent() {
    return std::filesystem::exists("/usr/bin/dbus-run-session")&&std::filesystem::exists("/usr/bin/gnome-keyring-daemon");
}
void MarkPassed() {std::ofstream(std::getenv("CNA_TEST_PRIVATE_KEYRING"))<<"passed";}
}
// GSH-07: in the default location, and with a desktop keyring answering, a record lives in the
// freedesktop Secret Service rather than a file; a file from before moves into it. The test runs
// itself again inside a private D-Bus session with a throwaway gnome-keyring, so the owner's
// keyring is never touched.
TEST(CredentialStoreKeyringTest, KeepsRecordsInTheSecretServiceAndMovesOldFilesIntoIt) {
    if(!std::getenv("CNA_TEST_PRIVATE_KEYRING")) {
        if(!KeyringToolsPresent())GTEST_SKIP()<<"no dbus-run-session or gnome-keyring-daemon";
        EXPECT_TRUE(RunInPrivateKeyringSession("CredentialStoreKeyringTest.KeepsRecordsInTheSecretServiceAndMovesOldFilesIntoIt","unlocked"));
        return;
    }
    const CNA::GamerServices::Configuration config{"https://keyring.test/cna/v1","one","",false};
    const StoredCredential first{std::string(64,'b'),2000000000},second{std::string(64,'c'),2000000001};
    const std::filesystem::path directory=std::filesystem::path(std::getenv("XDG_STATE_HOME"))/"cna/gamer-services/credentials";
    auto files=[&]{int count=0;for(const auto& entry:std::filesystem::directory_iterator(directory))count+=entry.path().extension()==".json";return count;};
    {
        // Before the keyring: a private file.
        setenv("CNA_GAMER_SERVICES_KEYRING","0",1);
        CredentialStore store(config);
        EXPECT_STREQ("private-file",store.protection());
        ASSERT_TRUE(store.save(2,second));
        EXPECT_EQ(1,files());
        unsetenv("CNA_GAMER_SERVICES_KEYRING");
    }
    CredentialStore store(config);
    ASSERT_STREQ("secret-service",store.protection());
    const auto moved=store.load(2);
    ASSERT_TRUE(moved);EXPECT_EQ(second.refreshToken,moved->refreshToken);
    EXPECT_EQ(0,files())<<"the old file moved into the keyring";
    EXPECT_EQ(second.expires,store.load(2)->expires);
    ASSERT_TRUE(store.save(0,first));
    EXPECT_EQ(0,files());
    EXPECT_EQ(first.refreshToken,store.load(0)->refreshToken);
    const CNA::GamerServices::Configuration other{"https://keyring.test/cna/v1","two","",false};
    {CredentialStore another(other);EXPECT_FALSE(another.load(0))<<"records are per endpoint and title";}
    store.remove(0);store.remove(2);
    EXPECT_FALSE(store.load(0));EXPECT_FALSE(store.load(2));
    if(!testing::Test::HasFailure())MarkPassed();
}
// A keyring that refuses to store (locked, no collection to unlock without a prompt) leaves the
// record in a private file, where the next run finds it.
TEST(CredentialStoreKeyringTest, AKeyringThatRefusesWritesLeavesPrivateFiles) {
    if(!std::getenv("CNA_TEST_PRIVATE_KEYRING")) {
        if(!KeyringToolsPresent())GTEST_SKIP()<<"no dbus-run-session or gnome-keyring-daemon";
        EXPECT_TRUE(RunInPrivateKeyringSession("CredentialStoreKeyringTest.AKeyringThatRefusesWritesLeavesPrivateFiles","locked"));
        return;
    }
    const CNA::GamerServices::Configuration config{"https://keyring.test/cna/v1","one","",false};
    const StoredCredential value{std::string(64,'e'),2000000000};
    {CredentialStore store(config);EXPECT_STREQ("secret-service",store.protection());EXPECT_TRUE(store.save(1,value));}
    const std::filesystem::path directory=std::filesystem::path(std::getenv("XDG_STATE_HOME"))/"cna/gamer-services/credentials";
    int files=0;for(const auto& entry:std::filesystem::directory_iterator(directory))files+=entry.path().extension()==".json";
    EXPECT_EQ(1,files)<<"the refused record is in a private file";
    CredentialStore store(config);
    ASSERT_TRUE(store.load(1));EXPECT_EQ(value.refreshToken,store.load(1)->refreshToken);
    if(!testing::Test::HasFailure())MarkPassed();
}
// A keyring that never answers is given two seconds, then the store keeps private files: a broken
// desktop keyring never holds a game for the bus timeout.
TEST(CredentialStoreKeyringTest, AKeyringThatDoesNotAnswerIsGivenTwoSeconds) {
    if(!std::getenv("CNA_TEST_PRIVATE_KEYRING")) {
        EXPECT_TRUE(RunInPrivateKeyringSession("CredentialStoreKeyringTest.AKeyringThatDoesNotAnswerIsGivenTwoSeconds","silent"));
        return;
    }
    const CNA::GamerServices::Configuration config{"https://keyring.test/cna/v1","one","",false};
    const auto started=std::chrono::steady_clock::now();
    CredentialStore store(config);
    EXPECT_LT(std::chrono::steady_clock::now()-started,std::chrono::seconds(5));
    EXPECT_STREQ("private-file",store.protection());
    const StoredCredential value{std::string(64,'d'),2000000000};
    EXPECT_TRUE(store.save(1,value));
    EXPECT_EQ(value.refreshToken,store.load(1)->refreshToken);
    if(!testing::Test::HasFailure())MarkPassed();
    // The probe thread is still waiting on the silent bus; end without unwinding it.
    std::_Exit(testing::Test::HasFailure()?1:0);
}
#endif
TEST_F(CredentialStoreTest, ExplicitDisableCreatesNoUserFiles) {
    setenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR","0",1);CredentialStore store(config_);
    EXPECT_FALSE(store.save(0,fixture_));EXPECT_FALSE(store.load(0));EXPECT_FALSE(std::filesystem::exists(root_));
}
}
#endif
