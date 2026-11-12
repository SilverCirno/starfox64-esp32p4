#include "global.h"
#include "ship_api.h"
#include <stddef.h>
#include <string.h>
#include "host.h"

extern void Main_Initialize(void);
extern void Main_InitMesgQueues(void);
extern void Graphics_InitializeTask(u32 frame);
extern void Controller_Init(void);
extern void Controller_ReadData(void);
extern void Controller_UpdateInput(void);
extern void AudioSeq_InitSequencePlayers(void);
extern bool gGoToTitle;
extern OSContPad sNextController[4];
extern void Map_Setup_Menu(void);
extern void Map_PlayLevel(void);

void sf64_ship_begin(void) {
    _Static_assert(sizeof(Gfx)==8,"Starship RV32 Gfx ABI mismatch");
    _Static_assert(sizeof(Mtx)==64,"Starship float matrix ABI mismatch");
    _Static_assert(sizeof(OSMesg)==4,"Starship RV32 message ABI mismatch");
    _Static_assert(sizeof(Environment)==0x44,"ROM environment layout mismatch");
    _Static_assert(sizeof(ObjectInit)==20,"ROM object placement layout mismatch");
    _Static_assert(offsetof(ObjectInit,id)==16,"ROM object ID offset mismatch");
    Main_Initialize();
    Main_InitMesgQueues();
    Controller_Init();
    // Establish real disabled-channel sentinels even without an audio sink.
    // Radio queries these sequence players during Corneria's opening dialogue.
    AudioSeq_InitSequencePlayers();
    // Game drawing can cancel SFX even during boot. Initialize the original
    // bank linked-list terminators even while the hardware audio sink is absent.
    Audio_InitSounds();
    Game_Initialize();
    // Use Starship's existing return-to-title path for this title-first milestone.
    gGoToTitle=true;
}

const void *sf64_ship_step(unsigned *command_count) {
    sf64_ship_timer_poll();
    Graphics_InitializeTask(gSysFrameCount);
    // Starship extends OSContPad with gyro/right-stick fields. Populate that
    // actual layout rather than calling the reference N64 ABI's 6-byte bridge.
    memset(sNextController,0,sizeof(sNextController));
    if(gControllerLock) --gControllerLock;
    else sf64_host_controller(&sNextController[0].button,&sNextController[0].stick_x,&sNextController[0].stick_y);
    for(unsigned i=1;i<4;++i) sNextController[i].err_no=CONT_NO_RESPONSE_ERROR;
    Controller_UpdateInput();
    if(gControllerPress[0].button) printf("Starship input: pressed=%04x lock=%u\n",gControllerPress[0].button,gControllerLock);
    // Development shortcut for the requested first-level milestone. Reuse the
    // original new-game initialization and real level transition; no scripted
    // replacement scene or substitute gameplay is involved.
    if((gGameState==GSTATE_TITLE || gGameState==GSTATE_MENU || gGameState==GSTATE_MAP) &&
       (gControllerPress[0].button&Z_TRIG)) {
        Map_Setup_Menu();
        Map_PlayLevel();
        printf("Starship: original Corneria quick start requested\n");
    }
    __gSPSegment(gUnkDisp1++,0,0);
    __gSPDisplayList(gMasterDisp++,gGfxPool->unkDL1);
    Game_Update();
    gSPEndDisplayList(gUnkDisp1++);
    gSPEndDisplayList(gUnkDisp2++);
    __gSPDisplayList(gMasterDisp++,gGfxPool->unkDL2);
    gDPFullSync(gMasterDisp++);
    gSPEndDisplayList(gMasterDisp++);
    *command_count=(unsigned)(gMasterDisp-gGfxPool->masterDL);
    ++gSysFrameCount;
    return gGfxPool->masterDL;
}
int sf64_ship_state(void) { return gGameState; }
void sf64_ship_gameplay_stats(Sf64GameplayStats *stats) {
    memset(stats,0,sizeof(*stats)); stats->player_state=-1;
    if(gGameState!=GSTATE_PLAY) return;
    stats->play_state=gPlayState; stats->frame=gGameFrameCount; stats->path=gPathProgress;
    if(gPlayState==PLAY_UPDATE && gPlayer) {
        stats->player_state=gPlayer[0].state; stats->x=gPlayer[0].pos.x; stats->y=gPlayer[0].pos.y;
        for(unsigned i=0;i<ARRAY_COUNT(gPlayerShots);++i) if(gPlayerShots[i].obj.status==SHOT_ACTIVE) ++stats->shots;
    }
}
