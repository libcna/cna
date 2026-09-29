// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "System/ObjectDisposedException.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "../../modules/gamer-services/src/Internal/Guide/GuideUi.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "System/IServiceProvider.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include <chrono>
#include <array>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace Microsoft::Xna::Framework::GamerServices;
namespace Service=CNA::Internal::GamerServices;
namespace Ui=CNA::Internal::GamerServices::GuideUi;
namespace {
// Waits for a Guide screen with at least this many items (its service reads are asynchronous).
bool uiItems(const std::string& screen,std::size_t minimum) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    while(Ui::currentScreenForTesting()!=screen||Ui::labelsForTesting().size()<minimum) {
        if(std::chrono::steady_clock::now()>deadline)return false;
        GamerServicesDispatcher::Update();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}
// Waits for a Guide screen whose item shows a label.
bool uiLabel(const std::string& screen,std::size_t index,const std::string& label) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    for(;;) {
        const auto labels=Ui::labelsForTesting();
        if(Ui::currentScreenForTesting()==screen&&labels.size()>index&&labels[index]==label)return true;
        if(std::chrono::steady_clock::now()>deadline)return false;
        GamerServicesDispatcher::Update();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void settle(){for(int i=0;i<100;++i){GamerServicesDispatcher::Update();std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
}
int checks=0;
void check(bool condition,const char* reason){++checks;if(!condition)throw std::runtime_error(reason);}
LeaderboardEntry firstEntry(const LeaderboardReader& reader) {const auto entries=reader.getEntriesProperty();return entries[0];}
class Provider : public System::IServiceProvider {public:void* GetService(const std::type_info&)const override{return nullptr;}};
void waitFor(int count) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    while(Gamer::getSignedInGamersProperty()->getCountProperty()!=count) {
        GamerServicesDispatcher::Update();
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Sign-in timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void enterText(const std::string& text) {
    for(unsigned char c:text)Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(c);
    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r');
}
void exerciseLocalLeaderboard(SignedInGamer* gamer,const std::vector<SignedInGamer*>& players) {
    namespace Net=Microsoft::Xna::Framework::Net;
    const auto id=LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    bool outside=false;try{(void)gamer->getLeaderboardWriterProperty().GetLeaderboard(id);}catch(const System::InvalidOperationException&){outside=true;}check(outside,"writer allowed outside network gameplay");
    const auto before=LeaderboardReader::Read(id,std::vector<Gamer*>{gamer},gamer,1);
    const auto oldRating=firstEntry(before).getRatingProperty();
    std::unique_ptr<Net::NetworkSession> session(Net::NetworkSession::Create(Net::NetworkSessionType::LocalWithLeaderboards,players,4,0,{}));
    auto* local=session->getLocalGamersProperty()[0];check(local->getGamertagProperty()==gamer->getGamertagProperty(),"local service gamer identity");
    session->StartGame();
    outside=false;try{(void)local->getLeaderboardWriterProperty().GetLeaderboard(id);}catch(const System::InvalidOperationException&){outside=true;}check(outside,"write scope published before session Update");
    session->Update();check(session->getSessionStateProperty()==Net::NetworkSessionState::Playing,"service playing state");
    auto* entry=local->getLeaderboardWriterProperty().GetLeaderboard(id);entry->setRatingProperty(600);entry->getColumnsProperty().SetValue("Rounds",5);
    auto* signedEntry=gamer->getLeaderboardWriterProperty().GetLeaderboard(id);
    check(firstEntry(LeaderboardReader::Read(id,std::vector<Gamer*>{gamer},gamer,1)).getRatingProperty()==oldRating,"gameplay setter flushed before EndGame");
    int writes=0,ended=0;std::string order;
    auto writing=session->WriteUnarbitratedLeaderboard.Add([&](auto*,const Net::WriteLeaderboardsEventArgs& args){
        ++writes;order+='W';bool restricted=false;try{(void)gamer->GetProfile();}catch(const System::InvalidOperationException&){restricted=true;}check(restricted,"service call inside final write handler");
        restricted=false;try{session->ResetReady();}catch(const System::InvalidOperationException&){restricted=true;}check(restricted,"network call inside final write handler");
        restricted=false;try{local->SendData(std::vector<SharpRuntime::bytecs>{1},Net::SendDataOptions::Reliable);}catch(const System::InvalidOperationException&){restricted=true;}check(restricted,"packet send inside final write handler");
        check(!args.getIsLeavingProperty()&&session->getSessionStateProperty()==Net::NetworkSessionState::Playing,"final write event state");
        if(args.getGamerProperty()==local)args.getGamerProperty()->getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(650);
    });
    auto ending=session->GameEnded.Add([&](auto*,const auto&){++ended;order+='E';});
    session->EndGame();check(writes==0&&ended==0,"EndGame events before Update");
    session->Update();check(writes==static_cast<int>(players.size())&&ended==1&&order==std::string(players.size(),'W')+"E","final write before GameEnded ordering");
    check(session->getSessionStateProperty()==Net::NetworkSessionState::Lobby,"post commit lobby");
    auto after=LeaderboardReader::Read(id,std::vector<Gamer*>{gamer},gamer,1);
    check(firstEntry(after).getRatingProperty()==650&&firstEntry(after).getColumnsProperty().GetValueInt32("Rounds")==5,"final callback score/columns persisted");
    outside=false;try{entry->setRatingProperty(700);}catch(const System::InvalidOperationException&){outside=true;}check(outside,"retained rating writable in Lobby");
    outside=false;try{entry->getColumnsProperty().SetValue("Rounds",7);}catch(const System::InvalidOperationException&){outside=true;}check(outside,"retained columns writable in Lobby");
    session->WriteUnarbitratedLeaderboard.Remove(writing);session->GameEnded.Remove(ending);
    session->Dispose();outside=false;try{signedEntry->setRatingProperty(700);}catch(const System::InvalidOperationException&){outside=true;}check(outside,"signed writer scope survives disposed session");
}
void exerciseLocalLeave(SignedInGamer* gamer,const std::vector<SignedInGamer*>& players) {
    namespace Net=Microsoft::Xna::Framework::Net;
    const auto id=LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
    std::unique_ptr<Net::NetworkSession> session(Net::NetworkSession::Create(Net::NetworkSessionType::LocalWithLeaderboards,players,4,0,{}));
    session->StartGame();session->Update();
    int leaving=0;
    session->WriteUnarbitratedLeaderboard += [&](auto*,const Net::WriteLeaderboardsEventArgs& args){
        check(args.getIsLeavingProperty()&&!args.getGamerProperty()->getHasLeftSessionProperty(),"leave callback identity/state");++leaving;
        if(args.getGamerProperty()==session->getLocalGamersProperty()[0])args.getGamerProperty()->getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(850);
    };
    session->Dispose();check(session->getIsDisposedProperty()&&leaving==static_cast<int>(players.size()),"Dispose submits final local writes");
    check(firstEntry(LeaderboardReader::Read(id,std::vector<Gamer*>{gamer},gamer,1)).getRatingProperty()==850,"early leaving score persisted");
    session.reset(Net::NetworkSession::Create(Net::NetworkSessionType::LocalWithLeaderboards,players,4,0,{}));
    session->StartGame();session->Update();leaving=0;int ended=0;
    auto* departing=session->getLocalGamersProperty()[0];
    session->WriteUnarbitratedLeaderboard += [&](auto*,const Net::WriteLeaderboardsEventArgs& args){check(args.getIsLeavingProperty(),"disconnect final write flag");++leaving;if(args.getGamerProperty()==departing)departing->getLeaderboardWriterProperty().GetLeaderboard(id)->setRatingProperty(900);};
    session->SessionEnded += [&](auto*,const auto&){++ended;check(leaving==static_cast<int>(players.size()),"SessionEnded before leave writes");};
    session->RemoveGamer(departing,Net::NetworkSessionEndReason::Disconnected);session->Update();
    check(ended==1&&departing->getHasLeftSessionProperty(),"local disconnect ended session");session->Dispose();
    check(firstEntry(LeaderboardReader::Read(id,std::vector<Gamer*>{gamer},gamer,1)).getRatingProperty()==900,"disconnect score persisted");
    // Unpublished gameplay/destructor abandonment must release quotas without callbacks or scores.
    for(int i=0;i<20;++i) {
        auto* abandoned=Net::NetworkSession::Create(Net::NetworkSessionType::LocalWithLeaderboards,players,4,0,{});
        abandoned->StartGame();delete abandoned;
        bool drained=false;Service::backend()->submit([]{},[&]{drained=true;});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
        while(!drained){GamerServicesDispatcher::Update();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Abandon cleanup timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    }
    check(firstEntry(LeaderboardReader::Read(id,std::vector<Gamer*>{gamer},gamer,1)).getRatingProperty()==900,"abandoned gameplay changed score");
}
int main(int argc,char** argv) {
    try {
        const bool real=argc>1&&std::string(argv[1])=="--real";
        const std::string action=real&&argc==5?argv[4]:"";
        const int localCount=!real||action=="remember-four"||action=="resume-four"?4:1;
        if(!real) {
            std::vector<Service::ServiceIdentity> people;
            for(int i=0;i<4;++i){Service::ServiceIdentity person;person.userId="id"+std::to_string(i);person.gamertag="Player"+std::to_string(i);person.displayName=person.gamertag;person.allowOnlineSessions=true;people.push_back(person);}
            Service::ServiceAchievement achievement;achievement.key="first";achievement.name="First";achievement.description="A real catalog entry";achievement.howToEarn="Play";achievement.score=10;
            Service::ServiceLeaderboardFixture board;board.key="BestScoreLifeTime";
            for(int i=0;i<4;++i){Service::ServiceLeaderboardEntry row;row.userId=people[i].userId;row.gamertag=people[i].gamertag;row.rating=(i+1)*100;row.columns["Rounds"]={"int32",3LL};board.entries.push_back(row);}
            Service::setBackendForTesting(Service::makeFakeBackend(people,{achievement},{board}));
        }
        if(real&&!std::getenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR")) {
#if defined(_WIN32)
            _putenv_s("CNA_GAMER_SERVICES_CREDENTIALS_DIR","0");
#else
            setenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR","0",1);
#endif
        }
        Provider provider;GamerServicesDispatcher::Initialize(provider);
        auto* collection=Gamer::getSignedInGamersProperty();check(collection->getCountProperty()==0,"Initialize fabricated gamers");
        int signedIn=0,signedOut=0;
        auto in=SignedInGamer::SignedIn.Add([&](auto*,const SignedInEventArgs& e){++signedIn;check(e.getGamerProperty()->getIsSignedInToLiveProperty(),"identity before event");std::unique_ptr<GamerProfile> nested(e.getGamerProperty()->GetProfile());check(!nested->getIsDisposedProperty(),"sync read inside event");});
        auto out=SignedInGamer::SignedOut.Add([&](auto*,const SignedOutEventArgs& e){++signedOut;check(e.getGamerProperty()->getIsDisposedProperty(),"signed-out gamer disposed before event");});
        std::string password;
        if(real){check(argc==5,"real arguments");std::getline(std::cin,password);}
        if(real&&(action=="resume"||action=="resume-four")) {
            waitFor(localCount);check((*collection)[0]->getGamertagProperty()=="Alice","restored service identity");
        }else {
        Guide::ShowSignIn(localCount,true);
        // The sign-in picker; typing the account name starts it.
        check(Guide::getIsVisibleProperty()&&Ui::currentScreenForTesting()=="signIn","Guide sign-in picker");
        bool busy=false;try{Guide::ShowSignIn(1,true);}catch(const GuideAlreadyVisibleException&){busy=true;}check(busy,"Guide overlapping sign-in");
        for(int i=0;i<localCount;++i) {
            const std::array<std::string,4> accounts{"alice","bob","charlie","dana"};
            if(real&&i>0)check(static_cast<bool>(std::getline(std::cin,password)),"local credential input");
            enterText(real?(localCount==4?accounts[i]:argv[2]):"Player"+std::to_string(i));
            const auto secret=real?password:std::string("test-fixture");
            for(unsigned char c:secret)Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(c);
            check(Guide::GetPendingKeyboardInputDisplayTextForTestingEXT()==std::string(secret.size(),'*'),"password masking");
            Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r');
            check(collection->getCountProperty()==i,"authentication published before Update");
            if(real&&std::string(argv[4])=="reject") {
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
                while(!Guide::getHasPendingMessageBoxEXTProperty()) {
                    GamerServicesDispatcher::Update();
                    if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("authentication rejection timeout");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                check(collection->getCountProperty()==0&&signedIn==0,"rejected identity published");
                Guide::SimulateMessageBoxClickEXT(0);check(!Guide::getIsVisibleProperty(),"error pane cleanup");
                std::cout<<checks<<" rejection checks passed\n";return 0;
            }
            waitFor(i+1);
        }
        }
        check(!Guide::getIsVisibleProperty(),"Guide sign-in completed");
        check(collection==Gamer::getSignedInGamersProperty(),"collection lifetime changed");check(signedIn==localCount,"sign-in count");
        auto* gamer=(*collection)[0];
        // GSP-L1: an account's GameDefaults come from the service (the driver stores Alice's).
        if(real&&localCount==1&&std::string(argv[2])=="alice") {
            const auto& defaults=gamer->getGameDefaultsProperty();
            check(defaults.getGameDifficultyProperty()==GameDifficulty::Hard&&defaults.getInvertYAxisProperty(),"account game defaults");
        }
        if(!real)for(int i=0;i<4;++i)check((*collection)[i]->getPlayerIndexProperty()==static_cast<Microsoft::Xna::Framework::PlayerIndex>(i),"local slot mapping");
        bool callback=false;
        std::unique_ptr<System::IAsyncResult> result(gamer->BeginGetAchievements([&](System::IAsyncResult& action){callback=true;check(action.getIsCompletedProperty(),"callback completion");check(!action.getCompletedSynchronouslyProperty(),"queued work synchronous flag");},42));
        check(!result->getIsCompletedProperty()&&!callback,"Begin completed prematurely");
        check(!result->getAsyncWaitHandleProperty().WaitOne(0),"wait handle initially signaled");
        auto achievements=gamer->EndGetAchievements(result.get());check(callback,"callback once");check(achievements.getCountProperty()==1,"catalog");
        check(achievements[0].getGamerScoreProperty()==10,"catalog score");check(achievements[0].getNameProperty()=="First","catalog name");
        if(real)check(achievements[0].getIsEarnedProperty()==(std::string(argv[3])=="earned"),"restart/user earned state");
        bool duplicate=false;try{(void)gamer->EndGetAchievements(result.get());}catch(const System::InvalidOperationException&){duplicate=true;}check(duplicate,"repeated End");
        if(!real) {
            auto* other=(*collection)[1];
            std::unique_ptr<System::IAsyncResult> pending(gamer->BeginGetAchievements({},{}));
            bool wrongOwner=false;try{(void)other->EndGetAchievements(pending.get());}catch(const System::ArgumentException&){wrongOwner=true;}check(wrongOwner,"foreign owner");
            bool wrongOperation=false;try{gamer->EndAwardAchievement(pending.get());}catch(const System::ArgumentException&){wrongOperation=true;}check(wrongOperation,"wrong operation");
            (void)gamer->EndGetAchievements(pending.get());
        }
        if(!real||std::string(argv[4])=="award") {
            int callbacks=0;std::unique_ptr<System::IAsyncResult> award(gamer->BeginAwardAchievement("first",[&](auto&){++callbacks;},{}));
            gamer->EndAwardAchievement(award.get());GamerServicesDispatcher::Update();check(callbacks==1,"award callback count");
            gamer->AwardAchievement("first");check(gamer->GetAchievements()[0].getIsEarnedProperty(),"persistent award");
        }
        std::unique_ptr<Gamer> lookup(Gamer::GetFromGamertag(gamer->getGamertagProperty()));check(lookup->getGamertagProperty()==gamer->getGamertagProperty(),"lookup");
        std::unique_ptr<GamerProfile> profile(gamer->GetProfile());check(!profile->getIsDisposedProperty(),"profile snapshot");
        if(real) {
            auto picturedAchievement=achievements[0];
            std::unique_ptr<System::IO::Stream> picture(picturedAchievement.GetPicture());
            check(picture&&picture->getPositionProperty()==0&&!picture->getCanWriteProperty(),"achievement stream ownership/position");
            Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
            auto texture=Microsoft::Xna::Framework::Graphics::Texture2D::FromStream(device,*picture);
            check(texture.getWidthProperty()==2&&texture.getHeightProperty()==2,"achievement PNG texture decode");
            std::unique_ptr<System::IO::Stream> again(picturedAchievement.GetPicture());check(again.get()!=picture.get()&&again->getPositionProperty()==0,"independent cached streams");
            std::unique_ptr<System::IO::Stream> gamerPicture(profile->GetGamerPicture());check(gamerPicture&&gamerPicture->getLengthProperty()==again->getLengthProperty(),"profile picture retrieval");
        }
        if(!real) {
            const auto id=LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
            bool complete=false;std::unique_ptr<System::IAsyncResult> pending(LeaderboardReader::BeginRead(id,0,2,[&](auto& value){complete=true;check(!value.getCompletedSynchronouslyProperty(),"fake queued leaderboard flag");},{}));
            check(!complete&&!pending->getIsCompletedProperty(),"fake leaderboard update boundary");
            auto reader=LeaderboardReader::EndRead(pending.get());check(complete&&reader.getTotalLeaderboardSizeProperty()==4&&reader.getEntriesProperty().getCountProperty()==2,"fake remote total/page");
            check(firstEntry(reader).getGamerProperty()->getGamertagProperty()=="Player3"&&firstEntry(reader).getColumnsProperty().GetValueInt32("Rounds")==3,"fake owned gamer/column");
            std::unique_ptr<System::IAsyncResult> down(reader.BeginPageDown({},{}));
            bool overlap=false;try{std::unique_ptr<System::IAsyncResult> bad(reader.BeginPageDown({},{}));}catch(const System::InvalidOperationException&){overlap=true;}check(overlap,"fake page overlap");
            bool wrong=false;try{reader.EndPageUp(down.get());}catch(const System::ArgumentException&){wrong=true;}check(wrong,"fake page wrong operation");
            reader.EndPageDown(down.get());check(reader.getPageStartProperty()==2&&!reader.getCanPageDownProperty()&&reader.getCanPageUpProperty(),"fake page down");reader.PageUp();check(reader.getPageStartProperty()==0,"fake page up");
            auto centered=LeaderboardReader::Read(id,gamer,1);check(centered.getPageStartProperty()==3,"fake centered read");
            auto restricted=LeaderboardReader::Read(id,std::vector<Gamer*>{gamer},gamer,1);check(restricted.getTotalLeaderboardSizeProperty()==1&&firstEntry(restricted).getRankingEXTProperty()==4,"fake restricted global rank");
            auto empty=LeaderboardReader::Read(id,std::vector<Gamer*>{},gamer,1);check(empty.getTotalLeaderboardSizeProperty()==0,"fake explicit empty gamers");
            reader.Dispose();bool disposed=false;try{reader.PageDown();}catch(const System::ObjectDisposedException&){disposed=true;}check(disposed,"fake disposed remote reader");
            bool badSize=false;try{std::unique_ptr<System::IAsyncResult> bad(LeaderboardReader::BeginRead(id,0,0,{},{}));}catch(const System::ArgumentOutOfRangeException&){badSize=true;}check(badSize,"fake remote size validation");
        }
        if(real&&std::string(argv[4])!="leaderboard-after") {
            const auto id=LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
            bool callback=false;int callbackCount=0;
            std::unique_ptr<System::IAsyncResult> pending(LeaderboardReader::BeginRead(id,0,1,[&](auto& value){callback=true;++callbackCount;check(!value.getCompletedSynchronouslyProperty(),"remote leaderboard synchronous flag");},77));
            check(!pending->getIsCompletedProperty()&&!callback,"leaderboard premature callback");
            auto reader=LeaderboardReader::EndRead(pending.get());check(callback&&callbackCount==1,"leaderboard callback");
            const auto ownerThread=std::this_thread::get_id();
            bool callbackReadComplete=false;
            std::optional<LeaderboardReader> callbackReader;
            std::unique_ptr<System::IAsyncResult> callbackRead(LeaderboardReader::BeginRead(id,0,1,[&](auto& result) {
                check(std::this_thread::get_id()==ownerThread,"leaderboard materialization callback thread");
                callbackReader.emplace(LeaderboardReader::EndRead(&result));callbackReadComplete=true;
            },{}));
            const auto callbackDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(!callbackReadComplete) {
                GamerServicesDispatcher::Update();
                if(std::chrono::steady_clock::now()>=callbackDeadline)throw std::runtime_error("callback read timeout");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(callbackRead->getIsCompletedProperty()&&callbackReader.has_value(),"callback EndRead metadata lifetime");
            const bool mainTitle=CNA::GamerServices::resolveConfiguration().gameId=="one";
            check(reader.getTotalLeaderboardSizeProperty()==(mainTitle?2:0),"remote board title isolation");
            bool repeated=false;try{(void)LeaderboardReader::EndRead(pending.get());}catch(const System::InvalidOperationException&){repeated=true;}check(repeated,"leaderboard repeated End");
            if(mainTitle) {
                check(firstEntry(reader).getGamerProperty()->getGamertagProperty()=="Bob"&&firstEntry(reader).getRatingProperty()==200,"remote gamer/descending rating");
                auto columns=firstEntry(reader).getColumnsProperty();
                check(columns.GetValueInt32("Rounds")==3&&columns.GetValueString("Label")=="Original"&&columns.GetValueInt64("Total")==9223372036854775807LL,"typed integer/string precision");
                check(columns.GetValueSingle("Scale")==1.25f&&columns.GetValueDouble("Precision")==2.5&&columns.GetValueDateTime("When").getTicksProperty()==123456&&columns.GetValueTimeSpan("Duration").getTicksProperty()==-1000&&columns.GetValueOutcome("Outcome")==LeaderboardOutcome::Win,"remaining scalar board column types");
                auto second=LeaderboardReader::Read(id,1,1);check(second.getPageStartProperty()==1&&firstEntry(second).getGamerProperty()->getGamertagProperty()=="Alice","nonzero page start");
                check(reader.getCanPageDownProperty()&&!reader.getCanPageUpProperty(),"remote first page flags");
                std::unique_ptr<System::IAsyncResult> down(reader.BeginPageDown({},{}));
                bool overlap=false;try{std::unique_ptr<System::IAsyncResult> other(reader.BeginPageDown({},{}));}catch(const System::InvalidOperationException&){overlap=true;}check(overlap,"overlapping page requests");
                bool wrongOwner=false;try{second.EndPageDown(down.get());}catch(const System::ArgumentException&){wrongOwner=true;}check(wrongOwner,"remote page foreign owner");
                bool wrongOperation=false;try{reader.EndPageUp(down.get());}catch(const System::ArgumentException&){wrongOperation=true;}check(wrongOperation,"remote page wrong End");
                reader.EndPageDown(down.get());check(reader.getPageStartProperty()==1&&!reader.getCanPageDownProperty()&&reader.getCanPageUpProperty(),"remote next page flags");
                reader.PageUp();check(reader.getPageStartProperty()==0,"remote PageUp");
                std::unique_ptr<Gamer> alice(Gamer::GetFromGamertag("Alice"));
                auto centered=LeaderboardReader::Read(id,alice.get(),1);check(centered.getPageStartProperty()==1&&firstEntry(centered).getGamerProperty()->getGamertagProperty()=="Alice","remote centered read");
                auto restricted=LeaderboardReader::Read(id,std::vector<Gamer*>{alice.get()},alice.get(),1);check(restricted.getTotalLeaderboardSizeProperty()==1&&firstEntry(restricted).getRankingEXTProperty()==2,"remote restricted/global rank");
                auto empty=LeaderboardReader::Read(id,std::vector<Gamer*>{},alice.get(),1);check(empty.getTotalLeaderboardSizeProperty()==0,"remote empty gamer set");
                bool outside=false;try{(void)gamer->getLeaderboardWriterProperty().GetLeaderboard(id);}catch(const System::InvalidOperationException&){outside=true;}check(outside,"online writer outside session");
            }
            reader.Dispose();bool disposed=false;try{(void)reader.getEntriesProperty();}catch(const System::ObjectDisposedException&){disposed=true;}check(disposed,"remote reader disposal");
            bool sizeInvalid=false;try{std::unique_ptr<System::IAsyncResult> bad(LeaderboardReader::BeginRead(id,0,0,{},{}));}catch(const System::ArgumentOutOfRangeException&){sizeInvalid=true;}check(sizeInvalid,"remote page size validation");
        }
        if(!real) {std::vector<SignedInGamer*> locals;for(int i=0;i<4;++i)locals.push_back((*collection)[i]);exerciseLocalLeaderboard(gamer,locals);exerciseLocalLeave(gamer,locals);}
        if(real&&std::string(argv[4])=="leaderboard-write"){exerciseLocalLeaderboard(gamer,{gamer});exerciseLocalLeave(gamer,{gamer});}
        if(real&&std::string(argv[4])=="leaderboard-after") {
            auto after=LeaderboardReader::Read(LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime),0,2);
            check(firstEntry(after).getGamerProperty()->getGamertagProperty()=="Alice"&&firstEntry(after).getRatingProperty()==900,"EndGame/leave persistence across clients/server restart");
        }
        profile->Dispose();
        if(!real) {
            std::unique_ptr<System::IAsyncResult> failure(gamer->BeginAwardAchievement("missing",{},{}));
            bool failed=false;try{gamer->EndAwardAchievement(failure.get());}catch(const GamerServicesNotAvailableException&){failed=true;}check(failed,"async exception propagation");
        }
        if(!real) {
            auto* other=(*collection)[1];
            // The friend request is the gamer card's first action.
            Guide::ShowFriendRequest(Microsoft::Xna::Framework::PlayerIndex::One,other);
            check(uiLabel("gamerCard",1,"Send friend request"),"friend request UI");
            Ui::clickForTesting(0);check(Guide::getIsVisibleProperty(),"pending social request");
            check(uiLabel("gamerCard",1,"Cancel friend request"),"card after request");
            check(!gamer->IsFriend(other)&&gamer->GetFriends()[0]->getFriendRequestSentToProperty(),"pending not accepted");
            check(other->GetFriends()[0]->getFriendRequestReceivedFromProperty(),"incoming friend request");
            Ui::closeAll();
            Guide::ShowGamerCard(Microsoft::Xna::Framework::PlayerIndex::Two,gamer);
            check(uiLabel("gamerCard",1,"Accept friend request"),"incoming request on the card");Ui::clickForTesting(0);
            check(uiLabel("gamerCard",1,"Remove friend"),"card after accepting");
            check(gamer->IsFriend(other)&&other->IsFriend(gamer),"mutual accepted friendship");
            Ui::closeAll();
            other->getPresenceProperty().setPresenceModeProperty(GamerPresenceMode::Level);
            other->getPresenceProperty().setPresenceValueProperty(12);
            check(gamer->GetFriends()[0]->getPresenceProperty().empty(),"presence changed before Update");
            GamerServicesDispatcher::Update();check(gamer->GetFriends()[0]->getPresenceProperty()=="Level 12","presence mode ordinal/value");
            auto snapshot=gamer->GetFriends();snapshot.Dispose();check(snapshot.getIsDisposedProperty(),"owned friend snapshot disposal");
            // Y finds a gamer by gamertag; an unknown one gets the card's "not found".
            Guide::ShowFriends(Microsoft::Xna::Framework::PlayerIndex::One);check(uiItems("friends",1),"friends list");
            Ui::sendForTesting(Ui::Command::Y);enterText("missing");
            check(Ui::currentScreenForTesting()=="gamerCard","missing profile card");settle();check(Ui::labelsForTesting().empty(),"missing profile error UI");
            Ui::closeAll();
            Guide::ShowGamerCard(Microsoft::Xna::Framework::PlayerIndex::One,other);
            check(uiLabel("gamerCard",1,"Remove friend"),"remove offered");Ui::clickForTesting(0);
            check(uiLabel("gamerCard",1,"Send friend request"),"card after removal");
            check(!gamer->IsFriend(other)&&other->GetFriends().getCountProperty()==0,"mutual friend removal");Ui::closeAll();
        }
        if(real&&(std::string(argv[4])=="request"||std::string(argv[4])=="presence-wait")) {
            std::unique_ptr<Gamer> target(Gamer::GetFromGamertag(std::string(argv[4])=="request"?"Bob":"Alice"));
            if(std::string(argv[4])=="request")Guide::ShowFriendRequest(Microsoft::Xna::Framework::PlayerIndex::One,target.get());
            else Guide::ShowGamerCard(Microsoft::Xna::Framework::PlayerIndex::One,target.get());
            check(uiItems("gamerCard",2),"real gamer card");
            const auto before=Ui::labelsForTesting()[1];
            Ui::clickForTesting(0);
            // The card reads the friendship again once the service answered.
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
            while(Ui::labelsForTesting().size()<2||Ui::labelsForTesting()[1]==before) {
                GamerServicesDispatcher::Update();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("social completion timeout");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Ui::closeAll();
            if(std::string(argv[4])=="presence-wait") {
                check(gamer->IsFriend(target.get()),"real mutual friendship");
                gamer->getPresenceProperty().setPresenceModeProperty(GamerPresenceMode::Level);gamer->getPresenceProperty().setPresenceValueProperty(12);
                // GSP-L2: the account-wide status the Guide's Online status sets.
                const auto self=Service::GamerAccess::userId(*gamer);auto* executor=Service::backend().get();
                Service::backend()->submit([executor,self]{executor->setPresenceStatus(self,"busy");},[]{});
                GamerServicesDispatcher::Update();bool barrier=false;Service::backend()->submit([]{},[&]{barrier=true;});
                while(!barrier){GamerServicesDispatcher::Update();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                std::cout<<"READY_PRESENCE"<<std::endl;std::string command;std::getline(std::cin,command);
            }else check(!gamer->IsFriend(target.get()),"real pending request");
        }
        if(real&&std::string(argv[4])=="friends") {
            std::unique_ptr<Gamer> other(Gamer::GetFromGamertag("Bob"));check(gamer->IsFriend(other.get()),"real accepted friend");
            auto friends=gamer->GetFriends();check(friends.getCountProperty()==1&&friends[0]->getIsOnlineProperty(),"real online friend");
            check(friends[0]->getPresenceProperty()=="Level 12","real rich presence/value");
            check(friends[0]->getIsPlayingProperty()&&!friends[0]->getIsJoinableProperty()&&!friends[0]->getInviteReceivedFromProperty()
                &&!friends[0]->getInviteSentToProperty()&&!friends[0]->getIsAwayProperty(),"real friend state flags");
            check(friends[0]->getIsBusyProperty(),"real friend chose busy");
            Guide::ShowFriends(Microsoft::Xna::Framework::PlayerIndex::One);check(uiItems("friends",1),"real friends list");
            check(Ui::labelsForTesting()[0].starts_with("Bob - Busy"),"the list shows the chosen status");Ui::sendForTesting(Ui::Command::Back);
        }
        if(real&&std::string(argv[4])=="revoke-wait") {
            std::cout<<"READY_REVOKE"<<std::endl;std::string command;std::getline(std::cin,command);
            bool denied=false;try{(void)gamer->GetAchievements();}catch(const GamerServicesNotAvailableException&){denied=true;}
            check(denied,"revoked operation allowed");waitFor(0);check(signedOut==1,"revoked credential signout event");
            std::cout<<checks<<" revocation checks passed\n";return 0;
        }
        if(real&&(action=="remember"||action=="remember-four")) {
            SignedInGamer::SignedIn.Remove(in);SignedInGamer::SignedOut.Remove(out);
            std::fill(password.begin(),password.end(),'\0');std::cout<<checks<<" remembered-client checks passed\n";return 0;
        }
        if(real&&std::string(argv[4])=="maintenance") {
            std::string command;std::cout<<"maintenance-ready"<<std::endl;check(static_cast<bool>(std::getline(std::cin,command)),"expiry command");
            std::unique_ptr<GamerProfile> renewed(gamer->GetProfile());GamerServicesDispatcher::Update();
            check(signedIn==1&&signedOut==0&&renewed->getGamerScoreProperty()==10,"refresh changed gamer lifetime/events");
            check(renewed->getTitlesPlayedProperty()==1&&renewed->getGamerZoneProperty()==GamerZone::Unknown
                &&renewed->getReputationProperty()==0.0f,"profile titles/zone/reputation");
            std::cout<<"maintenance-refreshed"<<std::endl;check(static_cast<bool>(std::getline(std::cin,command)),"heartbeat command");
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(33);
            while(std::chrono::steady_clock::now()<until){GamerServicesDispatcher::Update();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
            check(signedIn==1&&signedOut==0,"heartbeat changed identity events");
            std::cout<<"maintenance-heartbeat"<<std::endl;check(static_cast<bool>(std::getline(std::cin,command)),"offline command");
            bool failed=false;try{std::unique_ptr<GamerProfile> offline(gamer->GetProfile());}catch(const GamerServicesNotAvailableException&){failed=true;}
            check(failed&&signedOut==0,"server loss fabricated success or signout");
            std::cout<<"maintenance-offline"<<std::endl;check(static_cast<bool>(std::getline(std::cin,command)),"reconnect command");
            std::unique_ptr<GamerProfile> reconnected(gamer->GetProfile());GamerServicesDispatcher::Update();
            check(reconnected->getGamerScoreProperty()==10&&signedIn==1&&signedOut==0,"reconnect lost identity or persistence");
        }
        auto* retired=gamer;Service::backend()->signOut(0);check(!retired->getIsDisposedProperty(),"signout before pump");waitFor(localCount-1);
        // XNA disposes a signed-out gamer and leaves IsSignedInToLive as it was.
        check(signedOut==1&&retired->getIsDisposedProperty()&&retired->getIsSignedInToLiveProperty(),"retired identity/event lifetime");
        if(!real) {
            // Back on the picker cancels; cancelling the password returns to the picker.
            Guide::ShowSignIn(1,true);Ui::sendForTesting(Ui::Command::Back);
            check(!Guide::getIsVisibleProperty()&&collection->getCountProperty()==3,"Guide sign-in cancellation");
            Guide::ShowSignIn(1,true);enterText("Player0");Guide::SimulateKeyboardInputCancelEXT();
            check(Ui::currentScreenForTesting()=="signIn"&&!Guide::getHasPendingKeyboardInputEXTProperty(),"Guide password cancellation");
            Ui::sendForTesting(Ui::Command::Back);check(!Guide::getIsVisibleProperty(),"Guide sign-in closed");
        }
        SignedInGamer::SignedIn.Remove(in);SignedInGamer::SignedOut.Remove(out);
        std::fill(password.begin(),password.end(),'\0');std::cout<<checks<<" client checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Client check failed: "<<e.what()<<'\n';return 1;}
}
