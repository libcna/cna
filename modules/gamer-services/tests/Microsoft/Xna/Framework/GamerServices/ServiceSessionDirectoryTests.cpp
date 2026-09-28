// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "../../../../../src/Internal/ServiceSessionDirectoryClient.hpp"
#include "../../../../../src/Internal/ServiceSessionDirectoryFake.hpp"
#include <gtest/gtest.h>
#include <set>

namespace {
using namespace CNA::Internal::GamerServices;
using CnaService::Json;
const std::string sessionId(32,'1'),machineId(32,'2'),inviteId(32,'3');
Json advertisement() {
    return Json{{"session",sessionId},{"kind","player"},{"state","lobby"},{"maxGamers",4},{"privateSlots",1},
        {"properties",Json::array({7,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,-2147483648LL})},
        {"allowJoinInProgress",false},{"revision",1},{"hostId","alice-id"},{"hostGamertag","Alice"},
        {"hostMachine",machineId},{"currentGamers",1},{"openPublicSlots",2},{"openPrivateSlots",1}};
}
Json roster() {
    auto value=advertisement();value["machine"]=machineId;
    value["members"]=Json::array({Json{{"userId","alice-id"},{"gamertag","Alice"},{"machine",machineId},{"privateSlot",false},{"ordinal",0}}});return value;
}
Json invitation() {
    return Json{{"invite",inviteId},{"session",sessionId},{"senderId","alice-id"},{"senderGamertag","Alice"},
        {"kind","player"},{"status","pending"},{"created",1000},{"expires",1900},{"acceptedAt",0}};
}
template<class Work> void expectCode(Work work,const char* expected) {
    try{work();FAIL()<<"Expected "<<expected;}catch(const ServiceOperationError& error){EXPECT_EQ(expected,error.code);}
}
struct Fixture {
    long long time=1000;
    std::set<std::string> accounts{"a","b","c","d"},authorized=accounts;
    std::unique_ptr<IServiceSessionDirectory> directory=makeFakeSessionDirectory(
        [this](const auto& id){if(!authorized.contains(id))throw ServiceOperationError("UNAUTHENTICATED");},
        [this](const auto& id){if(!accounts.contains(id))throw ServiceOperationError("NOT_FOUND");return "Tag"+id;},
        [this](const auto& tag){for(const auto& id:accounts)if(tag=="Tag"+id)return id;throw ServiceOperationError("NOT_FOUND");},
        [this]{return time;});
};
}
TEST(ServiceSessionDirectoryTest, TypedClientSerializesLogicalValuesWithoutSupplyingCredentials) {
    std::string op,actor;Json args;std::vector<std::string> users;
    auto client=makeSessionDirectoryClient([&](const auto& operation,Json arguments,const auto& owner,const auto& participants) {
        op=operation;args=std::move(arguments);actor=owner;users=participants;return roster();
    });
    ServiceSessionSettings settings;settings.maxGamers=4;settings.privateSlots=1;settings.properties[0]=7;settings.properties[7]=-2147483647-1;
    const auto value=client->create("alice-id",{"alice-id"},ServiceSessionKind::PlayerMatch,settings);
    EXPECT_EQ("sessions.create",op);EXPECT_EQ("alice-id",actor);EXPECT_EQ(std::vector<std::string>{"alice-id"},users);
    EXPECT_FALSE(args.contains("token"));EXPECT_FALSE(args.contains("participants"));EXPECT_FALSE(args.contains("state"));
    EXPECT_EQ(8U,args["properties"].size());EXPECT_EQ(-2147483648LL,args["properties"][7]);EXPECT_EQ(sessionId,value.session);
    EXPECT_EQ(1U,value.members.size());EXPECT_EQ(machineId,value.machine);EXPECT_EQ("alice-id",value.members[0].userId);
}
TEST(ServiceSessionDirectoryTest, RejectsMalformedAndInconsistentRosterBeforeReturningAnyMember) {
    std::vector<Json> cases;
    auto edit=[&](auto modify){auto value=roster();modify(value);cases.push_back(std::move(value));};
    edit([](auto& j){j["maxGamers"]=32;});edit([](auto& j){j["maxGamers"]=true;});edit([](auto& j){j["revision"]=18446744073709551615ULL;});
    edit([](auto& j){j["openPublicSlots"]=0;});edit([](auto& j){j["session"]="path/../../file";});
    edit([](auto& j){j["machine"]=std::string(32,'f');});edit([](auto& j){j["members"]=Json::array();});
    edit([](auto& j){j["members"][0]["ordinal"]=31;});edit([](auto& j){j["members"][0]["privateSlot"]=true;});
    edit([](auto& j){j["members"][0]["userId"]="foreign";});edit([](auto& j){j["members"][0]["gamertag"]="wrong";});
    edit([](auto& j){j["hostMachine"]=std::string(32,'f');});edit([](auto& j){j["properties"].push_back(nullptr);});
    edit([](auto& j){j["properties"][0]=2147483648LL;});edit([](auto& j){j["members"][0]["password"]="must never propagate";});
    edit([](auto& j){j["unknown"]=0;});edit([](auto& j){j["state"]="ended";});
    edit([](auto& j){j["currentGamers"]=2;j["openPublicSlots"]=1;j["members"].push_back(j["members"][0]);});
    edit([](auto& j){j["currentGamers"]=2;j["openPublicSlots"]=1;auto member=j["members"][0];member["userId"]="b";member["gamertag"]="Bob";j["members"].push_back(member);});
    for(const auto& value:cases) {
        auto client=makeSessionDirectoryClient([&](const auto&,auto,const auto&,const auto&){return value;});
        expectCode([&]{(void)client->get("alice-id",sessionId);},"INVALID_RESPONSE");
    }
}
TEST(ServiceSessionDirectoryTest, SearchValidatesCorrelationFiltersPagingAndDuplicateIds) {
    Json reply{{"sessions",Json::array({advertisement()})},{"start",0},{"more",false}};
    auto client=makeSessionDirectoryClient([&](const auto& op,const auto& args,const auto& actor,const auto& users) {
        EXPECT_EQ("sessions.find",op);EXPECT_EQ("bob-id",actor);EXPECT_TRUE(users.empty());EXPECT_EQ(2,args["localCount"]);return reply;
    });
    ServiceSessionProperties filters{};filters[0]=7;EXPECT_EQ(1U,client->find("bob-id",ServiceSessionKind::PlayerMatch,2,filters,0,2).sessions.size());
    const auto good=reply;
    reply["start"]=1;expectCode([&]{(void)client->find("bob-id",ServiceSessionKind::PlayerMatch,2,filters,0,2);},"INVALID_RESPONSE");reply=good;
    reply["sessions"].push_back(advertisement());expectCode([&]{(void)client->find("bob-id",ServiceSessionKind::PlayerMatch,2,filters,0,2);},"INVALID_RESPONSE");reply=good;
    reply["more"]=true;expectCode([&]{(void)client->find("bob-id",ServiceSessionKind::PlayerMatch,2,filters,0,2);},"INVALID_RESPONSE");reply=good;
    reply["sessions"][0]["properties"][0]=8;expectCode([&]{(void)client->find("bob-id",ServiceSessionKind::PlayerMatch,2,filters,0,2);},"INVALID_RESPONSE");reply=good;
    reply["sessions"][0]["kind"]="ranked";expectCode([&]{(void)client->find("bob-id",ServiceSessionKind::PlayerMatch,2,filters,0,2);},"INVALID_RESPONSE");
    expectCode([&]{(void)client->find("bob-id",ServiceSessionKind::PlayerMatch,0,filters,0,2);},"INVALID_ARGUMENT");
    expectCode([&]{(void)client->find("bob-id",ServiceSessionKind::PlayerMatch,2,filters,0,33);},"INVALID_ARGUMENT");
}
TEST(ServiceSessionDirectoryTest, InvitedJoinUsesExplicitIdentifierAndChecksReturnedLocalGroup) {
    auto result=roster();std::string operation;Json args;std::vector<std::string> locals;
    auto client=makeSessionDirectoryClient([&](const auto& op,Json fields,const auto&,const auto& users){operation=op;args=std::move(fields);locals=users;return result;});
    EXPECT_EQ(sessionId,client->join("alice-id",{"alice-id"},sessionId,inviteId).session);
    EXPECT_EQ("sessions.joinInvited",operation);EXPECT_EQ(inviteId,args["invite"]);EXPECT_EQ(std::vector<std::string>{"alice-id"},locals);
    (void)client->join("alice-id",{"alice-id"},sessionId);EXPECT_EQ("sessions.join",operation);EXPECT_FALSE(args.contains("invite"));
    expectCode([&]{(void)client->join("alice-id",{"alice-id","b"},sessionId,inviteId);},"INVALID_RESPONSE");
    expectCode([&]{(void)client->join("alice-id",{"b"},sessionId,inviteId);},"INVALID_ARGUMENT");
    expectCode([&]{(void)client->join("alice-id",{"alice-id","alice-id"},sessionId,inviteId);},"INVALID_ARGUMENT");
}
TEST(ServiceSessionDirectoryTest, InvitationsValidateAuthorityStateTimestampsAndBoundedInbox) {
    Json data=invitation();std::string op;
    auto client=makeSessionDirectoryClient([&](const auto& operation,auto,const auto&,const auto&){op=operation;return data;});
    EXPECT_EQ(inviteId,client->sendInvite("alice-id",sessionId,"Bob").invite);EXPECT_EQ("invites.send",op);
    EXPECT_EQ(ServiceInvitationState::Pending,client->getInvite("bob-id",inviteId).state);
    data["status"]="accepted";data["acceptedAt"]=1050;EXPECT_EQ(1050,client->acceptInvite("bob-id",inviteId).acceptedAt);
    data["status"]="dismissed";EXPECT_EQ(ServiceInvitationState::Dismissed,client->dismissInvite("bob-id",inviteId).state);
    expectCode([&]{(void)client->acceptInvite("bob-id",inviteId);},"INVALID_RESPONSE");
    for(const auto& field:{"invite","session","senderId","senderGamertag","kind","status","created","expires","acceptedAt"}) {
        data=invitation();data.erase(field);expectCode([&]{(void)client->getInvite("bob-id",inviteId);},"INVALID_RESPONSE");
    }
    data=invitation();data["expires"]=1901;expectCode([&]{(void)client->getInvite("bob-id",inviteId);},"INVALID_RESPONSE");
    data=invitation();data["acceptedAt"]=1000;expectCode([&]{(void)client->getInvite("bob-id",inviteId);},"INVALID_RESPONSE");
    data=invitation();data["status"]="accepted";expectCode([&]{(void)client->getInvite("bob-id",inviteId);},"INVALID_RESPONSE");
    data=invitation();expectCode([&]{(void)client->getInvite("alice-id",inviteId);},"INVALID_RESPONSE");
    data={{"invites",Json::array({invitation()})},{"start",0},{"more",false}};
    EXPECT_EQ(1U,client->listInvites("bob-id",0,32).invites.size());
    data["invites"].push_back(invitation());expectCode([&]{(void)client->listInvites("bob-id",0,32);},"INVALID_RESPONSE");
    expectCode([&]{(void)client->listInvites("bob-id",0,33);},"INVALID_ARGUMENT");
}
TEST(ServiceSessionDirectoryTest, HostUpdateAndLeaveValidateExpectedRevisionAndShape) {
    Json data=roster();auto client=makeSessionDirectoryClient([&](const auto&,auto,const auto&,const auto&){return data;});
    ServiceSessionSettings settings;settings.maxGamers=4;settings.privateSlots=1;settings.properties[0]=7;settings.properties[7]=-2147483647-1;
    data["revision"]=2;EXPECT_EQ(2,client->update("alice-id",sessionId,1,settings).revision);
    data["revision"]=1;expectCode([&]{(void)client->update("alice-id",sessionId,1,settings);},"INVALID_RESPONSE");
    data=roster();EXPECT_EQ(machineId,client->touch("alice-id",sessionId).machine);
    data={{"ended",false}};EXPECT_FALSE(client->leave("alice-id",sessionId));data["ended"]=true;EXPECT_TRUE(client->leave("alice-id",sessionId));
    data["ended"]=1;expectCode([&]{(void)client->leave("alice-id",sessionId);},"INVALID_RESPONSE");
    auto error=makeSessionDirectoryClient([](const auto&,auto,const auto&,const auto&)->Json{throw ServiceOperationError("CONFLICT");});
    expectCode([&]{(void)error->get("alice-id",sessionId);},"CONFLICT");
}
TEST(ServiceSessionDirectoryTest, RelayTicketChecksExactGroupCorrelationSecretBoundsAndNegotiatedFrameLimits) {
    Json response{{"ticket",std::string(64,'a')},{"session",sessionId},{"machine",machineId},
        {"serverTime",1000},{"expires",1060},{"relayVersion",1},{"maxDatagramBytes",4096}};
    auto client=makeSessionDirectoryClient([&](const auto& op,const auto& args,const auto& actor,const auto& users) {
        EXPECT_EQ("sessions.relayTicket",op);EXPECT_EQ(sessionId,args["session"]);EXPECT_EQ("alice-id",actor);
        EXPECT_EQ((std::vector<std::string>{"alice-id","charlie-id"}),users);EXPECT_FALSE(args.contains("participants"));return response;
    });
    const auto ticket=client->issueRelayTicket("alice-id",{"alice-id","charlie-id"},sessionId);
    EXPECT_EQ(1060,ticket.expires);EXPECT_EQ(1000,ticket.issuedAt);EXPECT_EQ(machineId,ticket.machine);EXPECT_EQ(std::string(64,'a'),ticket.ticket);
    const auto valid=response;
    for(const auto& [key,value]:std::vector<std::pair<std::string,Json>>{
        {"ticket",std::string(65,'a')},{"ticket",std::string(64,'x')},{"ticket",""},{"session",std::string(32,'f')},
        {"machine","../path"},{"expires",1061},{"expires",1000},{"serverTime",-1},{"relayVersion",2},{"relayVersion",true},
        {"maxDatagramBytes",4097},{"maxDatagramBytes",4096.0},{"serverTime",18446744073709551615ULL}}) {
        response=valid;response[key]=value;
        expectCode([&]{(void)client->issueRelayTicket("alice-id",{"alice-id","charlie-id"},sessionId);},"INVALID_RESPONSE");
    }
    expectCode([&]{(void)client->issueRelayTicket("alice-id",{"charlie-id"},sessionId);},"INVALID_ARGUMENT");
    Fixture fixture;const auto host=fixture.directory->create("a",{"a","c"},ServiceSessionKind::PlayerMatch,{});
    const auto fake=fixture.directory->issueRelayTicket("a",{"a","c"},host.session);
    EXPECT_EQ(host.machine,fake.machine);EXPECT_EQ(fixture.time+60,fake.expires);EXPECT_EQ(64U,fake.ticket.size());
    expectCode([&]{(void)fixture.directory->issueRelayTicket("a",{"a"},host.session);},"NOT_AUTHORIZED");
    expectCode([&]{(void)fixture.directory->issueRelayTicket("c",{"a","c"},host.session);},"NOT_AUTHORIZED");
}
TEST(ServiceSessionDirectoryTest, FakeModelsBothKindsFourLocalsFilteringAndPrivateInvitationAtomicity) {
    for(const auto kind:{ServiceSessionKind::PlayerMatch,ServiceSessionKind::Ranked}) {
        Fixture f;auto& directory=*f.directory;ServiceSessionSettings settings;settings.maxGamers=4;settings.privateSlots=1;settings.properties[2]=17;
        const auto host=directory.create("a",{"a","c"},kind,settings);EXPECT_EQ(2,host.currentGamers);
        EXPECT_EQ(1U,directory.find("b",kind,1,settings.properties,0,32).sessions.size());
        EXPECT_TRUE(directory.find("b",kind,2,settings.properties,0,32).sessions.empty());
        auto filter=settings.properties;filter[2]=18;EXPECT_TRUE(directory.find("b",kind,1,filter,0,32).sessions.empty());
        const auto invite=directory.sendInvite("a",host.session,"Tagb");EXPECT_EQ(invite.invite,directory.sendInvite("a",host.session,"Tagb").invite);
        expectCode([&]{(void)directory.join("b",{"b","d"},host.session,invite.invite);},"INVALID_STATE");
        expectCode([&]{(void)directory.acceptInvite("d",invite.invite);},"NOT_AUTHORIZED");
        auto accepted=directory.acceptInvite("b",invite.invite);++f.time;
        EXPECT_EQ(accepted.acceptedAt,directory.acceptInvite("b",invite.invite).acceptedAt);
        const auto joined=directory.join("b",{"b","d"},host.session,invite.invite);EXPECT_EQ(4,joined.currentGamers);EXPECT_EQ(0,joined.openPrivateSlots);
        EXPECT_EQ(joined.machine,directory.join("b",{"b","d"},host.session,invite.invite).machine);
        expectCode([&]{(void)directory.join("b",{"b"},host.session,invite.invite);},"INVALID_STATE");
        expectCode([&]{(void)directory.leave("d",host.session);},"NOT_AUTHORIZED");
        EXPECT_FALSE(directory.leave("b",host.session));EXPECT_EQ(2,directory.get("a",host.session).currentGamers);
        expectCode([&]{(void)directory.join("b",{"b","d"},host.session,invite.invite);},"INVALID_STATE");
        EXPECT_TRUE(directory.leave("a",host.session));EXPECT_TRUE(directory.listInvites("b",0,32).invites.empty());
        expectCode([&]{(void)directory.get("a",host.session);},"NOT_FOUND");
    }
}
TEST(ServiceSessionDirectoryTest, RankedCreateCannotEnableJoinInProgressBeforeDispatch) {
    int calls=0;auto client=makeSessionDirectoryClient([&](const auto&,auto,const auto&,const auto&){++calls;return roster();});
    ServiceSessionSettings settings;settings.maxGamers=4;settings.allowJoinInProgress=true;
    expectCode([&]{(void)client->create("alice-id",{"alice-id"},ServiceSessionKind::Ranked,settings);},"INVALID_ARGUMENT");
    EXPECT_EQ(0,calls);
}
TEST(ServiceSessionDirectoryTest, RankedJoinInProgressSnapshotsAreInvalidAuthority) {
    auto data=roster();data["kind"]="ranked";data["allowJoinInProgress"]=true;
    auto client=makeSessionDirectoryClient([&](const auto&,auto,const auto&,const auto&){return data;});
    expectCode([&]{(void)client->get("alice-id",sessionId);},"INVALID_RESPONSE");
    auto advertised=advertisement();advertised["kind"]="ranked";advertised["allowJoinInProgress"]=true;
    data={{"start",0},{"more",false},{"sessions",Json::array({advertised})}};
    expectCode([&]{(void)client->find("bob-id",ServiceSessionKind::Ranked,1,{},0,32);},"INVALID_RESPONSE");
}
TEST(ServiceSessionDirectoryTest, FakeRankedGameplayCannotAdmitOrdinaryOrInvitedNewGroups) {
    Fixture fixture;auto& directory=*fixture.directory;ServiceSessionSettings settings;settings.maxGamers=4;settings.allowJoinInProgress=true;
    expectCode([&]{(void)directory.create("a",{"a","c"},ServiceSessionKind::Ranked,settings);},"INVALID_ARGUMENT");
    settings.allowJoinInProgress=false;const auto host=directory.create("a",{"a","c"},ServiceSessionKind::Ranked,settings);
    const auto invite=directory.sendInvite("a",host.session,"Tagb");(void)directory.acceptInvite("b",invite.invite);
    settings.allowJoinInProgress=true;
    expectCode([&]{(void)directory.update("a",host.session,host.revision,settings);},"INVALID_ARGUMENT");
    auto unchanged=directory.get("a",host.session);EXPECT_EQ(host.revision,unchanged.revision);EXPECT_FALSE(unchanged.allowJoinInProgress);
    settings.allowJoinInProgress=false;settings.state=ServiceSessionState::Playing;
    const auto playing=directory.update("a",host.session,host.revision,settings);
    EXPECT_TRUE(directory.find("b",ServiceSessionKind::Ranked,2,{},0,32).sessions.empty());
    expectCode([&]{(void)directory.join("b",{"b","d"},host.session);},"INVALID_STATE");
    expectCode([&]{(void)directory.join("b",{"b","d"},host.session,invite.invite);},"INVALID_STATE");
    EXPECT_EQ(ServiceInvitationState::Accepted,directory.getInvite("b",invite.invite).state);
    unchanged=directory.get("a",host.session);EXPECT_EQ(2,unchanged.currentGamers);EXPECT_EQ(playing.revision,unchanged.revision);
    EXPECT_EQ(host.machine,directory.join("a",{"a","c"},host.session).machine);
}
TEST(ServiceSessionDirectoryTest, FakeCapacityFailureKeepsAcceptedInviteAndAllMembersUntouched) {
    Fixture f;ServiceSessionSettings settings;settings.maxGamers=3;settings.privateSlots=1;
    const auto host=f.directory->create("a",{"a","c"},ServiceSessionKind::PlayerMatch,settings);
    const auto invite=f.directory->sendInvite("a",host.session,"Tagb");(void)f.directory->acceptInvite("b",invite.invite);
    expectCode([&]{(void)f.directory->join("b",{"b","d"},host.session,invite.invite);},"SESSION_FULL");
    EXPECT_EQ(2,f.directory->get("a",host.session).currentGamers);EXPECT_EQ(ServiceInvitationState::Accepted,f.directory->getInvite("b",invite.invite).state);
    settings.maxGamers=4;const auto updated=f.directory->update("a",host.session,host.revision,settings);
    EXPECT_EQ(4,f.directory->join("b",{"b","d"},host.session,invite.invite).currentGamers);EXPECT_EQ(2,updated.currentGamers);
}
TEST(ServiceSessionDirectoryTest, FakeHostRevisionsPlayingPolicyExpiryAndAuthentication) {
    Fixture f;ServiceSessionSettings settings;settings.maxGamers=4;
    auto host=f.directory->create("a",{"a"},ServiceSessionKind::PlayerMatch,settings);
    expectCode([&]{(void)f.directory->update("a",host.session,host.revision+1,settings);},"CONFLICT");
    settings.state=ServiceSessionState::Playing;host=f.directory->update("a",host.session,host.revision,settings);
    EXPECT_TRUE(f.directory->find("b",ServiceSessionKind::PlayerMatch,1,{},0,32).sessions.empty());
    expectCode([&]{(void)f.directory->join("b",{"b"},host.session);},"INVALID_STATE");
    settings.allowJoinInProgress=true;host=f.directory->update("a",host.session,host.revision,settings);
    const auto joined=f.directory->join("b",{"b","d"},host.session);EXPECT_EQ(3,joined.currentGamers);
    f.time+=60;(void)f.directory->touch("a",host.session);f.time+=31;
    EXPECT_EQ(1,f.directory->get("a",host.session).currentGamers);
    expectCode([&]{(void)f.directory->get("b",host.session);},"NOT_AUTHORIZED");
    f.authorized.erase("a");expectCode([&]{(void)f.directory->touch("a",host.session);},"UNAUTHENTICATED");
    f.authorized.insert("a");f.time+=60;EXPECT_TRUE(f.directory->find("b",ServiceSessionKind::PlayerMatch,1,{},0,32).sessions.empty());
    expectCode([&]{(void)f.directory->get("a",host.session);},"NOT_FOUND");
}
TEST(ServiceSessionDirectoryTest, FakeInvitationExpiryAndQuotaSurviveSessionCloseRecreate) {
    Fixture f;ServiceSessionSettings settings;settings.maxGamers=4;
    auto host=f.directory->create("a",{"a"},ServiceSessionKind::PlayerMatch,settings);
    auto invite=f.directory->sendInvite("a",host.session,"Tagb");
    for(int step=0;step<15;++step){f.time+=60;(void)f.directory->touch("a",host.session);}
    EXPECT_TRUE(f.directory->listInvites("b",0,32).invites.empty());
    expectCode([&]{(void)f.directory->acceptInvite("b",invite.invite);},"INVALID_STATE");
    for(int i=1;i<32;++i) {
        EXPECT_TRUE(f.directory->leave("a",host.session));host=f.directory->create("a",{"a"},ServiceSessionKind::PlayerMatch,settings);
        invite=f.directory->sendInvite("a",host.session,"Tagb");(void)f.directory->dismissInvite("b",invite.invite);
    }
    expectCode([&]{(void)f.directory->sendInvite("a",host.session,"Tagb");},"RATE_LIMITED");
    for(int step=0;step<46;++step){f.time+=60;(void)f.directory->touch("a",host.session);}
    EXPECT_EQ(ServiceInvitationState::Pending,f.directory->sendInvite("a",host.session,"Tagb").state);
}
TEST(ServiceSessionDirectoryTest, ExplicitFakeBackendUsesSignedInIdentityPrivilegesAndTypedDirectory) {
    ServiceIdentity alice;alice.userId="a";alice.gamertag="Alice";alice.allowOnlineSessions=true;
    ServiceIdentity bob;bob.userId="b";bob.gamertag="Bob";bob.allowOnlineSessions=true;
    auto backend=makeFakeBackend({alice,bob});backend->signIn(0,"Alice","fixture");backend->signIn(1,"Bob","fixture");
    EXPECT_EQ(2U,backend->pump().size());auto& directory=backend->sessionDirectory();
    const auto host=directory.create("a",{"a"},ServiceSessionKind::PlayerMatch,{});
    EXPECT_EQ(2,directory.join("b",{"b"},host.session).currentGamers);
    backend->signOut(1);(void)backend->pump();EXPECT_THROW((void)directory.get("b",host.session),Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException);
}
// XNA NetworkMachine.RemoveFromSession: the host alone removes another machine; its users are
// then answered REMOVED_BY_HOST (fake and typed client agree with the server contract).
TEST(ServiceSessionDirectoryTest, TheHostRemovesAnotherMachineWhoseUsersAreToldWhy) {
    Fixture fixture;auto& directory=*fixture.directory;ServiceSessionSettings settings;settings.maxGamers=6;
    const auto host=directory.create("a",{"a","c"},ServiceSessionKind::PlayerMatch,settings);
    const auto guest=directory.join("b",{"b","d"},host.session);
    expectCode([&]{(void)directory.remove("b",host.session,host.machine);},"NOT_AUTHORIZED");
    expectCode([&]{(void)directory.remove("a",host.session,host.machine);},"INVALID_ARGUMENT");
    expectCode([&]{(void)directory.remove("a",host.session,std::string(32,'9'));},"NOT_FOUND");
    const auto after=directory.remove("a",host.session,guest.machine);
    EXPECT_EQ(2U,after.members.size());EXPECT_EQ(guest.revision+1,after.revision);
    for(const auto* user:{"b","d"})expectCode([&]{(void)directory.get(user,host.session);},"REMOVED_BY_HOST");
    expectCode([&]{(void)directory.issueRelayTicket("b",{"b","d"},host.session);},"REMOVED_BY_HOST");

    std::string op;Json args;
    auto result=roster();
    auto client=makeSessionDirectoryClient([&](const auto& operation,Json fields,const auto&,const auto&){op=operation;args=std::move(fields);return result;});
    const std::string other(32,'4');
    EXPECT_EQ(1U,client->remove("alice-id",sessionId,other).members.size());
    EXPECT_EQ("sessions.remove",op);EXPECT_EQ(sessionId,args["session"]);EXPECT_EQ(other,args["machine"]);
    // The reply must be the host's own snapshot without the removed machine.
    expectCode([&]{(void)client->remove("alice-id",sessionId,machineId);},"INVALID_RESPONSE");
    expectCode([&]{(void)client->remove("alice-id",sessionId,"../x");},"INVALID_ARGUMENT");
}
