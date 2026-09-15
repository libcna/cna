// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0169: file dialogs and URL opening through the desktop portal.
//
// No test reaches the session bus of the desktop it runs on: a file chooser opened there would open
// on that desktop. This binary gives itself no session bus as it loads (below), ctest and the X11
// test launcher say the same, and every test that wants a portal starts a private dbus-daemon --
// with no service it could start, so no real portal can appear on it -- and plays the portal on it
// itself.

#include <gtest/gtest.h>

#include "../../../src/X11/X11DesktopPortal.hpp"
#include "../../../src/X11/X11Headers.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "System/Environment.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(CNA_X11_HAVE_DBUS)
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::X11;

// Before any test runs, and whatever started the binary: no session bus.
const bool kNoDesktopSessionBus = [] {
    System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS",
                                                std::string("unix:path=/nonexistent/cna-test-no-session-bus"));
    return true;
}();

TEST(X11PortalRequest, NoTestOfThisBinaryReachesTheDesktopsSessionBus)
{
    // Whatever environment the binary was started in -- a developer's shell has their desktop's
    // bus in it -- a test sees none.
    const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    ASSERT_NE(address, nullptr);
    EXPECT_STREQ(address, "unix:path=/nonexistent/cna-test-no-session-bus");
#if defined(CNA_X11_HAVE_DBUS)
    EXPECT_EQ(X11DesktopPortal::Connect(), nullptr);
#endif
}

// --- what goes on the bus, worked out without one ---------------------------------------------

TEST(X11PortalRequest, AnExtensionBecomesACaseInsensitiveGlob)
{
    EXPECT_EQ(PortalGlob("png"), "*.[pP][nN][gG]");
    EXPECT_EQ(PortalGlob("tar.gz"), "*.[tT][aA][rR].[gG][zZ]");
    EXPECT_EQ(PortalGlob("mp3"), "*.[mM][pP]3");
    EXPECT_EQ(PortalGlob("*"), "*") << "everything stays everything";
}

TEST(X11PortalRequest, FiltersSplitTheirPatternsAndDropTheEmpty)
{
    const std::vector<X11PortalFilter> filters =
        ToPortalFilters({{"Images", "png;jpg"}, {"Everything", "*"}, {"Nothing", ";; "}, {"Spaced", " txt ; *.md"}});
    ASSERT_EQ(filters.size(), 3u) << "a filter with no pattern is not offered";
    EXPECT_EQ(filters[0].name, "Images");
    ASSERT_EQ(filters[0].patterns.size(), 2u);
    EXPECT_EQ(filters[0].patterns[0].kind, 0u);
    EXPECT_EQ(filters[0].patterns[0].pattern, "*.[pP][nN][gG]");
    EXPECT_EQ(filters[0].patterns[1].pattern, "*.[jJ][pP][gG]");
    EXPECT_EQ(filters[1].patterns[0].pattern, "*");
    ASSERT_EQ(filters[2].patterns.size(), 2u);
    EXPECT_EQ(filters[2].patterns[0].pattern, "*.[tT][xX][tT]");
    EXPECT_EQ(filters[2].patterns[1].pattern, "*.[mM][dD]") << "a pattern written as *.md is the same extension";
}

TEST(X11PortalRequest, AFileUriNamesALocalPathPercentDecoded)
{
    EXPECT_EQ(PathFromFileUri("file:///home/user/a%20b.png"), "/home/user/a b.png");
    EXPECT_EQ(PathFromFileUri("file://localhost/tmp/x"), "/tmp/x");
    EXPECT_EQ(PathFromFileUri("FILE:///tmp/%C5%BE"), "/tmp/\xC5\xBE");
    EXPECT_EQ(PathFromFileUri("file:///tmp/x?query#fragment"), "/tmp/x");
    EXPECT_EQ(PathFromFileUri("file://elsewhere/tmp/x"), std::nullopt) << "another machine's file";
    EXPECT_EQ(PathFromFileUri("https://example.com/"), std::nullopt);
    EXPECT_EQ(PathFromFileUri("file:///tmp/%00"), std::nullopt) << "an encoded NUL names no path";
    EXPECT_EQ(PathFromFileUri("file:///tmp/%zz"), std::nullopt);
    EXPECT_EQ(PathFromFileUri("file:///tmp/%2"), std::nullopt);
}

TEST(X11PortalRequest, TheRequestPathIsBuiltFromTheSenderAndTheToken)
{
    EXPECT_EQ(PortalRequestPath(":1.42", "cna7"), "/org/freedesktop/portal/desktop/request/1_42/cna7");
    EXPECT_EQ(PortalParentWindow(0x1a00003), "x11:1a00003");
    EXPECT_EQ(PortalParentWindow(0), "") << "no parent";
}

TEST(X11PortalRequest, ASaveLocationIsAFolderAFileOrASuggestedName)
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("cna-portal-save-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "existing.txt") << "x";

    X11PortalSaveLocation folder = SplitSaveLocation(directory.string());
    EXPECT_EQ(folder.currentFolder, directory.string());
    EXPECT_TRUE(folder.currentName.empty());

    X11PortalSaveLocation existing = SplitSaveLocation((directory / "existing.txt").string());
    EXPECT_EQ(existing.currentFile, (directory / "existing.txt").string());
    EXPECT_EQ(existing.currentName, "existing.txt");

    X11PortalSaveLocation suggested = SplitSaveLocation((directory / "new.sav").string());
    EXPECT_EQ(suggested.currentFolder, directory.string());
    EXPECT_EQ(suggested.currentName, "new.sav");
    EXPECT_TRUE(suggested.currentFile.empty());

    X11PortalSaveLocation bare = SplitSaveLocation("slot1.sav");
    EXPECT_TRUE(bare.currentFolder.empty());
    EXPECT_EQ(bare.currentName, "slot1.sav");

    EXPECT_TRUE(SplitSaveLocation("").currentName.empty());
    std::filesystem::remove_all(directory);
}

#if defined(CNA_X11_HAVE_DBUS)

// --- a private bus and a portal played on it --------------------------------------------------

/// A dbus-daemon of the test's own: a session bus with no service it could start.
class PrivateBus
{
public:
    PrivateBus()
    {
        directory_ = std::filesystem::temp_directory_path() /
                     ("cna-bus-" + std::to_string(::getpid()) + "-" + std::to_string(++counter_));
        std::filesystem::create_directories(directory_);
        const std::filesystem::path config = directory_ / "bus.conf";
        std::ofstream(config) << "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
                                 " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                                 "<busconfig><type>session</type>"
                                 "<listen>unix:dir=" << directory_.string() << "</listen>"
                                 "<auth>EXTERNAL</auth>"
                                 "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
                                 "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n";
        int pipeEnds[2] = {-1, -1};
        if (::pipe2(pipeEnds, O_CLOEXEC) != 0)
        {
            return;
        }
        // The daemon writes its address to this descriptor; the child gets a copy without CLOEXEC.
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], 3);
        const std::string configArgument = "--config-file=" + config.string();
        const char* const arguments[] = {"dbus-daemon", configArgument.c_str(), "--nofork", "--print-address=3",
                                         nullptr};
        const int spawned = posix_spawnp(&pid_, "dbus-daemon", &actions, nullptr, const_cast<char* const*>(arguments),
                                         environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(pipeEnds[1]);
        if (spawned != 0)
        {
            pid_ = -1;
            ::close(pipeEnds[0]);
            return;
        }
        std::string line;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (line.find('\n') == std::string::npos && std::chrono::steady_clock::now() < deadline)
        {
            pollfd waiting{pipeEnds[0], POLLIN, 0};
            if (::poll(&waiting, 1, 100) <= 0) { continue; }
            char buffer[256];
            const ssize_t got = ::read(pipeEnds[0], buffer, sizeof(buffer));
            if (got <= 0) { break; }
            line.append(buffer, static_cast<std::size_t>(got));
        }
        ::close(pipeEnds[0]);
        if (const std::size_t end = line.find('\n'); end != std::string::npos)
        {
            address_ = line.substr(0, end);
        }
    }

    ~PrivateBus()
    {
        Stop();
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    PrivateBus(const PrivateBus&) = delete;
    PrivateBus& operator=(const PrivateBus&) = delete;

    [[nodiscard]] const std::string& Address() const { return address_; }

    void Stop()
    {
        if (pid_ > 0)
        {
            ::kill(pid_, SIGTERM);
            int status = 0;
            ::waitpid(pid_, &status, 0);
            pid_ = -1;
        }
    }

private:
    static inline int counter_ = 0;
    std::filesystem::path directory_;
    pid_t pid_ = -1;
    std::string address_;
};

/// What the test's portal was asked.
struct PortalCall
{
    std::string interface;
    std::string method;
    std::string sender;
    std::string parent;
    std::string title;
    std::string uri;
    std::string fileTarget;  ///< Where an OpenFile descriptor pointed.
    std::map<std::string, std::string> strings;
    std::map<std::string, bool> flags;
    std::map<std::string, std::string> paths;  ///< The `ay` options, NUL dropped.
    std::vector<X11PortalFilter> filters;
};

/// How the test's portal answers the next file chooser.
struct PortalAnswer
{
    std::uint32_t response = 0;
    std::vector<std::string> uris;
    bool error = false;          ///< Reply with an error instead of a request.
    bool ownPath = false;        ///< Choose its own request path, as an older portal does.
    bool silent = false;         ///< Never send the Response.
};

/// org.freedesktop.portal.Desktop, played on a private bus from a thread of its own.
class FakePortal
{
public:
    explicit FakePortal(const std::string& address)
    {
        const X11DBusApi& dbus = DBusApi();
        DBusError error;
        dbus.error_init(&error);
        connection_ = dbus.connection_open_private(address.c_str(), &error);
        if (connection_ == nullptr)
        {
            dbus.error_free(&error);
            return;
        }
        dbus.connection_set_exit_on_disconnect(connection_, FALSE);
        if (!dbus.bus_register(connection_, &error) ||
            dbus.bus_request_name(connection_, "org.freedesktop.portal.Desktop", DBUS_NAME_FLAG_DO_NOT_QUEUE, &error) !=
                DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
        {
            dbus.error_free(&error);
            return;
        }
        ready_ = true;
        running_ = true;
        thread_ = std::thread([this] { Serve(); });
    }

    ~FakePortal()
    {
        running_ = false;
        if (thread_.joinable()) { thread_.join(); }
        if (connection_ != nullptr)
        {
            DBusApi().connection_close(connection_);
            DBusApi().connection_unref(connection_);
        }
    }

    [[nodiscard]] bool Ready() const { return ready_; }

    void Answer(PortalAnswer answer)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        answer_ = std::move(answer);
    }

    [[nodiscard]] std::vector<PortalCall> Calls()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return calls_;
    }

    /// Sends the Response a silent answer held back, on the path the last request had.
    void RespondLater(const std::uint32_t response, const std::vector<std::string>& uris)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        late_.push_back({lastPath_, response, uris});
    }

private:
    static std::string Text(const X11DBusApi& dbus, DBusMessageIter* iter)
    {
        const char* text = nullptr;
        if (dbus.message_iter_get_arg_type(iter) == DBUS_TYPE_STRING ||
            dbus.message_iter_get_arg_type(iter) == DBUS_TYPE_OBJECT_PATH)
        {
            dbus.message_iter_get_basic(iter, &text);
        }
        return text != nullptr ? text : "";
    }

    static void ReadOptions(const X11DBusApi& dbus, DBusMessageIter* iter, PortalCall& call)
    {
        DBusMessageIter dictionary;
        dbus.message_iter_recurse(iter, &dictionary);
        while (dbus.message_iter_get_arg_type(&dictionary) == DBUS_TYPE_DICT_ENTRY)
        {
            DBusMessageIter entry;
            dbus.message_iter_recurse(&dictionary, &entry);
            const std::string key = Text(dbus, &entry);
            dbus.message_iter_next(&entry);
            DBusMessageIter value;
            dbus.message_iter_recurse(&entry, &value);
            const int type = dbus.message_iter_get_arg_type(&value);
            if (type == DBUS_TYPE_STRING)
            {
                call.strings[key] = Text(dbus, &value);
            }
            else if (type == DBUS_TYPE_BOOLEAN)
            {
                dbus_bool_t flag = FALSE;
                dbus.message_iter_get_basic(&value, &flag);
                call.flags[key] = flag != FALSE;
            }
            else if (type == DBUS_TYPE_ARRAY && dbus.message_iter_get_element_type(&value) == DBUS_TYPE_BYTE)
            {
                DBusMessageIter bytes;
                dbus.message_iter_recurse(&value, &bytes);
                const char* data = nullptr;
                int length = 0;
                dbus.message_iter_get_fixed_array(&bytes, &data, &length);
                std::string text(data != nullptr ? data : "", static_cast<std::size_t>(length));
                if (!text.empty() && text.back() == '\0') { text.pop_back(); }
                call.paths[key] = text;
            }
            else if (type == DBUS_TYPE_ARRAY && key == "filters")
            {
                DBusMessageIter filters;
                dbus.message_iter_recurse(&value, &filters);
                while (dbus.message_iter_get_arg_type(&filters) == DBUS_TYPE_STRUCT)
                {
                    DBusMessageIter filter;
                    dbus.message_iter_recurse(&filters, &filter);
                    X11PortalFilter read;
                    read.name = Text(dbus, &filter);
                    dbus.message_iter_next(&filter);
                    DBusMessageIter patterns;
                    dbus.message_iter_recurse(&filter, &patterns);
                    while (dbus.message_iter_get_arg_type(&patterns) == DBUS_TYPE_STRUCT)
                    {
                        DBusMessageIter pair;
                        dbus.message_iter_recurse(&patterns, &pair);
                        X11PortalPattern pattern;
                        dbus_uint32_t kind = 0;
                        dbus.message_iter_get_basic(&pair, &kind);
                        pattern.kind = kind;
                        dbus.message_iter_next(&pair);
                        pattern.pattern = Text(dbus, &pair);
                        read.patterns.push_back(pattern);
                        dbus.message_iter_next(&patterns);
                    }
                    call.filters.push_back(read);
                    dbus.message_iter_next(&filters);
                }
            }
            dbus.message_iter_next(&dictionary);
        }
    }

    void EmitResponse(const std::string& path, const std::uint32_t response, const std::vector<std::string>& uris)
    {
        const X11DBusApi& dbus = DBusApi();
        DBusMessage* signal = dbus.message_new_signal(path.c_str(), "org.freedesktop.portal.Request", "Response");
        DBusMessageIter arguments;
        dbus.message_iter_init_append(signal, &arguments);
        const dbus_uint32_t code = response;
        dbus.message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &code);
        DBusMessageIter results;
        dbus.message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &results);
        if (!uris.empty())
        {
            DBusMessageIter entry;
            DBusMessageIter variant;
            DBusMessageIter list;
            const char* key = "uris";
            dbus.message_iter_open_container(&results, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus.message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            dbus.message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &variant);
            dbus.message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &list);
            for (const std::string& uri : uris)
            {
                const char* text = uri.c_str();
                dbus.message_iter_append_basic(&list, DBUS_TYPE_STRING, &text);
            }
            dbus.message_iter_close_container(&variant, &list);
            dbus.message_iter_close_container(&entry, &variant);
            dbus.message_iter_close_container(&results, &entry);
        }
        dbus.message_iter_close_container(&arguments, &results);
        dbus.connection_send(connection_, signal, nullptr);
        dbus.connection_flush(connection_);
        dbus.message_unref(signal);
    }

    void Serve()
    {
        const X11DBusApi& dbus = DBusApi();
        while (running_)
        {
            dbus.connection_read_write(connection_, 10);
            while (DBusMessage* message = dbus.connection_pop_message(connection_))
            {
                Handle(message);
                dbus.message_unref(message);
            }
            std::vector<Late> late;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                late.swap(late_);
            }
            for (const Late& response : late)
            {
                EmitResponse(response.path, response.response, response.uris);
            }
        }
    }

    void Handle(DBusMessage* message)
    {
        const X11DBusApi& dbus = DBusApi();
        const bool openFile = dbus.message_is_method_call(message, "org.freedesktop.portal.FileChooser", "OpenFile");
        const bool saveFile = dbus.message_is_method_call(message, "org.freedesktop.portal.FileChooser", "SaveFile");
        const bool openUri = dbus.message_is_method_call(message, "org.freedesktop.portal.OpenURI", "OpenURI");
        const bool openUriFile = dbus.message_is_method_call(message, "org.freedesktop.portal.OpenURI", "OpenFile");
        if (!openFile && !saveFile && !openUri && !openUriFile)
        {
            return;
        }
        PortalCall call;
        call.interface = openUri || openUriFile ? "org.freedesktop.portal.OpenURI" : "org.freedesktop.portal.FileChooser";
        call.method = openFile || openUriFile ? "OpenFile" : saveFile ? "SaveFile" : "OpenURI";
        const char* sender = dbus.message_get_sender(message);
        call.sender = sender != nullptr ? sender : "";
        DBusMessageIter arguments;
        dbus.message_iter_init(message, &arguments);
        call.parent = Text(dbus, &arguments);
        dbus.message_iter_next(&arguments);
        if (openUriFile)
        {
            int descriptor = -1;
            dbus.message_iter_get_basic(&arguments, &descriptor);
            if (descriptor >= 0)
            {
                std::error_code error;
                call.fileTarget =
                    std::filesystem::read_symlink("/proc/self/fd/" + std::to_string(descriptor), error).string();
                ::close(descriptor);
            }
        }
        else if (openUri)
        {
            call.uri = Text(dbus, &arguments);
        }
        else
        {
            call.title = Text(dbus, &arguments);
        }
        dbus.message_iter_next(&arguments);
        ReadOptions(dbus, &arguments, call);

        PortalAnswer answer;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            answer = answer_;
        }
        if (answer.error && !openUri && !openUriFile)
        {
            DBusMessage* failure =
                dbus.message_new_error(message, "org.freedesktop.portal.Error.Failed", "no chooser for you");
            dbus.connection_send(connection_, failure, nullptr);
            dbus.message_unref(failure);
        }
        else
        {
            std::string path = PortalRequestPath(call.sender, call.strings["handle_token"]);
            if (answer.ownPath)
            {
                path = "/org/freedesktop/portal/desktop/request/elsewhere/r" + std::to_string(calls_.size());
            }
            DBusMessage* reply = dbus.message_new_method_return(message);
            DBusMessageIter replyArguments;
            dbus.message_iter_init_append(reply, &replyArguments);
            const char* handle = path.c_str();
            dbus.message_iter_append_basic(&replyArguments, DBUS_TYPE_OBJECT_PATH, &handle);
            dbus.connection_send(connection_, reply, nullptr);
            dbus.connection_flush(connection_);
            dbus.message_unref(reply);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lastPath_ = path;
            }
            if (!answer.silent && !openUri && !openUriFile)
            {
                EmitResponse(path, answer.response, answer.uris);
            }
        }
        dbus.connection_flush(connection_);
        std::lock_guard<std::mutex> lock(mutex_);
        calls_.push_back(std::move(call));
    }

    struct Late
    {
        std::string path;
        std::uint32_t response = 0;
        std::vector<std::string> uris;
    };

    DBusConnection* connection_ = nullptr;
    bool ready_ = false;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::vector<Late> late_;
    PortalAnswer answer_;
    std::vector<PortalCall> calls_;
    std::string lastPath_;
};

/// A private bus, the portal on it, and the session bus variable pointing there while it lasts.
class X11DesktopPortalBus : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!DBusApi().loaded)
        {
            GTEST_SKIP() << "libdbus is not installed";
        }
        bus_ = std::make_unique<PrivateBus>();
        if (bus_->Address().empty())
        {
            GTEST_SKIP() << "dbus-daemon could not be started (is it installed?)";
        }
        saved_ = System::Environment::GetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS");
        System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS", bus_->Address());
    }

    void TearDown() override
    {
        client_.reset();
        portal_.reset();
        bus_.reset();
        System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS", saved_);
    }

    void StartPortal()
    {
        portal_ = std::make_unique<FakePortal>(bus_->Address());
        ASSERT_TRUE(portal_->Ready());
        client_ = X11DesktopPortal::Connect();
        ASSERT_NE(client_, nullptr) << "the portal on the bus was not found";
    }

    /// Pumps until a callback has run; returns what it received.
    std::optional<std::vector<std::string>> PumpForAnswer(const std::shared_ptr<std::optional<std::vector<std::string>>>& got,
                                                          const std::chrono::milliseconds budget = std::chrono::milliseconds(5000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (!got->has_value() && std::chrono::steady_clock::now() < deadline)
        {
            client_->Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return *got;
    }

    static FileDialogCallback Capture(const std::shared_ptr<std::optional<std::vector<std::string>>>& got,
                                      const std::shared_ptr<int>& count)
    {
        return [got, count](const std::vector<std::string>& paths) {
            *got = paths;
            ++*count;
        };
    }

    std::unique_ptr<PrivateBus> bus_;
    std::unique_ptr<FakePortal> portal_;
    std::unique_ptr<X11DesktopPortal> client_;
    std::optional<std::string> saved_;
};

TEST_F(X11DesktopPortalBus, WithoutAPortalOnTheBusThereIsNone)
{
    EXPECT_EQ(X11DesktopPortal::Connect(), nullptr) << "a bus with nothing on it";
    System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS",
                                                std::string("unix:path=/nonexistent/cna-no-bus"));
    EXPECT_EQ(X11DesktopPortal::Connect(), nullptr) << "no bus at all";
}

TEST_F(X11DesktopPortalBus, AnOpenDialogCarriesItsOptionsAndItsAnswerArrivesFromPump)
{
    StartPortal();
    portal_->Answer({0, {"file:///tmp/a%20b.png", "file:///tmp/c.jpg", "https://example.com/not-a-file"}});
    auto got = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    client_->ShowFileDialog(X11DesktopPortal::FileRequest::Open, Capture(got, count),
                            {{"Images", "png;jpg"}}, "/tmp", true, 0x1a00003);
    EXPECT_FALSE(got->has_value()) << "never inside the call";
    const std::optional<std::vector<std::string>> paths = PumpForAnswer(got);
    ASSERT_TRUE(paths.has_value()) << "no answer came";
    EXPECT_EQ(*paths, (std::vector<std::string>{"/tmp/a b.png", "/tmp/c.jpg"})) << "only local files are paths";
    for (int pass = 0; pass < 20; ++pass) { client_->Pump(); }
    EXPECT_EQ(*count, 1) << "exactly once";

    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_EQ(calls.size(), 1u);
    const PortalCall& call = calls[0];
    EXPECT_EQ(call.method, "OpenFile");
    EXPECT_EQ(call.parent, "x11:1a00003");
    EXPECT_EQ(call.title, "Open File");
    EXPECT_EQ(call.sender, client_->GetUniqueName());
    EXPECT_EQ(call.strings.at("handle_token").rfind("cna", 0), 0u);
    EXPECT_TRUE(call.flags.at("modal"));
    EXPECT_TRUE(call.flags.at("multiple"));
    EXPECT_EQ(call.flags.count("directory"), 0u);
    EXPECT_EQ(call.paths.at("current_folder"), "/tmp");
    ASSERT_EQ(call.filters.size(), 1u);
    EXPECT_EQ(call.filters[0].name, "Images");
    ASSERT_EQ(call.filters[0].patterns.size(), 2u);
    EXPECT_EQ(call.filters[0].patterns[0].pattern, "*.[pP][nN][gG]");
}

TEST_F(X11DesktopPortalBus, ACancelledOrFailedDialogAnswersNothingOnce)
{
    StartPortal();
    portal_->Answer({1, {"file:///tmp/ignored"}});
    auto cancelled = std::make_shared<std::optional<std::vector<std::string>>>();
    auto cancelledCount = std::make_shared<int>(0);
    client_->ShowFileDialog(X11DesktopPortal::FileRequest::Open, Capture(cancelled, cancelledCount), {}, "", false, 0);
    ASSERT_TRUE(PumpForAnswer(cancelled).has_value());
    EXPECT_TRUE((*cancelled)->empty()) << "cancelled: no paths, whatever came with it";

    PortalAnswer failure;
    failure.error = true;
    portal_->Answer(failure);
    auto failed = std::make_shared<std::optional<std::vector<std::string>>>();
    auto failedCount = std::make_shared<int>(0);
    client_->ShowFileDialog(X11DesktopPortal::FileRequest::Open, Capture(failed, failedCount), {}, "", false, 0);
    ASSERT_TRUE(PumpForAnswer(failed).has_value()) << "an error is an answer too";
    EXPECT_TRUE((*failed)->empty());
    for (int pass = 0; pass < 20; ++pass) { client_->Pump(); }
    EXPECT_EQ(*cancelledCount, 1);
    EXPECT_EQ(*failedCount, 1);
    EXPECT_FALSE(portal_->Calls()[0].flags.at("modal")) << "no parent, not modal";
    EXPECT_EQ(portal_->Calls()[0].flags.count("multiple"), 0u);
}

TEST_F(X11DesktopPortalBus, SaveAndFolderDialogsSayWhatTheyAre)
{
    StartPortal();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("cna-portal-dialogs-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);
    portal_->Answer({0, {"file://" + (directory / "new.sav").string()}});
    auto saved = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    client_->ShowFileDialog(X11DesktopPortal::FileRequest::Save, Capture(saved, count), {{"Saves", "sav"}},
                            (directory / "new.sav").string(), true, 0);
    ASSERT_TRUE(PumpForAnswer(saved).has_value());
    EXPECT_EQ(**saved, std::vector<std::string>{(directory / "new.sav").string()});

    portal_->Answer({0, {"file:///tmp", "file:///var"}});
    auto folders = std::make_shared<std::optional<std::vector<std::string>>>();
    client_->ShowFileDialog(X11DesktopPortal::FileRequest::OpenFolder, Capture(folders, count), {{"Ignored", "x"}},
                            directory.string(), true, 0);
    ASSERT_TRUE(PumpForAnswer(folders).has_value());
    EXPECT_EQ(**folders, (std::vector<std::string>{"/tmp", "/var"}));

    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[0].method, "SaveFile");
    EXPECT_EQ(calls[0].title, "Save File");
    EXPECT_EQ(calls[0].paths.at("current_folder"), directory.string());
    EXPECT_EQ(calls[0].strings.at("current_name"), "new.sav");
    EXPECT_EQ(calls[0].flags.count("multiple"), 0u) << "a save dialog saves one file";
    ASSERT_EQ(calls[0].filters.size(), 1u);
    EXPECT_EQ(calls[1].method, "OpenFile");
    EXPECT_EQ(calls[1].title, "Open Folder");
    EXPECT_TRUE(calls[1].flags.at("directory"));
    EXPECT_TRUE(calls[1].flags.at("multiple"));
    EXPECT_TRUE(calls[1].filters.empty()) << "a folder has no file type";
    EXPECT_EQ(calls[1].paths.at("current_folder"), directory.string());
    std::filesystem::remove_all(directory);
}

TEST_F(X11DesktopPortalBus, AnOlderPortalsOwnRequestPathIsFollowed)
{
    StartPortal();
    PortalAnswer answer;
    answer.uris = {"file:///tmp/older"};
    answer.ownPath = true;
    portal_->Answer(answer);
    auto got = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    client_->ShowFileDialog(X11DesktopPortal::FileRequest::Open, Capture(got, count), {}, "", false, 0);
    ASSERT_TRUE(PumpForAnswer(got).has_value()) << "the answer on the portal's own path was missed";
    EXPECT_EQ(**got, std::vector<std::string>{"/tmp/older"});
}

TEST_F(X11DesktopPortalBus, AnAnswerLongAfterTheQuestionStillArrives)
{
    StartPortal();
    PortalAnswer answer;
    answer.silent = true;
    portal_->Answer(answer);
    auto got = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    client_->ShowFileDialog(X11DesktopPortal::FileRequest::Open, Capture(got, count), {}, "", false, 0);
    // The user takes their time: frames go by.
    EXPECT_FALSE(PumpForAnswer(got, std::chrono::milliseconds(300)).has_value());
    portal_->RespondLater(0, {"file:///tmp/eventually"});
    ASSERT_TRUE(PumpForAnswer(got).has_value());
    EXPECT_EQ(**got, std::vector<std::string>{"/tmp/eventually"});
}

TEST_F(X11DesktopPortalBus, ACallbackMayAskForAnotherDialog)
{
    StartPortal();
    portal_->Answer({0, {"file:///tmp/first"}});
    auto second = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    auto first = std::make_shared<std::optional<std::vector<std::string>>>();
    X11DesktopPortal* client = client_.get();
    client_->ShowFileDialog(X11DesktopPortal::FileRequest::Open,
                            [first, second, count, client](const std::vector<std::string>& paths) {
                                *first = paths;
                                client->ShowFileDialog(X11DesktopPortal::FileRequest::Open, Capture(second, count), {},
                                                       "", false, 0);
                            },
                            {}, "", false, 0);
    ASSERT_TRUE(PumpForAnswer(first).has_value());
    ASSERT_TRUE(PumpForAnswer(second).has_value()) << "the second dialog, asked for from the first's callback";
    EXPECT_EQ(portal_->Calls().size(), 2u);
}

TEST_F(X11DesktopPortalBus, ABusThatGoesAwayAnswersEveryOpenDialog)
{
    StartPortal();
    PortalAnswer answer;
    answer.silent = true;
    portal_->Answer(answer);
    auto got = std::make_shared<std::optional<std::vector<std::string>>>();
    auto count = std::make_shared<int>(0);
    client_->ShowFileDialog(X11DesktopPortal::FileRequest::Open, Capture(got, count), {}, "", false, 0);
    EXPECT_FALSE(PumpForAnswer(got, std::chrono::milliseconds(200)).has_value());
    portal_.reset();
    bus_->Stop();
    ASSERT_TRUE(PumpForAnswer(got).has_value()) << "a dialog whose answer can never come is answered empty";
    EXPECT_TRUE((*got)->empty());
    EXPECT_EQ(*count, 1);
}

TEST_F(X11DesktopPortalBus, OpenUriHandsOverAUrlOrAnOpenFile)
{
    StartPortal();
    EXPECT_TRUE(client_->OpenUri("https://example.com/page?x=1", 0x2c00001));
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / ("cna-portal-open-" + std::to_string(::getpid()) + ".txt");
    std::ofstream(file) << "hello";
    EXPECT_TRUE(client_->OpenUri("file://" + file.string(), 0));
    EXPECT_FALSE(client_->OpenUri("file:///nonexistent/cna/file", 0)) << "a file that is not there";
    EXPECT_FALSE(client_->OpenUri("no scheme at all", 0));
    EXPECT_FALSE(client_->OpenUri(":empty-scheme", 0));

    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_EQ(calls.size(), 2u) << "only the two that were URLs reached the portal";
    EXPECT_EQ(calls[0].interface, "org.freedesktop.portal.OpenURI");
    EXPECT_EQ(calls[0].method, "OpenURI");
    EXPECT_EQ(calls[0].uri, "https://example.com/page?x=1");
    EXPECT_EQ(calls[0].parent, "x11:2c00001");
    EXPECT_EQ(calls[1].method, "OpenFile");
    EXPECT_EQ(calls[1].fileTarget, file.string()) << "the descriptor the portal received is the file's";
    std::filesystem::remove(file);
}

// --- through the platform, on the launcher's server --------------------------------------------

class X11DesktopPortalLive : public X11DesktopPortalBus
{
protected:
    void SetUp() override
    {
        const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
        if (privateServer == nullptr || std::string(privateServer) != "1")
        {
            GTEST_SKIP() << "creates windows; needs tools/platform/x11_test_server.sh";
        }
        X11DesktopPortalBus::SetUp();
    }

    void TearDown() override
    {
        window_.reset();
        if (platform_ != nullptr)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
        X11DesktopPortalBus::TearDown();
    }

    void OpenPlatform()
    {
        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
};

TEST_F(X11DesktopPortalLive, WithoutAPortalFileDialogsRefuseNamingTheirCapability)
{
    OpenPlatform();
    EXPECT_FALSE(platform_->GetCapabilities().nativeFileDialog);
    EXPECT_TRUE(platform_->GetCapabilities().messageBox);
    try
    {
        platform_->GetDialogs()->ShowOpenFileDialog([](const std::vector<std::string>&) {}, {}, "", false, nullptr);
        FAIL() << "no portal, yet a file dialog was shown";
    }
    catch (const PlatformNotSupportedException& refusal)
    {
        EXPECT_EQ(refusal.GetCapability(), PlatformCapability::NativeFileDialog);
    }
    EXPECT_FALSE(platform_->GetSystemInfo()->OpenUrl("https://example.com/"));
}

TEST_F(X11DesktopPortalLive, ThePlatformsDialogsAndOpenUrlGoThroughThePortal)
{
    portal_ = std::make_unique<FakePortal>(bus_->Address());
    ASSERT_TRUE(portal_->Ready());
    OpenPlatform();
    ASSERT_TRUE(platform_->GetCapabilities().nativeFileDialog);
    WindowDescription description;
    description.title = "CNA portal parent";
    description.width = 320;
    description.height = 200;
    window_ = platform_->CreateWindow(description);

    portal_->Answer({0, {"file:///tmp/chosen.png"}});
    std::optional<std::vector<std::string>> got;
    platform_->GetDialogs()->ShowOpenFileDialog(
        [&got](const std::vector<std::string>& paths) { got = paths; }, {{"Images", "png"}}, "", false, window_.get());
    EXPECT_FALSE(got.has_value()) << "never inside the call";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::vector<PlatformEvent> events;
    while (!got.has_value() && std::chrono::steady_clock::now() < deadline)
    {
        platform_->PollEvents(events);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(got.has_value()) << "PollEvents never delivered the answer";
    EXPECT_EQ(*got, std::vector<std::string>{"/tmp/chosen.png"});

    EXPECT_TRUE(platform_->GetSystemInfo()->OpenUrl("https://example.com/"));
    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_EQ(calls.size(), 2u);
    char expectedParent[32] = {};
    std::snprintf(expectedParent, sizeof(expectedParent), "x11:%lx",
                  static_cast<unsigned long>(window_->GetNativeHandle().windowId));
    EXPECT_EQ(calls[0].parent, expectedParent) << "the dialog belongs to the game's window";
    EXPECT_TRUE(calls[0].flags.at("modal"));
    EXPECT_EQ(calls[1].uri, "https://example.com/");
}

#endif // CNA_X11_HAVE_DBUS

} // namespace
