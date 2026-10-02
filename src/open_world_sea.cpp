// Optional open-water Plaza / Pinna connection. All models and textures are
// loaded from the player's disc; native Mario renders the animated Blooper.
#include <sms_open_world.h>
#include <sms_open_world_sea.h>
#include <System/Application.hpp>
#include <System/MarDirector.hpp>
#include <System/MarioGamePad.hpp>
#include <System/ScenarioArchiveName.hpp>
#include <System/Resolution.hpp>
#include <Strategic/NameRefPtrAry.hpp>
#include <Player/Mario.hpp>
#include <Player/WaterGun.hpp>
#include <Camera/Camera.hpp>
#include <Map/Map.hpp>
#include <Map/MapData.hpp>
#include <Map/MapCollisionData.hpp>
#include <M3DUtil/MActor.hpp>
#include <M3DUtil/MActorData.hpp>
#include <MarioUtil/DrawUtil.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/J3D/J3DGraphLoader/J3DModelLoader.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DDrawBuffer.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DSys.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DAnimation.hpp>
#include <JSystem/JUtility/JUTResFont.hpp>
#include <JSystem/J2D/J2DOrthoGraph.hpp>
#include <GC2D/ScrnFader.hpp>
#include <GC2D/GCConsole2.hpp>
#include <GC2D/ExPane.hpp>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/os.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern "C" unsigned port_open_world_resource(const char*,const char*,void*,unsigned);
extern "C" int port_endian_j3d(void*,unsigned);
extern "C" int port_endian_bti(void*,unsigned);
extern "C" int port_fps60_active;
extern "C" void port_open_world_prefetch(const char*,int);
extern "C" unsigned long long port_open_world_milliseconds();
extern "C" void GXPC_CoastalHold(int) __attribute__((weak));
extern "C" void GXPC_CoastalDissolve(int) __attribute__((weak));
namespace {
const int PLAZA=1;
int pinnaStage=5,parkStage=13,plazaEpisode=2,pinnaEpisode=0,currentStage=-1,pendingStage=-1;
const float rotation=.7853981634f,cs=.7071067812f,sn=.7071067812f;
const float islandX=-43000,islandZ=-26000;
struct P { float x,z; };
P toWorld(int stage,P p) {
    if(stage==PLAZA)return p;
    P q={islandX+cs*p.x+sn*p.z,islandZ-sn*p.x+cs*p.z};return q;
}
P fromWorld(int stage,P p) {
    if(stage==PLAZA)return p;
    float x=p.x-islandX,z=p.z-islandZ;P q={cs*x-sn*z,sn*x+cs*z};return q;
}
JGeometry::TVec3<f32> world(int stage,const JGeometry::TVec3<f32>& p) {
    P a={p.x,p.z};P b=toWorld(stage,a);return JGeometry::TVec3<f32>(b.x,p.y,b.z);
}
JGeometry::TVec3<f32> native(int stage,const JGeometry::TVec3<f32>& p) {
    P a={p.x,p.z};P b=fromWorld(stage,a);return JGeometry::TVec3<f32>(b.x,p.y,b.z);
}
P controls[6],path[401];float lengths[401],routeLength,swapDistance,progress,lateral,rideTime,hop;
int direction=1,rideCount,crossings,cooldown;float landingCamera;
bool active,ready,releasePending,testSpawnUsed,testDone,testPadActive;
signed char testStickX,testStickY;unsigned short testButtons;int testPulse;
MActor *squid,*idleSquid;J3DModel* scenery;
J3DDrawBuffer *opa,*xlu;
GXTexObj water;bool hasWater;
JGeometry::TVec3<f32> seaEye,seaAt,savedPos,savedEye,savedAt,savedVel;
float savedSpeed,savedFov,savedAnimFrame,savedSquidFrame,rideElapsed;bool testHopSent,testTurnSent;
s16 savedYaw,savedHealth;int savedWater;u8 savedNozzle,savedSecond;
unsigned long long crossingStart;
int dockMode;float dockRemaining;JGeometry::TVec3<f32> dockFrom,dockTo;
int testParkNode;bool testParkEntered,testParkControlled;
JGeometry::TVec3<f32> testParkStart;
void initPath() {
    controls[0].x=-11400;controls[0].z=1650;
    controls[1].x=-14000;controls[1].z=2600;
    controls[2].x=-23500;controls[2].z=-10000;
    P a={6000,12500},b={6000,7800},c={6000,6500};
    controls[3]=toWorld(pinnaStage,a);controls[4]=toWorld(pinnaStage,b);controls[5]=toWorld(pinnaStage,c);
    lengths[0]=0;
    for(int i=0;i<=400;++i) {
        float f=i/80.f;int segment=int(f);if(segment>4)segment=4;float t=f-segment;
        P a=controls[segment?segment-1:0],b=controls[segment],c=controls[segment+1],d=controls[segment<4?segment+2:5];
        path[i].x=.5f*(2*b.x+(-a.x+c.x)*t+(2*a.x-5*b.x+4*c.x-d.x)*t*t+(-a.x+3*b.x-3*c.x+d.x)*t*t*t);
        path[i].z=.5f*(2*b.z+(-a.z+c.z)*t+(2*a.z-5*b.z+4*c.z-d.z)*t*t+(-a.z+3*b.z-3*c.z+d.z)*t*t*t);
        if(i) {float dx=path[i].x-path[i-1].x,dz=path[i].z-path[i-1].z;lengths[i]=lengths[i-1]+sqrtf(dx*dx+dz*dz);}
    }
    routeLength=lengths[400];swapDistance=lengths[240];
}
P sample(float along,P* tangent=0) {
    if(along<0)along=0;if(along>routeLength)along=routeLength;
    int i=1;while(i<400 && lengths[i]<along)++i;
    float dx=path[i].x-path[i-1].x,dz=path[i].z-path[i-1].z,len=lengths[i]-lengths[i-1];
    float t=(along-lengths[i-1])/len;
    if(tangent) {tangent->x=dx/len;tangent->z=dz/len;}
    P p={path[i-1].x+dx*t,path[i-1].z+dz*t};return p;
}
void* resource(const char* archive,const char* name) {
    unsigned size=port_open_world_resource(archive,name,0,0);
    if(!size){OSReport("[sea-route] missing resource %s:%s\n",archive,name);return 0;}
    void* p=JKRHeap::alloc(size,32,0);
    if(!p || port_open_world_resource(archive,name,p,size)!=size)return 0;
    if(!(strstr(name,".bti")?port_endian_bti(p,size):port_endian_j3d(p,size)))return 0;
    return p;
}
J3DModel* model(const char* archive,const char* name) {
    void* r=resource(archive,name);if(!r)return 0;
    J3DModelData* data=J3DModelLoaderDataBase::load(r,J3DMLF_MaterialPEFull|(2<<J3DMLF_TevStageNumShift));
    if(!data)return 0;
    J3DModel* m=new J3DModel(data,0,1);SMS_MakeDLAndLock(m);return m;
}
MActor* makeSquid() {
    J3DModel* m=model("/data/scene/ricco1.szs","surfgeso.bmd");
    void* bck=resource("/data/scene/ricco1.szs","surfgeso_run1.bck");if(!m || !bck)return 0;
    MActorAnmData* data=new MActorAnmData;
    data->mBckNum=1;data->mBckAnms=new MActorAnmDataEach<J3DAnmTransformKey>(1);
    data->mBckAnms->mAnmNames[0]="surfgeso_run1";
    data->mBckAnms->mAnmKeyCodes[0]=MActorCalcKeyCode("surfgeso_run1");
    data->mBckAnms->mAnimations=new J3DAnmBase*[1];
    data->mBckAnms->mAnimations[0]=J3DAnmLoaderDataBase::load(bck);
    MActor* a=new MActor(data);a->setModel(m,0);a->setBck("surfgeso_run1");a->getFrameCtrl(0)->setRate(.5f);
    return a;
}
void prefetch(int stage,int episode,int slot) {
    if(!gpApplication.unk30 || stage>=gpApplication.unk30->getChildren().size())return;
    TNameRefAryT<TScenarioArchiveName>* names=gpApplication.unk30->getChildren()[stage];
    if(episode>=names->size())return;
    char p[128];snprintf(p,sizeof p,"/data/scene/%s",names->getChildren()[episode].mArcName);
    char* ext=strstr(p,".arc");if(ext)strcpy(ext,".szs");port_open_world_prefetch(p,slot);
}
// Despite its name, setting J3DShpFlag_Visible suppresses native entry.
void hideScenery(J3DModel* m,bool plaza) {
    if(!m)return;
    J3DModelData* d=m->getModelData();
    if(plaza) {
        const int hidden[]={10,11,12,19,22,40};
        for(unsigned i=0;i<sizeof(hidden)/sizeof(hidden[0]);++i)
            if(hidden[i]<d->getShapeNum()){d->getShapeNodePointer(hidden[i])->onFlag(J3DShpFlag_Visible);m->getShapePacket(hidden[i])->hide();}
    } else {
        // The beach model includes the static park as well as distant islands.
        // Keep its near island and remove the baked, differently scaled backdrop.
        for(int i=0;i<9 && i<d->getShapeNum();++i){d->getShapeNodePointer(i)->onFlag(J3DShpFlag_Visible);m->getShapePacket(i)->hide();}
    }
}
void drawModel(J3DModel* m,JDrama::TGraphics* g) {
    if(!m)return;
    J3DDrawBuffer* old0=j3dSys.getDrawBuffer(0),*old1=j3dSys.getDrawBuffer(1);
    opa->frameInit();xlu->frameInit();j3dSys.setDrawBuffer(opa,0);j3dSys.setDrawBuffer(xlu,1);
    j3dSys.setViewMtx(g->getViewMtx());opa->setZMtx(g->getViewMtx());xlu->setZMtx(g->getViewMtx());
    m->calc();m->viewCalc();m->entry();SMS_DrawInit();opa->draw();xlu->draw();
    j3dSys.setDrawBuffer(old0,0);j3dSys.setDrawBuffer(old1,1);SMS_DrawInit();
}
void cameraMatrices(bool previous=false) {
    C_MTXPerspective(gpCamera->unk16C,gpCamera->mFovy,gpCamera->mAspect,gpCamera->mNear,gpCamera->mFar);
    C_MTXLookAt(gpCamera->unk1EC,&gpCamera->unk124,&gpCamera->mUp,&gpCamera->unk148);
    if(previous){MTXCopy(gpCamera->unk1EC,gpCamera->unk21C);memcpy(gpCamera->unk1AC,gpCamera->unk16C,sizeof gpCamera->unk16C);}
}
P station() {return currentStage==PLAZA?controls[0]:controls[4];}
bool nearStation() {
    if(!ready || !gpMarioOriginal)return false;
    JGeometry::TVec3<f32> p=world(currentStage,gpMarioOriginal->mPosition);P s=station();
    float x=p.x-s.x,z=p.z-s.z;
    float radius=currentStage==PLAZA?850.f:1600.f;
    return x*x+z*z<radius*radius && p.y>-150 && p.y<550;
}
void board() {
    TMario* m=gpMarioOriginal;
    if(!ready || active || m->onYoshi() || m->isHolding() || m->getHolder())return;
    progress=currentStage==PLAZA?0:lengths[320];direction=currentStage==PLAZA?1:-1;
    lateral=hop=landingCamera=rideElapsed=0;testHopSent=testTurnSent=false;cooldown=30;active=true;++rideCount;
    dockMode=1;dockRemaining=30;dockFrom=world(currentStage,m->mPosition);
    P berth=sample(progress);dockTo.set(berth.x,8.f,berth.z);
    seaEye=world(currentStage,gpCamera->mPosition);seaAt=world(currentStage,gpCamera->mTarget);
    if(currentStage==PLAZA){J3DModel* m=gpMap->getModelManager()->getJointModel(0)->getModel();m->getModelData()->getShapeNodePointer(10)->onFlag(J3DShpFlag_Visible);m->getShapePacket(10)->hide();}
    m->mSurfGesso=squid;m->mSurfGessoType=TMario::SURF_GESSO_TYPE_GREEN;
    m->changePlayerStatus(MARIO_STATUS_SURF,0,true);m->mStatusTimer=0;
    m->setAnimation(TMario::ANIM_RIDE_SHELL,1.0f);
    OSReport("[sea-route] boarded stage=%d ride=%d progress=%.1f\n",currentStage,rideCount,progress);
}
void land() {
    TMario* m=gpMarioOriginal;
    JGeometry::TVec3<f32> p=currentStage==PLAZA?JGeometry::TVec3<f32>(-11000,400,1400):JGeometry::TVec3<f32>(6000,200,6500);
    const TBGCheckData* ground;
    float y=gpMapCollisionData->checkGround(p.x,p.y+300,p.z,0,&ground);
    p.y=fmaxf(y,100.f)+5;
    m->waitingStart(&p,currentStage==PLAZA?90:180);m->mPosition=p;m->mVel.zero();m->mForwardVel=0;
    m->mGroundPlane=ground;m->mFloorPosition.set(p.x,y,p.z);m->mSurfGesso=0;m->resetHistory();
    if(currentStage==PLAZA){J3DModel* m=gpMap->getModelManager()->getJointModel(0)->getModel();m->getModelData()->getShapeNodePointer(10)->offFlag(J3DShpFlag_Visible);m->getShapePacket(10)->show();}
    active=false;dockMode=0;cooldown=60;landingCamera=60;
    OSReport("[sea-route] landed stage=%d ride=%d xyz=(%.1f,%.1f,%.1f) health=%d water=%d\n",currentStage,rideCount,p.x,p.y,p.z,m->mHealth,m->mWaterGun?m->mWaterGun->mCurrentWater:0);
    if(getenv("SMS_SEA_TEST_RIDES") && rideCount>=atoi(getenv("SMS_SEA_TEST_RIDES")))testDone=true;
}
void clearHud(TMarDirector* d) {
    TGCConsole2* c=d->mConsole;c->unk39=0;c->startAppearCoin();c->startAppearTank();
    for(int i=0;i<180;++i){c->processAppearCoin(i);c->processAppearTank(i);}
    c->unk4F=0;c->unk45=0;c->unk46=1;c->unk50=0;c->unk3A=0;c->unk3B=0;c->unk3A8->getPane()->hide();c->startDisappearTelop();
}
// Verification only: walk from the ferry landing through the native gate
// using controller axes. Never move Mario or request a stage in this helper.
bool testParkWalk(TMarDirector* d) {
    if(!getenv("SMS_SEA_TEST_ENTER_PARK") || !testDone || active)return false;
    TMario* m=gpMarioOriginal;
    if(currentStage==pinnaStage) {
        // Approach the west staircase, keeping clear of the raised planter.
        const P nodes[]={{2000,6500},{-3500,5400},{-4700,5200},{-4700,3500},{-3515,3000},{-3515,1900}};
        float dx=nodes[testParkNode].x-m->mPosition.x,dz=nodes[testParkNode].z-m->mPosition.z;
        if(dx*dx+dz*dz<140*140 && testParkNode<5) {
            OSReport("[sea-route] park walk node=%d xyz=(%.1f,%.1f,%.1f)\n",testParkNode,m->mPosition.x,m->mPosition.y,m->mPosition.z);
            ++testParkNode;dx=nodes[testParkNode].x-m->mPosition.x;dz=nodes[testParkNode].z-m->mPosition.z;
        }
        float angle=atan2f(dx,dz)-gpCamera->getUnk258()*(6.283185307f/65536.f);
        static int timer;
        if(getenv("SMS_OPEN_WORLD_LOG") && (++timer%60)==0)
            OSReport("[sea-route] park walking node=%d xyz=(%.1f,%.1f,%.1f)\n",testParkNode,m->mPosition.x,m->mPosition.y,m->mPosition.z);
        testPadActive=true;testStickX=int(100*sinf(angle));testStickY=int(-100*cosf(angle));
        return true;
    }
    if(currentStage!=parkStage)return false;
    if(!gpApplication.mFader->isFullyFadedIn())return true;
    if(!testParkEntered) {
        testParkEntered=true;testParkStart=m->mPosition;
        OSReport("[sea-route] entered park stage=%d episode=%d xyz=(%.1f,%.1f,%.1f) health=%d\n",currentStage,d->unk7D,m->mPosition.x,m->mPosition.y,m->mPosition.z,m->mHealth);
    }
    float dx=testParkStart.x+400-m->mPosition.x,dz=testParkStart.z-m->mPosition.z;
    float movedX=m->mPosition.x-testParkStart.x,movedZ=m->mPosition.z-testParkStart.z;
    if(!testParkControlled && movedX*movedX+movedZ*movedZ>120*120) {
        testParkControlled=true;
        OSReport("[sea-route] park control proven stage=%d distance=%.1f xyz=(%.1f,%.1f,%.1f) health=%d\n",currentStage,sqrtf(movedX*movedX+movedZ*movedZ),m->mPosition.x,m->mPosition.y,m->mPosition.z,m->mHealth);
    }
    testPadActive=true;
    if(!testParkControlled) {
        float angle=atan2f(dx,dz)-gpCamera->getUnk258()*(6.283185307f/65536.f);
        testStickX=int(100*sinf(angle));testStickY=int(-100*cosf(angle));
    }
    return true;
}
void cross(TMarDirector* d) {
    TMario* m=gpMarioOriginal;
    testPadActive=false;
    pendingStage=currentStage==PLAZA?pinnaStage:PLAZA;
    savedPos=world(currentStage,m->mPosition);savedEye=world(currentStage,gpCamera->mPosition);savedAt=world(currentStage,gpCamera->mTarget);
    savedVel=m->mVel;if(currentStage!=PLAZA){float x=cs*savedVel.x+sn*savedVel.z,z=-sn*savedVel.x+cs*savedVel.z;savedVel.x=x;savedVel.z=z;}
    savedYaw=m->mFaceAngle.y+(currentStage==PLAZA?0:s16(0x2000));savedSpeed=m->mForwardVel;savedFov=gpCamera->mFovy;
    savedAnimFrame=m->getMotionFrameCtrl().getFrame();savedSquidFrame=squid->getFrameCtrl(0)->getFrame();
    savedHealth=m->mHealth;savedWater=m->mWaterGun?m->mWaterGun->mCurrentWater:0;
    savedNozzle=m->mWaterGun?m->mWaterGun->mCurrentNozzle:0;savedSecond=m->mWaterGun?m->mWaterGun->mSecondNozzle:4;
    if(GXPC_CoastalDissolve)GXPC_CoastalDissolve(port_fps60_active?36:18);
    if(GXPC_CoastalHold)GXPC_CoastalHold(8);
    crossingStart=port_open_world_milliseconds();
    int ep=pendingStage==PLAZA?plazaEpisode:pinnaEpisode;
    OSReport("[sea-route] crossing stage=%d -> stage=%d episode=%d progress=%.1f speed=%.2f health=%d water=%d\n",currentStage,pendingStage,ep,progress,savedSpeed,savedHealth,savedWater);
    d->setNextStage(((pendingStage+1)<<8)|ep,0);
}
}
void sms_sea_setup(TMarDirector* d) {
    testPadActive=false;
    ready=false;scenery=0;squid=idleSquid=0;hasWater=false;currentStage=d->mMap;
    if(!sms_open_world_enabled())return;
    if(gpApplication.unk30)for(unsigned i=0;i<gpApplication.unk30->getChildren().size();++i) {
        TNameRefAryT<TScenarioArchiveName>* n=gpApplication.unk30->getChildren()[i];
        if(n->size() && strstr(n->getChildren()[0].mArcName,"pinnaBeach"))pinnaStage=i;
        if(n->size() && strstr(n->getChildren()[0].mArcName,"pinnaParco"))parkStage=i;
    }
    initPath();
    if(currentStage!=PLAZA && currentStage!=pinnaStage){active=false;pendingStage=-1;return;}
    if(currentStage==PLAZA)plazaEpisode=d->unk7D;else pinnaEpisode=d->unk7D;
    if(pendingStage!=currentStage){pendingStage=-1;active=false;landingCamera=0;dockMode=0;}
    opa=new J3DDrawBuffer(512);xlu=new J3DDrawBuffer(512);opa->setNonSort();xlu->setNonSort();
    squid=makeSquid();idleSquid=makeSquid();
    scenery=model(currentStage==PLAZA?"/data/scene/pinnaBeach0.szs":"/data/scene/dolpic0.szs","map.bmd");
    hideScenery(scenery,currentStage!=PLAZA);
    if(scenery) {
        Mtx transform;C_MTXIdentity(transform);
        if(currentStage==PLAZA){transform[0][0]=cs;transform[0][2]=sn;transform[2][0]=-sn;transform[2][2]=cs;transform[0][3]=islandX;transform[2][3]=islandZ;}
        else {transform[0][0]=cs;transform[0][2]=-sn;transform[2][0]=sn;transform[2][2]=cs;transform[0][3]=-cs*islandX+sn*islandZ;transform[2][3]=-sn*islandX-cs*islandZ;}
        scenery->setBaseTRMtx(transform);
    }
    // Replace only the baked distant counterpart. The nearby native map is untouched.
    J3DModel* nativeMap=gpMap->getModelManager()->getJointModel(0)->getModel();
    J3DModelData* map=nativeMap->getModelData();
    if(currentStage==PLAZA && map->getShapeNum()>22){map->getShapeNodePointer(22)->onFlag(J3DShpFlag_Visible);nativeMap->getShapePacket(22)->hide();if(active){map->getShapeNodePointer(10)->onFlag(J3DShpFlag_Visible);nativeMap->getShapePacket(10)->hide();}}
    if(currentStage==pinnaStage && map->getShapeNum()>1){map->getShapeNodePointer(1)->onFlag(J3DShpFlag_Visible);nativeMap->getShapePacket(1)->hide();}
    const ResTIMG* t=(ResTIMG*)resource("/data/scene/dolpic0.szs","wave.bti");
    if(t) {
        GXInitTexObj(&water,(u8*)t+t->imageDataOffset,t->width,t->height,(GXTexFmt)t->format,GX_REPEAT,GX_REPEAT,GX_FALSE);
        GXInitTexObjLOD(&water,GX_LINEAR,GX_LINEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);hasWater=true;
        OSReport("[sea-route] water texture %dx%d format=%d\n",t->width,t->height,t->format);
    }
    // Warm both immutable host copies before departure, not during the swap.
    port_open_world_resource("/data/scene/dolpic0.szs","map.bmd",0,0);
    port_open_world_resource("/data/scene/pinnaBeach0.szs","map.bmd",0,0);
    prefetch(PLAZA,plazaEpisode,0);prefetch(pinnaStage,pinnaEpisode,2);
    ready=squid && idleSquid && scenery && hasWater;
    OSReport("[sea-route] setup stage=%d pinna=%d ready=%d length=%.1f swap=%.1f\n",currentStage,pinnaStage,ready,routeLength,swapDistance);
}
bool sms_sea_pending(){return pendingStage!=-1;}
bool sms_sea_active(){return active;}
bool sms_sea_arriving(TMarDirector* d) {
    return sms_open_world_enabled() && ready && (pendingStage==d->mMap || (!testSpawnUsed && getenv("SMS_SEA_TEST_SPAWN")));
}
void sms_sea_arrive(TMarDirector* d) {
    if(!sms_sea_arriving(d))return;
    TMario* m=gpMarioOriginal;
    if(pendingStage==d->mMap) {
        JGeometry::TVec3<f32> p=native(currentStage,savedPos);
        s16 yaw=savedYaw-(currentStage==PLAZA?0:s16(0x2000));
        m->waitingStart(&p,yaw*(360.f/65536.f));m->mPosition=p;m->mPrevPosition=p;m->resetHistory();
        m->mSurfGesso=squid;m->mSurfGessoType=TMario::SURF_GESSO_TYPE_GREEN;
        m->changePlayerStatus(MARIO_STATUS_SURF,0,true);m->mStatusTimer=0;
        m->mFaceAngle.y=yaw;m->mModelFaceAngle=yaw;m->mIntendedYaw=yaw;
        m->mVel=savedVel;if(currentStage!=PLAZA){m->mVel.x=cs*savedVel.x-sn*savedVel.z;m->mVel.z=sn*savedVel.x+cs*savedVel.z;}
        m->mForwardVel=savedSpeed;m->mSlideVelX=m->mVel.x;m->mSlideVelZ=m->mVel.z;
        float ground=gpMapCollisionData->checkGround(p.x,p.y+100,p.z,0,&m->mGroundPlane);m->mFloorPosition.set(p.x,ground,p.z);
        m->setAnimation(TMario::ANIM_RIDE_SHELL,1.f);m->getMotionFrameCtrl().setFrame(savedAnimFrame);squid->getFrameCtrl(0)->setFrame(savedSquidFrame);
        m->mHealth=savedHealth;if(m->mWaterGun){m->mWaterGun->mCurrentWater=savedWater;m->mWaterGun->changeNozzle((TWaterGun::TNozzleType)savedNozzle,false);m->mWaterGun->mSecondNozzle=savedSecond;}
        JGeometry::TVec3<f32> eye=native(currentStage,savedEye),at=native(currentStage,savedAt);
        gpCamera->endDemoCamera();gpCamera->warpPosAndAt(eye,at);gpCamera->mFovy=savedFov;cameraMatrices(true);
        clearHud(d);gpApplication.mFader->setFadeStatus(TSMSFader::FADE_STATUS_FULLY_FADED_IN);
        pendingStage=-1;releasePending=true;active=true;++crossings;
        OSReport("[sea-route] arrived stage=%d episode=%d crossing=%d elapsed_ms=%u speed=%.2f -> %.2f squid_frame=%.2f health=%d water=%d\n",currentStage,d->unk7D,crossings,(unsigned)(port_open_world_milliseconds()-crossingStart),savedSpeed,m->mForwardVel,savedSquidFrame,m->mHealth,savedWater);
        OSReport("[sea-route] animation mario=%.2f -> %.2f blooper=%.2f -> %.2f hop=%.2f lateral=%.2f\n",savedAnimFrame,m->getMotionFrameCtrl().getFrame(),savedSquidFrame,squid->getFrameCtrl(0)->getFrame(),hop,lateral);
    } else {
        JGeometry::TVec3<f32> p=currentStage==PLAZA?JGeometry::TVec3<f32>(-11000,330,1400):JGeometry::TVec3<f32>(6000,120,7300);
        m->waitingStart(&p,currentStage==PLAZA?-90:0);m->mPosition=p;m->mVel.zero();m->resetHistory();testSpawnUsed=true;
        Vec eye={p.x+(currentStage==PLAZA?-850:0),p.y+450,p.z+(currentStage==PLAZA?0:-850)},at={p.x,p.y+120,p.z};
        gpCamera->endDemoCamera();gpCamera->warpPosAndAt(eye,at);cameraMatrices(true);
        clearHud(d);gpApplication.mFader->setFadeStatus(TSMSFader::FADE_STATUS_FULLY_FADED_IN);
        OSReport("[sea-route] test spawn stage=%d\n",currentStage);
    }
}
void sms_sea_tick(TMarDirector* d) {
    testPadActive=false;testButtons=0;testStickX=testStickY=0;
    if(!sms_open_world_enabled() || pendingStage!=-1 || d->mState!=TMarDirector::STATE_UNK4 || d->unk124 || d->unk4C&0x1ff)return;
    if(testParkWalk(d) || !ready)return;
    TMario* m=gpMarioOriginal;TMarioGamePad* pad=m->getGamePad();float dt=port_fps60_active?.5f:1.f;
    if(cooldown>0)--cooldown;
    if(getenv("SMS_SEA_TEST_RIDES") && !testDone) {
        // Store scalar inputs for PADRead; never dereference stage objects there.
        testPadActive=true;
        if(!active && !cooldown && nearStation() && ((++testPulse%16)<4))testButtons=JUTGamePad::X;
        if(active && getenv("SMS_SEA_TEST_CONTROL")) {
            if(rideElapsed>=80 && rideElapsed<110)testStickX=70;
            if(rideElapsed>=110 && rideElapsed<140)testStickX=-70;
            if(fabsf(progress-swapDistance)<750 && !testHopSent)testButtons=JUTGamePad::A;
            if(getenv("SMS_SEA_TEST_TURNBACK") && rideElapsed>180 && !testTurnSent)testButtons=JUTGamePad::B;
        }
    }
    if(!active) {
        if(!cooldown && nearStation() && pad->testTrigger(JUTGamePad::X))board();
        return;
    }
    rideTime+=dt;rideElapsed+=dt;
    if(pad->testTrigger(JUTGamePad::B) && !cooldown && !dockMode){direction=-direction;cooldown=30;testTurnSent=true;OSReport("[sea-route] turned back progress=%.1f direction=%d\n",progress,direction);}
    if(pad->testTrigger(JUTGamePad::A) && hop<=0 && !dockMode){hop=40;testHopSent=true;OSReport("[sea-route] hopped progress=%.1f\n",progress);}
    if(hop>0)hop=fmaxf(0,hop-dt);
    lateral+=pad->getMainStickX()*12*dt*direction;
    if(lateral>350)lateral=350;if(lateral<-350)lateral=-350;
    float remaining=direction>0?routeLength-progress:progress;
    float speed=fminf(85.f,28.f+remaining*.025f);
    float oldProgress=progress;
    if(!dockMode)progress+=direction*speed*dt;
    if(!dockMode && (progress>=routeLength || progress<=0)) {
        progress=fmaxf(0,fminf(routeLength,progress));
        if(currentStage==PLAZA && direction<0) {
            const TBGCheckData* ground;float y=gpMapCollisionData->checkGround(-11000,650,1400,0,&ground);
            dockMode=-1;dockRemaining=30;dockFrom=world(currentStage,m->mPosition);dockTo.set(-11000.f,fmaxf(y,100.f)+5.f,1400.f);
        } else {land();return;}
    }
    if(remaining<1500)lateral*=powf(.96f,dt);
    P tangent,p=sample(progress,&tangent);p.x+=tangent.z*lateral;p.z-=tangent.x*lateral;
    float height=8+3*sinf(rideTime*.11f)+(hop>0?100*sinf(hop/40*3.14159265f):0);
    JGeometry::TVec3<f32> nextWorld(p.x,height,p.z);
    if(dockMode) {
        dockRemaining=fmaxf(0.f,dockRemaining-dt);float t=1-dockRemaining/30,blend=t*t*(3-2*t);
        nextWorld.set(dockFrom.x+(dockTo.x-dockFrom.x)*blend,
            dockFrom.y+(dockTo.y-dockFrom.y)*blend+200*sinf(t*3.14159265f),
            dockFrom.z+(dockTo.z-dockFrom.z)*blend);
        if(dockRemaining<=0) {
            if(dockMode<0){land();return;}dockMode=0;
        }
    }
    JGeometry::TVec3<f32> next=native(currentStage,nextWorld);
    // Gentle beach run-out remains above the sampled native sand.
    const TBGCheckData* plane;float ground=gpMapCollisionData->checkGround(next.x,next.y+300,next.z,0,&plane);
    if(ground>height && ground<200)next.y=ground+5;
    m->mPosition=next;m->mGroundPlane=plane;m->mFloorPosition.set(next.x,ground,next.z);
    m->mSurfGesso=squid;m->mSurfGessoType=TMario::SURF_GESSO_TYPE_GREEN;
    if(m->mStatus!=MARIO_STATUS_SURF)m->changePlayerStatus(MARIO_STATUS_SURF,0,true);
    m->mStatusTimer=0;m->setAnimation(TMario::ANIM_RIDE_SHELL,1.f);
    float angle=atan2f(tangent.x*direction,tangent.z*direction)-(currentStage==PLAZA?0:rotation);
    m->mFaceAngle.y=s16(int(angle*65536.f/6.283185307f));m->mModelFaceAngle=m->mFaceAngle.y;m->mIntendedYaw=m->mFaceAngle.y;
    P velocity={tangent.x*direction*speed,tangent.z*direction*speed};
    if(currentStage!=PLAZA){float x=cs*velocity.x-sn*velocity.z,z=sn*velocity.x+cs*velocity.z;velocity.x=x;velocity.z=z;}
    m->mVel.set(velocity.x,0.f,velocity.z);m->mForwardVel=speed;m->mSlideVelX=velocity.x;m->mSlideVelZ=velocity.z;
    d->mConsole->startDisappearTelop();
    static int logTimer;
    if(getenv("SMS_OPEN_WORLD_LOG") && (++logTimer%60)==0)OSReport("[sea-route] position stage=%d progress=%.1f lateral=%.1f xyz=(%.1f,%.1f,%.1f) speed=%.2f status=%x\n",currentStage,progress,lateral,next.x,next.y,next.z,speed,m->mStatus);
    if((currentStage==PLAZA && direction>0 && oldProgress<swapDistance && progress>=swapDistance)
       || (currentStage==pinnaStage && direction<0 && oldProgress>swapDistance && progress<=swapDistance))cross(d);
}
bool sms_sea_camera() {
    if((!active && landingCamera<=0) || !ready || pendingStage!=-1 || !gpMarDirector || gpMarDirector->mState!=TMarDirector::STATE_UNK4)return false;
    if(!active) {
        float dt=port_fps60_active?.5f:1.f;landingCamera=fmaxf(0.f,landingCamera-dt);
        JGeometry::TVec3<f32> e=world(currentStage,gpCamera->mPosition),a=world(currentStage,gpCamera->mTarget);
        float blend=1-powf(.94f,dt);
        seaEye.x+=(e.x-seaEye.x)*blend;seaEye.y+=(e.y-seaEye.y)*blend;seaEye.z+=(e.z-seaEye.z)*blend;
        seaAt.x+=(a.x-seaAt.x)*blend;seaAt.y+=(a.y-seaAt.y)*blend;seaAt.z+=(a.z-seaAt.z)*blend;
        gpCamera->warpPosAndAt(native(currentStage,seaEye),native(currentStage,seaAt));cameraMatrices();return true;
    }
    P tangent;sample(progress,&tangent);
    JGeometry::TVec3<f32> player=world(currentStage,gpMarioOriginal->mPosition);
    float dt=port_fps60_active?.5f:1.f,blend=1-powf(.9f,dt);
    JGeometry::TVec3<f32> target(player.x,player.y+130,player.z),eye(player.x-tangent.x*direction*700,player.y+350,player.z-tangent.z*direction*700);
    seaEye.x+=(eye.x-seaEye.x)*blend;seaEye.y+=(eye.y-seaEye.y)*blend;seaEye.z+=(eye.z-seaEye.z)*blend;
    seaAt.x+=(target.x-seaAt.x)*blend;seaAt.y+=(target.y-seaAt.y)*blend;seaAt.z+=(target.z-seaAt.z)*blend;
    JGeometry::TVec3<f32> e=native(currentStage,seaEye),a=native(currentStage,seaAt);
    gpCamera->warpPosAndAt(e,a);gpCamera->unk258=s16(int(atan2f(e.x-a.x,e.z-a.z)*65536.f/6.283185307f));cameraMatrices();return true;
}
void sms_sea_draw(unsigned cue,JDrama::TGraphics* graphics) {
    if(!(cue&CUE_DRAW) || !sms_open_world_enabled() || !ready)return;
    gpCamera->perform(CUE_CALC_VIEW|CUE_SET_PROJECTION,graphics);
    drawModel(scenery,graphics);
    if(!active) {
        P p=fromWorld(currentStage,station());Mtx transform;C_MTXIdentity(transform);transform[0][3]=p.x;transform[1][3]=10+4*sinf(rideTime*.05f);transform[2][3]=p.z;
        idleSquid->getModel()->setBaseTRMtx(transform);idleSquid->frameUpdate();idleSquid->calc();drawModel(idleSquid->getModel(),graphics);
    }
    if(active) {
        // Identical world-space ocean texture and phase on both maps.
        JGeometry::TVec3<f32> center=world(currentStage,gpMarioOriginal->mPosition);
        GXLoadPosMtxImm(graphics->getViewMtx(),GX_PNMTX0);GXSetCurrentMtx(GX_PNMTX0);GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS,GX_DIRECT);GXSetVtxDesc(GX_VA_CLR0,GX_DIRECT);GXSetVtxDesc(GX_VA_TEX0,GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT7,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GXSetVtxAttrFmt(GX_VTXFMT7,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);GXSetVtxAttrFmt(GX_VTXFMT7,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
        GXSetNumChans(1);GXSetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,0,GX_DF_NONE,GX_AF_NONE);
        GXSetNumTexGens(1);GXSetTexCoordGen2(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY,GX_FALSE,GX_PTIDENTITY);GXLoadTexObj(&water,GX_TEXMAP0);
        GXSetNumTevStages(1);GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);GXSetTevColor(GX_TEVREG0,(GXColor){28,140,195,255});GXSetTevColor(GX_TEVREG1,(GXColor){85,220,238,255});
        GXSetTevColorIn(GX_TEVSTAGE0,GX_CC_C0,GX_CC_C1,GX_CC_TEXC,GX_CC_ZERO);
        GXSetTevColorOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
        GXSetTevAlphaIn(GX_TEVSTAGE0,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_A0);
        GXSetTevAlphaOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
        GXSetTevSwapMode(GX_TEVSTAGE0,GX_TEV_SWAP0,GX_TEV_SWAP0);GXSetNumIndStages(0);GXSetTevDirect(GX_TEVSTAGE0);
        GXSetZTexture(GX_ZT_DISABLE,GX_TF_Z24X8,0);GXSetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);GXSetZCompLoc(GX_TRUE);GXSetCullMode(GX_CULL_NONE);GXSetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,255});
        GXSetColorUpdate(GX_TRUE);GXSetAlphaUpdate(GX_TRUE);GXSetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
        const int grid=24;const float extent=100000,step=2*extent/grid;
        GXBegin(GX_QUADS,GX_VTXFMT7,grid*grid*4);
        for(int z=0;z<grid;++z)for(int x=0;x<grid;++x)for(int k=0;k<4;++k) {
            float wx=center.x-extent+(x+(k==2 || k==3))*step,wz=center.z-extent+(z+(k==1 || k==2))*step;
            JGeometry::TVec3<f32> p=native(currentStage,JGeometry::TVec3<f32>(wx,2.f,wz));
            GXPosition3f32(p.x,p.y,p.z);GXColor4u8(55,205,220,235);GXTexCoord2f32(wx/1600+rideTime*.004f,wz/1600+rideTime*.002f);
        }
        GXEnd();
    }
    if(releasePending && gpApplication.mFader->isFullyFadedIn()) {
        if(GXPC_CoastalHold)GXPC_CoastalHold(0);releasePending=false;
        OSReport("[sea-route] continuous frame released crossing=%d\n",crossings);
    }
    if(active || nearStation()) {
        J2DOrthoGraph graph(graphics->getViewport());graph.setPort();graph.setup2D();gpSystemFont->setGX();
        const char* text=active?(direction>0?"PINNA PARK   |   Steer: Stick   Hop: A   Turn back: B":"DELFINO PLAZA   |   Steer: Stick   Hop: A   Turn back: B"):(currentStage==PLAZA?"X  Ride a Blooper to Pinna Park":"X  Ride a Blooper to Delfino Plaza");
        gpSystemFont->setCharColor(JUtility::TColor(22,54,69,255));gpSystemFont->drawString_scale(49,417,15,18,text,true);
        gpSystemFont->setCharColor(JUtility::TColor(255,248,215,255));gpSystemFont->drawString_scale(48,416,15,18,text,true);
    }
}

extern "C" int sms_sea_test_pad(signed char* x,signed char* y,unsigned short* buttons) {
    if(!testPadActive)return 0;
    *x=testStickX;*y=testStickY;*buttons|=testButtons;return 1;
}

void sms_sea_filter_map() {
    if(!ready || !sms_open_world_enabled() || !gpMap)return;
    J3DModel* m=gpMap->getModelManager()->getJointModel(0)->getModel();
    J3DModelData* d=m->getModelData();
    // Native backdrop cards were built for a land-bound camera. They cannot
    // be approached as terrain. Replace them with our registered neighbor.
    for(unsigned i=0;i<d->getShapeNum();++i) {
        bool replace=currentStage==PLAZA?i==22:i==1;
        bool background=currentStage==PLAZA?(i==10 || i==11 || i==12 || i==19):i<9;
        bool hide=replace || (active && background);
        if(hide){d->getShapeNodePointer(i)->onFlag(J3DShpFlag_Visible);m->getShapePacket(i)->hide();}
        else if(background){d->getShapeNodePointer(i)->offFlag(J3DShpFlag_Visible);m->getShapePacket(i)->show();}
    }
}
