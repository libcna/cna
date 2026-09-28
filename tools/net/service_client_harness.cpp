// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "System/ObjectDisposedException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "System/IServiceProvider.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace Microsoft::Xna::Framework::GamerServices;
namespace Service=CNA::Internal::GamerServices;
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
int main(int argc,char** argv) {
    try {
        const bool real=argc>1&&std::string(argv[1])=="--real";
        if(!real) {
            std::vector<Service::ServiceIdentity> people;
            for(int i=0;i<4;++i){Service::ServiceIdentity person;person.userId="id"+std::to_string(i);person.gamertag="Player"+std::to_string(i);person.displayName=person.gamertag;person.allowOnlineSessions=true;people.push_back(person);}
            Service::ServiceAchievement achievement;achievement.key="first";achievement.name="First";achievement.description="A real catalog entry";achievement.howToEarn="Play";achievement.score=10;
            Service::ServiceLeaderboardFixture board;board.key="BestScoreLifeTime";
            for(int i=0;i<4;++i){Service::ServiceLeaderboardEntry row;row.userId=people[i].userId;row.gamertag=people[i].gamertag;row.rating=(i+1)*100;row.columns["Rounds"]={"int32",3LL};board.entries.push_back(row);}
            Service::setBackendForTesting(Service::makeFakeBackend(people,{achievement},{board}));
        }
        Provider provider;GamerServicesDispatcher::Initialize(provider);
        auto* collection=Gamer::getSignedInGamersProperty();check(collection->getCountProperty()==0,"Initialize fabricated gamers");
        int signedIn=0,signedOut=0;
        auto in=SignedInGamer::SignedIn.Add([&](auto*,const SignedInEventArgs& e){++signedIn;check(e.getGamerProperty()->getIsSignedInToLiveProperty(),"identity before event");std::unique_ptr<GamerProfile> nested(e.getGamerProperty()->GetProfile());check(!nested->getIsDisposedProperty(),"sync read inside event");});
        auto out=SignedInGamer::SignedOut.Add([&](auto*,const SignedOutEventArgs& e){++signedOut;check(!e.getGamerProperty()->getIsSignedInToLiveProperty(),"signout state before event");});
        std::string password;
        if(real){check(argc==5,"real arguments");std::getline(std::cin,password);}
        Guide::ShowSignIn(real?1:4,true);
        check(Guide::getIsVisibleProperty()&&Guide::getHasPendingKeyboardInputEXTProperty(),"Guide username pane");
        bool busy=false;try{Guide::ShowSignIn(1,true);}catch(const GuideAlreadyVisibleException&){busy=true;}check(busy,"Guide overlapping sign-in");
        for(int i=0;i<(real?1:4);++i) {
            enterText(real?argv[2]:"Player"+std::to_string(i));
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
        check(!Guide::getIsVisibleProperty(),"Guide sign-in completed");
        check(collection==Gamer::getSignedInGamersProperty(),"collection lifetime changed");check(signedIn==(real?1:4),"sign-in count");
        auto* gamer=(*collection)[0];
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
        if(real) {
            const auto id=LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);
            bool callback=false;int callbackCount=0;
            std::unique_ptr<System::IAsyncResult> pending(LeaderboardReader::BeginRead(id,0,1,[&](auto& value){callback=true;++callbackCount;check(!value.getCompletedSynchronouslyProperty(),"remote leaderboard synchronous flag");},77));
            check(!pending->getIsCompletedProperty()&&!callback,"leaderboard premature callback");
            auto reader=LeaderboardReader::EndRead(pending.get());check(callback&&callbackCount==1,"leaderboard callback");
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
                auto* write=gamer->getLeaderboardWriterProperty().GetLeaderboard(id);write->setRatingProperty(999);
                auto after=LeaderboardReader::Read(id,0,2);check(firstEntry(after).getRatingProperty()==200,"setter persisted without session commit");
            }
            reader.Dispose();bool disposed=false;try{(void)reader.getEntriesProperty();}catch(const System::ObjectDisposedException&){disposed=true;}check(disposed,"remote reader disposal");
            bool sizeInvalid=false;try{std::unique_ptr<System::IAsyncResult> bad(LeaderboardReader::BeginRead(id,0,0,{},{}));}catch(const System::ArgumentOutOfRangeException&){sizeInvalid=true;}check(sizeInvalid,"remote page size validation");
        }
        profile->Dispose();
        if(!real) {
            std::unique_ptr<System::IAsyncResult> failure(gamer->BeginAwardAchievement("missing",{},{}));
            bool failed=false;try{gamer->EndAwardAchievement(failure.get());}catch(const GamerServicesNotAvailableException&){failed=true;}check(failed,"async exception propagation");
        }
        if(!real) {
            auto* other=(*collection)[1];
            Guide::ShowFriendRequest(Microsoft::Xna::Framework::PlayerIndex::One,other);
            check(Guide::getHasPendingMessageBoxEXTProperty(),"friend confirmation UI");Guide::SimulateMessageBoxClickEXT(0);
            check(Guide::getIsVisibleProperty()&&!Guide::getHasPendingMessageBoxEXTProperty(),"pending social request");
            GamerServicesDispatcher::Update();check(Guide::getHasPendingMessageBoxEXTProperty(),"friends UI after request");
            check(!gamer->IsFriend(other)&&gamer->GetFriends()[0]->getFriendRequestSentToProperty(),"pending not accepted");
            check(other->GetFriends()[0]->getFriendRequestReceivedFromProperty(),"incoming friend request");
            Guide::SimulateMessageBoxClickEXT(2);
            Guide::ShowGamerCard(Microsoft::Xna::Framework::PlayerIndex::Two,gamer);Guide::SimulateMessageBoxClickEXT(0);
            GamerServicesDispatcher::Update();check(gamer->IsFriend(other)&&other->IsFriend(gamer),"mutual accepted friendship");
            Guide::SimulateMessageBoxClickEXT(2);
            other->getPresenceProperty().setPresenceModeProperty(GamerPresenceMode::Level);
            other->getPresenceProperty().setPresenceValueProperty(12);
            check(gamer->GetFriends()[0]->getPresenceProperty().empty(),"presence changed before Update");
            GamerServicesDispatcher::Update();check(gamer->GetFriends()[0]->getPresenceProperty()=="Level 12","presence mode ordinal/value");
            auto snapshot=gamer->GetFriends();snapshot.Dispose();check(snapshot.getIsDisposedProperty(),"owned friend snapshot disposal");
            Guide::ShowFriends(Microsoft::Xna::Framework::PlayerIndex::One);Guide::SimulateMessageBoxClickEXT(0);
            enterText("missing");check(Guide::getHasPendingMessageBoxEXTProperty(),"missing profile error UI");Guide::SimulateMessageBoxClickEXT(0);
            Guide::ShowGamerCard(Microsoft::Xna::Framework::PlayerIndex::One,other);Guide::SimulateMessageBoxClickEXT(0);
            GamerServicesDispatcher::Update();check(!gamer->IsFriend(other)&&other->GetFriends().getCountProperty()==0,"mutual friend removal");Guide::SimulateMessageBoxClickEXT(2);
        }
        if(real&&(std::string(argv[4])=="request"||std::string(argv[4])=="presence-wait")) {
            std::unique_ptr<Gamer> target(Gamer::GetFromGamertag(std::string(argv[4])=="request"?"Bob":"Alice"));
            if(std::string(argv[4])=="request")Guide::ShowFriendRequest(Microsoft::Xna::Framework::PlayerIndex::One,target.get());
            else Guide::ShowGamerCard(Microsoft::Xna::Framework::PlayerIndex::One,target.get());
            Guide::SimulateMessageBoxClickEXT(0);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
            while(!Guide::getHasPendingMessageBoxEXTProperty()) {
                GamerServicesDispatcher::Update();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("social completion timeout");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Guide::SimulateMessageBoxClickEXT(2);
            if(std::string(argv[4])=="presence-wait") {
                check(gamer->IsFriend(target.get()),"real mutual friendship");
                gamer->getPresenceProperty().setPresenceModeProperty(GamerPresenceMode::Level);gamer->getPresenceProperty().setPresenceValueProperty(12);
                GamerServicesDispatcher::Update();bool barrier=false;Service::backend()->submit([]{},[&]{barrier=true;});
                while(!barrier){GamerServicesDispatcher::Update();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                std::cout<<"READY_PRESENCE"<<std::endl;std::string command;std::getline(std::cin,command);
            }else check(!gamer->IsFriend(target.get()),"real pending request");
        }
        if(real&&std::string(argv[4])=="friends") {
            std::unique_ptr<Gamer> other(Gamer::GetFromGamertag("Bob"));check(gamer->IsFriend(other.get()),"real accepted friend");
            auto friends=gamer->GetFriends();check(friends.getCountProperty()==1&&friends[0]->getIsOnlineProperty(),"real online friend");
            check(friends[0]->getPresenceProperty()=="Level 12","real rich presence/value");
            Guide::ShowFriends(Microsoft::Xna::Framework::PlayerIndex::One);Guide::SimulateMessageBoxClickEXT(2);
        }
        if(real&&std::string(argv[4])=="revoke-wait") {
            std::cout<<"READY_REVOKE"<<std::endl;std::string command;std::getline(std::cin,command);
            bool denied=false;try{(void)gamer->GetAchievements();}catch(const GamerServicesNotAvailableException&){denied=true;}
            check(denied,"revoked operation allowed");waitFor(0);check(signedOut==1,"revoked credential signout event");
            std::cout<<checks<<" revocation checks passed\n";return 0;
        }
        auto* retired=gamer;Service::backend()->signOut(0);check(retired->getIsSignedInToLiveProperty(),"signout before pump");waitFor(real?0:3);
        check(signedOut==1&&!retired->getIsSignedInToLiveProperty(),"retired identity/event lifetime");
        if(!real) {
            Guide::ShowSignIn(1,true);Guide::SimulateKeyboardInputCancelEXT();
            check(!Guide::getIsVisibleProperty()&&collection->getCountProperty()==3,"Guide username cancellation");
            Guide::ShowSignIn(1,true);enterText("Player0");Guide::SimulateKeyboardInputCancelEXT();
            check(!Guide::getIsVisibleProperty(),"Guide password cancellation");
        }
        SignedInGamer::SignedIn.Remove(in);SignedInGamer::SignedOut.Remove(out);
        std::fill(password.begin(),password.end(),'\0');std::cout<<checks<<" client checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Client check failed: "<<e.what()<<'\n';return 1;}
}
