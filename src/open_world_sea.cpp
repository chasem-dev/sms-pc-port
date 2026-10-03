// Optional open-water Plaza / Pinna connection. All models and textures are
// loaded from the player's disc; native Mario renders the animated Blooper.
#include <sms_open_world.h>
#include <sms_open_world_sea.h>
#include <System/Application.hpp>
#include <System/MarDirector.hpp>
#include <System/MarioGamePad.hpp>
#include <System/ScenarioArchiveName.hpp>
#include <System/Resolution.hpp>
#include <System/FlagManager.hpp>
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
#include <MarioUtil/LightUtil.hpp>
#include <MarioUtil/MathUtil.hpp>
#include <MarioUtil/PacketUtil.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/J3D/J3DGraphLoader/J3DModelLoader.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
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
float islandX=-43000,islandZ=-26000;
bool geography(){const char* value=getenv("SMS_OPEN_WORLD_TEST_GEOGRAPHY");return !value || strcmp(value,"0")!=0;}
struct P { float x,z; };
P toWorld(int stage,P p) {
    if(stage==PLAZA)return p;
    if(stage==3 && geography()){Mtx m;sms_open_world_harbor_transform(m);P q={m[0][0]*p.x+m[0][2]*p.z+m[0][3],m[2][0]*p.x+m[2][2]*p.z+m[2][3]};return q;}
    P q={islandX+cs*p.x+sn*p.z,islandZ-sn*p.x+cs*p.z};return q;
}
P fromWorld(int stage,P p) {
    if(stage==PLAZA)return p;
    if(stage==3 && geography()){Mtx m;sms_open_world_harbor_transform(m);float x=p.x-m[0][3],z=p.z-m[2][3];P q={m[0][0]*x+m[2][0]*z,m[0][2]*x+m[2][2]*z};return q;}
    float x=p.x-islandX,z=p.z-islandZ;P q={cs*x-sn*z,sn*x+cs*z};return q;
}
JGeometry::TVec3<f32> world(int stage,const JGeometry::TVec3<f32>& p) {
    P a={p.x,p.z};P b=toWorld(stage,a);return JGeometry::TVec3<f32>(b.x,p.y,b.z);
}
JGeometry::TVec3<f32> native(int stage,const JGeometry::TVec3<f32>& p) {
    P a={p.x,p.z};P b=fromWorld(stage,a);return JGeometry::TVec3<f32>(b.x,p.y,b.z);
}
P controls[6],path[401];float lengths[401],routeLength,swapDistance,progress,lateral,rideTime,hop;
int direction=1,rideCount,crossings,cooldown,releaseFrames;float landingCamera,skyFrame,cameraHeading,cameraRadius;
bool active,ready,releasePending,testSpawnUsed,testDone,testPadActive;
signed char testStickX,testStickY;unsigned short testButtons;int testPulse;
MActor *squid,*idleSquid;J3DModel* scenery;
J3DModel* harborScenery;
const unsigned maxSceneryObjects=64;
J3DModel* sceneryObjects[maxSceneryObjects];unsigned sceneryObjectCount;
GXColor monumentColor;
J3DDrawBuffer *opa,*xlu;
GXTexObj water;bool hasWater;float waterTime,drawnWaterTime;
JGeometry::TVec3<f32> seaEye,seaAt,savedPos,savedEye,savedAt,savedVel;
float savedSpeed,savedFov,savedAnimFrame,savedSquidFrame,rideElapsed;bool testHopSent,testTurnSent;
float savedWaistRoll,savedWaistPitch;
s16 savedYaw,savedPreviousYaw,savedFaceRoll,savedHealth;int savedWater;u8 savedNozzle,savedSecond;
unsigned long long crossingStart;
int dockMode;float dockRemaining;JGeometry::TVec3<f32> dockFrom,dockTo;
int testParkNode;bool testParkEntered,testParkControlled;
JGeometry::TVec3<f32> testParkStart;
void initPath() {
    controls[0].x=-11400;controls[0].z=1650;
    controls[1].x=-15000;controls[1].z=4500;
    controls[2].x=-27000;controls[2].z=-7000;
    if(geography()) {
        islandX=-46000;islandZ=19000;
        controls[0].x=-10000;controls[0].z=-6600;
        controls[1].x=-16300;controls[1].z=-4000;
        controls[2].x=-20500;controls[2].z=10000;
    }
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
    routeLength=lengths[400];
    // Exchange after clearing the Plaza waterfront, while Pinna is still
    // offshore. Its native shoreline and actors are ready for the approach.
    swapDistance=lengths[geography()?140:100];
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
bool stageArchive(int stage,int episode,char* path,unsigned size) {
    if(!gpApplication.unk30 || stage<0 || stage>=gpApplication.unk30->getChildren().size())return false;
    TNameRefAryT<TScenarioArchiveName>* names=gpApplication.unk30->getChildren()[stage];
    if(episode<0 || episode>=names->size())return false;
    snprintf(path,size,"/data/scene/%s",names->getChildren()[episode].mArcName);
    char* ext=strstr(path,".arc");if(ext)strcpy(ext,".szs");return true;
}
unsigned sceneU16(const u8* p){return (unsigned(p[0])<<8)|p[1];}
unsigned sceneU32(const u8* p){return (unsigned(p[0])<<24)|(unsigned(p[1])<<16)|(unsigned(p[2])<<8)|p[3];}
bool sceneString(const u8*& p,const u8* end,const u8*& string,unsigned& size) {
    if(end-p<2)return false;
    size=sceneU16(p);p+=2;if(unsigned(end-p)<size)return false;
    string=p;p+=size;return true;
}
bool sceneEquals(const u8* string,unsigned size,const char* value) {
    return size==strlen(value) && !memcmp(string,value,size);
}
// Read only known, stationary scene objects. Native actor constructors would
// register their collisions and managers in the currently active stage.
// The preview instead uses the destination episode's exact saved SRT and
// shares model data between instances, with no gameplay actors or collisions.
void loadSceneryObjects(const char* archive,const Mtx stageTransform) {
    const char* types[]={"Palm","PalmLeaf","PalmNatume","MonumentShine"};
    const char* objects[]={"palmNormal","palmLeaf","palmNatume","monumentshine"};
    const char* models[]={"palmnormal.bmd","palmleaf.bmd","palmnatume.bmd","monumentshine.bmd"};
    J3DModelData* cache[4]={0,0,0,0};
    unsigned size=port_open_world_resource(archive,"scene.bin",0,0);
    if(!size || size>1024*1024)return;
    u8* bytes=(u8*)JKRHeap::alloc(size,32,0);if(!bytes)return;
    if(port_open_world_resource(archive,"scene.bin",bytes,size)!=size){JKRHeap::free(bytes,0);return;}
    unsigned before=sceneryObjectCount;
    for(unsigned offset=0;offset+16<size && sceneryObjectCount<maxSceneryObjects;++offset) {
        const u8* node=bytes+offset;unsigned length=sceneU32(node),typeLength=sceneU16(node+6);
        if(length<16 || length>size-offset || typeLength>length-8)continue;
        int type=-1;for(int i=0;i<4;++i)if(sceneEquals(node+8,typeLength,types[i])){type=i;break;}
        if(type<0 || sceneU16(node+4)!=JDrama::TNameRef::calcKeyCode(types[type]))continue;
        const u8* end=node+length;const u8* p=node+8+typeLength;
        if(end-p<4)continue;
        unsigned nameKey=sceneU16(p);p+=2;const u8* name;unsigned nameLength;
        if(!sceneString(p,end,name,nameLength))continue;
        unsigned key=0;for(unsigned i=0;i<nameLength;++i)key=(key*3+name[i])&65535;
        if(key!=nameKey || end-p<36)continue;
        float srt[9];bool valid=true;
        for(int i=0;i<9;++i){unsigned bits=sceneU32(p+i*4);memcpy(&srt[i],&bits,4);if(!isfinite(srt[i]) || fabsf(srt[i])>1000000)valid=false;}
        p+=36;if(!valid || srt[6]<=0 || srt[7]<=0 || srt[8]<=0)continue;
        const u8* text;unsigned textLength;
        if(!sceneString(p,end,text,textLength) || end-p<4)continue;
        unsigned lights=sceneU32(p);p+=4;if(lights>8)continue;
        for(unsigned i=0;i<lights;++i){if(end-p<4){valid=false;break;}p+=4;if(!sceneString(p,end,text,textLength)){valid=false;break;}}
        if(!valid || !sceneString(p,end,text,textLength))continue;
        // The scene instantiates both normal and cliff palms as class Palm.
        if(type==0 && sceneEquals(text,textLength,"palmLeaf"))type=1;
        if(!sceneEquals(text,textLength,objects[type]))continue;
        J3DModel* instance;
        if(!cache[type]){instance=model(archive,models[type]);if(!instance)continue;cache[type]=instance->getModelData();}
        else {instance=new J3DModel(cache[type],0,1);SMS_MakeDLAndLock(instance);}
        Mtx local,transform;
        MsMtxSetXYZRPH(local,srt[0],srt[1],srt[2],srt[3],srt[4],srt[5]);
        MTXConcat(stageTransform,local,transform);instance->setBaseTRMtx(transform);
        instance->setBaseScale(JGeometry::TVec3<f32>(srt[6],srt[7],srt[8]));
        if(type==3) {
            monumentColor=(GXColor){255,255,255,u8(TFlagManager::getInstance()->getFlag(0x10063)?0:100)};
            SMS_InitPacket_OneTevKColor(instance,0,GX_KCOLOR0,&monumentColor);
        }
        sceneryObjects[sceneryObjectCount++]=instance;
        offset+=length-1;
    }
    JKRHeap::free(bytes,0);
    OSReport("[sea-route] scene objects archive=%s count=%u\n",archive,sceneryObjectCount-before);
}
void prefetch(int stage,int episode,int slot) {
    char path[128];if(stageArchive(stage,episode,path,sizeof path))port_open_world_prefetch(path,slot);
}
// The Plaza mesh includes the old distant Pinna terrain in the same shape as
// its nearby cliffs. Keep the mainland portion when replacing that island;
// hiding the entire shape removes the ground behind the Plaza as well.
void retainPlazaCoast(J3DModel* model) {
    if(!model || model->getModelData()->getShapeNum()<=11)return;
    J3DModelData* data=model->getModelData();
    const GXVtxAttrFmtList* fmt=data->getVertexData().getVtxAttrFmtList();
    while(fmt->attr!=GX_VA_NULL && fmt->attr!=GX_VA_POS)++fmt;
    if(fmt->attr!=GX_VA_POS || fmt->type!=GX_F32 || fmt->cnt!=GX_POS_XYZ)return;
    const Vec* positions=(const Vec*)data->getVtxPosArray();
    J3DShape* shape=data->getShapeNodePointer(11);
    unsigned stride=0,posOffset=0,posBytes=0;
    for(const GXVtxDescList* v=shape->getVtxDesc();v->attr!=GX_VA_NULL;++v) {
        unsigned size=v->type==GX_NONE?0:v->type==GX_INDEX16?2:1;
        if(v->type==GX_DIRECT && v->attr>=GX_VA_POS)return;
        if(v->attr==GX_VA_POS){posOffset=stride;posBytes=size;}
        stride+=size;
    }
    if(!stride || !posBytes)return;
    unsigned removed=0,retained=0;
    for(unsigned group=0;group<shape->getMtxGroupNum();++group) {
        J3DShapeDraw* draw=shape->getShapeDraw(group);
        const u8* begin=draw->getDisplayList();const u8* end=begin+draw->getDisplayListSize();
        // Converting a strip/fan to independent triangles needs at most 3x
        // its vertex bytes plus per-triangle primitive headers.
        unsigned capacity=draw->getDisplayListSize()*6+32;
        u8* out=(u8*)JKRHeap::alloc(capacity,32,0);if(!out)return;
        u8* dest=out;bool valid=true;
        for(const u8* p=begin;p<end;) {
            u8 command=*p++;if(!command)continue;
            if(end-p<2){valid=false;break;}
            unsigned count=(p[0]<<8)|p[1];p+=2;
            unsigned primitive=command&0xf8;
            if(p+count*stride>end || (primitive!=GX_TRIANGLESTRIP && primitive!=GX_TRIANGLEFAN
                && primitive!=GX_TRIANGLES && primitive!=GX_QUADS)){valid=false;break;}
            unsigned triangles=primitive==GX_QUADS?count/4*2:primitive==GX_TRIANGLES?count/3:count>2?count-2:0;
            for(unsigned t=0;t<triangles;++t) {
                unsigned index[3];
                if(primitive==GX_TRIANGLES){index[0]=t*3;index[1]=t*3+1;index[2]=t*3+2;}
                else if(primitive==GX_QUADS){index[0]=t/2*4;index[1]=index[0]+1+t%2;index[2]=index[1]+1;}
                else if(primitive==GX_TRIANGLEFAN){index[0]=0;index[1]=t+1;index[2]=t+2;}
                else {index[0]=t+(t&1);index[1]=t+1-(t&1);index[2]=t+2;}
                bool mainland=false;
                for(int k=0;k<3;++k) {
                    const u8* v=p+index[k]*stride+posOffset;
                    unsigned id=posBytes==2?(v[0]<<8)|v[1]:v[0];
                    if(id>=data->getVtxNum()){valid=false;break;}
                    if(positions[id].x>=-16000.f)mainland=true;
                }
                if(!valid)break;
                if(!mainland){++removed;continue;}
                ++retained;*dest++=GX_TRIANGLES|(command&7);*dest++=0;*dest++=3;
                for(int k=0;k<3;++k){memcpy(dest,p+index[k]*stride,stride);dest+=stride;}
            }
            if(!valid)break;
            p+=count*stride;
        }
        if(valid) {
            while((dest-out)&31)*dest++=0;
            draw->mDisplayList=out;draw->mDisplayListSize=dest-out;
        } else {JKRHeap::free(out,0);OSReport("[sea-route] unsupported coast display list\n");}
    }
    OSReport("[sea-route] mainland triangles retained=%u replaced=%u\n",retained,removed);
}

void cropNeighbor(J3DModel* model,bool plaza,bool preview=true) {
    if(!model)return;
    J3DModelData* data=model->getModelData();
    const GXVtxAttrFmtList* fmt=data->getVertexData().getVtxAttrFmtList();
    while(fmt->attr!=GX_VA_NULL && fmt->attr!=GX_VA_POS)++fmt;
    if(fmt->attr!=GX_VA_POS || fmt->type!=GX_F32 || fmt->cnt!=GX_POS_XYZ)return;
    const Vec* positions=(const Vec*)data->getVtxPosArray();
    unsigned removed=0,retained=0;
    int bodyShape=-1,wallShape=-1;
    if(!plaza && data->getMaterialName()) {
        int material=data->getMaterialName()->getIndex("_MainBody_m");
        if(material>=0 && material<data->getMaterialNum())bodyShape=data->getMaterialNodePointer(material)->getShape()->getIndex();
        material=data->getMaterialName()->getIndex("_o8985");
        if(material>=0 && material<data->getMaterialNum())wallShape=data->getMaterialNodePointer(material)->getShape()->getIndex();
    }
    for(unsigned shapeIndex=0;shapeIndex<data->getShapeNum();++shapeIndex) {
        if(!preview && int(shapeIndex)!=bodyShape && int(shapeIndex)!=wallShape)continue;
        J3DShape* shape=data->getShapeNodePointer(shapeIndex);
        if(shape->checkFlag(J3DShpFlag_Visible))continue;
        unsigned stride=0,posOffset=0,posBytes=0;
        for(const GXVtxDescList* v=shape->getVtxDesc();v->attr!=GX_VA_NULL;++v) {
            unsigned size=v->type==GX_NONE?0:v->type==GX_INDEX16?2:1;
            if(v->type==GX_DIRECT && v->attr>=GX_VA_POS)return;
            if(v->attr==GX_VA_POS){posOffset=stride;posBytes=size;}
            stride+=size;
        }
        if(!stride || !posBytes)return;
        for(unsigned group=0;group<shape->getMtxGroupNum();++group) {
            J3DShapeDraw* draw=shape->getShapeDraw(group);
            const u8* begin=draw->getDisplayList();const u8* end=begin+draw->getDisplayListSize();
            // Count surviving triangles first: the 32-bit stage heap cannot
            // afford worst-case display-list buffers for two neighboring maps.
            u8* out=0;unsigned bytes=0;bool valid=true;
            for(int pass=0;pass<2 && valid;++pass) {
                u8* dest=out;u8* header=0;
                unsigned primitiveVertices=0;int format=-1;
                for(const u8* p=begin;p<end;) {
                    u8 command=*p++;if(!command)continue;
                    if(end-p<2){valid=false;break;}
                    unsigned count=(p[0]<<8)|p[1];p+=2;
                    unsigned primitive=command&0xf8;
                    if(p+count*stride>end || (primitive!=GX_TRIANGLESTRIP && primitive!=GX_TRIANGLEFAN
                        && primitive!=GX_TRIANGLES && primitive!=GX_QUADS)){valid=false;break;}
                    unsigned triangles=primitive==GX_QUADS?count/4*2:primitive==GX_TRIANGLES?count/3:count>2?count-2:0;
                    for(unsigned t=0;t<triangles;++t) {
                        unsigned index[3];
                        if(primitive==GX_TRIANGLES){index[0]=t*3;index[1]=t*3+1;index[2]=t*3+2;}
                        else if(primitive==GX_QUADS){index[0]=t/2*4;index[1]=index[0]+1+t%2;index[2]=index[1]+1;}
                        else if(primitive==GX_TRIANGLEFAN){index[0]=0;index[1]=t+1;index[2]=t+2;}
                        else {index[0]=t+(t&1);index[1]=t+1-(t&1);index[2]=t+2;}
                        bool mainland=!preview,backdropFloor=int(shapeIndex)==bodyShape,outsideBacking=false,nearWall=true,backHill=true,rightBacking=int(shapeIndex)==bodyShape;
                        bool vesselRigging=preview && !plaza && shapeIndex==6;
                        float lowY=1e30f,highY=-1e30f;
                        for(int k=0;k<3;++k) {
                            const u8* v=p+index[k]*stride+posOffset;
                            unsigned id=posBytes==2?(v[0]<<8)|v[1]:v[0];
                            if(id>=data->getVtxNum()){valid=false;break;}
                            const Vec& p=positions[id];
                            if(p.x< -20500.f || p.x>15401.f || p.z< -3500.f || p.z>4452.f
                                || p.y< -1200.f || p.y>1700.f)nearWall=false;
                            if(p.x< -20500.f || p.x>10000.f || p.z< -16000.f || p.z>0.f
                                || p.y< -1200.f || p.y>18000.f)backHill=false;
                            lowY=fminf(lowY,p.y);highY=fmaxf(highY,p.y);
                            if(p.x<14899.f || p.x>15401.f || p.z<99.f || p.z>4452.f || p.y>1620.f)rightBacking=false;
                            if(p.y>65.f)backdropFloor=false;
                            if(p.z< -3500.f)outsideBacking=true;
                            if(p.x<16000.f || p.x>20000.f || p.z<10000.f || p.z>12600.f
                               || p.y<1000.f || p.y>3000.f)vesselRigging=false;
                            if(p.y>=-5.f && p.y<=(plaza?35000.f:7500.f)
                                && p.x>=(plaza?-24000.f:-20500.f) && p.x<=(plaza?28000.f:30000.f)
                                && p.z>=(plaza?-60000.f:-3500.f) && p.z<=(plaza?14000.f:20000.f))mainland=true;
                        }
                        if(!valid)break;
                        // These faces form the old northern backdrop's water-level base,
                        // not the playable quay. Its top otherwise spans the Plaza
                        // boat lane after rotating the Harbor into the shared coast.
                        if(backdropFloor && (outsideBacking || highY> -100.f))mainland=false;
                        if(rightBacking && lowY< -500.f && highY>=1500.f)mainland=false;
                        if(int(shapeIndex)==wallShape)mainland=(nearWall || backHill) && highY>=250.f;
                        // The map contains two curved fittings belonging to a
                        // separate vessel actor. Without that actor in the
                        // distant preview they hang unsupported in the sky.
                        if(vesselRigging)mainland=false;
                        if(!mainland){if(!pass)++removed;continue;}
                        // Merge compatible triangles into one GX primitive;
                        // thousands of tiny headers waste per-frame CPU work.
                        bool newPrimitive=format!=(command&7) || primitiveVertices+3>65535;
                        if(newPrimitive){format=command&7;primitiveVertices=0;}
                        primitiveVertices+=3;
                        if(!pass){++retained;bytes+=3*stride+(newPrimitive?3:0);continue;}
                        if(newPrimitive){header=dest;*dest++=GX_TRIANGLES|format;dest+=2;}
                        header[1]=primitiveVertices>>8;header[2]=primitiveVertices&255;
                        for(int k=0;k<3;++k){memcpy(dest,p+index[k]*stride,stride);dest+=stride;}
                    }
                    if(!valid)break;
                    p+=count*stride;
                }
                if(valid && !pass) {
                    unsigned capacity=(bytes+31)&~31u;if(!capacity)capacity=32;
                    out=(u8*)JKRHeap::alloc(capacity,32,0);if(!out)return;
                } else if(valid) {
                    while((dest-out)&31)*dest++=0;
                    draw->mDisplayList=out;draw->mDisplayListSize=dest-out;
                }
            }
            if(!valid){if(out)JKRHeap::free(out,0);OSReport("[sea-route] unsupported coast display list\n");}
        }
    }
    OSReport("[coast-geography] preview triangles retained=%u removed=%u\n",retained,removed);
}

// Despite its name, setting J3DShpFlag_Visible suppresses native entry.
void hideScenery(J3DModel* m,bool plaza) {
    if(!m)return;
    J3DModelData* d=m->getModelData();
    if(plaza) {
        // Shape 10 (_m00suna) also contains the nearby islands' sand.
        // It is terrain, even though its bounds extend across the backdrop.
        const int hidden[]={19,22};
        for(unsigned i=0;i<sizeof(hidden)/sizeof(hidden[0]);++i)
            if(hidden[i]<d->getShapeNum()){d->getShapeNodePointer(hidden[i])->onFlag(J3DShpFlag_Visible);m->getShapePacket(hidden[i])->hide();}
        // The underground room's shape index differs between Plaza episodes.
        for(unsigned i=0;i<d->getMaterialNum();++i)if(d->getMaterialName()
            && !strcmp(d->getMaterialName()->getName((u16)i),"_m_underpass")) {
            J3DShape* shape=d->getMaterialNodePointer(i)->getShape();
            shape->onFlag(J3DShpFlag_Visible);m->getShapePacket(shape->getIndex())->hide();
        }
    } else {
        // The beach model includes the static park as well as distant islands.
        // Keep its near island and remove the baked, differently scaled backdrop.
        // Shape 7 (_m15_ji_d) contains the beach beneath the native actors.
        for(int i=0;i<9 && i<d->getShapeNum();++i)if(i!=7){d->getShapeNodePointer(i)->onFlag(J3DShpFlag_Visible);m->getShapePacket(i)->hide();}
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
void drawSceneryObjects(JDrama::TGraphics* g) {
    if(!sceneryObjectCount)return;
    J3DDrawBuffer* old0=j3dSys.getDrawBuffer(0),*old1=j3dSys.getDrawBuffer(1);
    opa->frameInit();xlu->frameInit();j3dSys.setDrawBuffer(opa,0);j3dSys.setDrawBuffer(xlu,1);
    j3dSys.setViewMtx(g->getViewMtx());opa->setZMtx(g->getViewMtx());xlu->setZMtx(g->getViewMtx());
    for(unsigned i=0;i<sceneryObjectCount;++i){J3DModel* m=sceneryObjects[i];m->calc();m->viewCalc();m->entry();}
    SMS_DrawInit();
    if(gpLightManager)gpLightManager->getLightSet(LIGHT_TYPE_MAPOBJECT)->getLightDrawBuffer(0)->perform(CUE_LIGHT,g);
    opa->draw();xlu->draw();j3dSys.setDrawBuffer(old0,0);j3dSys.setDrawBuffer(old1,1);SMS_DrawInit();
}
void cameraMatrices(bool previous=false) {
    C_MTXPerspective(gpCamera->unk16C,gpCamera->mFovy,gpCamera->mAspect,gpCamera->mNear,gpCamera->mFar);
    C_MTXLookAt(gpCamera->unk1EC,&gpCamera->unk124,&gpCamera->mUp,&gpCamera->unk148);
    if(previous){MTXCopy(gpCamera->unk1EC,gpCamera->unk21C);memcpy(gpCamera->unk1AC,gpCamera->unk16C,sizeof gpCamera->unk16C);}
}
void keepCameraClear(JGeometry::TVec3<f32>& eye,const JGeometry::TVec3<f32>& at) {
    if(!geography() || !gpMapCollisionData || currentStage!=PLAZA || progress>2000.f)return;
    const TMapCollisionData* map=gpMapCollisionData;
    // The native line query assumes both endpoints are inside its grid.
    // Offshore cameras need no terrain test and may lie beyond that grid.
    if(fabsf(at.x)>=map->mGridExtentX-1 || fabsf(eye.x)>=map->mGridExtentX-1
       || fabsf(at.z)>=map->mGridExtentY-1 || fabsf(eye.z)>=map->mGridExtentY-1)return;
    for(int i=0;i<8;++i) {
        JGeometry::TVec3<f32> hit;
        if(!map->intersectLine(at,eye,false,&hit))break;
        float x=hit.x-at.x,y=hit.y-at.y,z=hit.z-at.z;
        float distance=sqrtf(x*x+y*y+z*z);
        float scale=distance>45.f?(distance-45.f)/distance:0.f;
        eye.set(at.x+x*scale,at.y+y*scale,at.z+z*scale);
        if(distance<=45.f)break;
    }
}
P station() {return currentStage==PLAZA?controls[0]:controls[4];}
JGeometry::TVec3<f32> plazaLanding() {
    return geography()?JGeometry::TVec3<f32>(-9300.f,400.f,-6800.f)
                      :JGeometry::TVec3<f32>(-11000.f,400.f,1400.f);
}
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
    JGeometry::TVec3<f32> rider=world(currentStage,m->mPosition);
    float cameraX=rider.x-seaEye.x,cameraZ=rider.z-seaEye.z;
    cameraHeading=atan2f(cameraX,cameraZ);cameraRadius=sqrtf(cameraX*cameraX+cameraZ*cameraZ);
    savedFov=55.f;
    m->mSurfGesso=geography()?0:squid;m->mSurfGessoType=TMario::SURF_GESSO_TYPE_GREEN;
    m->changePlayerStatus(geography()?MARIO_STATUS_JUMP:MARIO_STATUS_SURF,0,true);m->mStatusTimer=0;
    m->setAnimation(geography()?TMario::ANIM_JUMP:TMario::ANIM_RIDE_SHELL,1.0f);
    OSReport("[sea-route] boarded stage=%d ride=%d progress=%.1f\n",currentStage,rideCount,progress);
}
void land() {
    TMario* m=gpMarioOriginal;
    JGeometry::TVec3<f32> p=currentStage==PLAZA?plazaLanding():JGeometry::TVec3<f32>(6000,200,6500);
    const TBGCheckData* ground;
    float y=gpMapCollisionData->checkGround(p.x,p.y+300,p.z,0,&ground);
    p.y=fmaxf(y,100.f)+5;
    m->waitingStart(&p,currentStage==PLAZA?(geography()?45:90):(geography()?-115:180));m->mPosition=p;m->mVel.zero();m->mForwardVel=0;
    m->mGroundPlane=ground;m->mFloorPosition.set(p.x,y,p.z);m->mSurfGesso=0;m->resetHistory();
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
    savedPreviousYaw=m->unk9C+(currentStage==PLAZA?0:s16(0x2000));savedFaceRoll=m->mFaceAngle.z;
    savedWaistRoll=m->mWaistRoll;savedWaistPitch=m->mWaistPitch;
    if(geography())waterTime=drawnWaterTime;
    savedAnimFrame=m->getMotionFrameCtrl().getFrame();savedSquidFrame=squid->getFrameCtrl(0)->getFrame();
    savedHealth=m->mHealth;savedWater=m->mWaterGun?m->mWaterGun->mCurrentWater:0;
    savedNozzle=m->mWaterGun?m->mWaterGun->mCurrentNozzle:0;savedSecond=m->mWaterGun?m->mWaterGun->mSecondNozzle:4;
    // Ease the first complete destination frame into the retained view.
    if(GXPC_CoastalDissolve)GXPC_CoastalDissolve(port_fps60_active?4:2);
    if(GXPC_CoastalHold)GXPC_CoastalHold(8);
    crossingStart=port_open_world_milliseconds();
    sms_open_world_profile("ferry crossing begin");
    int ep=pendingStage==PLAZA?plazaEpisode:pinnaEpisode;
    OSReport("[sea-route] crossing stage=%d -> stage=%d episode=%d progress=%.1f speed=%.2f health=%d water=%d\n",currentStage,pendingStage,ep,progress,savedSpeed,savedHealth,savedWater);
    d->setNextStage(((pendingStage+1)<<8)|ep,0);
    // Plaza -> Pinna normally starts the cannon/gate departure demo.
    // The ferry already supplies that journey; request the stage exchange
    // directly instead of waiting behind a frozen frame for the demo.
    d->offFlag(TMarDirector::DIRECTOR_FLAG_GATE_DEMO_STAGE_TRANSITION_PENDING);
    d->onFlag(TMarDirector::DIRECTOR_FLAG_STAGE_TRANSITION_PENDING);
}
}
void sms_open_world_trim_harbor_map(J3DModel* m){cropNeighbor(m,false,false);}
void sms_open_world_crop_model(J3DModel* m,bool plaza) {
    if(plaza){hideScenery(m,true);retainPlazaCoast(m);}
    if(m && !plaza) {
        const unsigned hidden[]={8,14,17,25,26,27,28,29};
        for(unsigned i=0;i<sizeof(hidden)/sizeof(hidden[0]);++i)
            if(hidden[i]<m->getModelData()->getShapeNum()) {
                m->getModelData()->getShapeNodePointer(hidden[i])->onFlag(J3DShpFlag_Visible);
                m->getShapePacket(hidden[i])->hide();
            }
    }
    cropNeighbor(m,plaza);
}
J3DModel* sms_open_world_load_model(const char* archive,const char* name) {
    return model(archive,name);
}
J3DModel* sms_sea_mainland_model(){return currentStage==pinnaStage?scenery:0;}
J3DModel* sms_sea_harbor_model(){return currentStage==pinnaStage?harborScenery:0;}
void sms_open_world_preview_objects(const char* archive,const float transform[3][4]){loadSceneryObjects(archive,transform);}
void sms_sea_world_to_native(float matrix[3][4]) {
    C_MTXIdentity(matrix);
    if(currentStage==pinnaStage) {
        matrix[0][0]=cs;matrix[0][2]=-sn;matrix[2][0]=sn;matrix[2][2]=cs;
        matrix[0][3]=-cs*islandX+sn*islandZ;matrix[2][3]=-sn*islandX-cs*islandZ;
    }
}
void sms_sea_setup(TMarDirector* d) {
    testPadActive=false;
    ready=false;scenery=harborScenery=0;sceneryObjectCount=0;squid=idleSquid=0;hasWater=false;currentStage=d->mMap;
    if(!sms_open_world_enabled())return;
    if(gpApplication.unk30)for(unsigned i=0;i<gpApplication.unk30->getChildren().size();++i) {
        TNameRefAryT<TScenarioArchiveName>* n=gpApplication.unk30->getChildren()[i];
        if(n->size() && strstr(n->getChildren()[0].mArcName,"pinnaBeach"))pinnaStage=i;
        if(n->size() && strstr(n->getChildren()[0].mArcName,"pinnaParco"))parkStage=i;
    }
    sms_open_world_profile("sea setup begin");
    initPath();
    bool harbor=geography() && currentStage==3;
    if(currentStage!=PLAZA && currentStage!=pinnaStage && !harbor){active=false;pendingStage=-1;return;}
    if(currentStage==PLAZA)plazaEpisode=d->unk7D;else if(!harbor)pinnaEpisode=d->unk7D;
    if(pendingStage!=currentStage){pendingStage=-1;active=false;landingCamera=0;dockMode=0;}
    opa=new J3DDrawBuffer(512);xlu=new J3DDrawBuffer(512);opa->setNonSort();xlu->setNonSort();
    if(!harbor){squid=makeSquid();idleSquid=makeSquid();}
    sms_open_world_profile("sea squids ready");
    char sceneryArchive[128];
    bool haveScenery=stageArchive(currentStage==PLAZA || harbor?pinnaStage:PLAZA,
        currentStage==PLAZA || harbor?pinnaEpisode:plazaEpisode,sceneryArchive,sizeof sceneryArchive);
    scenery=haveScenery?model(sceneryArchive,"map.bmd"):0;
    sms_open_world_profile("sea scenery ready");
    hideScenery(scenery,currentStage!=PLAZA && !harbor);
    if(currentStage!=PLAZA && !harbor) {
        if(geography())sms_open_world_crop_model(scenery,true);
        else retainPlazaCoast(scenery);
    }
    if(scenery) {
        int source=currentStage==PLAZA || harbor?pinnaStage:PLAZA;
        P zero={0,0};P o=fromWorld(currentStage,toWorld(source,zero));
        Mtx transform;C_MTXIdentity(transform);
        transform[0][0]=cs;transform[2][2]=cs;
        transform[0][2]=source==pinnaStage?sn:-sn;
        transform[2][0]=source==pinnaStage?-sn:sn;
        transform[0][3]=o.x;transform[2][3]=o.z;
        if(harbor) {
            Mtx harborWorld,inverse,pinnaWorld;sms_open_world_harbor_transform(harborWorld);
            MTXInverse(harborWorld,inverse);C_MTXIdentity(pinnaWorld);
            pinnaWorld[0][0]=cs;pinnaWorld[0][2]=sn;pinnaWorld[2][0]=-sn;pinnaWorld[2][2]=cs;
            pinnaWorld[0][3]=islandX;pinnaWorld[2][3]=islandZ;
            MTXConcat(inverse,pinnaWorld,transform);
        }
        scenery->setBaseTRMtx(transform);
        loadSceneryObjects(sceneryArchive,transform);
        sms_open_world_profile("sea scene objects ready");
    }
    if(geography() && currentStage==pinnaStage) {
        char archive[128];
        if(stageArchive(3,0,archive,sizeof archive))harborScenery=model(archive,"map.bmd");
        if(harborScenery) {
            sms_open_world_crop_model(harborScenery,false);
            Mtx harborWorld,inverse,transform;sms_open_world_harbor_transform(harborWorld);
            C_MTXIdentity(inverse);inverse[0][0]=cs;inverse[0][2]=-sn;
            inverse[2][0]=sn;inverse[2][2]=cs;
            inverse[0][3]=-cs*islandX+sn*islandZ;inverse[2][3]=-sn*islandX-cs*islandZ;
            MTXConcat(inverse,harborWorld,transform);
            harborScenery->setBaseTRMtx(transform);
        }
    }
    const ResTIMG* t=(ResTIMG*)resource("/data/scene/dolpic0.szs","wave.bti");
    if(t) {
        GXInitTexObj(&water,(u8*)t+t->imageDataOffset,t->width,t->height,(GXTexFmt)t->format,GX_REPEAT,GX_REPEAT,GX_FALSE);
        GXInitTexObjLOD(&water,GX_LINEAR,GX_LINEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);hasWater=true;
        OSReport("[sea-route] water texture %dx%d format=%d\n",t->width,t->height,t->format);
    }
    if(harbor)return;
    // Replace only the baked distant counterpart. The nearby native map is untouched.
    J3DModel* nativeMap=gpMap->getModelManager()->getJointModel(0)->getModel();
    J3DModelData* map=nativeMap->getModelData();
    if(currentStage==PLAZA)retainPlazaCoast(nativeMap);
    if(currentStage==PLAZA && map->getShapeNum()>22){map->getShapeNodePointer(22)->onFlag(J3DShpFlag_Visible);nativeMap->getShapePacket(22)->hide();}
    if(currentStage==pinnaStage && map->getShapeNum()>1){map->getShapeNodePointer(1)->onFlag(J3DShpFlag_Visible);nativeMap->getShapePacket(1)->hide();}
    // Warm both immutable host copies before departure, not during the swap.
    char nativeArchive[128];
    if(stageArchive(currentStage,d->unk7D,nativeArchive,sizeof nativeArchive))
        port_open_world_resource(nativeArchive,"map.bmd",0,0);
    prefetch(PLAZA,plazaEpisode,0);prefetch(pinnaStage,pinnaEpisode,2);
    ready=squid && idleSquid && scenery && hasWater;
    sms_open_world_profile("sea setup end");
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
        m->unk9C=savedPreviousYaw-(currentStage==PLAZA?0:s16(0x2000));m->mFaceAngle.z=savedFaceRoll;
        m->mWaistRoll=savedWaistRoll;m->mWaistPitch=savedWaistPitch;
        m->mVel=savedVel;if(currentStage!=PLAZA){m->mVel.x=cs*savedVel.x-sn*savedVel.z;m->mVel.z=sn*savedVel.x+cs*savedVel.z;}
        m->mForwardVel=savedSpeed;m->mSlideVelX=m->mVel.x;m->mSlideVelZ=m->mVel.z;
        float ground=gpMapCollisionData->checkGround(p.x,p.y+100,p.z,0,&m->mGroundPlane);m->mFloorPosition.set(p.x,ground,p.z);
        m->setAnimation(TMario::ANIM_RIDE_SHELL,1.f);m->getMotionFrameCtrl().setFrame(savedAnimFrame);squid->getFrameCtrl(0)->setFrame(savedSquidFrame);
        m->mHealth=savedHealth;if(m->mWaterGun){m->mWaterGun->mCurrentWater=savedWater;m->mWaterGun->changeNozzle((TWaterGun::TNozzleType)savedNozzle,false);m->mWaterGun->mSecondNozzle=savedSecond;}
        JGeometry::TVec3<f32> eye=native(currentStage,savedEye),at=native(currentStage,savedAt);
        gpCamera->endDemoCamera();gpCamera->warpPosAndAt(eye,at);gpCamera->mFovy=savedFov;cameraMatrices(true);
        clearHud(d);gpApplication.mFader->setFadeStatus(TSMSFader::FADE_STATUS_FULLY_FADED_IN);
        // The first entry can still contain the spawn pose in its draw packets.
        pendingStage=-1;releasePending=true;releaseFrames=2;active=true;++crossings;
        sms_open_world_profile("ferry arrived");
        OSReport("[sea-route] arrived stage=%d episode=%d crossing=%d elapsed_ms=%u speed=%.2f -> %.2f squid_frame=%.2f health=%d water=%d\n",currentStage,d->unk7D,crossings,(unsigned)(port_open_world_milliseconds()-crossingStart),savedSpeed,m->mForwardVel,savedSquidFrame,m->mHealth,savedWater);
        OSReport("[sea-route] animation mario=%.2f -> %.2f blooper=%.2f -> %.2f hop=%.2f lateral=%.2f\n",savedAnimFrame,m->getMotionFrameCtrl().getFrame(),savedSquidFrame,squid->getFrameCtrl(0)->getFrame(),hop,lateral);
        OSReport("[sea-route] rider lean roll=%.2f -> %.2f pitch=%.2f -> %.2f yaw_delta=%d\n",savedWaistRoll,m->mWaistRoll,savedWaistPitch,m->mWaistPitch,int(s16(m->mFaceAngle.y-m->unk9C)));
    } else {
        JGeometry::TVec3<f32> p=currentStage==PLAZA?plazaLanding():JGeometry::TVec3<f32>(6000,120,7300);
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
    if(geography() && hasWater && !releasePending)waterTime+=.5f;
    if(testParkWalk(d) || !ready)return;
    TMario* m=gpMarioOriginal;TMarioGamePad* pad=m->getGamePad();
    // The director runs 120 simulation ticks per second at either display
    // rate. Use 60th-second ride units so timing and steering agree at 30/60.
    float dt=geography()?.5f:(port_fps60_active?.5f:1.f);
    if(cooldown>0)--cooldown;
    if(getenv("SMS_SEA_TEST_RIDES") && !testDone) {
        // Store scalar inputs for PADRead; never dereference stage objects there.
        testPadActive=true;
        if(!active && !cooldown && nearStation() && ((++testPulse%16)<4))testButtons=JUTGamePad::X;
        if(active && getenv("SMS_SEA_TEST_CONTROL")) {
            if(rideElapsed>=80 && rideElapsed<110)testStickX=70;
            if(rideElapsed>=110 && rideElapsed<140)testStickX=-70;
            if(fabsf(progress-swapDistance)<750 && !testHopSent)testButtons=JUTGamePad::A;
            const char* turn=getenv("SMS_SEA_TEST_TURNBACK");
            bool turnReady=turn && (atoi(turn)==2
                ?crossings==1 && fabsf(progress-swapDistance)>1500
                :crossings==0 && rideElapsed>60);
            if(turnReady && !testTurnSent)testButtons=JUTGamePad::B;
        }
    }
    if(!active) {
        if(!cooldown && nearStation() && pad->testTrigger(JUTGamePad::X))board();
        return;
    }
    // Let native animation build the restored rider pose before advancing.
    if(releasePending)return;
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
            JGeometry::TVec3<f32> landing=plazaLanding();
            const TBGCheckData* ground;float y=gpMapCollisionData->checkGround(landing.x,650,landing.z,0,&ground);
            dockMode=-1;dockRemaining=30;dockFrom=world(currentStage,m->mPosition);dockTo.set(landing.x,fmaxf(y,100.f)+5.f,landing.z);
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
    bool boarding=geography() && dockMode!=0;
    m->mSurfGesso=boarding?0:squid;m->mSurfGessoType=TMario::SURF_GESSO_TYPE_GREEN;
    unsigned status=boarding?MARIO_STATUS_JUMP:MARIO_STATUS_SURF;
    if(m->mStatus!=status)m->changePlayerStatus(status,0,true);
    m->mStatusTimer=0;m->setAnimation(boarding?TMario::ANIM_JUMP:TMario::ANIM_RIDE_SHELL,1.f);
    float angle=(boarding?atan2f(dockTo.x-dockFrom.x,dockTo.z-dockFrom.z)
        :atan2f(tangent.x*direction,tangent.z*direction))-(currentStage==PLAZA?0:rotation);
    // Native playerControl normally records the previous yaw before moving.
    // Guided travel bypasses it; a stale shore yaw otherwise makes the torso
    // lean as though Mario is making one continuous sharp turn offshore.
    m->unk9C=m->mFaceAngle.y;
    m->mFaceAngle.y=s16(int(angle*65536.f/6.283185307f));m->mModelFaceAngle=m->mFaceAngle.y;m->mIntendedYaw=m->mFaceAngle.y;
    P velocity={boarding?0.f:tangent.x*direction*speed,boarding?0.f:tangent.z*direction*speed};
    if(currentStage!=PLAZA){float x=cs*velocity.x-sn*velocity.z,z=sn*velocity.x+cs*velocity.z;velocity.x=x;velocity.z=z;}
    m->mVel.set(velocity.x,0.f,velocity.z);m->mForwardVel=boarding?0.f:speed;m->mSlideVelX=velocity.x;m->mSlideVelZ=velocity.z;
    d->mConsole->startDisappearTelop();
    static int logTimer;
    if(getenv("SMS_OPEN_WORLD_LOG") && (++logTimer%60)==0)OSReport("[sea-route] position stage=%d progress=%.1f lateral=%.1f xyz=(%.1f,%.1f,%.1f) speed=%.2f status=%x\n",currentStage,progress,lateral,next.x,next.y,next.z,speed,m->mStatus);
    if((currentStage==PLAZA && direction>0 && oldProgress<swapDistance && progress>=swapDistance)
       || (currentStage==pinnaStage && direction<0 && oldProgress>swapDistance && progress<=swapDistance))cross(d);
}
bool sms_sea_camera() {
    if((!active && landingCamera<=0) || !ready || pendingStage!=-1 || !gpMarDirector || (gpMarDirector->mState!=TMarDirector::STATE_UNK4 && !releasePending))return false;
    if(releasePending) {
        gpCamera->mFovy=savedFov;
        gpCamera->warpPosAndAt(native(currentStage,seaEye),native(currentStage,seaAt));
        cameraMatrices(true);return true;
    }
    if(!active) {
        float dt=port_fps60_active?.5f:1.f;landingCamera=fmaxf(0.f,landingCamera-dt);
        JGeometry::TVec3<f32> e=world(currentStage,gpCamera->mPosition),a=world(currentStage,gpCamera->mTarget);
        float blend=1-powf(.94f,dt);
        savedFov+=(gpCamera->mFovy-savedFov)*blend;gpCamera->mFovy=savedFov;
        seaEye.x+=(e.x-seaEye.x)*blend;seaEye.y+=(e.y-seaEye.y)*blend;seaEye.z+=(e.z-seaEye.z)*blend;
        seaAt.x+=(a.x-seaAt.x)*blend;seaAt.y+=(a.y-seaAt.y)*blend;seaAt.z+=(a.z-seaAt.z)*blend;
        JGeometry::TVec3<f32> eye=native(currentStage,seaEye),at=native(currentStage,seaAt);
        keepCameraClear(eye,at);
        gpCamera->warpPosAndAt(eye,at);cameraMatrices();return true;
    }
    gpCamera->mFovy=savedFov;
    P tangent;sample(progress,&tangent);
    JGeometry::TVec3<f32> player=world(currentStage,gpMarioOriginal->mPosition);
    // Follow travel directly and ease the orbit angle. Interpolating eye
    // positions through a turn-back would pull the camera through the rider.
    float dt=port_fps60_active?.5f:1.f,blend=1-powf(.9f,dt);
    float desired=atan2f(tangent.x*direction,tangent.z*direction);
    if(geography()) {
        // Keep the destination in view during the offshore detour, while
        // retaining some travel heading so steering remains easy to read.
        P destination=direction>0?controls[4]:controls[0];
        float remaining=direction>0?routeLength-progress:progress;
        float toward=atan2f(destination.x-player.x,destination.z-player.z);
        float difference=toward-desired;
        while(difference>3.14159265f)difference-=6.283185307f;
        while(difference< -3.14159265f)difference+=6.283185307f;
        float offshore=fminf(1.f,fmaxf(0.f,(remaining-2500.f)/5000.f));
        desired+=difference*.65f*offshore;
        if(currentStage==pinnaStage && direction>0 && remaining<1400.f) {
            P gate={-3500,2500};gate=toWorld(pinnaStage,gate);
            toward=atan2f(gate.x-player.x,gate.z-player.z);difference=toward-desired;
            while(difference>3.14159265f)difference-=6.283185307f;
            while(difference< -3.14159265f)difference+=6.283185307f;
            float arrival=1.f-fmaxf(0.f,remaining)/1400.f;
            desired+=difference*arrival*arrival*(3.f-2.f*arrival);
        }
    }
    float turn=desired-cameraHeading;
    while(turn>3.14159265f)turn-=6.283185307f;
    while(turn<-3.14159265f)turn+=6.283185307f;
    cameraHeading+=turn*blend;cameraRadius+=(650.f-cameraRadius)*blend;
    seaEye.x=player.x-sinf(cameraHeading)*cameraRadius;
    seaEye.z=player.z-cosf(cameraHeading)*cameraRadius;
    float shoreLift=geography() && currentStage==PLAZA?
        400.f*(1.f-fminf(1.f,progress/1600.f)):0.f;
    seaEye.y+=(player.y+260+shoreLift-seaEye.y)*blend;
    seaAt.x=player.x;seaAt.z=player.z;seaAt.y=player.y+110;
    JGeometry::TVec3<f32> e=native(currentStage,seaEye),a=native(currentStage,seaAt);
    keepCameraClear(e,a);
    gpCamera->warpPosAndAt(e,a);gpCamera->unk258=s16(int(atan2f(e.x-a.x,e.z-a.z)*65536.f/6.283185307f));cameraMatrices();return true;
}
void sms_sea_draw(unsigned cue,JDrama::TGraphics* graphics) {
    if(!(cue&CUE_DRAW) || !sms_open_world_enabled() || !scenery)return;
    gpCamera->perform(CUE_CALC_VIEW|CUE_SET_PROJECTION,graphics);
    drawModel(scenery,graphics);
    drawModel(harborScenery,graphics);
    drawSceneryObjects(graphics);
    if(ready && (!active || (geography() && dockMode!=0))) {
        P p=fromWorld(currentStage,station());Mtx transform;C_MTXIdentity(transform);transform[0][3]=p.x;transform[1][3]=10+4*sinf(rideTime*.05f);transform[2][3]=p.z;
        idleSquid->getModel()->setBaseTRMtx(transform);idleSquid->frameUpdate();idleSquid->calc();drawModel(idleSquid->getModel(),graphics);
    }
    bool farWater=geography() && hasWater && !active && gpCamera->mPosition.y>5.f;
    if(active || farWater) {
        // Identical world-space ocean texture and phase on all coastal maps.
        float phase=geography()?waterTime:rideTime;
        if(geography())drawnWaterTime=waterTime;
        JGeometry::TVec3<f32> center=world(currentStage,gpMarioOriginal->mPosition);
        GXLoadPosMtxImm(graphics->getViewMtx(),GX_PNMTX0);GXSetCurrentMtx(GX_PNMTX0);GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS,GX_DIRECT);GXSetVtxDesc(GX_VA_CLR0,GX_DIRECT);GXSetVtxDesc(GX_VA_TEX0,GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT7,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GXSetVtxAttrFmt(GX_VTXFMT7,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);GXSetVtxAttrFmt(GX_VTXFMT7,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
        GXSetNumChans(1);GXSetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,0,GX_DF_NONE,GX_AF_NONE);
        GXSetNumTexGens(1);GXSetTexCoordGen2(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY,GX_FALSE,GX_PTIDENTITY);GXLoadTexObj(&water,GX_TEXMAP0);
        GXSetNumTevStages(1);GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);GXSetTevColor(GX_TEVREG0,(GXColor){28,140,195,255});GXSetTevColor(GX_TEVREG1,(GXColor){85,220,238,255});
        GXSetTevColorIn(GX_TEVSTAGE0,GX_CC_C0,GX_CC_C1,GX_CC_TEXC,GX_CC_ZERO);
        GXSetTevColorOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
        GXSetTevAlphaIn(GX_TEVSTAGE0,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,farWater?GX_CA_RASA:GX_CA_A0);
        GXSetTevAlphaOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
        GXSetTevSwapMode(GX_TEVSTAGE0,GX_TEV_SWAP0,GX_TEV_SWAP0);GXSetNumIndStages(0);GXSetTevDirect(GX_TEVSTAGE0);
        GXSetZTexture(GX_ZT_DISABLE,GX_TF_Z24X8,0);GXSetZMode(GX_TRUE,GX_LEQUAL,farWater?GX_FALSE:GX_TRUE);GXSetZCompLoc(GX_TRUE);GXSetCullMode(GX_CULL_NONE);GXSetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,255});
        GXSetColorUpdate(GX_TRUE);GXSetAlphaUpdate(GX_TRUE);GXSetBlendMode(farWater?GX_BM_BLEND:GX_BM_NONE,farWater?GX_BL_SRCALPHA:GX_BL_ONE,farWater?GX_BL_INVSRCALPHA:GX_BL_ZERO,GX_LO_COPY);GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
        if(farWater) {
            // Leave nearby native waves and underwater visibility intact.
            // Fade the same open-sea surface in beyond their short draw range.
            const float radius[]={2200,3400,6500,12000,100000};
            const u8 opacity[]={0,110,235,255,255};
            const int segments=64;
            GXBegin(GX_QUADS,GX_VTXFMT7,4*segments*4);
            for(int ring=0;ring<4;++ring)for(int segment=0;segment<segments;++segment)for(int k=0;k<4;++k) {
                int r=ring+(k==1 || k==2);float angle=(segment+(k>=2))*(6.283185307f/segments);
                float wx=center.x+radius[r]*sinf(angle),wz=center.z+radius[r]*cosf(angle);
                JGeometry::TVec3<f32> p=native(currentStage,JGeometry::TVec3<f32>(wx,2.f,wz));
                GXPosition3f32(p.x,p.y,p.z);GXColor4u8(55,205,220,opacity[r]);GXTexCoord2f32(wx/1600+phase*.004f,wz/1600+phase*.002f);
            }
            GXEnd();
        } else {
            const int grid=24;const float extent=100000,step=2*extent/grid;
            GXBegin(GX_QUADS,GX_VTXFMT7,grid*grid*4);
            for(int z=0;z<grid;++z)for(int x=0;x<grid;++x)for(int k=0;k<4;++k) {
                float wx=center.x-extent+(x+(k==2 || k==3))*step,wz=center.z-extent+(z+(k==1 || k==2))*step;
                JGeometry::TVec3<f32> p=native(currentStage,JGeometry::TVec3<f32>(wx,2.f,wz));
                GXPosition3f32(p.x,p.y,p.z);GXColor4u8(55,205,220,235);GXTexCoord2f32(wx/1600+phase*.004f,wz/1600+phase*.002f);
            }
            GXEnd();
        }
    }
    if(!ready)return;
    if(releasePending && gpApplication.mFader->isFullyFadedIn() && --releaseFrames<=0) {
        if(GXPC_CoastalHold)GXPC_CoastalHold(0);releasePending=false;
        sms_open_world_profile("ferry frame ready");
        OSReport("[sea-route] continuous frame released crossing=%d elapsed_ms=%u\n",crossings,
            (unsigned)(port_open_world_milliseconds()-crossingStart));
    }
    // Keep both lines left of FLUDD's gauge. Only advertise boarding when
    // X can actually work; use the landing camera blend to orient arrivals.
    TMario* player=gpMarioOriginal;
    bool canBoard=!active && !cooldown && nearStation() && player
        && !player->onYoshi() && !player->isHolding() && !player->getHolder();
    bool justLanded=!active && landingCamera>0;
    if(active || canBoard || justLanded) {
        J2DOrthoGraph graph(graphics->getViewport());graph.setPort();graph.setup2D();
        graph.setColor(JUtility::TColor(16,48,64,170));
        graph.fillBox(JUTRect(38,365,410,425));
        gpSystemFont->setGX();
        const char* title=active?(direction>0?"To Pinna Beach":"To Delfino Plaza")
            :justLanded?(currentStage==PLAZA?"Delfino Plaza":"Pinna Beach")
            :(currentStage==PLAZA?"Blooper ride to Pinna Beach":"Blooper ride to Delfino Plaza");
        const char* hint=active?(geography() && dockMode?(dockMode>0?"All aboard!":"Back to shore") : "Stick: Steer   A: Hop   B: Turn back")
            :justLanded?(currentStage==PLAZA?"Back at the waterfront":"Follow the beach left to the park entrance.")
            :"X: Board";
        gpSystemFont->setCharColor(JUtility::TColor(22,54,69,255));
        gpSystemFont->drawString_scale(49,389,15,18,title,true);
        gpSystemFont->drawString_scale(49,413,12,16,hint,true);
        gpSystemFont->setCharColor(JUtility::TColor(255,248,215,255));
        gpSystemFont->drawString_scale(48,388,15,18,title,true);
        gpSystemFont->drawString_scale(48,412,12,16,hint,true);
    }
}

extern "C" int sms_sea_test_pad(signed char* x,signed char* y,unsigned short* buttons) {
    if(!testPadActive)return 0;
    *x=testStickX;*y=testStickY;*buttons|=testButtons;return 1;
}

void sms_sea_filter_map() {
    sms_open_world_filter_map();
    if(!ready || !sms_open_world_enabled() || !gpMap)return;
    J3DModel* m=gpMap->getModelManager()->getJointModel(0)->getModel();
    J3DModelData* d=m->getModelData();
    // Replace the baked counterpart, preserving the Plaza's supporting
    // land and Pinna's sand. A large bounding box alone does not distinguish
    // a background card from a shape that also contains playable terrain.
    for(unsigned i=0;i<d->getShapeNum();++i) {
        bool replace=currentStage==PLAZA?(i==22 || (geography() && i==19)):i==1;
        bool background=currentStage==PLAZA?(i==11 || i==12 || i==19):(i<9 && i!=7);
        bool hide=replace || (active && currentStage!=PLAZA && background);
        if(hide){d->getShapeNodePointer(i)->onFlag(J3DShpFlag_Visible);m->getShapePacket(i)->hide();}
        else if(background){d->getShapeNodePointer(i)->offFlag(J3DShpFlag_Visible);m->getShapePacket(i)->show();}
    }
    sms_open_world_filter_map();
}

// Both stages use the same sky asset. Align it with the common island axes
// and carry its cloud animation so the horizon does not rotate at a swap.
void sms_sea_sky(MActor* sky,float matrix[3][4]) {
    if(!sms_open_world_enabled() || !ready || !sky)return;
    if(currentStage!=PLAZA) {
        matrix[0][0]=cs;matrix[0][2]=-sn;
        matrix[2][0]=sn;matrix[2][2]=cs;
    }
    J3DFrameCtrl* frame=sky->getFrameCtrl(ANM_TYPE_BTK);
    if(frame) {
        if(pendingStage==currentStage || releasePending)frame->setFrame(skyFrame);
        else skyFrame=frame->getFrame();
    }
}
