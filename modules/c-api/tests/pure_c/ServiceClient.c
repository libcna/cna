// SPDX-License-Identifier: MS-PL
#include <CNA/C/cna.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static int checks;
static void require(int valid) {++checks;if(!valid){fprintf(stderr,"C service check %d failed\n",checks);exit(1);}}
static void ok(CNA_Result result){require(result==CNA_RESULT_SUCCESS);}
static void type_text(CNA_Handle game,const char* value) {
    while(*value)ok(cna_text_input_raise_text_input_ext(game,(unsigned char)*value++));
    ok(cna_text_input_raise_text_input_ext(game,13));
}
int main(int argc,char** argv) {
    require(argc==3);
    char password[257];require(fgets(password,sizeof(password),stdin)!=NULL);
    password[strcspn(password,"\r\n")]=0;
    CNA_GameCreateInfo info={sizeof(CNA_GameCreateInfo),1,CNA_TRUE,{0},166667,{"Service C ABI",13},0};
    CNA_Handle game=CNA_INVALID_HANDLE;ok(cna_game_create(&info,&game));
    ok(cna_gamer_services_dispatcher_initialize(game));int32_t count=-1;
    ok(cna_gamer_get_signed_in_gamer_count(&count));require(count==0);
    ok(cna_guide_show_sign_in(1,CNA_TRUE));type_text(game,argv[1]);type_text(game,password);memset(password,0,sizeof(password));
    const time_t deadline=time(NULL)+15;
    while(count==0){ok(cna_gamer_services_dispatcher_update());ok(cna_gamer_get_signed_in_gamer_count(&count));require(time(NULL)<deadline);usleep(1000);}
    require(count==1);
    CNA_SignedInGamerHandle gamer=CNA_INVALID_HANDLE;ok(cna_gamer_get_signed_in_gamer_at(0,&gamer));
    CNA_AchievementCollectionHandle catalog=CNA_INVALID_HANDLE;ok(cna_signed_in_gamer_get_achievements(gamer,&catalog));
    ok(cna_achievement_collection_get_count(catalog,&count));require(count==1);
    CNA_AchievementHandle achievement=CNA_INVALID_HANDLE;ok(cna_achievement_collection_get_at(catalog,0,&achievement));
    CNA_AchievementInfo award={0};award.struct_size=sizeof(award);award.struct_version=1;ok(cna_achievement_get_info(achievement,&award));
    require(award.gamer_score==10&&award.is_earned==(strcmp(argv[2],"earned")==0?CNA_TRUE:CNA_FALSE));
    uint64_t size=0,required=0;ok(cna_achievement_get_picture_size(achievement,&size));require(size>8&&size<512);
    uint8_t picture[512];memset(picture,0x5a,sizeof(picture));
    require(cna_achievement_copy_picture(achievement,picture,size-1,&required)==CNA_RESULT_BUFFER_TOO_SMALL&&required==size&&picture[0]==0x5a);
    ok(cna_achievement_copy_picture(achievement,picture,sizeof(picture),&required));require(required==size&&memcmp(picture,"\x89PNG\r\n\x1a\n",8)==0);
    CNA_GamerProfileHandle profile=CNA_INVALID_HANDLE;ok(cna_gamer_get_profile(gamer,&profile));CNA_Bool present=CNA_FALSE;
    ok(cna_gamer_profile_get_picture_size(profile,&present,&required));require(present==CNA_TRUE&&required==size);
    ok(cna_gamer_profile_copy_picture(profile,picture,sizeof(picture),&required));require(required==size);
    CNA_LeaderboardIdentity board={0};ok(cna_leaderboard_identity_init(CNA_LEADERBOARD_KEY_BEST_SCORE_LIFE_TIME,0,&board));
    CNA_LeaderboardReaderHandle reader=CNA_INVALID_HANDLE;ok(cna_leaderboard_reader_read(&board,0,1,&reader));
    CNA_LeaderboardReaderInfo page={0};page.struct_size=sizeof(page);page.struct_version=1;ok(cna_leaderboard_reader_get_info(reader,&page));
    require(page.total_leaderboard_size==2&&page.entry_count==1&&page.can_page_down==CNA_TRUE);
    CNA_LeaderboardEntryHandle entry=CNA_INVALID_HANDLE;ok(cna_leaderboard_reader_get_entry_at(reader,0,&entry));
    ok(cna_leaderboard_reader_page_down(reader));ok(cna_leaderboard_reader_get_info(reader,&page));require(page.page_start==1&&page.can_page_up==CNA_TRUE);
    ok(cna_leaderboard_reader_destroy(reader));
    CNA_LeaderboardEntryInfo row={0};row.struct_size=sizeof(row);row.struct_version=1;ok(cna_leaderboard_entry_get_info(entry,&row));require(row.rating==200);
    CNA_GamerHandle remote=CNA_INVALID_HANDLE;ok(cna_leaderboard_entry_get_gamer(entry,&present,&remote));require(present==CNA_TRUE&&remote!=CNA_INVALID_HANDLE);
    uint64_t name_size=0;require(cna_gamer_copy_gamertag(remote,NULL,0,&name_size)==CNA_RESULT_BUFFER_TOO_SMALL&&name_size==3);
    char name[8]={0};ok(cna_gamer_copy_gamertag(remote,name,sizeof(name),&name_size));require(name_size==3&&memcmp(name,"Bob",3)==0);
    ok(cna_gamer_destroy(remote));ok(cna_leaderboard_entry_destroy(entry));
    ok(cna_gamer_profile_destroy(profile));ok(cna_achievement_destroy(achievement));ok(cna_achievement_collection_destroy(catalog));
    ok(cna_signed_in_gamer_destroy(gamer));ok(cna_game_destroy(game));
    printf("%d pure-C service checks passed\n",checks);return 0;
}
