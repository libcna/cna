// SPDX-License-Identifier: MS-PL
// The CNA avatar editor as a system screen: the avatar large and alive on a lit stage, the
// editor's categories beside it, and each category's choices as rendered cards, colour swatches,
// sliders and face shapes. What the focus rests on is tried on at once; Accept keeps it. The camera
// follows the category (face, top, bottoms, shoes). Saving is transactional: the CNA service
// (avatars.set) or the local profile either holds the new avatar, or the editor says why not and
// keeps editing.
#include "GuideScreen.hpp"
#include "CNA/Internal/GamerServices/AvatarEditor.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <random>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
namespace GS=Microsoft::Xna::Framework::GamerServices;
namespace Av=CNA::Internal::GamerServices::Avatars;
using Av::EditorCategory;
using Av::EditorRowKind;

struct CategoryEntry {EditorCategory category;Icon icon;};
constexpr CategoryEntry Categories[]{
    {EditorCategory::Body,Icon::Figure},{EditorCategory::Skin,Icon::Drop},{EditorCategory::Face,Icon::Face},
    {EditorCategory::Eyes,Icon::Eye},{EditorCategory::NoseAndMouth,Icon::Mouth},{EditorCategory::Hair,Icon::Hair},
    {EditorCategory::FacialHair,Icon::Beard},{EditorCategory::Tops,Icon::Shirt},{EditorCategory::Bottoms,Icon::Trousers},
    {EditorCategory::Shoes,Icon::Shoe},{EditorCategory::Glasses,Icon::Glasses},{EditorCategory::Headwear,Icon::Hat}};
static_assert(std::size(Categories)==static_cast<std::size_t>(Av::EditorCategoryCount));

constexpr int VisibleCards=4;

double clockSeconds()
{
    static const auto start=std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
}

Framing framingOf(Av::EditorView view)
{
    switch(view) {
    case Av::EditorView::Head: return Framing::Head;
    case Av::EditorView::Upper: return Framing::Upper;
    case Av::EditorView::Lower: return Framing::Lower;
    case Av::EditorView::Feet: return Framing::Feet;
    case Av::EditorView::Body: break;
    }
    return Framing::Body;
}

std::vector<unsigned char> bytesOf(const Av::AvatarDescriptor& descriptor)
{
    const auto bytes=Av::encode(descriptor);
    return {bytes.begin(),bytes.end()};
}

Xna::Color scaled(Xna::Color color,float alpha)
{
    return Xna::Color(static_cast<int>(color.getRProperty()*alpha),static_cast<int>(color.getGProperty()*alpha),
                      static_cast<int>(color.getBProperty()*alpha),static_cast<int>(color.getAProperty()*alpha));
}

// The stage on the left, then the category column and the choices, at the UI scale.
struct Layout {Box stage,rail,content;};
Layout layoutFor(float width,float height,float scale)
{
    auto px=[&](float value){return value*scale;};
    const float top=px(92),bottom=px(76);
    const Box content{width-px(24)-px(470),top,px(470),height-top-bottom};
    const Box rail{content.x-px(14)-px(212),top,px(212),content.h};
    return {Box{0,0,rail.x-px(8),height},rail,content};
}

class AvatarEditorScreen;

// Asked when the player leaves with changes that are not saved.
class LeaveEditorScreen final : public Screen {
public:
    explicit LeaveEditorScreen(std::weak_ptr<AvatarEditorScreen> editor):editor_(std::move(editor)){}
    std::string name() const override {return "avatarEditorLeave";}
    std::string title() const override {return "Save your avatar?";}
    std::string subtitle() const override {return "Your changes are not saved yet.";}
    float dialogWidth() const override {return 560;}
    float dialogHeight() const override {return 380;}
    void input(InputContext& ui) override {
        const int chosen=list_.input(ui,3);
        if(chosen>=0)activate(chosen);
    }
    void activate(int index) override;
    void draw(Ui& ui,const Box& area) override {
        drawActions(ui,list_,area,{{Icon::Check,"Save"},{Icon::Cross,"Discard changes"},{Icon::Pencil,"Keep editing"}});
    }
    std::vector<Hint> hints() const override {return {{Command::Accept,"Select"},{Command::Back,"Keep editing"}};}
    std::vector<std::string> labels() const override {return {"Save","Discard changes","Keep editing"};}
    int focus() const override {return list_.focus;}
private:
    std::weak_ptr<AvatarEditorScreen> editor_;
    List list_;
};

struct LoadedAvatar {ServiceAvatarRecord record;std::uint16_t catalog=0;};
struct SaveResult {long long revision=0;std::string error;};

class AvatarEditorScreen final : public Screen {
public:
    explicit AvatarEditorScreen(AvatarEditorOptions options):options_(std::move(options)){}
    ~AvatarEditorScreen() override {
        // Closed with the rest of the Guide (sign-out, closeAll): the caller still hears of it.
        if(auto done=std::move(options_.closed))done(savedOnce_);
    }
    std::string name() const override {return "avatarEditor";}
    std::string title() const override {return "Avatar";}
    bool fullScreen() const override {return true;}
    bool busy() const override {
        if(stage_==Stage::Loading||stage_==Stage::Saving)return true;
        return stage_==Stage::Editing&&(pending_!=nullptr||shownBytes_!=wanted());
    }

    void start(const std::shared_ptr<AvatarEditorScreen>& self) {
        self_=self;
        GS::SignedInGamer* gamer=nullptr;
        for(auto* candidate:*GS::Gamer::getSignedInGamersProperty())
            if(candidate->getPlayerIndexProperty()==player)gamer=candidate;
        if(!gamer){fail("Nobody is signed in","Sign in with a profile to edit its avatar.");return;}
        if(gamer->getIsGuestProperty()){fail("Playing as a guest","A guest has no avatar of its own. Sign in with your own profile to edit one.");return;}
        gamertag_=gamer->getGamertagProperty();
        userId_=GamerAccess::userId(*gamer);
        if(userId_.empty()) {
            // A local profile edits against the newest catalog of this release.
            begin(Av::embeddedCatalogs().back(),localProfileAvatar(gamertag_),0);
            return;
        }
        // An account edits against the newest catalog its service has imported: the one the
        // service validates a saved description against.
        load<LoadedAvatar>(self,[user=userId_](IGamerServicesBackend& s) {
            LoadedAvatar loaded;
            auto records=s.avatars({user});
            if(!records.empty())loaded.record=std::move(records.front());
            loaded.catalog=Av::parseManifest(s.avatarCatalog(0)).version;
            return loaded;
        },[](AvatarEditorScreen& screen,LoadedAvatar loaded){screen.received(std::move(loaded));},
        [](AvatarEditorScreen& screen){screen.fail("Avatar not available","CNA Gamer Services could not be reached. Try again later.");});
    }

    void input(InputContext& ui) override {
        tick();
        auto consume=[&](Command command){ui.input.pressed[static_cast<std::size_t>(command)]=false;};
        const bool back=ui.input(Command::Back);
        consume(Command::Back);
        if(stage_==Stage::Failed) {
            if(back||ui.input(Command::Accept)||ui.input.click)close();
            return;
        }
        if(stage_==Stage::Loading){if(back)close();return;}
        if(stage_!=Stage::Editing)return;
        look_=ui.input.look;
        if(ui.input(Command::Previous)||ui.input(Command::Next))turnCategory(ui.input(Command::Next)?1:-1);
        if(ui.input(Command::Y)){model_->randomize();note_.clear();}
        if(ui.input(Command::X)){model_->randomizeCategory();note_.clear();}
        pointer(ui);
        if(!content_) {
            const int count=Av::EditorCategoryCount;
            const int index=static_cast<int>(model_->category());
            if(ui.input(Command::Up))setCategory((index+count-1)%count);
            if(ui.input(Command::Down))setCategory((index+1)%count);
            if((ui.input(Command::Right)||ui.input(Command::Accept))&&model_->fieldCount()>0)enterContent(0);
            if(back)leave();
            return;
        }
        const auto rows=model_->fieldCount();
        if(rows==0){content_=false;return;}
        row_=std::min(row_,rows-1);
        if(ui.input(Command::Up)&&row_>0)enterContent(row_-1);
        if(ui.input(Command::Down)&&row_+1<rows)enterContent(row_+1);
        const bool slider=model_->field(row_).kind==EditorRowKind::Slider;
        const auto count=static_cast<int>(model_->options(row_).size());
        if(ui.input(Command::Left)) {
            if(slider)step(-1);
            else if(option_>0)--option_;
            else content_=false;
        }
        if(ui.input(Command::Right)) {
            if(slider)step(1);
            else if(option_+1<count)++option_;
        }
        if(ui.input(Command::Accept)&&!slider&&option_<count)choose(row_,option_);
        if(back)content_=false;
    }

    void activate(int index) override {
        if(stage_!=Stage::Editing||index<0)return;
        if(!content_) {
            if(index<Av::EditorCategoryCount){setCategory(index);if(model_->fieldCount()>0)enterContent(0);}
        } else if(static_cast<std::size_t>(index)<model_->fieldCount()) {
            enterContent(static_cast<std::size_t>(index));
        }
    }

    std::vector<Hint> hints() const override {
        switch(stage_) {
        case Stage::Failed: return {{Command::Accept,"OK"}};
        case Stage::Loading: return {{Command::Back,"Cancel"}};
        case Stage::Saving: return {};
        case Stage::Editing: break;
        }
        std::vector<Hint> out;
        if(!content_)out.push_back({Command::Accept,"Choose"});
        else if(model_->field(row_).kind!=EditorRowKind::Slider)out.push_back({Command::Accept,"Select"});
        out.push_back({Command::X,"Shuffle"});
        out.push_back({Command::Y,"Random"});
        out.push_back({Command::Back,content_?"Back":model_->differsFromStored()?"Save":"Done"});
        return out;
    }

    std::vector<std::string> labels() const override {
        if(stage_==Stage::Loading)return {"Loading"};
        if(stage_==Stage::Failed)return {failTitle_};
        std::vector<std::string> out;
        if(!content_) {
            for(const auto& entry:Categories)out.push_back(Av::editorCategoryName(entry.category));
            return out;
        }
        for(std::size_t row=0;row<model_->fieldCount();++row) {
            const auto field=model_->field(row);
            auto text=field.label+": "+field.value;
            if(row==row_&&field.kind!=EditorRowKind::Slider) {
                const auto options=model_->options(row);
                if(option_<static_cast<int>(options.size())&&!options[static_cast<std::size_t>(option_)].current)
                    text+=" (trying "+optionName(options,option_)+")";
            }
            out.push_back(std::move(text));
        }
        return out;
    }
    int focus() const override {
        if(!model_)return -1;
        return content_?static_cast<int>(row_):static_cast<int>(model_->category());
    }

    void render(Xna::Graphics::GraphicsDevice& device,double seconds) override {
        const double dt=lastRender_<0?0.0:std::clamp(seconds-lastRender_,0.0,0.1);
        lastRender_=seconds;
        if(!shown_||shown_->getStateProperty()!=GS::AvatarRendererState::Ready||stageWidth_<8||stageHeight_<8)return;
        animate(dt);
        if(!target_||target_->getWidthProperty()!=stageWidth_||target_->getHeightProperty()!=stageHeight_)
            target_=std::make_unique<Xna::Graphics::RenderTarget2D>(device,stageWidth_,stageHeight_,false,Xna::Graphics::SurfaceFormat::Color,
                Xna::Graphics::DepthFormat::Depth24);
        // The camera eases to the category's view; turning and zoom are the player's.
        const float aspect=static_cast<float>(stageWidth_)/static_cast<float>(stageHeight_);
        const float height=shownDescription_->getHeightProperty();
        const auto view=model_?model_->view():Av::EditorView::Body;
        // Framed into the part of the stage between the header and the footer.
        const float usableTop=static_cast<float>(stageHeight_)*0.17f,usableBottom=static_cast<float>(stageHeight_)*0.91f;
        const float usable=(usableBottom-usableTop)/static_cast<float>(stageHeight_);
        const auto joints=bindJoints(*shown_);
        auto shot=shotFor(framingOf(view),joints,height,aspect*usable);
        if(view==Av::EditorView::Head&&joints.size()>19) {
            // Head and shoulders with room for hair and a hat: wider than a portrait's head.
            const float head=joints[19].Y,bottom=head-0.16f,top=head+0.38f;
            const float half=std::max((top-bottom)/2,0.15f/(aspect*usable));
            shot.target=Xna::Vector3(0,(bottom+top)/2,0);
            shot.offset=Xna::Vector3(0,0.04f,half/std::tan(shot.fov/2));
        }
        shot.offset.X=0;
        if(view==Av::EditorView::Body)shot.offset.Y=0.03f*shot.offset.Z;
        shot.offset=shot.offset/usable;
        // Centre the framed part in that band: the target moves by the band's offset from the middle.
        const float perPixel=2.0f*shot.offset.Length()*std::tan(shot.fov/2)/static_cast<float>(stageHeight_);
        shot.target.Y+=((usableTop+usableBottom)/2-static_cast<float>(stageHeight_)/2)*perPixel;
        const float follow=reducedMotion()||!cameraSet_?1.0f:1.0f-static_cast<float>(std::exp(-dt*5.0));
        cameraSet_=true;
        cameraTarget_=cameraTarget_+(shot.target-cameraTarget_)*follow;
        cameraOffset_=cameraOffset_+(shot.offset-cameraOffset_)*follow;
        cameraFov_+=(shot.fov-cameraFov_)*follow;
        const auto eye=cameraTarget_+cameraOffset_*zoom_;
        const auto world=Xna::Matrix::CreateRotationY(yaw_);
        const auto viewMatrix=Xna::Matrix::CreateLookAt(eye,cameraTarget_,Xna::Vector3::Up);
        const auto projection=Xna::Matrix::CreatePerspectiveFieldOfView(cameraFov_,aspect,0.05f,30.0f);
        shown_->setWorldProperty(world);
        shown_->setViewProperty(viewMatrix);
        shown_->setProjectionProperty(projection);
        Xna::Vector3 light(-0.40f,-0.50f,-0.77f);
        light.Normalize();
        shown_->setLightDirectionProperty(light);
        shown_->setLightColorProperty(Xna::Vector3(0.80f,0.77f,0.72f));
        shown_->setAmbientLightColorProperty(Xna::Vector3(0.42f,0.45f,0.52f));
        // Where the floor under the avatar lands on the stage, for its shadow.
        auto project=[&](Xna::Vector3 point) {
            const auto clip=Xna::Vector4::Transform(point,viewMatrix*projection);
            return Xna::Vector2((clip.X/clip.W*0.5f+0.5f)*static_cast<float>(stageWidth_),(0.5f-clip.Y/clip.W*0.5f)*static_cast<float>(stageHeight_));
        };
        floor_=project(Xna::Vector3::Zero);
        floorRadius_=std::fabs(project(Xna::Vector3(0.32f,0,0)).X-floor_.X);
        device.SetRenderTarget(target_.get());
        device.Clear(Xna::Color(0,0,0,0));
        const auto bones=animation_->getBoneTransformsProperty();
        shown_->Draw(std::vector<Xna::Matrix>(bones.begin(),bones.end()),animation_->getExpressionProperty());
        device.SetRenderTarget(nullptr);
        stageDrawn_=true;
    }

    void draw(Ui& ui,const Box& area) override {
        tick();
        const auto layout=layoutFor(area.w,area.h,ui.style.scale());
        stageWidth_=static_cast<int>(std::lround(layout.stage.w));
        stageHeight_=static_cast<int>(std::lround(layout.stage.h));
        const double dt=lastDraw_<0?0.0:std::clamp(ui.time-lastDraw_,0.0,0.1);
        lastDraw_=ui.time;
        yaw_+=look_.X*static_cast<float>(dt)*2.4f;
        zoom_=std::clamp(zoom_-look_.Y*static_cast<float>(dt)*1.1f,0.55f,1.6f);
        follow();
        hits_.clear();

        // Backdrop: a deep slate room, a soft light on the stage.
        ui.style.gradient(ui.batch,area,Xna::Color(44,54,74),Xna::Color(13,16,23));
        const auto& stage=layout.stage;
        ui.style.shadow(ui.batch,Box{stage.x+stage.w*0.16f,stage.h*0.10f,stage.w*0.68f,stage.h*0.70f},ui.px(260),ui.px(170),Xna::Color(40,52,74,0));
        drawStage(ui,stage);
        drawHeader(ui,stage);
        const Box right{layout.rail.x,layout.rail.y,layout.content.right()-layout.rail.x,layout.rail.h};
        if(stage_==Stage::Failed) {
            ui.style.rounded(ui.batch,right,ui.px(18),Palette::panel());
            emptyState(ui,right.inset(ui.px(24)),Icon::Warning,failTitle_,failText_);
        } else if(!model_) {
            ui.style.rounded(ui.batch,right,ui.px(18),Palette::panel());
            loading(ui,right,"Loading your avatar");
        } else {
            drawRail(ui,layout.rail);
            drawContent(ui,layout.content);
        }
        // Footer: how to turn the avatar under the stage and the buttons under the choices;
        // category switching sits over the category column.
        const float footer=area.bottom()-ui.px(38);
        const char* turn=ui.input.device==Device::Pad?"Turn: right stick   Zoom: triggers":ui.input.device==Device::Mouse?"Turn: drag the avatar   Zoom: wheel":
                         "Turn: Z / C   Zoom: + / -";
        if(stage_==Stage::Editing) {
            ui.style.text(ui.batch,Font::Caption,turn,Xna::Vector2(stage.x+ui.px(40),footer-ui.px(10)),Palette::faint());
            drawHints(ui,{{Command::Previous,""},{Command::Next,"Category"}},Xna::Vector2(layout.rail.x+ui.px(6),layout.rail.y-ui.px(24)),false);
        }
        drawHints(ui,hints(),Xna::Vector2(area.right()-ui.px(28),footer),true);
    }

    void save(bool thenClose) {
        if(stage_!=Stage::Editing||!model_)return;
        closeAfterSave_=thenClose;
        if(!model_->differsFromStored()) {
            if(thenClose)close();
            else note_="Nothing to save.";
            return;
        }
        const auto bytes=bytesOf(model_->descriptor());
        if(userId_.empty()) {
            if(setLocalProfileAvatar(gamertag_,bytes))saved(bytes,0);
            else inform(player,"Avatar not saved","The profile store on this computer could not be written.",Icon::Error);
            return;
        }
        stage_=Stage::Saving;
        load<SaveResult>(self_.lock(),[user=userId_,bytes](IGamerServicesBackend& s) {
            SaveResult result;
            try {
                result.revision=s.setAvatar(user,bytes);
            } catch(const ServiceOperationError& error) {
                result.error=error.code=="RATE_LIMITED"?"Your avatar was saved moments ago. Wait a few seconds and save again.":
                             "CNA Gamer Services did not accept this avatar ("+error.code+").";
            } catch(const std::exception&) {
                result.error="CNA Gamer Services could not be reached. Your changes are still here; try again.";
            }
            return result;
        },[bytes](AvatarEditorScreen& screen,SaveResult result) {
            if(result.error.empty()){screen.saved(bytes,result.revision);return;}
            screen.stage_=Stage::Editing;
            inform(screen.player,"Avatar not saved",result.error,Icon::Error);
        },[](AvatarEditorScreen& screen) {
            screen.stage_=Stage::Editing;
            inform(screen.player,"Avatar not saved","CNA Gamer Services could not be reached. Your changes are still here; try again.",Icon::Error);
        });
    }

    void close() {
        auto done=std::move(options_.closed);
        options_.closed=nullptr;
        pop(this);
        if(done)done(savedOnce_);
    }

private:
    enum class Stage : std::uint8_t { Loading, Editing, Saving, Failed };
    struct Hit {
        enum class Kind : std::uint8_t { Category, Option, Slider, Stage } kind;
        std::size_t row=0;
        int index=0;
        Box box;
    };

    // ---- State ---------------------------------------------------------------------------
    void fail(std::string title,std::string text) {
        stage_=Stage::Failed;
        failTitle_=std::move(title);
        failText_=std::move(text);
    }
    void received(LoadedAvatar loaded) {
        if(loaded.record.projected) {
            fail("Avatar catalog needed","Your avatar uses an avatar catalog this computer does not have. Turn on avatar catalog updates to edit it here.");
            return;
        }
        loadedStored_=std::move(loaded.record.description);
        revision_=loaded.record.revision;
        catalogVersion_=loaded.catalog;
        catalogLoad_=Av::loadCatalogAsync(loaded.catalog);
        tick();
    }
    void begin(std::shared_ptr<const Av::CatalogManifest> catalog,std::vector<unsigned char> stored,long long revision) {
        catalog_=std::move(catalog);
        stored_=std::move(stored);
        revision_=revision;
        seed_=options_.seed?options_.seed:std::random_device{}();
        model_=std::make_unique<Av::AvatarEditorModel>(catalog_,std::vector<std::uint8_t>(stored_.begin(),stored_.end()),seed_);
        stage_=Stage::Editing;
        note_=Av::decode(std::vector<std::uint8_t>(stored_.begin(),stored_.end()))?"":"No avatar yet: here is one to start from.";
        checkAt_=clockSeconds()+options_.refreshSeconds;
    }
    void tick() {
        if(catalogLoad_) {
            std::shared_ptr<const Av::CatalogManifest> manifest;
            {
                std::lock_guard guard(catalogLoad_->lock);
                if(!catalogLoad_->done)return;
                manifest=catalogLoad_->manifest;
            }
            catalogLoad_.reset();
            if(manifest)begin(std::move(manifest),std::move(loadedStored_),revision_);
            else fail("Avatar catalog "+std::to_string(catalogVersion_)+" unavailable",
                      "This computer does not have the avatar catalog your account uses and could not install it. "
                      "Check the connection and the avatar catalog update setting.");
            return;
        }
        if(stage_!=Stage::Editing||checking_||clockSeconds()<checkAt_)return;
        checkAt_=clockSeconds()+options_.refreshSeconds;
        // The stored avatar may change while the editor is open (another computer, another game).
        if(userId_.empty()) {
            auto bytes=localProfileAvatar(gamertag_);
            if(bytes!=stored_)external(std::move(bytes),0);
            return;
        }
        checking_=true;
        load<ServiceAvatarRecord>(self_.lock(),[user=userId_](IGamerServicesBackend& s) {
            auto records=s.avatars({user});
            return records.empty()?ServiceAvatarRecord{}:std::move(records.front());
        },[](AvatarEditorScreen& screen,ServiceAvatarRecord record) {
            screen.checking_=false;
            if(!record.projected&&record.revision!=0&&record.revision!=screen.revision_&&screen.stage_==Stage::Editing)
                screen.external(std::move(record.description),record.revision);
        },[](AvatarEditorScreen& screen){screen.checking_=false;});
    }
    void external(std::vector<unsigned char> stored,long long revision) {
        if(revision)revision_=revision;
        if(model_->differsFromStored()) {
            // The player's edits stay; saving replaces what changed elsewhere.
            stored_=std::move(stored);
            note_="Your avatar was changed elsewhere. Saving replaces that change.";
            return;
        }
        const auto category=model_->category();
        stored_=std::move(stored);
        model_=std::make_unique<Av::AvatarEditorModel>(catalog_,std::vector<std::uint8_t>(stored_.begin(),stored_.end()),seed_);
        model_->setCategory(category);
        row_=std::min(row_,model_->fieldCount()?model_->fieldCount()-1:0);
        option_=currentOption(row_);
        note_="Your avatar was changed elsewhere; this is the new one.";
    }
    void saved(const std::vector<unsigned char>& bytes,long long revision) {
        stage_=Stage::Editing;
        model_->markSaved();
        stored_=bytes;
        if(revision)revision_=revision;
        savedOnce_=true;
        note_.clear();
        celebrate_=true;
        // The rail and every portrait show the new avatar at once.
        identity(player).avatar=bytes;
        notify({Notification::Kind::Info,"Avatar saved",userId_.empty()?"Saved to this profile":"Saved to your CNA account",bytes});
        if(closeAfterSave_)close();
    }
    void leave() {
        if(model_&&model_->differsFromStored())push(std::make_shared<LeaveEditorScreen>(self_));
        else close();
    }

    // ---- Navigation ----------------------------------------------------------------------
    void setCategory(int index) {
        model_->setCategory(Categories[static_cast<std::size_t>(index)].category);
        strips_.clear();
        row_=0;
        option_=currentOption(0);
    }
    void turnCategory(int delta) {
        const int count=Av::EditorCategoryCount;
        setCategory((static_cast<int>(model_->category())+delta+count)%count);
        if(content_&&model_->fieldCount()==0)content_=false;
    }
    void enterContent(std::size_t row) {
        content_=true;
        row_=row;
        option_=currentOption(row);
    }
    int currentOption(std::size_t row) const {
        if(!model_||row>=model_->fieldCount())return 0;
        const auto options=model_->options(row);
        const auto current=std::ranges::find_if(options,&Av::EditorOption::current);
        return current==options.end()?0:static_cast<int>(current-options.begin());
    }
    void choose(std::size_t row,int option) {
        model_->choose(row,static_cast<std::size_t>(option));
        note_.clear();
        chosenAt_=lastDraw_;
    }
    void step(int delta) {
        model_->selectRow(row_);
        model_->adjust(delta);
        note_.clear();
    }
    // Sets a slider row to the position nearest a point on its track.
    void setSlider(std::size_t row,float position) {
        model_->selectRow(row);
        for(int i=0;i<64;++i) {
            const float current=model_->field(row).slider.value_or(0.0f);
            if(std::fabs(position-current)<0.035f)break;
            const int direction=position>current?1:-1;
            if(!model_->adjust(direction))break;
            const float after=model_->field(row).slider.value_or(0.0f);
            if((position-after)*static_cast<float>(direction)<0) {
                if(std::fabs(position-after)>std::fabs(position-current))model_->adjust(-direction);
                break;
            }
        }
        note_.clear();
    }
    void pointer(InputContext& ui) {
        const auto mouse=ui.input.mouse;
        if(dragging_) {
            if(!ui.input.mouseDown)dragging_=false;
            else {yaw_+=(mouse.X-dragFrom_.X)*0.012f;dragFrom_=mouse;}
        }
        if(!ui.input.mouseDown)sliderDrag_=-1;
        for(const auto& hit:hits_) {
            const bool inside=hit.box.contains(mouse.X,mouse.Y);
            if(hit.kind==Hit::Kind::Slider&&sliderDrag_==static_cast<int>(hit.row)&&ui.input.mouseDown) {
                setSlider(hit.row,std::clamp((mouse.X-hit.box.x)/hit.box.w*2.0f-1.0f,-1.0f,1.0f));
                continue;
            }
            if(!inside)continue;
            switch(hit.kind) {
            case Hit::Kind::Category:
                if(ui.input.click){setCategory(hit.index);content_=false;}
                break;
            case Hit::Kind::Option:
                if(ui.input.mouseMoved||ui.input.click){content_=true;row_=hit.row;option_=hit.index;}
                if(ui.input.click)choose(hit.row,hit.index);
                break;
            case Hit::Kind::Slider:
                if(ui.input.click) {
                    content_=true;
                    row_=hit.row;
                    sliderDrag_=static_cast<int>(hit.row);
                    setSlider(hit.row,std::clamp((mouse.X-hit.box.x)/hit.box.w*2.0f-1.0f,-1.0f,1.0f));
                }
                break;
            case Hit::Kind::Stage:
                if(ui.input.click){dragging_=true;dragFrom_=mouse;}
                if(ui.input.wheel)zoom_=std::clamp(zoom_-0.08f*static_cast<float>(ui.input.wheel),0.55f,1.6f);
                break;
            }
        }
    }

    // ---- Preview -------------------------------------------------------------------------
    // What the stage shows: the focused choice tried on, or the avatar as edited.
    std::vector<unsigned char> wanted() const {
        if(!model_)return {};
        if(content_&&row_<model_->fieldCount()&&model_->field(row_).kind!=EditorRowKind::Slider) {
            const auto options=model_->options(row_);
            if(option_<static_cast<int>(options.size())&&!options[static_cast<std::size_t>(option_)].current)
                return bytesOf(options[static_cast<std::size_t>(option_)].result);
        }
        return bytesOf(model_->descriptor());
    }
    void follow() {
        const auto want=wanted();
        if(!want.empty()&&want!=pendingBytes_&&!(pending_==nullptr&&want==shownBytes_)&&want!=failedBytes_) {
            // A description still loading is dropped for the newer one; the shown one keeps drawing.
            pendingDescription_=std::make_unique<GS::AvatarDescription>(std::vector<SharpRuntime::bytecs>(want.begin(),want.end()));
            pending_=std::make_unique<GS::AvatarRenderer>(pendingDescription_.get(),false);
            pendingBytes_=want;
        }
        if(!pending_)return;
        const auto state=pending_->getStateProperty();
        if(state==GS::AvatarRendererState::Ready) {
            shown_=std::move(pending_);
            shownDescription_=std::move(pendingDescription_);
            shownBytes_=std::move(pendingBytes_);
            pendingBytes_.clear();
        } else if(state==GS::AvatarRendererState::Unavailable) {
            failedBytes_=std::move(pendingBytes_);
            pending_.reset();
            pendingDescription_.reset();
            pendingBytes_.clear();
        }
    }
    void play(GS::AvatarAnimationPreset preset,bool once) {
        animation_=std::make_unique<GS::AvatarAnimation>(preset);
        once_=once;
        idle_=0;
    }
    void animate(double dt) {
        // A wave hello, the standing idle with a small gesture now and then, a cheer on saving.
        if(!animation_)play(reducedMotion()?GS::AvatarAnimationPreset::Stand0:GS::AvatarAnimationPreset::Wave,!reducedMotion());
        if(celebrate_) {
            celebrate_=false;
            if(!reducedMotion())play(GS::AvatarAnimationPreset::Celebrate,true);
        }
        animation_->Update(System::TimeSpan::FromTicks(static_cast<SharpRuntime::longcs>(dt*1.0e7)),!once_);
        if(once_&&animation_->getCurrentPositionProperty().getTotalSecondsProperty()>=animation_->getLengthProperty().getTotalSecondsProperty()) {
            play(GS::AvatarAnimationPreset::Stand0,false);
            return;
        }
        if(!once_&&!reducedMotion()&&(idle_+=dt)>12.0) {
            using P=GS::AvatarAnimationPreset;
            static constexpr P female[]{P::FemaleIdleLookAround,P::FemaleIdleShiftWeight,P::FemaleIdleCheckNails};
            static constexpr P male[]{P::MaleIdleLookAround,P::MaleIdleShiftWeight,P::MaleIdleCheckHand};
            const bool masculine=model_&&model_->descriptor().bodyType==1;
            play((masculine?male:female)[idleTurn_++%3],true);
        }
    }

    // ---- Drawing -------------------------------------------------------------------------
    void drawStage(Ui& ui,const Box& stage) {
        hits_.push_back({Hit::Kind::Stage,0,0,Box{stage.x,ui.px(130),stage.w,stage.h-ui.px(200)}});
        if(stageDrawn_&&target_) {
            // A soft pool of light on the floor, the avatar's shadow in it.
            const float r=std::max(floorRadius_,ui.px(30));
            const Xna::Vector2 f(stage.x+floor_.X,stage.y+floor_.Y);
            ui.style.shadow(ui.batch,Box{f.X-r*2.2f,f.Y-r*0.42f,r*4.4f,r*0.84f},r*0.42f,ui.px(30),Xna::Color(30,38,54,0));
            ui.style.shadow(ui.batch,Box{f.X-r,f.Y-r*0.17f,r*2.0f,r*0.34f},r*0.17f,ui.px(14),Xna::Color(0,0,0,130));
            ui.batch.Draw(*target_,Xna::Vector2(stage.x,stage.y),Xna::Color::White);
        } else if(stage_!=Stage::Failed) {
            loading(ui,stage,"");
        }
    }

    void drawHeader(Ui& ui,const Box& stage) {
        const float x=stage.x+ui.px(40);
        ui.style.text(ui.batch,Font::Title,"Avatar",Xna::Vector2(x,ui.px(26)),Palette::text());
        if(!gamertag_.empty())
            ui.style.text(ui.batch,Font::Body,gamertag_+"  \xe2\x80\xa2  "+(userId_.empty()?"Offline profile":"CNA account"),
                Xna::Vector2(x,ui.px(72)),Palette::muted());
        // Where the avatar stands: saved, changed, saving.
        std::string chip;
        Xna::Color dot=Palette::online();
        if(stage_==Stage::Saving){chip="Saving";dot=Palette::accent();}
        else if(stage_==Stage::Editing&&model_->differsFromStored()){chip="Unsaved changes";dot=Palette::accent();}
        else if(stage_==Stage::Editing)chip=savedOnce_?"Saved":"Up to date";
        if(!chip.empty()) {
            const float cx=x+ui.style.measure(Font::Title,"Avatar").X+ui.px(18);
            const float w=ui.style.measure(Font::Caption,chip).X+ui.px(40);
            const Box box{cx,ui.px(36),w,ui.px(28)};
            ui.style.rounded(ui.batch,box,ui.px(14),Palette::surface());
            ui.style.disc(ui.batch,Xna::Vector2(box.x+ui.px(15),box.y+box.h/2),ui.px(5),dot);
            ui.style.text(ui.batch,Font::Caption,chip,Xna::Vector2(box.x+ui.px(27),box.y+ui.px(4)),Palette::text());
        }
        if(!note_.empty())
            for(std::size_t i=0;const auto& line:ui.style.wrap(Font::Caption,note_,stage.w-ui.px(80)))
                ui.style.text(ui.batch,Font::Caption,line,Xna::Vector2(x,ui.px(104)+static_cast<float>(i++)*ui.px(22)),Palette::accent());
    }

    void drawRail(Ui& ui,const Box& rail) {
        ui.style.shadow(ui.batch,rail,ui.px(18),ui.px(20),Xna::Color(0,0,0,110));
        ui.style.rounded(ui.batch,rail,ui.px(18),Palette::panel());
        const float rowH=std::min(ui.px(44),(rail.h-ui.px(16))/static_cast<float>(std::size(Categories)));
        float y=rail.y+ui.px(8);
        for(int index=0;const auto& entry:Categories) {
            const Box row{rail.x+ui.px(8),y,rail.w-ui.px(16),rowH-ui.px(2)};
            const bool current=entry.category==model_->category();
            if(current&&!content_)rowBackground(ui,row,true);
            else if(current) {
                ui.style.rounded(ui.batch,row,ui.px(12),Palette::surface());
                ui.style.rounded(ui.batch,Box{row.x,row.y+ui.px(9),ui.px(4),row.h-ui.px(18)},ui.px(2),Palette::accent());
            }
            const float s=ui.px(22);
            ui.style.icon(ui.batch,entry.icon,Box{row.x+ui.px(14),row.y+(row.h-s)/2,s,s},current?Palette::accent():Palette::muted());
            const float h=ui.style.measure(Font::Body,"Ag").Y;
            ui.style.text(ui.batch,current?Font::BodyBold:Font::Body,Av::editorCategoryName(entry.category),
                Xna::Vector2(row.x+ui.px(46),row.y+(row.h-h)/2),current?Palette::text():Palette::muted());
            hits_.push_back({Hit::Kind::Category,0,index++,row});
            y+=rowH;
        }
    }

    float rowHeight(Ui& ui,const Av::EditorField& field,std::size_t options,float width) const {
        const float label=ui.px(30);
        switch(field.kind) {
        case EditorRowKind::Cards:
        case EditorRowKind::Presets: return label+cardWidth(ui,width)+ui.px(30)+ui.px(16);
        case EditorRowKind::Swatches: {
            const int columns=swatchColumns(ui,width);
            const int rows=(static_cast<int>(options)+columns-1)/columns;
            return label+static_cast<float>(rows)*ui.px(48)+ui.px(10);
        }
        case EditorRowKind::Slider: return ui.px(72);
        }
        return label;
    }
    float cardWidth(Ui& ui,float width) const {return (width-ui.px(10)*(VisibleCards-1))/VisibleCards;}
    int swatchColumns(Ui& ui,float width) const {return std::max(1,static_cast<int>((width+ui.px(10))/ui.px(48)));}

    void drawContent(Ui& ui,const Box& content) {
        ui.style.shadow(ui.batch,content,ui.px(18),ui.px(20),Xna::Color(0,0,0,110));
        ui.style.rounded(ui.batch,content,ui.px(18),Palette::panel());
        ui.style.text(ui.batch,Font::Heading,Av::editorCategoryName(model_->category()),Xna::Vector2(content.x+ui.px(22),content.y+ui.px(16)),
            Palette::text());
        const Box inner{content.x+ui.px(18),content.y+ui.px(62),content.w-ui.px(36),content.h-ui.px(74)};
        const auto count=model_->fieldCount();
        if(count==0) {
            emptyState(ui,inner,Icon::Face,"Nothing to change here","The avatar catalog in use has no choices in this category.");
            return;
        }
        // Rows stack; the list scrolls to keep the focused row whole.
        std::vector<float> tops,heights;
        float y=0;
        for(std::size_t row=0;row<count;++row) {
            const auto field=model_->field(row);
            tops.push_back(y);
            heights.push_back(rowHeight(ui,field,model_->options(row).size(),inner.w));
            y+=heights.back()+ui.px(8);
        }
        if(content_) {
            if(tops[row_]<scroll_)scroll_=tops[row_];
            if(tops[row_]+heights[row_]>scroll_+inner.h)scroll_=tops[row_]+heights[row_]-inner.h;
        } else {
            scroll_=0;
        }
        for(std::size_t row=0;row<count;++row) {
            const Box box{inner.x,inner.y+tops[row]-scroll_,inner.w,heights[row]};
            if(box.y<inner.y-1||box.bottom()>inner.bottom()+1)continue;
            drawRow(ui,box,row);
        }
        if(scroll_>0)ui.style.icon(ui.batch,Icon::ChevronLeft,Box{content.right()-ui.px(40),content.y+ui.px(22),ui.px(18),ui.px(18)},Palette::faint());
    }

    void drawRow(Ui& ui,const Box& box,std::size_t row) {
        const auto field=model_->field(row);
        const auto options=model_->options(row);
        const bool focusedRow=content_&&row==row_;
        if(focusedRow&&field.kind==EditorRowKind::Slider)rowBackground(ui,box.inset(-ui.px(6)),true);
        // The row's name, and what it is set to (or what is being tried).
        std::string value=field.value;
        if(focusedRow&&field.kind!=EditorRowKind::Slider&&option_<static_cast<int>(options.size())&&!options[static_cast<std::size_t>(option_)].current)
            value="Trying "+optionName(options,option_);
        ui.style.text(ui.batch,Font::BodyBold,field.label,Xna::Vector2(box.x,box.y),focusedRow?Palette::text():Palette::muted());
        ui.style.text(ui.batch,Font::Caption,value,Xna::Vector2(box.right(),box.y+ui.px(3)),focusedRow?Palette::accent():Palette::muted(),Align::Right);
        const float top=box.y+ui.px(30);
        switch(field.kind) {
        case EditorRowKind::Cards:
        case EditorRowKind::Presets: {
            const float cw=cardWidth(ui,box.w),ch=cw+ui.px(30);
            const int total=static_cast<int>(options.size());
            // The strip scrolls to keep the focused card in view.
            int& first=strips_[row];
            const int focus=focusedRow?option_:currentOption(row);
            first=std::clamp(first,std::max(0,focus-VisibleCards+1),focus);
            first=std::clamp(first,0,std::max(0,total-VisibleCards));
            const Framing framing=field.kind==EditorRowKind::Presets?Framing::Head:model_->category()==EditorCategory::Body?Framing::Body:
                                  framingOf(model_->view());
            for(int i=first;i<std::min(total,first+VisibleCards);++i) {
                const auto& option=options[static_cast<std::size_t>(i)];
                const Box card{box.x+static_cast<float>(i-first)*(cw+ui.px(10)),top,cw,ch};
                const bool focused=focusedRow&&i==option_;
                const float pulse=focused&&chosenAt_>=0&&lastDraw_-chosenAt_<0.25&&!reducedMotion()?ui.px(3):0.0f;
                if(focused)ui.style.rounded(ui.batch,card.inset(-ui.px(3)-pulse),ui.px(14),Palette::accent());
                ui.style.rounded(ui.batch,card,ui.px(12),Palette::surface());
                portrait(ui,bytesOf(option.result),Box{card.x+ui.px(4),card.y+ui.px(4),cw-ui.px(8),cw-ui.px(8)},framing);
                ui.style.text(ui.batch,Font::Caption,ui.style.fit(Font::Caption,option.label,cw-ui.px(4)),
                    Xna::Vector2(card.x+cw/2,card.y+cw-ui.px(1)),focused||option.current?Palette::text():Palette::muted(),Align::Center);
                if(option.current) {
                    const Xna::Vector2 badge(card.right()-ui.px(14),card.y+ui.px(14));
                    ui.style.disc(ui.batch,badge,ui.px(11),Palette::accent());
                    ui.style.icon(ui.batch,Icon::Check,Box{badge.X-ui.px(7),badge.Y-ui.px(7),ui.px(14),ui.px(14)},Palette::panel());
                }
                hits_.push_back({Hit::Kind::Option,row,i,card});
            }
            const float mid=top+cw/2-ui.px(9);
            if(first>0)ui.style.icon(ui.batch,Icon::ChevronLeft,Box{box.x-ui.px(17),mid,ui.px(18),ui.px(18)},Palette::muted());
            if(first+VisibleCards<total)ui.style.icon(ui.batch,Icon::ChevronRight,Box{box.right()-ui.px(1),mid,ui.px(18),ui.px(18)},Palette::muted());
            break;
        }
        case EditorRowKind::Swatches: {
            const int columns=swatchColumns(ui,box.w);
            const float size=ui.px(36);
            for(int i=0;i<static_cast<int>(options.size());++i) {
                const auto& option=options[static_cast<std::size_t>(i)];
                const Xna::Vector2 c(box.x+size/2+ui.px(2)+static_cast<float>(i%columns)*ui.px(48),top+size/2+ui.px(4)+static_cast<float>(i/columns)*ui.px(48));
                const bool focused=focusedRow&&i==option_;
                if(focused)ui.style.disc(ui.batch,c,size/2+ui.px(6),Palette::accent());
                else if(option.current)ui.style.disc(ui.batch,c,size/2+ui.px(4),Palette::text());
                if(focused||option.current)ui.style.disc(ui.batch,c,size/2+ui.px(2),Palette::panel());
                const auto color=option.color.value_or(Av::AvatarColor{});
                ui.style.disc(ui.batch,c,size/2,Xna::Color(color.r,color.g,color.b));
                hits_.push_back({Hit::Kind::Option,row,i,Box{c.X-size/2-ui.px(4),c.Y-size/2-ui.px(4),size+ui.px(8),size+ui.px(8)}});
            }
            break;
        }
        case EditorRowKind::Slider: {
            const float x0=box.x+ui.px(10),x1=box.right()-ui.px(10),ty=box.y+ui.px(48);
            const float position=field.slider.value_or(0.0f);
            const float mid=(x0+x1)/2,knob=mid+position*(x1-x0)/2;
            ui.style.rounded(ui.batch,Box{x0,ty-ui.px(3),x1-x0,ui.px(6)},ui.px(3),Palette::surface());
            ui.style.fill(ui.batch,Box{mid-ui.px(1),ty-ui.px(9),ui.px(2),ui.px(18)},Palette::faint());
            ui.style.rounded(ui.batch,Box{std::min(mid,knob),ty-ui.px(3),std::fabs(knob-mid),ui.px(6)},ui.px(3),focusedRow?Palette::accent():Palette::muted());
            ui.style.disc(ui.batch,Xna::Vector2(knob,ty),ui.px(11),focusedRow?Palette::accent():Palette::text());
            ui.style.disc(ui.batch,Xna::Vector2(knob,ty),ui.px(4),Palette::panel());
            hits_.push_back({Hit::Kind::Slider,row,0,Box{x0,ty-ui.px(16),x1-x0,ui.px(32)}});
            break;
        }
        }
    }

    static std::string optionName(const std::vector<Av::EditorOption>& options,int index) {
        const auto& option=options[static_cast<std::size_t>(index)];
        if(!option.label.empty())return option.label;
        return std::to_string(index+1)+" of "+std::to_string(options.size());
    }

    AvatarEditorOptions options_;
    std::weak_ptr<AvatarEditorScreen> self_;
    Stage stage_=Stage::Loading;
    std::string failTitle_,failText_,note_;
    std::string gamertag_,userId_;
    std::shared_ptr<const Av::CatalogManifest> catalog_;
    std::shared_ptr<Av::CatalogLoad> catalogLoad_;
    std::uint16_t catalogVersion_=0;
    std::vector<unsigned char> stored_,loadedStored_;
    long long revision_=0;
    std::uint32_t seed_=0;
    std::unique_ptr<Av::AvatarEditorModel> model_;
    bool content_=false;
    std::size_t row_=0;
    int option_=0;
    std::map<std::size_t,int> strips_;
    float scroll_=0;
    double checkAt_=0;
    bool checking_=false,closeAfterSave_=false,savedOnce_=false,celebrate_=false;
    double chosenAt_=-1;
    // Pointer.
    std::vector<Hit> hits_;
    bool dragging_=false;
    Xna::Vector2 dragFrom_;
    int sliderDrag_=-1;
    // Stage.
    std::vector<unsigned char> shownBytes_,pendingBytes_,failedBytes_;
    std::unique_ptr<GS::AvatarDescription> shownDescription_,pendingDescription_;
    std::unique_ptr<GS::AvatarRenderer> shown_,pending_;
    std::unique_ptr<GS::AvatarAnimation> animation_;
    bool once_=false;
    double idle_=0;
    int idleTurn_=0;
    std::unique_ptr<Xna::Graphics::RenderTarget2D> target_;
    int stageWidth_=0,stageHeight_=0;
    bool stageDrawn_=false,cameraSet_=false;
    Xna::Vector3 cameraTarget_,cameraOffset_;
    float cameraFov_=0.5f,yaw_=0.3f,zoom_=1.0f;
    Xna::Vector2 look_,floor_;
    float floorRadius_=0;
    double lastRender_=-1,lastDraw_=-1;
};

void LeaveEditorScreen::activate(int index)
{
    auto editor=editor_.lock();
    pop(this);
    if(!editor)return;
    if(index==0)editor->save(true);
    else if(index==1)editor->close();
}
}

std::shared_ptr<Screen> avatarEditorScreen(Xna::PlayerIndex player,AvatarEditorOptions options)
{
    auto screen=std::make_shared<AvatarEditorScreen>(std::move(options));
    screen->player=player;
    screen->start(screen);
    return screen;
}
}
