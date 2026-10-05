// Native crossover: original Minecraft PNG assets on local block geometry.
// The original player controller, camera, water simulation and FLUDD remain.
#include "minecraft.h"
#include "building.h"
#include "assets.h"
#include "sound.h"
#include "nozzle_preview.h"
#include <Player/Mario.hpp>
#include <Player/WaterGun.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DShape.hpp>
#include <MoveBG/MapObjTree.hpp>
#include <MoveBG/Item.hpp>
#include <MoveBG/ItemManager.hpp>
#include <System/Application.hpp>
#include <System/FlagManager.hpp>
#include <MSound/MSound.hpp>
#include <MSound/MSoundSE.hpp>
#include <Enemy/Conductor.hpp>
#include <Enemy/EnemyManager.hpp>
#include <Enemy/SmallEnemy.hpp>
#include <Camera/Camera.hpp>
#include <Map/MapCollisionManager.hpp>
#include <Map/MapData.hpp>
#include <Map/Map.hpp>
#include <System/MarDirector.hpp>
#include <JSystem/JDrama/JDRGraphics.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DSys.hpp>
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
using namespace minecraft;
State game;
Inventory& inventory=game.inventory;
bool loaded,saveBlocked,dirty,shiftHeld;
float saveRetry,mouseX=320,mouseY=240;
float chestLid[WorldCapacity];
int blockTarget=-1,blockFace=-1;
struct Point { float x,y,z; };
Point previousPosition;bool previousValid;
extern "C" int sms_gx_mouse_to_hud(int,int,float*,float*) __attribute__((weak));
extern "C" void GXPC_SetHud(int);
extern "C" void GXPC_HudPaneBegin(float,float);
extern "C" void GXPC_HudPaneEnd(void);
extern "C" float GXPC_GetWidescreen(void);
struct Tree { TMapObjTree* actor; bool felled; };
Tree trees[128];
int treeCount;
TreeBreak treeBreak,blockBreak;
SwordSwing swordSwing;
Point crackCenter,crackNormal;
float miningSound;
int crackMeshDraws;
WorldDrop (&drops)[DropCapacity]=game.drops;
struct PickupFlight {Item item;Point start;float seconds;bool active;} pickupFlights[64];
struct Particle {Point position,velocity;float age,life;Item item;int u,v;bool active;} particles[256];
Point aimOrigin,aimDirection,aimHit;bool aimGround;
bool showHelp,showPlacement=true;
bool buildMode,buildPointerReady;
float buildYaw,buildPitch;
int achievementQueue[AchievementCount],achievementQueued;float achievementTime;float inventoryHintTime=-2.5f;
unsigned particleRandom=0x6d696e65;
TMario* player;
bool attackHeld;
TWaterGun* forcedGun;TWaterGun* readyGun;int restoreNozzle,restoreSecond,forcedAttachment=-1;
float cooldown, swing, walkPhase,hurtFlash,swordHitFlash;
float messageTime;
const char* message = "";
OSTime lastTime;
int target = -1;
int enabledCache = -1;
float radians(s16 angle) { return angle * (3.14159265358979323846f / 32768.0f); }
s16 shortAngle(float angle){return s16(int(angle*(32768.f/3.14159265358979323846f)));}
void clampBuildPitch(){if(buildPitch<-.15f)buildPitch=-.15f;if(buildPitch>1.1f)buildPitch=1.1f;buildYaw=fmodf(buildYaw,6.2831853f);}
bool playing() {
    return player && gpMarDirector && gpMarDirector->mState == TMarDirector::STATE_UNK4
        && !gpMarDirector->isTalkModeNow() && !gpMarDirector->isDemoModeNow()
        && player->mFreezeTimer <= 0;
}
void tell(const char* text) { message = text; messageTime = 2.5f; }
void toggleBuildMode() {
    buildMode=!buildMode;buildPointerReady=false;
    if(buildMode&&gpCamera) {
        const JGeometry::TVec3<float>& eye=gpCamera->unk124;
        const JGeometry::TVec3<float>& at=gpCamera->unk148;
        float dx=at.x-eye.x,dz=at.z-eye.z;
        buildYaw=atan2f(dx,dz);buildPitch=atan2f(eye.y-at.y,sqrtf(dx*dx+dz*dz));
        if(buildPitch<.35f)buildPitch=.35f;clampBuildPitch();
    }
    tell(buildMode?"BUILD MODE ON - B TO EXIT":"BUILD MODE OFF");
}
void persist();
void unlock(Achievement achievement) {
    if(!game.unlock(achievement))return;
    if(achievementQueued<AchievementCount)achievementQueue[achievementQueued++]=achievement;
    if(achievementQueued==1)achievementTime=0;
    persist();OSReport("[minecraft] achievement unlocked: %d\n",achievement);
}
unsigned randomParticle(){particleRandom^=particleRandom<<13;particleRandom^=particleRandom>>17;particleRandom^=particleRandom<<5;return particleRandom;}
void spawnParticles(Item item,const Point& point,int count) {
    for(int i=0;i<256&&count>0;++i)if(!particles[i].active) {
        Particle& p=particles[i];p.active=true;p.position=point;p.item=item;
        p.velocity.x=int(randomParticle()%121)-60;p.velocity.y=40+randomParticle()%100;p.velocity.z=int(randomParticle()%121)-60;
        p.age=0;p.life=.3f+(randomParticle()%40)*.01f;p.u=randomParticle()%13;p.v=randomParticle()%13;--count;
    }
}
void pickupFlight(Item item,float x,float y,float z) {
    for(int i=0;i<64;++i)if(!pickupFlights[i].active){pickupFlights[i].active=true;pickupFlights[i].item=item;pickupFlights[i].start=(Point){x,y,z};pickupFlights[i].seconds=0;return;}
}
// Apply only after the director, player and native FLUDD finish setup.
void updateNozzleEquipment(TMario* mario) {
    if(!mario->checkFlag(MARIO_FLAG_HAS_FLUDD)||mario->mWaterGun!=readyGun)return;
    TWaterGun* gun=mario->mWaterGun;
    const int attachment=nozzleType(game.chestArmor.item);
    if(gun&&attachment>=0) {
        if(forcedGun!=gun) {forcedGun=gun;restoreNozzle=gun->mCurrentNozzle;restoreSecond=gun->mSecondNozzle;forcedAttachment=-1;}
        // Preserve Yoshi, diving and other special native modes. Reapply the
        // armor when the player returns to a regular FLUDD attachment.
        if(gun->mCurrentNozzle==0||gun->mCurrentNozzle==1||gun->mCurrentNozzle==4||gun->mCurrentNozzle==5) {
            // Equip a newly selected attachment once. Native switching may
            // then animate freely between Spray and that same attachment.
            if(forcedAttachment!=attachment||(gun->mCurrentNozzle!=0&&gun->mCurrentNozzle!=attachment)) {
                gun->mSwitchToSecondNozzleSpeed=0;
                gun->changeNozzle((TWaterGun::TNozzleType)attachment,true);
                forcedAttachment=attachment;
            }
            gun->mSecondNozzle=attachment;
        }
    }else if(gun&&forcedGun==gun) {
        forcedGun=0;forcedAttachment=-1;
        if(gun->mCurrentNozzle==0||gun->mCurrentNozzle==1||gun->mCurrentNozzle==4||gun->mCurrentNozzle==5) {
            if(restoreNozzle!=0&&restoreNozzle!=1&&restoreNozzle!=4&&restoreNozzle!=5)restoreNozzle=0;
            gun->changeNozzle((TWaterGun::TNozzleType)restoreNozzle,true);
        }
        gun->mSecondNozzle=restoreSecond;gun->mSwitchToSecondNozzleSpeed=0;
    }
}

struct Color { unsigned char r,g,b,a; Color(int R=255,int G=255,int B=255,int A=255):r(R),g(G),b(B),a(A){} };
bool textured;
void solidMode();
void color(Color c, float shade=1.f) { GXColor4u8(c.r*shade,c.g*shade,c.b*shade,c.a); }
void quad(float x,float y,float z,float X,float Y,float Z,int face,Color c,float shade=1.f) {
    // Face ordering: front, back, left, right, top, bottom. Culling is off.
    float v[6][4][3] = {
        {{x,y,Z},{X,y,Z},{X,Y,Z},{x,Y,Z}}, {{X,y,z},{x,y,z},{x,Y,z},{X,Y,z}},
        {{x,y,z},{x,y,Z},{x,Y,Z},{x,Y,z}}, {{X,y,Z},{X,y,z},{X,Y,z},{X,Y,Z}},
        {{x,Y,Z},{X,Y,Z},{X,Y,z},{x,Y,z}}, {{x,y,z},{X,y,z},{X,y,Z},{x,y,Z}}
    };
    for(int i=0;i<4;++i) { GXPosition3f32(v[face][i][0],v[face][i][1],v[face][i][2]); color(c,shade); }
}
void box(float x,float y,float z,float w,float h,float d,Color c) {
    solidMode();
    const float shade[6] = {1,.72f,.8f,.88f,1,.58f};
    GXBegin(GX_QUADS,GX_VTXFMT0,24);
    for(int f=0;f<6;++f) quad(x,y,z,x+w,y+h,z+d,f,c,shade[f]);
    GXEnd();
}
void setup(bool hud) {
    textured=false;
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS,GX_DIRECT); GXSetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    GXSetNumChans(1); GXSetChanCtrl(GX_COLOR0A0,GX_FALSE,GX_SRC_VTX,GX_SRC_VTX,0,GX_DF_NONE,GX_AF_NONE);
    GXSetNumTexGens(0); GXSetNumTevStages(1); GXSetNumIndStages(0);
    GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD_NULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
    GXSetCullMode(GX_CULL_NONE); GXSetZCompLoc(GX_TRUE);
    GXSetZMode(hud ? GX_FALSE : GX_TRUE,GX_LEQUAL,hud ? GX_FALSE : GX_TRUE);
    GXSetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_NOOP);
    GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_OR,GX_ALWAYS,0);
    GXSetColorUpdate(GX_TRUE); GXSetAlphaUpdate(GX_FALSE); GXSetDstAlpha(GX_DISABLE,0);
    GXSetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
    GXSetCurrentMtx(GX_PNMTX0);
}
Mtx view, root, boneWorld;
float worldProjection[7];bool worldViewReady;
void playerRoot(TMario* mario, Mtx result) {
    MTXRotRad(result,'y',radians(mario->mModelFaceAngle));
    result[0][3]=mario->mPosition.x;
    result[1][3]=mario->mPosition.y;
    result[2][3]=mario->mPosition.z;
}
void transform(float x,float y,float z,float angle=0) {
    Mtx local,rotation,result;
    MTXRotRad(rotation,'x',angle);
    MTXTrans(local,x,y,z);
    MTXConcat(local,rotation,local); MTXConcat(root,local,boneWorld); MTXConcat(view,boneWorld,result);
    GXLoadPosMtxImm(result,GX_PNMTX0);
}
void solidMode() {
    if(!textured)return;
    GXSetVtxDesc(GX_VA_TEX0,GX_NONE);GXSetNumTexGens(0);
    GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD_NULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0,GX_PASSCLR);textured=false;
}
void textureMode() {
    if(textured)return;
    static GXTexObj texture;static bool initialized;
    if(!initialized) {
        GXInitTexObj(&texture,(void*)minecraft_assets::Atlas,512,512,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
        GXInitTexObjLOD(&texture,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
        initialized=true;
    }
    GXLoadTexObj(&texture,GX_TEXMAP0);
    GXSetVtxDesc(GX_VA_TEX0,GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
    GXSetNumTexGens(1);GXSetTexCoordGen2(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY,GX_FALSE,GX_PTIDENTITY);
    GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0,GX_MODULATE);textured=true;
}
void texVertex(float x,float y,float z,float u,float v,Color c=Color(),float shade=1.f) {
    GXPosition3f32(x,y,z);color(c,shade);GXTexCoord2f32(u/512.f,v/512.f);
}
void sprite(float x,float y,float w,float h,int u,int v,int tw,int th,Color c=Color()) {
    textureMode();GXBegin(GX_QUADS,GX_VTXFMT0,4);
    texVertex(x,y,0,u,v,c);texVertex(x+w,y,0,u+tw,v,c);
    texVertex(x+w,y+h,0,u+tw,v+th,c);texVertex(x,y+h,0,u,v+th,c);GXEnd();
}
void sprite(float x,float y,const minecraft_assets::Region& r,float scale,Color c=Color()) {
    sprite(x,y,r.w*scale,r.h*scale,r.x,r.y,r.w,r.h,c);
}
void texturedBox(float x,float y,float z,float w,float h,float d,const minecraft_assets::Region faces[6],const Mtx* iconProjection=nullptr,bool hurt=false) {
    textureMode();
    if(hurt) {
        // Minecraft mixes a red overlay into the shaded skin. Keep its
        // texture detail and alpha; equipment/world/UI use their own colors.
        GXSetNumTevStages(2);GXSetTevColor(GX_TEVREG0,(GXColor){255,0,0,77});
        GXSetTevOrder(GX_TEVSTAGE1,GX_TEXCOORD_NULL,GX_TEXMAP_NULL,GX_COLOR_NULL);
        GXSetTevColorIn(GX_TEVSTAGE1,GX_CC_CPREV,GX_CC_C0,GX_CC_A0,GX_CC_ZERO);
        GXSetTevColorOp(GX_TEVSTAGE1,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
        GXSetTevAlphaIn(GX_TEVSTAGE1,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV);
        GXSetTevAlphaOp(GX_TEVSTAGE1,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    }
    float X=x+w,Y=y+h,Z=z+d;
    float vertices[6][4][3]={
        {{x,y,Z},{X,y,Z},{X,Y,Z},{x,Y,Z}},{{X,y,z},{x,y,z},{x,Y,z},{X,Y,z}},
        {{x,y,z},{x,y,Z},{x,Y,Z},{x,Y,z}},{{X,y,Z},{X,y,z},{X,Y,z},{X,Y,Z}},
        {{x,Y,Z},{X,Y,Z},{X,Y,z},{x,Y,z}},{{x,y,z},{X,y,z},{X,y,Z},{x,y,Z}}
    };
    const float shade[6]={1,.78f,.82f,.9f,1,.65f};
    GXBegin(GX_QUADS,GX_VTXFMT0,iconProjection?12:24);
    for(int f=0;f<6;++f) {
        if(iconProjection&&f!=0&&f!=3&&f!=4)continue;
        const minecraft_assets::Region& r=faces[f];
        for(int i=0;i<4;++i) {
            float u=r.x+((i==1||i==2)?r.w:0),v=r.y+(i<2?r.h:0);
            Vec position={vertices[f][i][0],vertices[f][i][1],vertices[f][i][2]};
            if(iconProjection)MTXMultVec(*iconProjection,&position,&position);
            texVertex(position.x,position.y,position.z,u,v,Color(),shade[f]);
        }
    }
    GXEnd();if(hurt)GXSetNumTevStages(1);
}
void body(float x,float y,float z,int w,int h,int d,int part) {
    // Skin UVs: front, back, character right, character left, top, bottom.
    static const int uv[6][6][2]={
        {{8,8},{24,8},{0,8},{16,8},{8,0},{16,0}},
        {{20,20},{32,20},{16,20},{28,20},{20,16},{28,16}},
        {{44,20},{52,20},{40,20},{48,20},{44,16},{48,16}},
        {{4,20},{12,20},{0,20},{8,20},{4,16},{8,16}},
        {{36,52},{44,52},{32,52},{40,52},{36,48},{40,48}},
        {{20,52},{28,52},{16,52},{24,52},{20,48},{24,48}}
    };
    minecraft_assets::Region faces[6];
    for(int f=0;f<6;++f) {
        faces[f].x=minecraft_assets::Steve.x+uv[part][f][0];faces[f].y=uv[part][f][1];
        faces[f].w=(f<2||f>=4)?w:d;faces[f].h=f>=4?d:h;
    }
    texturedBox(x*4.5f,y*4.5f,z*4.5f,w*4.5f,h*4.5f,d*4.5f,faces,nullptr,hurtFlash>0);
}
Color pixel(const unsigned char* pixels,int width,int x,int y) {
    const unsigned char* p=pixels+4*(y*width+x);return Color(p[0],p[1],p[2],p[3]);
}
void logBlock(float x,float y,float z,float size) {
    minecraft_assets::Region faces[6];
    for(int f=0;f<6;++f)faces[f]=f<4?minecraft_assets::Oak:minecraft_assets::OakTop;
    texturedBox(x,y,z,size,size,size,faces);
}
void blockFaces(Item item,minecraft_assets::Region faces[6]) {
    for(int f=0;f<6;++f) {
        faces[f]=item==WoodenLog?(f<4?minecraft_assets::Oak:minecraft_assets::OakTop):minecraft_assets::Planks;
        if(item==CraftingTable)faces[f]=f==4?minecraft_assets::TableTop:f==5?minecraft_assets::Planks:(f<2?minecraft_assets::TableFront:minecraft_assets::TableSide);
    }
}
void chestFaces(minecraft_assets::Region faces[6],int width,int height,int depth,int textureY) {
    // ChestRenderer/ModelPart cube UVs use Y-up, unlike the Steve skin.
    // Keep signed UV spans: chest side rows run from bottom to top.
    const int xs[6]={2*depth+2*width,depth+width,depth,2*depth+width,depth+width,depth};
    for(int f=0;f<6;++f) {
        faces[f].x=minecraft_assets::ChestSkin.x+xs[f];
        faces[f].y=minecraft_assets::ChestSkin.y+textureY+(f<4?depth+height:f==4?depth:0);
        faces[f].w=f<4?-(f<2?width:depth):width;
        faces[f].h=f<4?-height:f==4?-depth:depth;
    }
}
void drawChest(float size,float openness=0,const Mtx* iconProjection=nullptr) {
    const float unit=size/16;
    minecraft_assets::Region faces[6];
    chestFaces(faces,14,10,14,19);
    texturedBox(unit,0,unit,14*unit,10*unit,14*unit,faces,iconProjection);
    Mtx result;
    const bool open=openness>0;
    if(open) {
        const float closed=1-openness;
        const float angle=(1-closed*closed*closed)*-1.5707963f;
        Mtx pivot,rotation;MTXTrans(pivot,0,9*unit,unit);MTXRotRad(rotation,'x',angle);
        MTXConcat(pivot,rotation,pivot);MTXConcat(boneWorld,pivot,result);
        MTXConcat(view,result,result);GXLoadPosMtxImm(result,GX_PNMTX0);
    }
    // The lid overlaps the body by one pixel; the latch shares its hinge.
    chestFaces(faces,14,5,14,0);
    texturedBox(unit,open?0:9*unit,open?0:unit,14*unit,5*unit,14*unit,faces,iconProjection);
    chestFaces(faces,2,4,1,0);
    texturedBox(7*unit,open?-2*unit:7*unit,open?14*unit:15*unit,2*unit,4*unit,unit,faces,iconProjection);
    if(open) {MTXConcat(view,boneWorld,result);GXLoadPosMtxImm(result,GX_PNMTX0);}
}
void drawBlock(Item item,float size,float openness=0) {
    if(item==Chest)drawChest(size,openness);
    else {minecraft_assets::Region faces[6];blockFaces(item,faces);texturedBox(0,0,0,size,size,size,faces);}
}
void heldTool(Item item) {
    Mtx mount,ry,rz,flip,world,result;
    MTXTrans(mount,0,-46,0);
    if(item==IronAxe||item==DiamondSword||item==Stick) {
        // Turn the item plane perpendicular to the torso, then angle it.
        // The grip is the handle's lower pixels, not the image centre.
        // Subtract 45 degrees before the flip to raise the held blade by
        // 45 degrees toward Steve's facing direction, pivoting at the grip.
        MTXRotRad(ry,'y',-1.5707963f);MTXRotRad(rz,'z',-1.0471976f);
        // Preserve the approved flipped orientation and contact with the hand.
        MTXRotRad(flip,'x',3.1415927f);MTXConcat(flip,rz,rz);
        MTXConcat(ry,rz,ry);MTXConcat(mount,ry,mount);
    }
    MTXConcat(boneWorld,mount,world);MTXConcat(view,world,result);GXLoadPosMtxImm(result,GX_PNMTX0);
    if(item==IronAxe||item==DiamondSword||item==Stick) {
        const unsigned char* pixels=item==IronAxe?minecraft_assets::AxePixels:item==Stick?minecraft_assets::StickPixels:minecraft_assets::SwordPixels;
        for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
            Color c=pixel(pixels,16,x,y);
            if(c.a)box((x-1.5f)*3.f,(14.5f-y)*3.f,-1.5f,3,3,3,c);
        }
    }else if(placeable(item)) {
        if(item==WoodenDoor) {
            minecraft_assets::Region faces[6];for(int f=0;f<6;++f)faces[f]=minecraft_assets::DoorBottom;
            texturedBox(-8,-12,0,16,32,3,faces);
        }else drawBlock(item,24);
    }
}
// Reuse the actual trunk geometry, with projected texture coordinates and a
// transparent border. This confines the original crack pixels to an 80x80
// patch while following the bark exactly, even on a tapered or curved palm.
void crackTexture(int stage) {
    const unsigned char* pixels[10]={minecraft_assets::CrackTexture0,minecraft_assets::CrackTexture1,minecraft_assets::CrackTexture2,minecraft_assets::CrackTexture3,minecraft_assets::CrackTexture4,minecraft_assets::CrackTexture5,minecraft_assets::CrackTexture6,minecraft_assets::CrackTexture7,minecraft_assets::CrackTexture8,minecraft_assets::CrackTexture9};
    static GXTexObj textures[10];static bool initialized;
    if(!initialized) {
        for(int i=0;i<10;++i) {
            GXInitTexObj(&textures[i],(void*)pixels[i],32,32,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
            GXInitTexObjLOD(&textures[i],GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
        }
        initialized=true;
    }
    GXLoadTexObj(&textures[stage],GX_TEXMAP0);
}
void drawTreeCracks() {
    crackMeshDraws=0;
    int stage=treeBreak.stage();if(stage<0||treeBreak.target>=treeCount)return;
    Tree& tree=trees[treeBreak.target];if(tree.felled)return;
    J3DModel* model=tree.actor->getModel();if(!model)return;
    J3DModelData* data=model->getModelData();
    crackTexture(stage);
    GXSetNumTexGens(1);
    GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR_NULL);
    GXSetTevOp(GX_TEVSTAGE0,GX_REPLACE);
    GXSetZMode(GX_TRUE,GX_LEQUAL,GX_FALSE);
    GXSetAlphaCompare(GX_GREATER,16,GX_AOP_AND,GX_ALWAYS,0);
    // Multiply the original crack shades into the already-lit native bark.
    GXSetBlendMode(GX_BM_BLEND,GX_BL_DSTCLR,GX_BL_ZERO,GX_LO_NOOP);
    Mtx world,result,projection;MTXCopy(model->getAnmMtx(0),world);
    MTXConcat(view,world,result);result[2][3]+=.1f; // Avoid coplanar depth flicker.
    MTXIdentity(projection);
    for(int k=0;k<3;++k) {
        projection[0][k]=(crackNormal.z*world[0][k]-crackNormal.x*world[2][k])/160;
        projection[1][k]=-world[1][k]/160;
    }
    projection[0][3]=.5f+((world[0][3]-crackCenter.x)*crackNormal.z-(world[2][3]-crackCenter.z)*crackNormal.x)/160;
    projection[1][3]=.5f-(world[1][3]-crackCenter.y)/160;
    for(u16 i=0;i<data->getShapeNum();++i) {
        J3DShape* shape=data->getShapeNodePointer(i);if(!shape)continue;
        for(u16 group=0;group<shape->getMtxGroupNum();++group) {
            J3DShapeMtx* matrix=shape->getShapeMtx(group);J3DShapeDraw* geometry=shape->getShapeDraw(group);
            if(!matrix||!geometry||matrix->getUseMtxNum()!=1)continue;
            u16 index=matrix->getUseMtxIndex(0);
            if(index>=data->getDrawMtxNum()||data->getDrawMtxFlag(index)||data->getDrawMtxIndex(index)!=0)continue;
            // The rigid root owns the trunk; leaf joint groups are excluded.
            GXCallDisplayList(shape->getVcdVatCmd(),J3DShape::kVcdVatDLSize);
            GXLoadPosMtxImm(result,GX_PNMTX0);GXSetCurrentMtx(GX_PNMTX0);
            GXLoadTexMtxImm(projection,GX_TEXMTX0,GX_MTX2x4);
            GXSetTexCoordGen2(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_POS,GX_TEXMTX0,GX_FALSE,GX_PTIDENTITY);
            geometry->draw();++crackMeshDraws;
        }
    }
}
void outlineBox(const Box& b,Color c) {
    solidMode();Mtx identity;MTXIdentity(identity);GXLoadPosMtxImm(view,GX_PNMTX0);
    GXSetZMode(GX_TRUE,GX_LEQUAL,GX_FALSE);GXSetLineWidth(6,GX_TO_ZERO);
    const float v[8][3]={{b.x,b.y,b.z},{b.X,b.y,b.z},{b.X,b.Y,b.z},{b.x,b.Y,b.z},{b.x,b.y,b.Z},{b.X,b.y,b.Z},{b.X,b.Y,b.Z},{b.x,b.Y,b.Z}};
    const int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    GXBegin(GX_LINES,GX_VTXFMT0,24);
    for(int i=0;i<12;++i)for(int j=0;j<2;++j){const float* p=v[edges[i][j]];GXPosition3f32(p[0],p[1],p[2]);color(c);}GXEnd();
}
void drawBlockCracks() {
    int stage=blockBreak.stage();if(stage<0||blockBreak.target<0||game.cells[blockBreak.target].item==Empty)return;
    Box b=cellBox(game.cells[blockBreak.target],game.originY);
    // The original crack pixels span the whole face, without their safety
    // border. Depth writes stay off so this cannot occlude world geometry.
    crackTexture(stage);GXSetNumTexGens(1);GXSetTexCoordGen2(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY,GX_FALSE,GX_PTIDENTITY);
    GXSetVtxDesc(GX_VA_TEX0,GX_DIRECT);GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
    GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR_NULL);GXSetTevOp(GX_TEVSTAGE0,GX_REPLACE);
    GXSetBlendMode(GX_BM_BLEND,GX_BL_DSTCLR,GX_BL_ZERO,GX_LO_NOOP);GXSetAlphaCompare(GX_GREATER,16,GX_AOP_AND,GX_ALWAYS,0);
    GXSetZMode(GX_TRUE,GX_LEQUAL,GX_FALSE);GXLoadPosMtxImm(view,GX_PNMTX0);
    b.x-=.12f;b.y-=.12f;b.z-=.12f;b.X+=.12f;b.Y+=.12f;b.Z+=.12f;
    const float v[6][4][3]={{{b.x,b.y,b.Z},{b.X,b.y,b.Z},{b.X,b.Y,b.Z},{b.x,b.Y,b.Z}},{{b.X,b.y,b.z},{b.x,b.y,b.z},{b.x,b.Y,b.z},{b.X,b.Y,b.z}},{{b.x,b.y,b.z},{b.x,b.y,b.Z},{b.x,b.Y,b.Z},{b.x,b.Y,b.z}},{{b.X,b.y,b.Z},{b.X,b.y,b.z},{b.X,b.Y,b.z},{b.X,b.Y,b.Z}},{{b.x,b.Y,b.Z},{b.X,b.Y,b.Z},{b.X,b.Y,b.z},{b.x,b.Y,b.z}},{{b.x,b.y,b.z},{b.X,b.y,b.z},{b.X,b.y,b.Z},{b.x,b.y,b.Z}}};
    GXBegin(GX_QUADS,GX_VTXFMT0,24);
    for(int f=0;f<6;++f)for(int i=0;i<4;++i){GXPosition3f32(v[f][i][0],v[f][i][1],v[f][i][2]);GXColor4u8(255,255,255,255);GXTexCoord2f32(i==1||i==2?.75f:.25f,i<2?.75f:.25f);}GXEnd();
}
void drawItemDrop(Item item,const Point& position,float angle,float size) {
    MTXRotRad(root,'y',angle);root[0][3]=position.x;root[1][3]=position.y;root[2][3]=position.z;transform(0,0,0);
    if(placeable(item)&&item!=WoodenDoor){transform(-size*.5f,-size*.5f,-size*.5f);drawBlock(item,size);return;}
    const minecraft_assets::Region* r=item==IronAxe?&minecraft_assets::Axe:item==DiamondSword?&minecraft_assets::Sword:item==Stick?&minecraft_assets::Stick:item==WoodenDoor?&minecraft_assets::DoorItem:item==HoverNozzle?&minecraft_assets::HoverNozzle:item==RocketNozzle?&minecraft_assets::RocketNozzle:&minecraft_assets::TurboNozzle;
    textureMode();GXSetAlphaCompare(GX_GREATER,16,GX_AOP_AND,GX_ALWAYS,0);
    GXBegin(GX_QUADS,GX_VTXFMT0,4);
    texVertex(-size*.5f,-size*.5f,0,r->x,r->y+r->h);texVertex(size*.5f,-size*.5f,0,r->x+r->w,r->y+r->h);
    texVertex(size*.5f,size*.5f,0,r->x+r->w,r->y);texVertex(-size*.5f,size*.5f,0,r->x,r->y);GXEnd();GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_OR,GX_ALWAYS,0);
}
void drawParticles() {
    Mtx inverse;if(!MTXInverse(view,inverse))return;
    for(int i=0;i<256;++i)if(particles[i].active) {
        const Particle& p=particles[i];MTXCopy(inverse,root);
        root[0][3]=p.position.x;root[1][3]=p.position.y;root[2][3]=p.position.z;transform(0,0,0);
        minecraft_assets::Region faces[6];blockFaces(p.item,faces);minecraft_assets::Region r=faces[0];if(p.item==Chest){r=minecraft_assets::ChestSkin;r.x+=20;r.y+=38;}
        textureMode();Color c(255,255,255,255*(1-p.age/p.life));float size=3.f;
        GXBegin(GX_QUADS,GX_VTXFMT0,4);
        texVertex(-size,-size,0,r.x+p.u,r.y+p.v+3,c);texVertex(size,-size,0,r.x+p.u+3,r.y+p.v+3,c);
        texVertex(size,size,0,r.x+p.u+3,r.y+p.v,c);texVertex(-size,size,0,r.x+p.u,r.y+p.v,c);GXEnd();
    }
}
void removeCollision(TMapCollisionBase* collision) {
    if(!collision) return;
    collision->remove();
    // Static palm trunks cannot be detached from the static spatial grid.
    // Marking every owned triangle illegal makes queries skip them there.
    for(u32 n=0;n<collision->mCheckDataNum;++n) collision->mCheckDatas[n].mFlags |= BG_CHECK_FLAG_ILLEGAL;
}
void fell(Tree& tree) {
    tree.felled=true;
    sms_minecraft_sound(WoodBreak);
    TMapObjTree* a=tree.actor;
    for(int i=0;i<a->mLeafNum;++i) removeCollision(a->mLeaves[i].mCollision);
    TMapCollisionManager* manager=a->getMapCollisionManager();
    if(manager) for(int i=0;i<manager->mEntryNum;++i) removeCollision(manager->mEntries[i]);
    a->kill();
    int spawned=0;
    for(int n=0;n<4;++n) {
        float ang=n*1.5707963f,x=a->mPosition.x+sinf(ang)*75,z=a->mPosition.z+cosf(ang)*75;
        const TBGCheckData* ground=0;float y=gpMap->checkGround(x,a->mPosition.y+150,z,&ground);
        if(y<a->mPosition.y-300||y>a->mPosition.y+150)y=a->mPosition.y;
        if(game.spawnDrop(WoodenLog,1,x,y+100,z,y)>=0)++spawned;
    }
    spawnParticles(WoodenLog,crackCenter,32);persist();
    tell("TREE CHOPPED - 4 WOODEN LOGS");
    OSReport("[minecraft] felled plaza palm at %.1f %.1f %.1f; dropped %d wooden logs\n",a->mPosition.x,a->mPosition.y,a->mPosition.z,spawned);
}
void rect(float x,float y,float w,float h,Color c) {
    solidMode();GXBegin(GX_QUADS,GX_VTXFMT0,4);quad(x,y,0,x+w,y+h,0,0,c);GXEnd();
}
float textWidth(const char* str,float scale) {
    float width=0;for(;*str;++str)width+=minecraft_assets::FontAdvance[(unsigned char)*str<128?(unsigned char)*str:63]*scale;
    return width;
}
void textPass(float x,float y,const char* str,Color c,float scale) {
    for(;*str;++str) {
        unsigned char ch=(unsigned char)*str;if(ch>=128)ch=63;
        sprite(x,y,8*scale,8*scale,minecraft_assets::Font.x+(ch%16)*8,(ch/16)*8,8,8,c);
        x+=minecraft_assets::FontAdvance[ch]*scale;
    }
}
void text(float x,float y,const char* str,Color c=Color(),float scale=1.5f) {
    textPass(x+scale,y+scale,str,Color(c.r/4,c.g/4,c.b/4,c.a),scale);
    textPass(x,y,str,c,scale);
}
void blockIcon(Item item,float x,float y,float scale) {
    if(item==Chest) {
        Mtx projection;MTXIdentity(projection);
        projection[0][0]=.5f*scale;projection[0][2]=-.5f*scale;projection[0][3]=x+8*scale;
        projection[1][0]=projection[1][2]=.25f*scale;projection[1][1]=-.5f*scale;projection[1][3]=y+8*scale;
        projection[2][2]=0;
        drawChest(16,false,&projection);return;
    }
    textureMode();minecraft_assets::Region regions[6];blockFaces(item,regions);
    const minecraft_assets::Region* faces[3]={&regions[4],&regions[0],&regions[3]};
    const float vertices[3][4][2]={{{8,1},{15,5},{8,9},{1,5}},{{1,5},{8,9},{8,16},{1,12}},{{8,9},{15,5},{15,12},{8,16}}};
    const float shades[3]={1,.8f,.6f};
    GXBegin(GX_QUADS,GX_VTXFMT0,12);
    for(int f=0;f<3;++f)for(int i=0;i<4;++i) {
        const minecraft_assets::Region& r=*faces[f];
        texVertex(x+vertices[f][i][0]*scale,y+vertices[f][i][1]*scale,0,r.x+((i==1||i==2)?r.w:0),r.y+(i>=2?r.h:0),Color(),shades[f]);
    }
    GXEnd();
}
void icon(Item item,float x,float y,float scale) {
    if(item==IronAxe)sprite(x,y,minecraft_assets::Axe,scale);
    else if(item==DiamondSword)sprite(x,y,minecraft_assets::Sword,scale);
    else if(item==Stick)sprite(x,y,minecraft_assets::Stick,scale);
    else if(item==WoodenDoor)sprite(x,y,minecraft_assets::DoorItem,scale);
    else if(placeable(item))blockIcon(item,x,y,scale);
    else if(nozzleItem(item)) {
        const minecraft_assets::Region& r=item==HoverNozzle?minecraft_assets::HoverNozzle:item==RocketNozzle?minecraft_assets::RocketNozzle:minecraft_assets::TurboNozzle;
        sprite(x,y,16*scale,16*scale,r.x,r.y,r.w,r.h);
    }
}
const char* itemName(Item item) {
    switch(item){case Nozzle:return "HOVER NOZZLE";case RocketNozzle:return "ROCKET NOZZLE";case TurboNozzle:return "TURBO NOZZLE";case IronAxe:return "IRON AXE";case DiamondSword:return "DIAMOND SWORD";case WoodenLog:return "OAK LOG";case WoodenPlanks:return "OAK PLANKS";case CraftingTable:return "CRAFTING TABLE";case WoodenDoor:return "OAK DOOR";case Chest:return "CHEST";case Stick:return "STICK";default:return "EMPTY HAND";}
}
const char* tooltipName(Item item) {
    switch(item){case HoverNozzle:return "Hover Nozzle";case RocketNozzle:return "Rocket Nozzle";case TurboNozzle:return "Turbo Nozzle";case IronAxe:return "Iron Axe";case DiamondSword:return "Diamond Sword";case WoodenLog:return "Oak Log";case WoodenPlanks:return "Oak Planks";case CraftingTable:return "Crafting Table";case WoodenDoor:return "Oak Door";case Chest:return "Chest";case Stick:return "Stick";default:return "";}
}
void drawTooltip(const char* title,const char* description=0) {
    float width=textWidth(title,1.5f);if(description&&textWidth(description,1)>width)width=textWidth(description,1);
    float x=mouseX+12,y=mouseY-12,height=description?29:15;
    if(x+width+8>638)x=mouseX-width-14;if(x<3)x=3;
    if(y+height+8>478)y=470-height;if(y<3)y=3;
    rect(x-4,y-4,width+8,height+8,Color(16,0,16,240));
    rect(x-3,y-3,width+6,1,Color(80,0,255,160));rect(x-3,y+height+2,width+6,1,Color(40,0,127,160));
    rect(x-3,y-2,1,height+4,Color(65,0,200,160));rect(x+width+2,y-2,1,height+4,Color(65,0,200,160));
    text(x,y,title,Color(),1.5f);if(description)text(x,y+18,description,Color(170,170,170),1);
}
void drawAchievement() {
    const bool hint=!achievementQueued&&!(game.achievements&(1u<<TakingInventory))&&playing();
    if(!achievementQueued&&!hint)return;
    if(hint&&inventoryHintTime<0)return;
    // Anchor the entire toast as one right-side HUD piece. Menus and the
    // hotbar retain their centred mapping, including after the hint return.
    struct ToastAnchor {
        ToastAnchor(){GXPC_SetHud(1);GXPC_HudPaneBegin(320,640);}
        ~ToastAnchor(){GXPC_HudPaneEnd();GXPC_SetHud(0);}
    } anchor;
    // The original GuiAchievement's three-second fourth-power slide curve.
    float t=(hint?inventoryHintTime:achievementTime)/3.f;if(hint&&t>.5f)t=.5f;float d=t*2;if(d>1)d=2-d;d=1-d*4;if(d<0)d=0;d=d*d*d*d;
    float x=320,y=-int(d*36)*2;sprite(x,y,minecraft_assets::AchievementToast,2);
    if(hint) {
        // GuiAchievement.drawSplitString uses white text at (30,7), with
        // a 120-pixel wrap width and nine-pixel line spacing, plus the book.
        const char* lines[]={"Press 'E' to open your","inventory"};
        for(int line=0;line<2;++line) {
            float px=x+60,py=y+14+line*18;
            for(const char* str=lines[line];*str;++str){unsigned char ch=*str;const minecraft_assets::Region& r=minecraft_assets::AchievementFont;sprite(px,py,16,16,r.x+(ch%16)*8,r.y+(ch/16)*8,8,8);px+=minecraft_assets::AchievementFontAdvance[ch]*2;}
        }
        sprite(x+16,y+16,minecraft_assets::AchievementBook,2);return;
    }
    int achievement=achievementQueue[0];
    const char* title=achievement==TakingInventory?"Taking Inventory":achievement==GettingWood?"Getting Wood":"Benchmaking";
    const char* lines[]={"Achievement get!",title};
    for(int line=0;line<2;++line)for(int pass=0;pass<2;++pass) {
        float px=x+60+(pass?0:2),py=y+(line?36:14)+(pass?0:2);
        Color c=line?Color():Color(255,255,0);if(!pass)c=line?Color(63,63,63):Color(63,63,0);
        for(const char* str=lines[line];*str;++str){unsigned char ch=*str;const minecraft_assets::Region& r=minecraft_assets::AchievementFont;sprite(px,py,16,16,r.x+(ch%16)*8,r.y+(ch/16)*8,8,8,c);px+=minecraft_assets::AchievementFontAdvance[ch]*2;}
    }
    if(achievement==TakingInventory)sprite(x+16,y+16,minecraft_assets::AchievementBook,2);
    else {
        // Render the exact 1.11.2 oak/table textures in the original icon slot.
        textureMode();const minecraft_assets::Region* regions[3];
        regions[0]=achievement==GettingWood?&minecraft_assets::AchievementOakTop:&minecraft_assets::AchievementTableTop;
        regions[1]=achievement==GettingWood?&minecraft_assets::AchievementOak:&minecraft_assets::AchievementTableFront;
        regions[2]=achievement==GettingWood?&minecraft_assets::AchievementOak:&minecraft_assets::AchievementTableSide;
        const float v[3][4][2]={{{8,1},{15,5},{8,9},{1,5}},{{1,5},{8,9},{8,16},{1,12}},{{8,9},{15,5},{15,12},{8,16}}};const float shades[]={1,.8f,.6f};
        GXBegin(GX_QUADS,GX_VTXFMT0,12);
        for(int f=0;f<3;++f)for(int i=0;i<4;++i){const minecraft_assets::Region& r=*regions[f];texVertex(x+16+v[f][i][0]*2,y+16+v[f][i][1]*2,0,r.x+((i==1||i==2)?r.w:0),r.y+(i>=2?r.h:0),Color(),shades[f]);}GXEnd();
    }
}
void drawInventorySteve(JDrama::TGraphics* graphics,float x,float y) {
    Mtx savedView,savedRoot,identity,yaw,pitch;MTXCopy(view,savedView);MTXCopy(root,savedRoot);MTXIdentity(view);
    const float cx=x+101,cy=y+86,scale=3.5f/4.5f;
    float lookX=atanf((mouseX-cx)/80),lookY=atanf((mouseY-y-42)/80);
    MTXRotRad(yaw,'y',lookX*.35f);MTXRotRad(pitch,'x',lookY*.12f);MTXConcat(pitch,yaw,root);
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)root[row][col]*=scale*(row==1?-1:1);
    root[0][3]=cx;root[1][3]=cy+56;root[2][3]=5000;
    Mtx44 projection;C_MTXOrtho(projection,0,480,0,640,-10000,10000);GXSetProjection(projection,GX_ORTHOGRAPHIC);setup(false);
    const JDrama::TRect& viewport=graphics->mViewportRect;float sx=viewport.getWidth()/640.f,sy=viewport.getHeight()/480.f;
    GXSetScissor(viewport.x1+int((x+52)*sx),viewport.y1+int((y+16)*sy),int(98*sx),int(140*sy));
    transform(0,0,0);body(-4,12,-2,8,12,4,1);
    Mtx head,headYaw,headPitch;MTXTrans(head,0,108,0);MTXRotRad(headYaw,'y',lookX*.35f);MTXRotRad(headPitch,'x',lookY*.3f);
    MTXConcat(head,headYaw,head);MTXConcat(head,headPitch,head);MTXConcat(root,head,boneWorld);GXLoadPosMtxImm(boneWorld,GX_PNMTX0);body(-4,0,-4,8,8,8,0);
    transform(-27,108,0,.03f);body(-2,-12,-2,4,12,4,4);
    transform(27,108,0,-.03f);body(-2,-12,-2,4,12,4,2);heldTool(inventory.equipped());
    transform(-9,54,0,.02f);body(-2,-12,-2,4,12,4,5);transform(9,54,0,-.02f);body(-2,-12,-2,4,12,4,3);
    if(player->checkFlag(MARIO_FLAG_HAS_FLUDD)&&player->mWaterGun==readyGun) {
        Mtx socket={{0,0,.75f,0},{.75f,0,0,78},{0,.75f,0,-4}},base;MTXConcat(root,socket,base);
        sms_minecraft_fludd_preview(player->mWaterGun,base);
    }
    MTXCopy(savedView,view);MTXCopy(savedRoot,root);graphics->setScissor(viewport);
    setup(true);C_MTXOrtho(projection,0,480,0,640,-1,1);GXSetProjection(projection,GX_ORTHOGRAPHIC);MTXIdentity(identity);GXLoadPosMtxImm(identity,GX_PNMTX0);
}
void persist() {
    dirty=true;
    if(saveBlocked){tell("SAVE FILE INVALID - ORIGINAL PRESERVED");return;}
    if(sms_minecraft_save(&game)){dirty=false;saveRetry=0;}
    else {saveRetry=2;tell("WORLD SAVE FAILED - RETRYING");}
}
bool closeMenu() {
    bool wasChest=game.menu==ChestMenu;
    if(!game.close()){tell("MAKE SPACE FOR CURSOR AND CRAFTING ITEMS");return false;}
    if(wasChest)sms_minecraft_sound(ChestClose);
    attackHeld=false;swordSwing.cancel();treeBreak.cancel();blockBreak.cancel();persist();return true;
}
void openMenu(Menu menu,int chest=-1) {
    if(game.menu!=NoMenu&&!closeMenu())return;
    game.menu=menu;game.activeChest=chest;buildPointerReady=false;attackHeld=false;swordSwing.cancel();treeBreak.cancel();blockBreak.cancel();persist();
    if(menu==ChestMenu)sms_minecraft_sound(ChestOpen);
}
void menuClick(bool right) {
    Hit hit=menuHit(game.menu,mouseX,mouseY);Slot* slot=0;
    if(hit.group==1)slot=&inventory.slots[hit.index];
    if(hit.group==2)slot=&game.craft[hit.index];
    if(hit.group==3&&game.activeChest>=0)slot=&game.cells[game.activeChest].slots[hit.index];
    if(hit.group==5) {
        if(shiftHeld){if(!game.unequipArmor())tell("INVENTORY FULL");}
        else if(!game.clickArmor(right))tell("CHEST SLOT: FLUDD NOZZLES ONLY");
        persist();return;
    }
    if(hit.group==4) {
        Item resultItem=game.result().item;
        bool crafted=false;if(shiftHeld) {while(game.takeResult(true))crafted=true;}
        else crafted=game.takeResult();
        if(crafted){if(resultItem==CraftingTable)unlock(Benchmaking);persist();}return;
    }
    if(!slot)return;
    if(shiftHeld&&slot->item!=Empty) {
        int remaining=slot->count;
        if(hit.group==3)remaining=inventory.add(slot->item,slot->count);
        else if(hit.group==1&&game.menu==ChestMenu)remaining=addSlots(game.cells[game.activeChest].slots,ChestSlots,slot->item,slot->count);
        else if(hit.group==1&&game.menu==InventoryMenu&&nozzleItem(slot->item)) {game.equipFromInventory(hit.index);persist();return;}
        else if(hit.group==1)remaining=hit.index<9?addSlots(inventory.slots+9,27,slot->item,slot->count):addSlots(inventory.slots,9,slot->item,slot->count);
        slot->count=remaining;if(!remaining)*slot=emptySlot();
    }else clickSlot(*slot,game.cursor,right);
    persist();
}
Box playerBox(float x,float y,float z) {Box b={x-18,y+1,z-18,x+18,y+143,z+18};return b;}
bool swordAttack(TMario* mario) {
    if(!gpConductor||!gpMap)return false;
    TSpineEnemy* nearest=0;float best=1.e30f;
    const float facing=radians(mario->mModelFaceAngle);
    for(JGadget::TList<TEnemyManager*>::iterator it=gpConductor->unk20.begin(),end=gpConductor->unk20.end();it!=end;++it) {
        TEnemyManager* manager=*it;if(!manager)continue;
        for(int i=0;i<manager->getObjNum();++i) {
            TSpineEnemy* enemy=manager->getObj(i);
            if(!enemy||!enemy->checkActorType(ACTOR_TYPE_ENEMY)||enemy->checkActorType(ACTOR_TYPE_BOSS)
                ||enemy->checkLiveFlag(LIVE_FLAG_DEAD|LIVE_FLAG_HIDDEN)
                ||enemy->checkHitFlag(HIT_FLAG_NO_COLLISION|HIT_FLAG_CANNOT_GET_HIT))continue;
            // UNK40 records an earlier kill and is retained by native
            // killChildren/reset respawns. Only the current death state
            // makes a live pooled Stu unavailable for another sword hit.
            if(enemy->mSpine&&(enemy->mSpine->getCurrentNerve()==&TNerveSmallEnemyDie::theNerve()
                ||(!enemy->mSpine->getCurrentNerve()&&enemy->mSpine->getTop()==&TNerveSmallEnemyDie::theNerve())))continue;
            float dx=enemy->mPosition.x-mario->mPosition.x,dy=enemy->mPosition.y-mario->mPosition.y,dz=enemy->mPosition.z-mario->mPosition.z;
            float distance=dx*dx+dz*dz;
            if(distance>=best||!swordReach(dx,dy,dz,facing,enemy->getDamageRadius(),enemy->getDamageHeight()))continue;
            // Rotate the forward detector with the same model angle used by
            // playerRoot. Camera position and crosshair do not select enemies.
            JGeometry::TVec3<float> start=mario->mPosition,endPoint=enemy->mPosition,hit;
            start.y+=70;float height=enemy->getDamageHeight();endPoint.y+=height<140?height*.5f:70;
            if(gpMap->intersectLine(start,endPoint,false,&hit))continue;
            bool blocked=false;
            if(gpMarDirector&&gpMarDirector->mMap==1)for(int j=0;j<WorldCapacity&&!blocked;++j)if(game.cells[j].item!=Empty) {
                float fraction=1;int face=-1;
                blocked=rayBox(cellBox(game.cells[j],game.originY),start.x,start.y,start.z,endPoint.x-start.x,endPoint.y-start.y,endPoint.z-start.z,fraction,face);
            }
            if(!blocked){nearest=enemy;best=distance;}
        }
    }
    if(!nearest)return false;
    // Same receiver, sender and message as TMario::trampleExec. The enemy
    // retains its normal vulnerability checks, animation, effects and drops.
    bool accepted=nearest->receiveMessage(mario,HIT_MESSAGE_TRAMPLE)!=0;
    OSReport("[minecraft] sword trample type=%08x accepted=%d\n",nearest->getActorType(),accepted);
    return accepted;
}
void updateLowerBlockTarget() {
    // A level third-person camera often sees ground far beyond build reach.
    // Fall back to a short downward segment in front of the player's feet.
    float length=sqrtf(aimDirection.x*aimDirection.x+aimDirection.z*aimDirection.z);
    float x=length>.001f?aimDirection.x/length:sinf(radians(player->mModelFaceAngle));
    float z=length>.001f?aimDirection.z/length:cosf(radians(player->mModelFaceAngle));
    JGeometry::TVec3<float> start=player->mPosition,end,hit;
    start.y+=100;end.set(player->mPosition.x+x*300,player->mPosition.y-100,player->mPosition.z+z*300);
    const TBGCheckData* terrain=gpMap->intersectLine(start,end,false,&hit);
    float closest=1;
    if(terrain)closest=((hit.x-start.x)*x*300+(hit.y-start.y)*-200+(hit.z-start.z)*z*300)/130000.f;
    int cell=-1,face=-1;
    for(int i=0;i<WorldCapacity;++i)if(game.cells[i].item!=Empty) {
        float fraction=closest;int side=-1;
        if(rayBox(cellBox(game.cells[i],game.originY),start.x,start.y,start.z,x*300,-200,z*300,fraction,side)) {
            closest=fraction;cell=i;face=side;
        }
    }
    if(cell>=0) {
        blockTarget=cell;blockFace=face;
        aimHit=(Point){start.x+x*300*closest,start.y-200*closest,start.z+z*300*closest};
    }else if(terrain&&terrain->mNormal.y>.5f) {
        aimHit=(Point){hit.x,hit.y,hit.z};aimGround=true;
    }
}
void updateBlockTarget() {
    blockTarget=-1;blockFace=-1;target=-1;aimGround=false;
    if(!player||!gpMarDirector)return;
    // Direct aiming follows the screen-centre camera ray. Reach is measured
    // from Steve; nearby ground gets a lower fallback when this ray misses.
    Mtx inverse;
    if(worldViewReady&&MTXInverse(view,inverse)) {
        aimOrigin=(Point){inverse[0][3],inverse[1][3],inverse[2][3]};
        float localX=0,localY=0;
        if(buildMode&&fabsf(worldProjection[1])>.001f&&fabsf(worldProjection[3])>.001f) {
            float factor=GXPC_GetWidescreen();
            localX=((mouseX-320)/(320*factor)+worldProjection[2])/worldProjection[1];
            localY=(1-mouseY/240+worldProjection[4])/worldProjection[3];
        }
        aimDirection=(Point){inverse[0][0]*localX+inverse[0][1]*localY-inverse[0][2],
            inverse[1][0]*localX+inverse[1][1]*localY-inverse[1][2],
            inverse[2][0]*localX+inverse[2][1]*localY-inverse[2][2]};
    }else {
        float yaw=radians(player->mFaceAngle.y);
        aimOrigin=(Point){player->mPosition.x,player->mPosition.y+70,player->mPosition.z};
        aimDirection=(Point){sinf(yaw),-.04f,cosf(yaw)};
    }
    float length=sqrtf(aimDirection.x*aimDirection.x+aimDirection.y*aimDirection.y+aimDirection.z*aimDirection.z);
    if(length<.001f)return;aimDirection.x/=length;aimDirection.y/=length;aimDirection.z/=length;
    aimHit=(Point){aimOrigin.x+aimDirection.x*3000,aimOrigin.y+aimDirection.y*3000,aimOrigin.z+aimDirection.z*3000};
    if(gpMarDirector->mMap!=1)return;
    float distance=3000;
    for(int i=0;i<WorldCapacity;++i)if(game.cells[i].item!=Empty) {
        int face=-1;float hitDistance=distance;Box b=cellBox(game.cells[i],game.originY);
        if(rayBox(b,aimOrigin.x,aimOrigin.y,aimOrigin.z,aimDirection.x,aimDirection.y,aimDirection.z,hitDistance,face)) {
            Point hit={aimOrigin.x+aimDirection.x*hitDistance,aimOrigin.y+aimDirection.y*hitDistance,aimOrigin.z+aimDirection.z*hitDistance};
            float dx=hit.x-player->mPosition.x,dy=hit.y-player->mPosition.y-70,dz=hit.z-player->mPosition.z;
            if(dx*dx+dy*dy+dz*dz<=360*360){distance=hitDistance;blockTarget=i;blockFace=face;aimHit=hit;}
        }
    }
    for(int i=0;i<treeCount;++i)if(!trees[i].felled) {
        const JGeometry::TVec3<float>& p=trees[i].actor->mPosition;
        Box bounds={p.x-38,p.y+10,p.z-38,p.x+38,p.y+1200,p.z+38};float hitDistance=distance;int face=-1;
        if(rayBox(bounds,aimOrigin.x,aimOrigin.y,aimOrigin.z,aimDirection.x,aimDirection.y,aimDirection.z,hitDistance,face)) {
            Point hit={aimOrigin.x+aimDirection.x*hitDistance,aimOrigin.y+aimDirection.y*hitDistance,aimOrigin.z+aimDirection.z*hitDistance};
            float dx=hit.x-player->mPosition.x,dy=hit.y-player->mPosition.y-70,dz=hit.z-player->mPosition.z;
            if(dx*dx+dy*dy+dz*dz<=220*220){distance=hitDistance;target=i;blockTarget=-1;blockFace=-1;aimHit=hit;}
        }
    }
    JGeometry::TVec3<float> a,b,hit;a.set(aimOrigin.x,aimOrigin.y,aimOrigin.z);
    b.set(aimOrigin.x+aimDirection.x*distance,aimOrigin.y+aimDirection.y*distance,aimOrigin.z+aimDirection.z*distance);
    if(gpMap->intersectLine(a,b,false,&hit)) {
        blockTarget=-1;blockFace=-1;target=-1;aimHit=(Point){hit.x,hit.y,hit.z};
        float dx=hit.x-player->mPosition.x,dy=hit.y-player->mPosition.y,dz=hit.z-player->mPosition.z;
        aimGround=dx*dx+dy*dy+dz*dz<=360*360;
    }
    if(blockTarget<0&&target<0&&!aimGround)updateLowerBlockTarget();
}
bool interactBlock() {
    updateBlockTarget();if(blockTarget<0)return false;
    Cell& c=game.cells[blockTarget];
    if(c.item==CraftingTable){openMenu(TableMenu);return true;}
    if(c.item==Chest){openMenu(ChestMenu,blockTarget);return true;}
    if(c.item==WoodenDoor) {
        c.open=!c.open;Box b=cellBox(c,game.originY);
        if(overlaps(b,playerBox(player->mPosition.x,player->mPosition.y,player->mPosition.z))) {c.open=!c.open;tell("DOORWAY BLOCKED");}
        else {persist();sms_minecraft_sound(c.open?DoorOpen:DoorClose);}return true;
    }
    return false;
}
struct Placement {Cell cell;float origin;bool available,valid;const char* reason;Placement():origin(0),available(false),valid(false),reason("AIM AT GROUND OR A BLOCK IN REACH"){} };
Placement placementCandidate() {
    Placement p;p.origin=game.originY;p.cell.item=inventory.equipped();
    if(!player||!gpMarDirector||gpMarDirector->mMap!=1){p.reason="BUILD IN DELFINO PLAZA";return p;}
    if(!placeable(p.cell.item)){p.reason="SELECT A BLOCK TO PLACE";return p;}
    int& x=p.cell.x;int& y=p.cell.y;int& z=p.cell.z;
    if(blockTarget>=0) {
        const Cell& c=game.cells[blockTarget];x=c.x;y=c.y;z=c.z;
        if(shiftHeld)y+=c.item==WoodenDoor?2:1;
        else if(blockFace==0)--x;else if(blockFace==1)++x;else if(blockFace==2)--y;else if(blockFace==3)++y;else if(blockFace==4)--z;else ++z;
    }else {
        if(!aimGround)return p;
        x=int(floorf(aimHit.x/BlockSize));z=int(floorf(aimHit.z/BlockSize));
        const TBGCheckData* floor=0;float height=gpMap->checkGround(x*80+40,player->mPosition.y+160,z*80+40,&floor);
        if(!floor||height<player->mPosition.y-120||height>player->mPosition.y+80){p.reason="NO BUILDABLE GROUND IN REACH";return p;}
        if(!game.originSet)p.origin=height;
        y=int(floorf((height-p.origin)/80+.5f));
    }
    p.available=true;p.cell.facing=(int(floorf(atan2f(aimDirection.x,aimDirection.z)/1.5707963f+.5f))+4)&3;
    if(game.find(x,y,z)>=0||(p.cell.item==WoodenDoor&&game.find(x,y+1,z)>=0)){p.reason="GRID SPACE OCCUPIED";return p;}
    Box b=cellBox(p.cell,p.origin);
    if(overlaps(b,playerBox(player->mPosition.x,player->mPosition.y,player->mPosition.z))){p.reason="CANNOT PLACE INSIDE STEVE";return p;}
    if(gpMap->isTouchedOneWall((b.x+b.X)*.5f,b.y+40,(b.z+b.Z)*.5f,34)){p.reason="PLAZA WALL BLOCKS THIS GRID SPACE";return p;}
    JGeometry::TVec3<float> a=player->mPosition,end,hit;a.y+=45;end.set(x*80.f+40,p.origin+y*80+40,z*80.f+40);
    if(gpMap->intersectLine(a,end,false,&hit)){p.reason="PLAZA WALL BLOCKS PLACEMENT";return p;}
    for(int i=0;i<WorldCapacity;++i)if(game.cells[i].item==Empty){p.valid=true;return p;}
    p.reason="BUILD LIMIT REACHED";return p;
}
void placeBlock() {
    updateBlockTarget();Placement p=placementCandidate();if(!p.valid){tell(p.reason);return;}
    if(!game.originSet){game.originY=p.origin;game.originSet=true;}
    int index=game.place(p.cell.item,p.cell.x,p.cell.y,p.cell.z,p.cell.facing);if(index<0){tell("BUILD LIMIT REACHED");return;}
    chestLid[index]=0;
    Slot& s=inventory.slots[inventory.selected];if(--s.count==0)s=emptySlot();persist();
    sms_minecraft_sound(WoodPlace);
    OSReport("[minecraft] placed %s at grid %d %d %d\n",itemName(p.cell.item),p.cell.x,p.cell.y,p.cell.z);
}
void drawTarget() {
    if(blockTarget>=0) {
        Box b=cellBox(game.cells[blockTarget],game.originY);b.x-=.3f;b.y-=.3f;b.z-=.3f;b.X+=.3f;b.Y+=.3f;b.Z+=.3f;
        outlineBox(b,Color(0,0,0,210));
    }else if(target>=0) {
        const JGeometry::TVec3<float>& p=trees[target].actor->mPosition;
        float dx=aimHit.x-p.x,dz=aimHit.z-p.z,length=sqrtf(dx*dx+dz*dz);if(length<1)length=1;
        float nx=dx/length,nz=dz/length,x=p.x+nx*29,z=p.z+nz*29;
        solidMode();GXLoadPosMtxImm(view,GX_PNMTX0);GXSetZMode(GX_TRUE,GX_LEQUAL,GX_FALSE);GXSetLineWidth(6,GX_TO_ZERO);
        GXBegin(GX_LINESTRIP,GX_VTXFMT0,5);
        const int u[5]={-1,1,1,-1,-1},v[5]={-1,-1,1,1,-1};
        for(int i=0;i<5;++i){GXPosition3f32(x+nz*u[i]*40,aimHit.y+v[i]*40,z-nx*u[i]*40);GXColor4u8(0,0,0,210);}GXEnd();
    }
    if(showPlacement&&placeable(inventory.equipped())) {
        Placement p=placementCandidate();if(p.available)outlineBox(cellBox(p.cell,p.origin),p.valid?Color(160,255,180,150):Color(255,90,90,150));
    }
}

void collideBlocks(TMario* mario) {
    if(!previousValid||!gpMarDirector||gpMarDirector->mMap!=1)return;
    float& x=mario->mPosition.x;float& y=mario->mPosition.y;float& z=mario->mPosition.z;
    // Reset after a warp/teleport; ordinary movement is much shorter.
    if(fabsf(x-previousPosition.x)>400||fabsf(y-previousPosition.y)>400||fabsf(z-previousPosition.z)>400)return;
    for(int i=0;i<WorldCapacity;++i)if(game.cells[i].item!=Empty) {
        Box b=cellBox(game.cells[i],game.originY);
        bool horizontal=x+18>b.x&&x-18<b.X&&z+18>b.z&&z-18<b.Z;
        if(horizontal&&previousPosition.y>=b.Y-.5f&&y<=b.Y&&mario->mVel.y<=0) {
            y=b.Y;mario->mVel.y=0;mario->mFloorPosition.y=b.Y;
        }else if(overlaps(playerBox(x,y,z),b)) {
            if(previousPosition.y+144<=b.y+1&&mario->mVel.y>0){y=b.y-144;mario->mVel.y=0;}
            else if(previousPosition.x+18<=b.x+1)x=b.x-18;
            else if(previousPosition.x-18>=b.X-1)x=b.X+18;
            else if(previousPosition.z+18<=b.z+1)z=b.z-18;
            else if(previousPosition.z-18>=b.Z-1)z=b.Z+18;
            else {float d[4]={fabsf(x-(b.x-18)),fabsf(x-(b.X+18)),fabsf(z-(b.z-18)),fabsf(z-(b.Z+18))};int best=0;for(int j=1;j<4;++j)if(d[j]<d[best])best=j;if(best<2)x=best==0?b.x-18:b.X+18;else z=best==2?b.z-18:b.Z+18;}
        }
    }
}

}
extern "C" int sms_minecraft_enabled() {
    if(enabledCache<0) { const char* e=getenv("SMS_MINECRAFT");enabledCache=e&&strcmp(e,"0")&&strcmp(e,"off")?1:0; }
    return enabledCache;
}
extern "C" float sms_minecraft_ground(float x,float y,float z,float native,const TBGCheckData** result) {
    if(!sms_minecraft_enabled()||!loaded||!gpMarDirector||gpMarDirector->mMap!=1)return native;
    static TBGCheckData tops[WorldCapacity];
    for(int i=0;i<WorldCapacity;++i)if(game.cells[i].item!=Empty) {
        Box b=cellBox(game.cells[i],game.originY);
        if(x<b.x||x>b.X||z<b.z||z>b.Z||b.Y>y+.5f||b.Y<=native)continue;
        // A high probe during walking must not turn an 80-unit wall into a step.
        if(player&&player->mPosition.y<b.Y-6&&(!previousValid||previousPosition.y<b.Y-6))continue;
        TBGCheckData& top=tops[i];top.mMinY=top.mMaxY=b.Y;top.mNormal.x=0;top.mNormal.y=1;top.mNormal.z=0;top.mPlaneDistance=-b.Y;
        top.mPoint1.set(b.x,b.Y,b.z);top.mPoint2.set(b.x,b.Y,b.Z);top.mPoint3.set(b.X,b.Y,b.z);
        *result=&top;native=b.Y;
    }
    return native;
}
extern "C" void sms_minecraft_damage(TMario* mario,int healthLost) {
    if(sms_minecraft_enabled()&&mario==gpMarioOriginal&&healthLost>0&&mario->mHealth>0) {
        hurtFlash=.5f;sms_minecraft_sound(PlayerHurt);
    }
}
extern "C" int sms_minecraft_menu_open() {return sms_minecraft_enabled()&&game.menu!=NoMenu;}
extern "C" int sms_minecraft_event(const void* event) {
    if(!sms_minecraft_enabled() || !playing()) {attackHeld=false;swordSwing.cancel();treeBreak.cancel();blockBreak.cancel();return 0;}
    Input input=decodeEvent(event);
    float oldMouseX=mouseX,oldMouseY=mouseY;
    if(input.x>=0&&input.y>=0) {
        if(!sms_gx_mouse_to_hud||!sms_gx_mouse_to_hud(input.x,input.y,&mouseX,&mouseY)) {mouseX=input.x;mouseY=input.y;}
    }
    if(input.action==Pointer&&buildMode&&game.menu==NoMenu) {
        if(buildPointerReady) {buildYaw-=(mouseX-oldMouseX)*.006f;buildPitch+=(mouseY-oldMouseY)*.004f;clampBuildPitch();}
        buildPointerReady=true;
    }
    if(input.action==BuildMode&&game.menu==NoMenu){toggleBuildMode();return 1;}
    if(input.action==Help){showHelp=!showHelp;return 1;}
    if(input.action==Shift){shiftHeld=input.value!=0;return game.menu!=NoMenu;}
    if(input.action==FocusLost){buildPointerReady=false;attackHeld=false;swordSwing.cancel();treeBreak.cancel();blockBreak.cancel();shiftHeld=false;return 0;}
    if(game.menu!=NoMenu) {
        if(input.action==Backpack||input.action==CloseMenu||input.action==Interact) {closeMenu();return 1;}
        if((input.action==Swing||input.action==Use)&&input.button&&input.value)menuClick(input.button==3);
        // Suppress game movement and quit while a container owns input.
        return 1;
    }
    switch(input.action) {
    case Select:swordSwing.cancel();treeBreak.cancel();blockBreak.cancel();inventory.selected=input.value;return 1;
    case Scroll:swordSwing.cancel();treeBreak.cancel();blockBreak.cancel();inventory.scroll(input.value);return 1;
    case Swing:
        if(input.value&&!attackHeld)swordSwing.press();
        attackHeld=input.value!=0;if(!attackHeld){treeBreak.cancel();blockBreak.cancel();}return 1;
    case Use:if(input.value){if(shiftHeld||!interactBlock())placeBlock();}return 1;
    case Interact:return interactBlock()?1:0;
    case Backpack:openMenu(InventoryMenu);unlock(TakingInventory);return 1;
    case Help:showHelp=!showHelp;return 1;
    case PlacementPreview:showPlacement=!showPlacement;return 1;
    case CloseMenu:return 0;
    case Ignore: {
        return 1;
    }
    default:return 0;
    }
}
extern "C" int sms_minecraft_locked_nozzle(TWaterGun* gun) {
    if(!sms_minecraft_enabled()||!playing()||!gpMarioOriginal||!gpMarioOriginal->checkFlag(MARIO_FLAG_HAS_FLUDD)||gun!=gpMarioOriginal->mWaterGun||gun!=readyGun)return -1;
    return nozzleType(game.chestArmor.item);
}
extern "C" int sms_minecraft_pickup_nozzle(TItemNozzle* pickup,THitActor* actor) {
    if(!sms_minecraft_enabled())return 0;
    const Item item=nozzleDrop(pickup->mActorType);
    if(item==Empty)return 0;
    // Returning true reserves the pickup for inventory, including when full.
    if(actor!=gpMarioOriginal||pickup->isState(TMapObjGeneral::STATE_HOLDING)
       ||pickup->checkHitFlag(HIT_FLAG_NO_COLLISION)||pickup->checkLiveFlag(LIVE_FLAG_DEAD|LIVE_FLAG_HIDDEN)
       ||SMS_IsMarioOnYoshi())return 1;
    if(inventory.add(item,1)){tell("INVENTORY FULL - MAKE ROOM FOR NOZZLE");return 1;}
    pickup->kill();
    if(item==RocketNozzle||item==TurboNozzle)
        TFlagManager::smInstance->setNozzleRight(gpApplication.mCurrArea.getStage(),item==RocketNozzle?0:1);
    gpItemManager->resetNozzleBoxesModel(nozzleType(item));
    gpMSound->startSoundActor(MSD_SE_SY_GET_NOZZLE,&pickup->mPosition,0,0,0,4);
    tell(itemName(item));persist();
    OSReport("[minecraft] picked up %s for chest armor\n",itemName(item));
    return 1;
}
extern "C" void sms_minecraft_reset_scene() {
    if(!sms_minecraft_enabled())return;
    memset(trees,0,sizeof trees);memset(particles,0,sizeof particles);memset(pickupFlights,0,sizeof pickupFlights);treeCount=0;player=0;
    memset(chestLid,0,sizeof chestLid);forcedGun=0;readyGun=0;forcedAttachment=-1;hurtFlash=0;sms_minecraft_nozzle_preview_reset();
    inventoryHintTime=-2.5f;
    buildMode=false;buildPointerReady=false;
    attackHeld=false;swordSwing.cancel();swordHitFlash=0;treeBreak.cancel();blockBreak.cancel();miningSound=0;crackMeshDraws=0;worldViewReady=false;cooldown=0;swing=0;target=-1;lastTime=0;previousValid=false;
    if(!loaded){loaded=true;saveBlocked=sms_minecraft_load(&game)<0;}

}
extern "C" void sms_minecraft_register_tree(TMapObjTree* tree) {
    if(!sms_minecraft_enabled()||!gpMarDirector||gpMarDirector->mMap!=1)return;
    if(tree->mActorType<0x40000034 || tree->mActorType>0x40000038)return;
    for(int i=0;i<treeCount;++i)if(trees[i].actor==tree)return;
    if(treeCount<128) {trees[treeCount].actor=tree;trees[treeCount].felled=false;++treeCount;}
}
extern "C" int sms_minecraft_tree_felled(TMapObjTree* tree) {
    if(!sms_minecraft_enabled())return 0;
    for(int i=0;i<treeCount;++i)if(trees[i].actor==tree)return trees[i].felled;
    return 0;
}
extern "C" void sms_minecraft_update(TMario* mario) {
    if(!sms_minecraft_enabled() || mario!=gpMarioOriginal)return;
    player=mario;
    OSTime now=OSGetTime();float dt=lastTime?float(now-lastTime)/40500000.f:1.f/30;
    lastTime=now;if(dt<0)dt=0;if(dt>.1f)dt=.1f;
    hurtFlash-=dt;if(hurtFlash<0)hurtFlash=0;
    swordHitFlash-=dt;if(swordHitFlash<0)swordHitFlash=0;
    // Advance even while the storage GUI owns input. Progress is transient;
    // saved chests start closed, and closing the GUI eases the lid back down.
    for(int i=0;i<WorldCapacity;++i) {
        if(game.cells[i].item!=Chest){chestLid[i]=0;continue;}
        const bool opening=game.menu==ChestMenu&&game.activeChest==i;
        chestLid[i]+=dt*(opening?2.5f:-2.5f);
        if(chestLid[i]<0)chestLid[i]=0;
        if(chestLid[i]>1)chestLid[i]=1;
    }
    if(!playing()){attackHeld=false;swordSwing.cancel();treeBreak.cancel();blockBreak.cancel();return;}
    updateNozzleEquipment(mario);
    if(buildMode&&buildPointerReady&&game.menu==NoMenu) {
        float halfWidth=320*GXPC_GetWidescreen();
        if(mouseX<320-halfWidth+24)buildYaw+=dt*1.3f;
        if(mouseX>320+halfWidth-24)buildYaw-=dt*1.3f;
        if(mouseY<20)buildPitch-=dt*.9f;if(mouseY>460)buildPitch+=dt*.9f;
        clampBuildPitch();
    }
    cooldown-=dt;swing-=dt;messageTime-=dt;
    saveRetry-=dt;if(dirty&&saveRetry<=0&&!saveBlocked)persist();
    collideBlocks(mario);previousPosition.x=mario->mPosition.x;previousPosition.y=mario->mPosition.y;previousPosition.z=mario->mPosition.z;previousValid=true;
    updateBlockTarget();
    if(buildMode&&game.menu==NoMenu) {
        float dx=aimDirection.x,dz=aimDirection.z;
        if(blockTarget>=0||target>=0||aimGround){dx=aimHit.x-mario->mPosition.x;dz=aimHit.z-mario->mPosition.z;}
        if(dx*dx+dz*dz>.001f)mario->mFaceAngle.y=mario->mModelFaceAngle=shortAngle(atan2f(dx,dz));
    }
    if(!(game.achievements&(1u<<TakingInventory))&&inventoryHintTime<1.5f)inventoryHintTime+=dt;
    if(achievementQueued) {
        achievementTime+=dt;
        if(achievementTime>=3.f){for(int i=1;i<achievementQueued;++i)achievementQueue[i-1]=achievementQueue[i];--achievementQueued;achievementTime=0;}
    }
    for(int i=0;i<256;++i)if(particles[i].active) {
        Particle& p=particles[i];p.age+=dt;p.velocity.y-=250*dt;
        p.position.x+=p.velocity.x*dt;p.position.y+=p.velocity.y*dt;p.position.z+=p.velocity.z*dt;
        if(p.age>=p.life)p.active=false;
    }
    for(int i=0;i<64;++i)if(pickupFlights[i].active){pickupFlights[i].seconds+=dt;if(pickupFlights[i].seconds>=.18f)pickupFlights[i].active=false;}
    if(game.menu!=NoMenu){attackHeld=false;swordSwing.cancel();treeBreak.cancel();blockBreak.cancel();mario->mForwardVel=0;return;}
    walkPhase+=fabsf(mario->mForwardVel)*dt*.5f;
    Item tool=inventory.equipped();
    // Latch quick clicks, then test throughout the visible swing, including
    // after button release. Moving enemies can enter reach during the swing.
    if(tool==DiamondSword) {
        if(swordSwing.advance(dt,attackHeld)) {
            swing=.3f;OSReport("[minecraft] sword swing\n");
        }
        if(swordSwing.canHit()&&swordAttack(mario)) {
            swordSwing.hit=true;swordHitFlash=.2f;tell("SWORD HIT");
        }
    }else swordSwing.cancel();
    bool mining=attackHeld&&target>=0&&blockTarget<0&&canChop(tool);
    if(mining&&treeBreak.target!=target) {
        const JGeometry::TVec3<float>& pos=trees[target].actor->mPosition;
        float dx=aimHit.x-pos.x,dz=aimHit.z-pos.z,length=sqrtf(dx*dx+dz*dz);
        crackNormal.x=length>1?dx/length:0;crackNormal.y=0;crackNormal.z=length>1?dz/length:-1;
        crackCenter=aimHit;crackCenter.x=pos.x+crackNormal.x*27;crackCenter.z=pos.z+crackNormal.z*27;
        miningSound=0;
    }
    bool blockMining=attackHeld&&blockTarget>=0&&(tool==Empty||tool==IronAxe||tool==DiamondSword)
        &&!(tool==DiamondSword&&swordSwing.hit&&swordSwing.seconds>0);
    if(blockMining&&(blockBreak.target!=blockTarget||blockBreak.tool!=tool))miningSound=0;
    bool treeFinished=treeBreak.advance(target,mining,dt,tool==IronAxe?1.4f:4.2f,tool);
    bool blockFinished=blockBreak.advance(blockTarget,blockMining,dt,blockMining?woodMiningTime(game.cells[blockTarget].item,tool):1.f,tool);
    if(mining||blockMining) {
        float seconds=mining?treeBreak.seconds:blockBreak.seconds;
        swing=.3f-fmodf(seconds,.3f);miningSound-=dt;
        if(treeFinished) {
            if(game.freeDrops()>=4)fell(trees[target]);else tell("TOO MANY DROPPED ITEMS - COLLECT SOME FIRST");
            treeBreak.cancel();target=-1;
        }else if(blockFinished) {
            Item item=game.cells[blockTarget].item;
            if(game.breakBlock(blockTarget)) {
                spawnParticles(item,aimHit,32);chestLid[blockTarget]=0;sms_minecraft_sound(WoodBreak);
                // Native terrain supports drops; a remaining placed block can
                // support them too. Reserve contents before mutating the chest.
                for(int i=0;i<DropCapacity;++i)if(drops[i].active&&drops[i].age==0) {
                    const TBGCheckData* floor=0;float ground=gpMap->checkGround(drops[i].x,drops[i].y+40,drops[i].z,&floor);
                    if(floor&&ground>-100000) {const TBGCheckData* placed=floor;drops[i].ground=sms_minecraft_ground(drops[i].x,drops[i].y,drops[i].z,ground,&placed);}
                }
                persist();OSReport("[minecraft] continuously mined %s into world drops\n",itemName(item));
            }else tell("TOO MANY DROPPED ITEMS - COLLECT SOME FIRST");
            blockBreak.cancel();blockTarget=-1;
        }else if(miningSound<=0) {
            sms_minecraft_sound(WoodHit);spawnParticles(mining?WoodenLog:game.cells[blockTarget].item,mining?crackCenter:aimHit,3);miningSound=.2f;
        }
    }else if(attackHeld&&tool!=DiamondSword&&cooldown<=0) {
        cooldown=.38f;swing=.3f;
    }
    if(gpMarDirector->mMap==1)for(int i=0;i<DropCapacity;++i)if(drops[i].active) {
        WorldDrop& d=drops[i];d.age+=dt;d.vy-=350*dt;d.y+=d.vy*dt;
        if(d.y<d.ground+18){d.y=d.ground+18;d.vy=0;}
        float dx=d.x-mario->mPosition.x,dz=d.z-mario->mPosition.z;
        if(d.age>.75f&&dx*dx+dz*dz<115*115&&fabsf(d.y-mario->mPosition.y)<120) {
            Inventory copy=inventory;int remaining=copy.add(d.item,d.count);
            if(remaining<d.count) {
                inventory=copy;pickupFlight(d.item,d.x,d.y,d.z);sms_minecraft_sound(ItemPickup);
                d.count=remaining;if(!remaining)d.active=false;
                if(d.item==WoodenLog)unlock(GettingWood);persist();
                OSReport("[minecraft] collected %s; remaining world stack=%d\n",itemName(d.item),remaining);
            }
        }
    }
}
extern "C" void sms_minecraft_camera(CPolarSubCamera* camera) {
    if(!sms_minecraft_enabled()||!buildMode||!playing()||game.menu!=NoMenu||camera!=gpCamera)return;
    const float sx=sinf(buildYaw),cz=cosf(buildYaw),distance=300;
    JGeometry::TVec3<float> at,eye,hit,head=player->mPosition;head.y+=80;
    at.set(head.x-cz*55,head.y,head.z+sx*55);
    if(gpMap&&gpMap->intersectLine(head,at,false,&hit))at=head;
    eye.set(at.x-sx*cosf(buildPitch)*distance,at.y+sinf(buildPitch)*distance,at.z-cz*cosf(buildPitch)*distance);
    if(gpMap&&gpMap->intersectLine(at,eye,false,&hit)) {
        eye.set(at.x+(hit.x-at.x)*.85f,at.y+(hit.y-at.y)*.85f,at.z+(hit.z-at.z)*.85f);
    }
    camera->mPosition=camera->unk124=camera->unk130=eye;
    camera->mTarget=camera->unk148=camera->unk154=at;
    camera->mCurrentTarget.mPosition=camera->mCurrentTarget.unk18=eye;
    camera->mCurrentTarget.mTarget=at;
    camera->mCurrentTarget.mYaw=shortAngle(buildYaw+3.14159265f);
    camera->mCurrentTarget.mPitch=shortAngle(buildPitch);
    camera->mUp.set(0,1,0);
}
extern "C" void sms_minecraft_attach_fludd(TMario* mario) {
    if(!sms_minecraft_enabled() || !mario->mWaterGun)return;
    // FLUDD's authored chest space uses X up, Y forward, Z right.
    // Mount it on Steve's rigid torso instead of the hidden Mario skeleton:
    // the pack turns with Steve without inheriting Mario's lean or chest roll.
    // Its own nozzle joints still animate and supply the real water emitters.
    const float scale=.75f;
    Mtx torso, socket={{0,0,scale,0},{scale,0,0,78},{0,scale,0,-4}}, mounted;
    playerRoot(mario,torso);
    MTXConcat(torso,socket,mounted);
    mario->mWaterGun->getModel()->setBaseTRMtx(mounted);
}
extern "C" void sms_minecraft_draw(TMario* mario,JDrama::TGraphics* graphics) {
    if(!sms_minecraft_enabled())return;
    // Entry/draw follows the native FLUDD animation pass. Do not force a
    // restored attachment until that first pass has initialized its BCKs.
    if(mario->checkFlag(MARIO_FLAG_HAS_FLUDD))readyGun=mario->mWaterGun;
    setup(false);MTXCopy(graphics->getViewMtx(),view);
    GXGetProjectionv(worldProjection);worldViewReady=true;
    playerRoot(mario,root);
    float gait=fabsf(mario->mForwardVel)>1?sinf(walkPhase)*.65f:0;
    transform(0,0,0);body(-4,12,-2,8,12,4,1);body(-4,24,-4,8,8,8,0);
    transform(-27,108,0,gait);body(-2,-12,-2,4,12,4,4);
    float right=swing>0?-.9f-sinf(swing/.3f*3.1415927f)*1.3f:-gait;
    transform(27,108,0,right);body(-2,-12,-2,4,12,4,2);heldTool(inventory.equipped());
    transform(-9,54,0,-gait);body(-2,-12,-2,4,12,4,5);
    transform(9,54,0,gait);body(-2,-12,-2,4,12,4,3);
    // Actual FLUDD is rendered by its original model and emits real water.
    if(mario==gpMarioOriginal&&gpMarDirector&&gpMarDirector->mMap==1) {
        for(int i=0;i<DropCapacity;++i)if(drops[i].active){WorldDrop& d=drops[i];drawItemDrop(d.item,(Point){d.x,d.y+3*sinf(d.age*3),d.z},d.age*1.4f,28);}
        for(int i=0;i<64;++i)if(pickupFlights[i].active) {
            PickupFlight& f=pickupFlights[i];float t=f.seconds/.18f;t=t*t;
            Point p={f.start.x+(mario->mPosition.x-f.start.x)*t,f.start.y+(mario->mPosition.y+85-f.start.y)*t,f.start.z+(mario->mPosition.z-f.start.z)*t};
            drawItemDrop(f.item,p,0,28*(1-t*.65f));
        }
        drawParticles();
    }
    if(mario==gpMarioOriginal&&gpMarDirector&&gpMarDirector->mMap==1)for(int i=0;i<WorldCapacity;++i)if(game.cells[i].item!=Empty) {
        Cell& c=game.cells[i];MTXIdentity(root);
        if(c.item==Chest)MTXRotRad(root,'y',((c.facing+2)&3)*1.5707963f);
        root[0][3]=c.x*80+40-40*(root[0][0]+root[0][2]);
        root[1][3]=game.originY+c.y*80;
        root[2][3]=c.z*80+40-40*(root[2][0]+root[2][2]);
        transform(0,0,0);
        if(c.item==WoodenDoor) {
            Box b=cellBox(c,game.originY);minecraft_assets::Region faces[6];
            for(int f=0;f<6;++f)faces[f]=minecraft_assets::DoorBottom;
            texturedBox(b.x-c.x*80,0,b.z-c.z*80,b.X-b.x,80,b.Z-b.z,faces);
            for(int f=0;f<6;++f)faces[f]=minecraft_assets::DoorTop;
            texturedBox(b.x-c.x*80,80,b.z-c.z*80,b.X-b.x,80,b.Z-b.z,faces);
        }else drawBlock(c.item,80,c.item==Chest?chestLid[i]:0);
    }
    j3dSys.reinitGX();
}
extern "C" void sms_minecraft_hud(JDrama::TGraphics* graphics) {
    if(!sms_minecraft_enabled()||!player||!gpMarDirector||gpMarDirector->mState!=TMarDirector::STATE_UNK4)return;
    float savedProj[7];GXGetProjectionv(savedProj);
    if(worldViewReady&&game.menu==NoMenu&&playing()) {
        setup(false);GXSetProjectionv(worldProjection);drawTarget();
        setup(false);GXSetProjectionv(worldProjection);drawTreeCracks();
        setup(false);GXSetProjectionv(worldProjection);drawBlockCracks();
    }else crackMeshDraws=0;
    setup(true);Mtx44 proj;C_MTXOrtho(proj,0,480,0,640,-1,1);GXSetProjection(proj,GX_ORTHOGRAPHIC);
    Mtx identity;MTXIdentity(identity);GXLoadPosMtxImm(identity,GX_PNMTX0);
    graphics->setScissor(graphics->mViewportRect);
    if(getenv("SMS_NOZZLE_RENDER")) {
        rect(0,0,640,480,Color(22,27,36));
        const char* labels[]={"HOVER NOZZLE","ROCKET NOZZLE","TURBO NOZZLE"};
        for(int i=0;i<3;++i) {
            sms_minecraft_nozzle_preview(i,32+i*208,144,160);
            setup(true);C_MTXOrtho(proj,0,480,0,640,-1,1);GXSetProjection(proj,GX_ORTHOGRAPHIC);GXLoadPosMtxImm(identity,GX_PNMTX0);
            text(112+i*208-textWidth(labels[i],1.5f)/2,326,labels[i]);
        }
        text(162,60,"ORIGINAL SUNSHINE PICKUP MODELS",Color(185,232,249),1.5f);
        GXSetProjectionv(savedProj);j3dSys.reinitGX();return;
    }
    if(game.menu==NoMenu) {
    const float left=138,top=431,slot=40;
    sprite(left,top,minecraft_assets::Hotbar,2);
    sprite(left-2+inventory.selected*slot,top-2,minecraft_assets::Selection,2);
    for(int i=0;i<9;++i) {
        float x=left+6+i*slot;
        icon(inventory.slots[i].item,x,top+6,2);
        if(inventory.slots[i].count>1) {
            char n[12];snprintf(n,sizeof n,"%d",inventory.slots[i].count);
            text(x+33-textWidth(n,1.5f),top+25,n,Color(),1.5f);
        }
    }
    int filled=(player->mHealth*20+7)/8;if(filled>20)filled=20;if(filled<0)filled=0;
    for(int i=0;i<10;++i) {
        float x=left+i*16;
        sprite(x,399,minecraft_assets::HeartEmpty,2);
        if(filled>=i*2+2)sprite(x,399,minecraft_assets::HeartFull,2);
        else if(filled==i*2+1)sprite(x,399,minecraft_assets::HeartHalf,2);
    }
    float water=player->mWaterGun&&player->mWaterGun->getMaxWater()>0?float(player->mWaterGun->getCurrentWater())/player->mWaterGun->getMaxWater():0;
    if(water<0)water=0;if(water>1)water=1;
    sprite(left,419,minecraft_assets::WaterBarEmpty,2);
    const int waterPixels=int(minecraft_assets::WaterBarFill.w*water);
    if(waterPixels>0) {
        const minecraft_assets::Region& r=minecraft_assets::WaterBarFill;
        sprite(left,419,waterPixels*2,r.h*2,r.x,r.y,waterPixels,r.h);
    }
    text(369,402,"FLUDD",Color(187,229,245),1.2f);
    char waterPercent[8];snprintf(waterPercent,sizeof waterPercent,"%d%%",int(water*100+.5f));
    text(left+minecraft_assets::WaterBarEmpty.w*2-textWidth(waterPercent,1.2f),402,waterPercent,Color(187,229,245),1.2f);
    const char* name=itemName(inventory.equipped());
    text(320-textWidth(name,1.5f)/2,378,name);
    if(messageTime>0){float w=textWidth(message,1.2f);rect(320-w/2-7,52,w+14,20,Color(0,0,0,180));text(320-w/2,57,message,Color(237,231,192),1.2f);}
    if(playing())sprite((buildMode?mouseX:320)-7.5f,(buildMode?mouseY:240)-7.5f,minecraft_assets::Crosshair,1,swordHitFlash>0?Color(255,210,70):Color());
    if(buildMode)text(320-textWidth("BUILD MODE [B]",1.2f)/2,80,"BUILD MODE [B]",Color(187,229,245),1.2f);
    if(blockTarget>=0&&game.menu==NoMenu) {
        const char* label=game.cells[blockTarget].item==CraftingTable?"RIGHT CLICK: CRAFT":game.cells[blockTarget].item==Chest?"RIGHT CLICK: OPEN CHEST":game.cells[blockTarget].item==WoodenDoor?"RIGHT CLICK: OPEN / CLOSE":"RIGHT CLICK: PLACE   SHIFT: STACK";
        text(320-textWidth(label,1)/2,356,label,Color(),1);
    }
    }
    if(game.menu!=NoMenu) {
        rect(0,0,640,480,Color(0,0,0,145));const float x=144,y=66;
        sprite(x,y,game.menu==InventoryMenu?minecraft_assets::Inventory:game.menu==TableMenu?minecraft_assets::Crafting:minecraft_assets::ChestGui,2);
        if(game.menu==ChestMenu){textPass(x+16,y+12,"Chest",Color(64,64,64),2);textPass(x+16,y+144,"Inventory",Color(64,64,64),2);}
        else textPass(x+(game.menu==TableMenu?56:194),y+12,"Crafting",Color(64,64,64),2);
        for(int i=0;i<InventorySize;++i) {
            float sx=x+16+(i<9?i:i-9)%9*36,sy=y+(i<9?284:168+(i-9)/9*36)+(game.menu==ChestMenu?2:0);
            icon(inventory.slots[i].item,sx,sy,2);
            if(inventory.slots[i].count>1){char n[12];snprintf(n,sizeof n,"%d",inventory.slots[i].count);text(sx+33-textWidth(n,1.5f),sy+20,n,Color(),1.5f);}
        }
        if(game.menu==ChestMenu&&game.activeChest>=0) {
            for(int i=0;i<ChestSlots;++i) {
                Slot& s=game.cells[game.activeChest].slots[i];float sx=x+16+i%9*36,sy=y+36+i/9*36;icon(s.item,sx,sy,2);
                if(s.count>1){char n[12];snprintf(n,sizeof n,"%d",s.count);text(sx+33-textWidth(n,1.5f),sy+20,n,Color(),1.5f);}
            }
        }else {
            int size=game.gridSize();float gx=x+(size==3?60:196),gy=y+(size==3?34:36);
            for(int i=0;i<size*size;++i){Slot& s=game.craft[i];float sx=gx+i%size*36,sy=gy+i/size*36;icon(s.item,sx,sy,2);if(s.count>1){char n[12];snprintf(n,sizeof n,"%d",s.count);text(sx+33-textWidth(n,1.5f),sy+20,n,Color(),1.5f);}}
            Recipe r=game.result();float sx=x+(size==3?248:308),sy=y+(size==3?70:56);icon(r.item,sx,sy,2);
            if(r.count>1){char n[12];snprintf(n,sizeof n,"%d",r.count);text(sx+33-textWidth(n,1.5f),sy+20,n,Color(),1.5f);}
            if(game.menu==InventoryMenu) {
                icon(game.chestArmor.item,x+16,y+52,2);
                drawInventorySteve(graphics,x,y);
            }
        }
        Hit h=menuHit(game.menu,mouseX,mouseY);
        if(h.group&&game.cursor.item==Empty) {
            float hx=0,hy=0;
            if(h.group==1){hx=x+16+(h.index<9?h.index:h.index-9)%9*36;hy=y+(h.index<9?284:168+(h.index-9)/9*36)+(game.menu==ChestMenu?2:0);}
            else if(h.group==2){int n=game.gridSize();hx=x+(n==3?60:196)+h.index%n*36;hy=y+(n==3?34:36)+h.index/n*36;}
            else if(h.group==3){hx=x+16+h.index%9*36;hy=y+36+h.index/9*36;}
            else if(h.group==5){hx=x+16;hy=y+52;}
            else {hx=x+(game.gridSize()==3?248:308);hy=y+(game.gridSize()==3?70:56);}
            rect(hx,hy,32,32,Color(255,255,255,80));
        }
        if(game.cursor.item==Empty&&h.group) {
            Slot hovered;
            if(h.group==1)hovered=inventory.slots[h.index];
            else if(h.group==2)hovered=game.craft[h.index];
            else if(h.group==3&&game.activeChest>=0)hovered=game.cells[game.activeChest].slots[h.index];
            else if(h.group==4){Recipe recipe=game.result();hovered.item=recipe.item;hovered.count=recipe.count;}
            else if(h.group==5)hovered=game.chestArmor;
            if(hovered.item!=Empty)drawTooltip(tooltipName(hovered.item),h.group==5?"Equipped FLUDD attachment":0);
            else if(h.group==5)drawTooltip("FLUDD Attachment","Equip a nozzle in this slot");
        }
        if(game.cursor.item!=Empty){icon(game.cursor.item,mouseX-16,mouseY-16,2);if(game.cursor.count>1){char n[12];snprintf(n,sizeof n,"%d",game.cursor.count);text(mouseX+12-textWidth(n,1.5f),mouseY+5,n,Color(),1.5f);}}
    }
    if(showHelp) {
        rect(114,400,412,70,Color(0,0,0,210));
        if(game.menu!=NoMenu) {
            text(124,410,"Left click: Move   Right click: Split / place one",Color(),1);
            text(124,428,"Shift click: Transfer   Tab / Esc: Close",Color(),1);
            text(124,446,"Chest armor: Equip a FLUDD nozzle   H: Hide help",Color(),1);
        }else {
            text(124,410,"Wheel / 1-9: Select   Hold left: Mine   Tab: Inventory",Color(),1);
            text(124,428,"Right click: Place / interact   E: FLUDD",Color(),1);
            text(124,446,"B: Build mode   P: Preview   Shift: Stack   H: Hide help",Color(),1);
        }
    }
    drawAchievement();
    GXSetProjectionv(savedProj);j3dSys.reinitGX();
}
