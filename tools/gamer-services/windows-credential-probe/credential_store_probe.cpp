// GSH-07 probe: the Windows credential store (DPAPI-sealed files, one lease per title). Build from
// the repository root with MinGW (or compile the same three sources with MSVC on Windows):
//   x86_64-w64-mingw32-g++ -std=c++23 -static -Itools/gamer-services/windows-credential-probe/shim
//     -I<dir holding nlohmann/> -Imodules/gamer-services/include -Imodules/gamer-services/src/Internal/Protocol
//     tools/gamer-services/windows-credential-probe/credential_store_probe.cpp
//     modules/gamer-services/src/Internal/CredentialStore.cpp modules/gamer-services/src/Internal/ServiceProtocol.cpp -lcrypt32
// Run it in an empty directory. On 2026-09-30 it passed under Wine 10.0; Wine is not Windows, so this
// checks the code path, not the platform. The shim stands in for sharp-runtime's SHA-256.
#include "../../../modules/gamer-services/src/Internal/CredentialStore.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace CNA::Internal::GamerServices;
int failures=0;
void check(bool ok,const char* what){std::printf("%s %s\n",ok?"PASS":"FAIL",what);if(!ok)++failures;}
int main() {
    const auto root=std::filesystem::current_path()/"gsh07-store";
    std::filesystem::remove_all(root);
    _putenv_s("CNA_GAMER_SERVICES_CREDENTIALS_DIR",root.string().c_str());
    const CNA::GamerServices::Configuration config{"https://dpapi.test/cna/v1","one","",false,true,std::uint64_t{64}<<20,""};
    const StoredCredential value{std::string(64,'a'),2000000000};
    {
        CredentialStore store(config);
        check(std::string(store.protection())=="dpapi","protection is DPAPI");
        check(store.save(0,value),"save");
        const auto loaded=store.load(0);
        check(loaded&&loaded->refreshToken==value.refreshToken&&loaded->expires==value.expires,"load round trip");
        std::filesystem::path record;
        for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.path().extension()==".json")record=entry.path();
        std::ifstream in(record,std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(in)),{});in.close();
        check(!record.empty()&&bytes.find(value.refreshToken)==std::string::npos,"the file does not hold the token in clear");
        CredentialStore second(config);
        check(std::string(second.protection())=="none"&&!second.save(1,value),"a second process-lease holder keeps nothing");
        const CNA::GamerServices::Configuration other{"https://dpapi.test/cna/v1","two","",false,true,std::uint64_t{64}<<20,""};
        {
            CredentialStore another(other);
            check(!another.load(0),"another title has no record");
            // The same sealed bytes under the other title's name do not open: the binding is DPAPI entropy.
            check(another.save(0,value),"the other title saves its own");
            std::filesystem::path theirs;
            for(const auto& entry:std::filesystem::directory_iterator(root))
                if(entry.path().extension()==".json"&&entry.path()!=record)theirs=entry.path();
            std::filesystem::copy_file(record,theirs,std::filesystem::copy_options::overwrite_existing);
            check(!another.load(0),"a record copied under another title's name does not open");
        }
        std::ofstream(record,std::ios::binary|std::ios::trunc)<<"not a DPAPI blob";
        check(!store.load(0),"a corrupt record is refused");
        check(store.save(0,value)&&store.load(0),"a new save replaces it");
        store.remove(0);
        check(!store.load(0),"remove");
    }
    std::filesystem::remove_all(root);
    std::printf("%d failures\n",failures);
    return failures==0?0:1;
}
