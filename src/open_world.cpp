// Optional coastal link. Built with the game headers / allocator so collision,
// camera and Mario all use the native port's actual class layouts.
#include <sms_open_world.h>
#include <sms_open_world_sea.h>
#include <System/MarDirector.hpp>
#include <System/Application.hpp>
#include <System/PerformList.hpp>
#include <System/FlagManager.hpp>
#include <System/ScenarioArchiveName.hpp>
#include <System/Resolution.hpp>
#include <Strategic/NameRefPtrAry.hpp>
#include <Player/Mario.hpp>
#include <Player/WaterGun.hpp>
#include <GC2D/ScrnFader.hpp>
#include <GC2D/GCConsole2.hpp>
#include <GC2D/ExPane.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DAnimation.hpp>
#include <dolphin/mtx.h>
#include <Camera/Camera.hpp>
#include <Map/MapCollisionData.hpp>
#include <Map/MapCollisionEntry.hpp>
#include <Map/MapData.hpp>
#include <Map/Map.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <JSystem/JDrama/JDRViewObj.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DJoint.hpp>
#include <Enemy/FruitsBoat.hpp>
#include <Enemy/Graph.hpp>
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern "C" void port_open_world_prefetch(const char*,int);
extern "C" unsigned long long port_open_world_milliseconds();
extern "C" void GXPC_CoastalHold(int) __attribute__((weak));

namespace {
const int PLAZA = 1, HARBOR = 3;
struct Route {
    int stage;
    float x, y, z, dx, dz;
};
const Route routes[] = {
    { PLAZA, -7900, 300, 1000, -0.70710678f, -0.70710678f },
    { HARBOR, -300, 300, 1900, 0.17364818f, 0.98480775f }
};
const float halfWidth = 360, routeEnd = 1560, triggerAt = 2830, seamV = -1810;
const float seamDU = 0.12797134f, seamDV = -0.99177885f;
const float passageHeight = 400;
struct Bend { float u,v; };
// Broad, progressively turning coastal approaches. The central straight and
// its half-turn counterpart keep both shorelines out of the exchange view.
const Bend passage[] = {{1560,0},{1870,-40},{2160,-160},{2400,-350},
    {2590,-590},{2710,-880},{2750,-1190},{2910,-2430},
    {2950,-2740},{3070,-3030},{3260,-3270},{3500,-3460},
    {3790,-3580},{4100,-3620}};
struct Triangle { JGeometry::TVec3<f32> v[3]; GXColor color; u8 surface; float uv[3][2]; };
int pendingStage = -1;
// If a save/debug entry starts in Harbor, use the ordinary dry Plaza.
// Episodes 0/1 can redirect through opening story movies before map setup.
int plazaEpisode = 2, harborEpisode = 0;
s16 savedHealth;
s32 savedWater;
u8 savedNozzle, savedSecondNozzle;
unsigned long long crossingStart;
bool testSpawnUsed;
bool finishingCrossing;
int crossings;
bool releaseFramePending;
int passageFacing=1;
float passageBlend=0;
struct CrossingPose {
    Route from;
    JGeometry::TVec3<f32> pos, vel, eye, at;
    float speed, fov, animationFrame, animationRate;
    s16 yaw, cameraYaw;
    int direction;
    u32 status;
    u16 state, timer, animation;
    bool actorDependent;
} savedPose;
// Integration replay supplies controller axes, never Mario transforms.
// Kept as scalar values so PADRead cannot touch a stage during teardown.
int testStickX, testStickY, testWaypoint=1, testDirection=1;
bool testPadActive, testWalkDone;
const Bend testPath[]={{-300,0},{1560,0},{1870,-40},{2160,-160},
    {2400,-350},{2590,-590},{2710,-880},{2750,-1190},{2862,-2058}};
const int testPathCount=sizeof(testPath)/sizeof(testPath[0]);

const Route* routeFor(int stage)
{
    for (int i = 0; i < 2; ++i)
        if (routes[i].stage == stage) return &routes[i];
    return 0;
}
JGeometry::TVec3<f32> point(const Route& r, float u, float v, float h)
{
    return JGeometry::TVec3<f32>(r.x + r.dx*u - r.dz*v, r.y+h,
                                r.z + r.dz*u + r.dx*v);
}
void local(const Route& r, const JGeometry::TVec3<f32>& p, float& u, float& v)
{
    u = (p.x-r.x)*r.dx + (p.z-r.z)*r.dz;
    v = -(p.x-r.x)*r.dz + (p.z-r.z)*r.dx;
}
GXColor color(u8 r, u8 g, u8 b)
{
    GXColor c = {r,g,b,255}; return c;
}

enum Surface { PLAIN, LOCAL_PAVING, LOCAL_WOOD, STONE, PLASTER, SHARED_PAVING, SURFACE_COUNT };
struct SurfaceTexture {
    u8 pixels[65536] __attribute__((aligned(32)));
    GXTexObj texture;
    bool valid;
};
// Host static storage survives native stage-heap teardown. Only texture pixels
// already on the player's disc are copied; no game art is bundled with source.
SurfaceTexture surfaces[SURFACE_COUNT];
const ResTIMG* mapTexture(const char* name) {
    if(!gpMap || !gpMap->getModelManager()) return 0;
    TMapModelManager* manager=gpMap->getModelManager();
    for(int i=0;i<manager->getJointModelNum();++i) {
        J3DModelData* model=manager->getJointModel(i)->getModelData();
        if(!model || !model->getTextureName() || !model->getTexture()) continue;
        int index=model->getTextureName()->getIndex(name);
        if(index>=0 && index<model->getTexture()->getNum())
            return model->getTexture()->getResTIMG(index);
    }
    return 0;
}
void surfaceTexture(int slot,const char* name,int x=0,int y=0,int w=0,int h=0) {
    const ResTIMG* src=mapTexture(name);
    surfaces[slot].valid=false;
    if(!src || src->format!=GX_TF_CMPR) {
        OSReport("[open-world] missing material %s\n",name);return;
    }
    if(!w)w=src->width;if(!h)h=src->height;
    if(x<0 || y<0 || x+w>src->width || y+h>src->height || (x|y|w|h)&7
        || w*h/2>sizeof surfaces[slot].pixels) return;
    const u8* image=(const u8*)src+src->imageDataOffset;
    for(int row=0;row<h/8;++row)
        memcpy(surfaces[slot].pixels+row*(w/8)*32,
            image+((row+y/8)*(src->width/8)+x/8)*32,(w/8)*32);
    GXInitTexObj(&surfaces[slot].texture,surfaces[slot].pixels,w,h,GX_TF_CMPR,GX_REPEAT,GX_REPEAT,GX_FALSE);
    GXInitTexObjLOD(&surfaces[slot].texture,GX_LINEAR,GX_LINEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
    surfaces[slot].valid=true;
    OSReport("[open-world] material %d = %s (%dx%d)\n",slot,name,w,h);
}
void setupSurfaces(int stage) {
    surfaceTexture(LOCAL_PAVING,stage==PLAZA?"A_yuka_nami_n3":"A_heban3_w2");
    if(stage==PLAZA)surfaceTexture(LOCAL_WOOD,"A_san04m2");
    else surfaceTexture(LOCAL_WOOD,"A_ricconuki06",72,192,56,64);
    // One material set for the shared passage, retained across the crossing.
    // Starting directly in Harbor chooses its corresponding native materials.
    if(!surfaces[STONE].valid) {
        if(stage==PLAZA)surfaceTexture(STONE,"A_dptgreen02",0,224,128,112);
        else surfaceTexture(STONE,"A_isiita02");
    }
    if(!surfaces[PLASTER].valid) {
        if(stage==PLAZA)surfaceTexture(PLASTER,"A_white_wall01",0,96,128,128);
        else surfaceTexture(PLASTER,"A_WhiteWall",0,32,32,64);
    }
    if(!surfaces[SHARED_PAVING].valid)
        surfaceTexture(SHARED_PAVING,stage==PLAZA?"A_yuka_itimatu01":"A_heban2");
    GXInvalidateTexAll();
}

class Walkway : public JDrama::TViewObj {
public:
    Route route;
    Triangle tris[12288];
    TBGCheckData collision[512];
    int count, collisionCount, surface;
    bool armed;
    Walkway(const Route& r) : JDrama::TViewObj("Coastal Walkway"), route(r),
        count(0), collisionCount(0), surface(PLAIN), armed(false)
    {
        setupSurfaces(r.stage);
        buildBridge();
        buildPassage();
        // Small dockside destination board, with posts anchored to the deck.
        surface=PLAIN;
        box(60,100,430,460,-350,350,color(75,89,92),false);
        box(60,100,1070,1100,-350,350,color(75,89,92),false);
        box(60,100,430,1100,220,380,color(35,87,117),false);
        label(r.stage==PLAZA ? "RICCO HARBOR" : "DELFINO PLAZA");
        for (int i=0;i<collisionCount;++i)
            gpMapCollisionData->addCheckDataToGrid(&collision[i],TMapCollisionBase::KIND_STATIC);
        OSReport("[open-world] walkway stage=%d origin=(%.0f,%.0f,%.0f) triangles=%d collision=%d\n",
                 r.stage,r.x,r.y,r.z,count,collisionCount);
    }
    void pillar(float u,float v,float radius,float lo,float hi,GXColor shade) {
        for(int i=0;i<10;++i) {
            float a=i*6.2831853f/10,b=(i+1)*6.2831853f/10;
            float x=u+radius*cosf(a),y=v+radius*sinf(a);
            float xx=u+radius*cosf(b),yy=v+radius*sinf(b);
            float light=.78f+.22f*(.5f+.5f*cosf(a-.6f));
            GXColor c=color(shade.r*light,shade.g*light,shade.b*light);
            quad(x,y,lo,xx,yy,lo,xx,yy,hi,x,y,hi,c,false);
            tri(point(route,u,v,hi),point(route,x,y,hi),point(route,xx,yy,hi),shade,false);
        }
    }
    void rope(float u0,float u1,float v,float height) {
        surface=PLAIN;
        for(int i=0;i<8;++i) {
            float t=i/8.f,q=(i+1)/8.f;
            float h=height-34*sinf(t*3.14159265f),hh=height-34*sinf(q*3.14159265f);
            float a=u0+(u1-u0)*t,b=u0+(u1-u0)*q;
            quad(a,v-5,h-5,b,v-5,hh-5,b,v-5,hh+5,a,v-5,h+5,color(180,152,96),false);
            quad(a,v+5,h+5,b,v+5,hh+5,b,v+5,hh-5,a,v+5,h-5,color(213,185,121),false);
            quad(a,v-5,h+5,b,v-5,hh+5,b,v+5,hh+5,a,v+5,h+5,color(239,212,153),false);
        }
    }
    void lantern(float u,float v) {
        surface=PLAIN;
        pillar(u,v,13,0,300,color(52,78,81));
        box(u-40,u+40,v-40,v+40,295,310,color(45,70,76),false);
        box(u-30,u+30,v-30,v+30,310,382,color(255,233,160),false);
        for(int x=-1;x<=1;x+=2)for(int y=-1;y<=1;y+=2)
            box(u+x*30-4,u+x*30+4,v+y*30-4,v+y*30+4,306,386,color(48,70,72),false);
        box(u-43,u+43,v-43,v+43,384,396,color(41,69,76),false);
        pillar(u,v,20,396,412,color(56,81,83));
    }
    void buildBridge() {
        bool plaza=route.stage==PLAZA;
        // Continuous collision slab beneath the visible paving/planks.
        surface=STONE;
        box(-150,routeEnd,-halfWidth,halfWidth,-85,0,color(219,214,192),true);
        if(plaza) {
            surface=LOCAL_PAVING;
            for(float u=-150;u<routeEnd;u+=320) {
                float end=fminf(u+320,routeEnd);
                quad(u,-300,1,u,300,1,end,300,1,end,-300,1,color(255,252,239),false);
            }
            surface=PLASTER;
            for(int side=-1;side<=1;side+=2) {
                float v=side*330;
                box(-150,routeEnd,v-30,v+30,-2,22,color(231,231,218),false);
                box(-100,routeEnd,v-25,v+25,164,189,color(220,229,221),false);
                for(float u=0;u<=routeEnd-260;u+=260) {
                    box(u-29,u+29,v-29,v+29,20,173,color(244,238,211),false);
                    box(u-42,u+42,v-42,v+42,160,190,color(252,247,228),false);
                    for(int k=1;k<=3;++k) {
                        float a=u+k*65;
                        pillar(a,v,14,30,65,color(247,241,216));
                        pillar(a,v,20,65,95,color(246,239,211));
                        pillar(a,v,12,95,165,color(247,241,216));
                    }
                }
                surface=PLAIN;
                box(-100,routeEnd,v-26,v+26,190,198,color(72,126,166),false);
                surface=PLASTER;
            }
        } else {
            surface=LOCAL_WOOD;
            for(float u=-150;u<routeEnd;u+=70) {
                float end=fminf(u+66,routeEnd);
                box(u,end,-halfWidth+8,halfWidth-8,0,2,color(210,195,162),false);
            }
            for(int side=-1;side<=1;side+=2) {
                float v=side*335;
                for(float u=0;u<=routeEnd-260;u+=260) {
                    surface=LOCAL_WOOD;
                    pillar(u,v,30,-480,198,color(184,168,133));
                    surface=PLAIN;
                    pillar(u,v,34,151,169,color(55,86,97));
                    pillar(u,v,36,196,210,color(65,105,119));
                    if(u<routeEnd-260) {rope(u,u+260,v,182);rope(u,u+260,v,101);}
                }
            }
        }
        // Visible supports tie the deck to the sea instead of leaving it afloat.
        for(float u=200;u<routeEnd;u+=650) {
            surface=plaza?STONE:LOCAL_WOOD;
            for(int side=-1;side<=1;side+=2) {
                float v=side*278;
                pillar(u,v,plaza?74:46,-530,-65,color(190,191,163));
                surface=STONE;
                box(u-90,u+90,v-90,v+90,-90,-55,color(212,208,184),false);
                surface=plaza?STONE:LOCAL_WOOD;
            }
            box(u-50,u+50,-360,360,-130,-80,color(176,176,149),false);
        }
        // Low collision curbs; the visible rail remains jumpable like native docks.
        surface=STONE;
        for(int side=-1;side<=1;side+=2) {
            float v=side*halfWidth;
            box(-100,routeEnd,v-18,v+18,0,42,color(216,215,190),true);
            lantern(routeEnd-350,side*330);
        }
        surface=PLAIN;
    }
    void wallStrip(Bend a,Bend b,float lo,float hi,GXColor shade) {
        float du=b.u-a.u,dv=b.v-a.v,len=sqrtf(du*du+dv*dv);
        float u=dv/len*2,v=-du/len*2;
        quad(a.u+u,a.v+v,lo,a.u+u,a.v+v,hi,b.u+u,b.v+v,hi,b.u+u,b.v+v,lo,shade,false);
    }
    void buildPassage() {
        const int n=sizeof(passage)/sizeof(passage[0]);
        Bend left[n],right[n];
        for (int i=0;i<n;++i) {
            Bend before=i ? passage[i-1] : passage[i];
            Bend after=i+1<n ? passage[i+1] : passage[i];
            float ax=passage[i].u-before.u,ay=passage[i].v-before.v;
            float bx=after.u-passage[i].u,by=after.v-passage[i].v;
            float al=sqrtf(ax*ax+ay*ay),bl=sqrtf(bx*bx+by*by);
            if (!al) {ax=bx;ay=by;al=bl;}
            if (!bl) {bx=ax;by=ay;bl=al;}
            ax/=al;ay/=al;bx/=bl;by/=bl;
            float scale=halfWidth/(1+ax*bx+ay*by);
            float du=-(ay+by)*scale,dv=(ax+bx)*scale;
            left[i].u=passage[i].u+du;left[i].v=passage[i].v+dv;
            right[i].u=passage[i].u-du;right[i].v=passage[i].v-dv;
        }
        for(int i=0;i<n-1;++i) {
            Bend a=right[i],b=left[i],c=left[i+1],d=right[i+1];
            surface=STONE;
            int before=count;
            quad(a.u,a.v,0,b.u,b.v,0,c.u,c.v,0,d.u,d.v,0,color(234,231,209),true);
            count=before; // floor collision; textured subdivisions below draw it
            surface=PLASTER;
            quad(b.u,b.v,0,b.u,b.v,260,c.u,c.v,260,c.u,c.v,0,color(242,236,207),true);
            quad(d.u,d.v,0,d.u,d.v,260,a.u,a.v,260,a.u,a.v,0,color(242,236,207),true);
            surface=STONE;
            wallStrip(b,c,0,85,color(190,202,201));
            wallStrip(d,a,0,85,color(190,202,201));
            surface=PLAIN;
            wallStrip(b,c,85,97,color(65,119,156));
            wallStrip(d,a,85,97,color(65,119,156));
            float du=passage[i+1].u-passage[i].u,dv=passage[i+1].v-passage[i].v;
            int steps=int(ceilf(sqrtf(du*du+dv*dv)/200));
            for(int j=0;j<steps;++j) {
                float t=float(j)/steps,t1=float(j+1)/steps;
                Bend aa={a.u+(d.u-a.u)*t,a.v+(d.v-a.v)*t};
                Bend bb={b.u+(c.u-b.u)*t,b.v+(c.v-b.v)*t};
                Bend cc={b.u+(c.u-b.u)*t1,b.v+(c.v-b.v)*t1};
                Bend dd={a.u+(d.u-a.u)*t1,a.v+(d.v-a.v)*t1};
                surface=SHARED_PAVING;
                for(int lane=0;lane<4;++lane) {
                    float l=lane/4.f,r=(lane+1)/4.f;
                    quad(aa.u+(bb.u-aa.u)*l,aa.v+(bb.v-aa.v)*l,0,
                         aa.u+(bb.u-aa.u)*r,aa.v+(bb.v-aa.v)*r,0,
                         dd.u+(cc.u-dd.u)*r,dd.v+(cc.v-dd.v)*r,0,
                         dd.u+(cc.u-dd.u)*l,dd.v+(cc.v-dd.v)*l,0,color(248,248,242),false);
                }
                // Low barrel vault: the matched shared silhouette survives the
                // half-turn at the crossing, and conceals Harbor's overhead beams.
                for(int k=0;k<8;++k) {
                    float l=k/8.f,r=(k+1)/8.f;
                    float h=260+140*sinf(l*3.14159265f),hh=260+140*sinf(r*3.14159265f);
                    Bend p={aa.u+(bb.u-aa.u)*l,aa.v+(bb.v-aa.v)*l};
                    Bend q={aa.u+(bb.u-aa.u)*r,aa.v+(bb.v-aa.v)*r};
                    Bend rr={dd.u+(cc.u-dd.u)*r,dd.v+(cc.v-dd.v)*r};
                    Bend ss={dd.u+(cc.u-dd.u)*l,dd.v+(cc.v-dd.v)*l};
                    surface=PLASTER;
                    quad(p.u,p.v,h,ss.u,ss.v,h,rr.u,rr.v,hh,q.u,q.v,hh,color(213,218,205),false);
                    surface=STONE;
                    quad(p.u,p.v,h+42,q.u,q.v,hh+42,rr.u,rr.v,hh+42,ss.u,ss.v,h+42,color(224,220,192),false);
                }
            }
            // Native curved-roof collision uses long faces rather than every
            // visual tile, keeping the collision grid comfortably bounded.
            for(int k=0;k<8;++k) {
                float l=k/8.f,r=(k+1)/8.f;
                float h=260+140*sinf(l*3.14159265f),hh=260+140*sinf(r*3.14159265f);
                before=count;
                quad(a.u+(b.u-a.u)*l,a.v+(b.v-a.v)*l,h,
                     d.u+(c.u-d.u)*l,d.v+(c.v-d.v)*l,h,
                     d.u+(c.u-d.u)*r,d.v+(c.v-d.v)*r,hh,
                     a.u+(b.u-a.u)*r,a.v+(b.v-a.v)*r,hh,color(0,0,0),true);
                count=before;
            }
            // Masonry ribs frame every bend and break up the long plaster runs.
            for(int end=0;end<2;++end) {
                Bend r=end?d:a,l=end?c:b;
                surface=STONE;
                for(int k=0;k<8;++k) {
                    float t=k/8.f,q=(k+1)/8.f;
                    float h=260+140*sinf(t*3.14159265f),hh=260+140*sinf(q*3.14159265f);
                    Bend p={r.u+(l.u-r.u)*t,r.v+(l.v-r.v)*t};
                    Bend pp={r.u+(l.u-r.u)*q,r.v+(l.v-r.v)*q};
                    float len=sqrtf(du*du+dv*dv),ou=du/len*14,ov=dv/len*14;
                    quad(p.u-ou,p.v-ov,h-9,pp.u-ou,pp.v-ov,hh-9,
                         pp.u+ou,pp.v+ov,hh-9,p.u+ou,p.v+ov,h-9,color(219,218,196),false);
                }
            }
        }
        // A dressed-stone arch and abutments anchor the passage to each pier.
        surface=STONE;
        for(int side=-1;side<=1;side+=2) {
            float v=side*392;
            box(routeEnd-80,routeEnd+70,v-40,v+40,-440,266,color(235,228,199),false);
            box(routeEnd-100,routeEnd+90,v-52,v+52,244,270,color(251,244,213),false);
        }
        for(int k=0;k<12;++k) {
            float a=k*3.14159265f/12,b=(k+1)*3.14159265f/12;
            float v0=-360*cosf(a),v1=-360*cosf(b),h0=260+140*sinf(a),h1=260+140*sinf(b);
            float vv0=-430*cosf(a),vv1=-430*cosf(b),hh0=260+210*sinf(a),hh1=260+210*sinf(b);
            quad(routeEnd-82,v0,h0,routeEnd-82,v1,h1,routeEnd-82,vv1,hh1,routeEnd-82,vv0,hh0,color(244,236,210),false);
            quad(routeEnd-82,vv0,hh0,routeEnd-82,vv1,hh1,routeEnd+20,vv1,hh1,routeEnd+20,vv0,hh0,color(206,205,183),false);
            surface=PLAIN;
            quad(routeEnd-83,v0,h0,routeEnd-83,v0+3,h0,routeEnd-83,vv0+3,hh0,routeEnd-83,vv0,hh0,color(147,157,148),false);
            surface=STONE;
        }
        surface=PLAIN;
        // Compact, warm ceiling fixtures, mirrored about the handoff.
        for(int i=-1;i<=1;++i) {
            float u=triggerAt+i*400*seamDU,v=seamV+i*400*seamDV;
            box(u-22,u+22,v-60,v+60,passageHeight-18,passageHeight-7,color(48,75,83),false);
            box(u-17,u+17,v-48,v+48,passageHeight-23,passageHeight-18,color(255,233,170),false);
        }
    }
    const char* glyph(char c) {
        // Original 5x7 block lettering; one row per byte, five low bits.
        switch (c) {
        case 'A': return "\x0e\x11\x11\x1f\x11\x11\x11";
        case 'B': return "\x1e\x11\x11\x1e\x11\x11\x1e";
        case 'C': return "\x0e\x11\x10\x10\x10\x11\x0e";
        case 'D': return "\x1e\x11\x11\x11\x11\x11\x1e";
        case 'E': return "\x1f\x10\x10\x1e\x10\x10\x1f";
        case 'F': return "\x1f\x10\x10\x1e\x10\x10\x10";
        case 'H': return "\x11\x11\x11\x1f\x11\x11\x11";
        case 'I': return "\x1f\x04\x04\x04\x04\x04\x1f";
        case 'L': return "\x10\x10\x10\x10\x10\x10\x1f";
        case 'N': return "\x11\x19\x15\x13\x11\x11\x11";
        case 'O': return "\x0e\x11\x11\x11\x11\x11\x0e";
        case 'P': return "\x1e\x11\x11\x1e\x10\x10\x10";
        case 'R': return "\x1e\x11\x11\x1e\x14\x12\x11";
        case 'Z': return "\x1f\x01\x02\x04\x08\x10\x1f";
        default: return "\0\0\0\0\0\0\0";
        }
    }
    void label(const char* text) {
        float size=8, start=765-(float)strlen(text)*6*size/2.0f;
        for (int n=0;text[n];++n) {
            const char* rows=glyph(text[n]);
            for (int y=0;y<7;++y) for (int x=0;x<5;++x) {
                if (!(rows[y] & (16>>x))) continue;
                float v=start+(n*6+x)*size,h=340-y*size;
                quad(59,v,h,59,v+size,h,59,v+size,h-size,59,v,h-size,
                     color(250,247,213),false);
            }
        }
    }
    void tri(const JGeometry::TVec3<f32>& a,const JGeometry::TVec3<f32>& b,
             const JGeometry::TVec3<f32>& c,GXColor col,bool solid)
    {
        if (count>=12288 || (solid && collisionCount>=512)) {
            OSReport("[open-world] geometry capacity exceeded\n"); abort();
        }
        if (solid) {
            TBGCheckData& d=collision[collisionCount++];
            d.setVertex(a,b,c);d.mBGType=0;
        }
        // Fold shared texture coordinates at the exchange centre. Split the
        // rendered face at each fold so interpolation stays linear, including
        // diagonal paving and the curved ceiling; keep collision unsplit.
        if(surface>=STONE) {
            JGeometry::TVec3<f32> vertices[3]={a,b,c};
            for(int axis=0;axis<2;++axis) {
                float distance[3],lo=1e30f,hi=-1e30f;
                for(int i=0;i<3;++i) {
                    float u,v;local(route,vertices[i],u,v);
                    distance[i]=axis ? v-seamV : u-triggerAt;
                    lo=fminf(lo,distance[i]);hi=fmaxf(hi,distance[i]);
                }
                if(lo>=-.01f || hi<=.01f)continue;
                for(int side=-1;side<=1;side+=2) {
                    JGeometry::TVec3<f32> clipped[4];int num=0;
                    for(int i=0;i<3;++i) {
                        int j=(i+1)%3;
                        bool inside=distance[i]*side>=0,next=distance[j]*side>=0;
                        if(inside)clipped[num++]=vertices[i];
                        if(inside!=next) {
                            float t=distance[i]/(distance[i]-distance[j]);
                            clipped[num++].set(vertices[i].x+t*(vertices[j].x-vertices[i].x),
                                vertices[i].y+t*(vertices[j].y-vertices[i].y),
                                vertices[i].z+t*(vertices[j].z-vertices[i].z));
                        }
                    }
                    for(int i=1;i+1<num;++i)tri(clipped[0],clipped[i],clipped[i+1],col,false);
                }
                return;
            }
        }
        Triangle& t=tris[count++];t.v[0]=a;t.v[1]=b;t.v[2]=c;t.color=col;t.surface=surface;
        float au,av,bu,bv,cu,cv;local(route,a,au,av);local(route,b,bu,bv);local(route,c,cu,cv);
        float nu=(bv-av)*(c.y-a.y)-(b.y-a.y)*(cv-av);
        float nv=(b.y-a.y)*(cu-au)-(bu-au)*(c.y-a.y);
        float nh=(bu-au)*(cv-av)-(bv-av)*(cu-au);
        for(int k=0;k<3;++k) {
            float u,v;local(route,t.v[k],u,v);float h=t.v[k].y-route.y;
            if(surface>=STONE) {u=fabsf(u-triggerAt);v=fabsf(v-seamV);}
            if(fabsf(nh)>=fabsf(nu) && fabsf(nh)>=fabsf(nv)) {t.uv[k][0]=u/256;t.uv[k][1]=v/256;}
            else {t.uv[k][0]=(fabsf(nu)>fabsf(nv)?v:u)/256;t.uv[k][1]=h/256;}
        }
    }
    void quad(float a,float b,float c,float d,float e,float f,
              float g,float h,float i,float j,float k,float l,GXColor col,bool solid)
    {
        JGeometry::TVec3<f32> p=point(route,a,b,c),q=point(route,d,e,f),
            r=point(route,g,h,i),s=point(route,j,k,l);
        tri(p,q,r,col,solid);tri(p,r,s,col,solid);
    }
    void box(float a,float b,float c,float d,float e,float f,GXColor col,bool solid)
    {
        // Local (u,v) basis has positive determinant: top winds upward.
        quad(a,c,f,a,d,f,b,d,f,b,c,f,col,solid);
        GXColor shade=color(col.r*0.82f,col.g*0.82f,col.b*0.82f);
        quad(a,c,e,a,c,f,b,c,f,b,c,e,shade,solid);
        quad(a,d,e,b,d,e,b,d,f,a,d,f,shade,solid);
        quad(a,c,e,a,d,e,a,d,f,a,c,f,shade,solid);
        quad(b,c,e,b,c,f,b,d,f,b,d,e,shade,solid);
        quad(a,c,e,b,c,e,b,d,e,a,d,e,shade,solid);
    }
    virtual void perform(u32 cue,JDrama::TGraphics* graphics)
    {
        if (!(cue&CUE_DRAW)) return;
        if(getenv("SMS_OPEN_WORLD_TRACE_CAMERA")) {
            OSReport("[coast-camera] stage=%d mario=%.1f,%.1f eye=%.1f,%.1f,%.1f at=%.1f,%.1f,%.1f fov=%.2f matrix=%.3f,%.3f,%.3f,%.3f\n",
                route.stage,gpMarioOriginal->mPosition.x,gpMarioOriginal->mPosition.z,
                gpCamera->unk124.x,gpCamera->unk124.y,gpCamera->unk124.z,
                gpCamera->unk148.x,gpCamera->unk148.y,gpCamera->unk148.z,gpCamera->mFovy,
                gpCamera->unk1EC[0][0],gpCamera->unk1EC[0][2],gpCamera->unk1EC[1][1],gpCamera->unk1EC[1][2]);
        }
        // This performer runs at the end of the opaque stage draw, ahead of
        // GX Post's UI. Restore the stage camera, use vertex colors only.
        gpCamera->perform(CUE_CALC_VIEW|CUE_SET_PROJECTION,graphics);
        GXSetViewport(0,0,SMSGetGameRenderWidth(),SMSGetGameRenderHeight(),0,1);
        GXSetClipMode(GX_CLIP_ENABLE);
        GXSetZTexture(GX_ZT_DISABLE,GX_TF_Z24X8,0);
        GXSetFog(GX_FOG_NONE,0,1,0,1,color(0,0,0));
        GXSetColorUpdate(GX_TRUE);GXSetAlphaUpdate(GX_TRUE);
        GXLoadPosMtxImm(graphics->getViewMtx(),GX_PNMTX0);
        GXSetCurrentMtx(GX_PNMTX0);
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS,GX_DIRECT);GXSetVtxDesc(GX_VA_CLR0,GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
        GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
        GXSetNumChans(1);
        GXSetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,0,GX_DF_NONE,GX_AF_NONE);
        GXSetNumTexGens(0);GXSetNumTevStages(1);
        GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD_NULL,GX_TEXMAP_NULL,GX_COLOR0A0);
        GXSetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
        GXSetNumIndStages(0);GXSetTevDirect(GX_TEVSTAGE0);
        GXSetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
        GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
        GXSetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);GXSetZCompLoc(GX_TRUE);
        GXSetCullMode(GX_CULL_NONE);
        GXSetVtxDesc(GX_VA_TEX0,GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
        GXSetTexCoordGen2(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY,GX_FALSE,GX_PTIDENTITY);
        for(int material=0;material<SURFACE_COUNT;++material) {
            bool textured=material && surfaces[material].valid;
            GXSetNumTexGens(textured?1:0);
            GXSetTevOrder(GX_TEVSTAGE0,textured?GX_TEXCOORD0:GX_TEXCOORD_NULL,
                textured?GX_TEXMAP0:GX_TEXMAP_NULL,GX_COLOR0A0);
            GXSetTevOp(GX_TEVSTAGE0,textured?GX_MODULATE:GX_PASSCLR);
            if(textured) GXLoadTexObj(&surfaces[material].texture,GX_TEXMAP0);
            int num=0;for(int i=0;i<count;++i)if(tris[i].surface==material)++num;
            if(!num)continue;
            GXBegin(GX_TRIANGLES,GX_VTXFMT0,num*3);
            for(int i=0;i<count;++i)if(tris[i].surface==material)
                for(int j=0;j<3;++j) {
                    GXPosition3f32(tris[i].v[j].x,tris[i].v[j].y,tris[i].v[j].z);
                    GXColor4u8(tris[i].color.r,tris[i].color.g,tris[i].color.b,255);
                    GXTexCoord2f32(tris[i].uv[j][0],tris[i].uv[j][1]);
                }
            GXEnd();
        }
        if (releaseFramePending && gpApplication.mFader->isFullyFadedIn()) {
            if (GXPC_CoastalHold) GXPC_CoastalHold(0);
            releaseFramePending=false;
            OSReport("[open-world] continuous frame released crossing=%d\n",crossings);
        }
    }
};
Walkway* walkway;
class CoastalDraw : public JDrama::TViewObj {
public:
    CoastalDraw() : JDrama::TViewObj("Coastal geometry") {}
    virtual void perform(u32 cue,JDrama::TGraphics* graphics) {
        if (walkway) walkway->perform(cue,graphics);
        sms_sea_draw(cue,graphics);
    }
};


void refreshCameraMatrices(bool resetPrevious)
{
    C_MTXPerspective(gpCamera->unk16C,gpCamera->mFovy,gpCamera->mAspect,gpCamera->mNear,gpCamera->mFar);
    C_MTXLookAt(gpCamera->unk1EC,&gpCamera->unk124,&gpCamera->mUp,&gpCamera->unk148);
    if(resetPrevious) {
        MTXCopy(gpCamera->unk1EC,gpCamera->unk21C);
        memcpy(gpCamera->unk1AC,gpCamera->unk16C,sizeof gpCamera->unk16C);
        gpCamera->unk13C=gpCamera->unk124;gpCamera->unk160=gpCamera->unk148;
    }
}

// Follow the centre line through the narrow bends. The short camera boom
// stays inside the passage; the open-waterfront camera blends in/out over
// the approach. The same centre line is used on either side of the exchange.
void passageCamera(const Route& r,TMario* m)
{
    float u,v;local(r,m->mPosition,u,v);
    if (u<routeEnd-600) {passageFacing=1;return;}
    if (m->mPosition.y>r.y+750) return;
    const int n=sizeof(passage)/sizeof(passage[0])+1;
    Bend path[n];path[0].u=routeEnd-1100;path[0].v=0;
    for(int i=1;i<n;++i) path[i]=passage[i-1];
    float lengths[n-1],best=1e30f,along=0,total=0;
    for(int i=0;i<n-1;++i) {
        float du=path[i+1].u-path[i].u,dv=path[i+1].v-path[i].v;
        float len=sqrtf(du*du+dv*dv);lengths[i]=len;
        float t=((u-path[i].u)*du+(v-path[i].v)*dv)/(len*len);
        if(t<0)t=0;if(t>1)t=1;
        float x=path[i].u+t*du-u,y=path[i].v+t*dv-v;
        if(x*x+y*y<best) {best=x*x+y*y;along=total+t*len;}
        total+=len;
    }
    if(best>(halfWidth+80)*(halfWidth+80)) return;
    float eyeAlong=along-passageFacing*550;
    if(eyeAlong<0)eyeAlong=0;if(eyeAlong>total)eyeAlong=total;
    Bend eyePath=path[n-1];
    for(int i=0;i<n-1;++i) {
        if(eyeAlong<=lengths[i]) {
            float t=eyeAlong/lengths[i];
            eyePath.u=path[i].u+(path[i+1].u-path[i].u)*t;
            eyePath.v=path[i].v+(path[i+1].v-path[i].v)*t;break;
        }
        eyeAlong-=lengths[i];
    }
    float blend=(u-(routeEnd-600))/600;if(blend>1)blend=1;
    passageBlend=blend;
    JGeometry::TVec3<f32> eye=point(r,eyePath.u,eyePath.v,250);
    JGeometry::TVec3<f32> at=m->mPosition;at.y+=150;
    eye.x=gpCamera->mPosition.x+(eye.x-gpCamera->mPosition.x)*blend;
    eye.y=gpCamera->mPosition.y+(eye.y-gpCamera->mPosition.y)*blend;
    eye.z=gpCamera->mPosition.z+(eye.z-gpCamera->mPosition.z)*blend;
    at.x=gpCamera->mTarget.x+(at.x-gpCamera->mTarget.x)*blend;
    at.y=gpCamera->mTarget.y+(at.y-gpCamera->mTarget.y)*blend;
    at.z=gpCamera->mTarget.z+(at.z-gpCamera->mTarget.z)*blend;
    gpCamera->warpPosAndAt(eye,at);
    gpCamera->unk258=s16(atan2f(eye.x-at.x,eye.z-at.z)*(65536.0f/6.283185307f));
    refreshCameraMatrices(false);
}

void placeMario(const Route& r,float u,float v,bool outward)
{
    TMario* m=gpMarioOriginal;
    OSReport("[open-world] original spawn=(%.0f,%.0f,%.0f)\n",m->mPosition.x,m->mPosition.y,m->mPosition.z);
    JGeometry::TVec3<f32> p=point(r,u,v,5);
    float angle=atan2f(r.dx,r.dz)*180.0f/3.14159265f+(outward?0:180);
    m->waitingStart(&p,angle);
    m->mPosition=p;m->mVel.zero();m->mForwardVel=0;m->resetHistory();
    m->changePlayerStatus(MARIO_STATUS_WAIT,0,true);
    Vec at={p.x,p.y+120,p.z};
    float sign=outward?-1:1;
    Vec eye={p.x+sign*r.dx*850,p.y+450,p.z+sign*r.dz*850};
    gpCamera->endDemoCamera();gpCamera->warpPosAndAt(eye,at);
}
}

JDrama::TViewObj* sms_open_world_draw_object()
{
    return new CoastalDraw;
}

extern "C" int sms_open_world_enabled()
{
    static int enabled=-1;
    if (enabled<0) {
        const char* e=getenv("SMS_OPEN_WORLD");
        enabled=e && (!strcmp(e,"1") || !strcmp(e,"on"));
    }
    return enabled;
}

void sms_open_world_profile(const char* phase)
{
    if (sms_open_world_enabled() && getenv("SMS_OPEN_WORLD_PROFILE"))
        OSReport("[open-world-profile] %llu %s\n",port_open_world_milliseconds(),phase);
}

void sms_open_world_setup(TMarDirector* d)
{
    walkway=0;
    testPadActive=false;
    sms_sea_setup(d);
    if (!sms_open_world_enabled()) return;
    const Route* r=routeFor(d->mMap);
    if (!r) { pendingStage=-1;return; }
    if (d->mMap==PLAZA) plazaEpisode=d->unk7D;
    else harborEpisode=d->unk7D;
    walkway=new Walkway(*r);
    OSReport("[open-world] collision grid extent=(%.0f,%.0f) lists=%u/%u\n",
             gpMapCollisionData->mGridExtentX,gpMapCollisionData->mGridExtentY,
             gpMapCollisionData->unk38,gpMapCollisionData->unk20);
    int neighbor = d->mMap==PLAZA ? HARBOR : PLAZA;
    int episode = neighbor==PLAZA ? plazaEpisode : harborEpisode;
    if (gpApplication.unk30 && neighbor < gpApplication.unk30->getChildren().size()) {
        TNameRefAryT<TScenarioArchiveName>* names=gpApplication.unk30->getChildren()[neighbor];
        if (episode < names->size()) {
            char path[128];
            snprintf(path,sizeof path,"/data/scene/%s",names->getChildren()[episode].mArcName);
            if (char* ext=strstr(path,".arc")) strcpy(ext,".szs");
            port_open_world_prefetch(path,neighbor==PLAZA ? 0 : 1);
        }
    }

    if (pendingStage!=-1 && pendingStage!=d->mMap) pendingStage=-1;
}

void sms_open_world_start_wipe(unsigned int type,float time,bool entering)
{
    if (sms_open_world_enabled() && (pendingStage!=-1 || sms_sea_pending())) {
        gpApplication.mFader->setFadeStatus(entering ? TSMSFader::FADE_STATUS_FULLY_FADED_IN
            : TSMSFader::FADE_STATUS_FULLY_FADED_OUT);
    } else gpApplication.mFader->startWipe(type,time,0.0f);
}

JGeometry::TVec3<f32> transferPoint(const Route& to,const JGeometry::TVec3<f32>& p)
{
    float u,v;local(savedPose.from,p,u,v);
    return point(to,2*triggerAt-u,2*seamV-v,p.y-savedPose.from.y);
}

bool sms_open_world_arriving(TMarDirector* d)
{
    if(sms_sea_arriving(d))return true;
    return sms_open_world_enabled() && walkway &&
        (pendingStage==d->mMap || (!testSpawnUsed && getenv("SMS_OPEN_WORLD_TEST_SPAWN")));
}

void sms_open_world_arrive(TMarDirector* d)
{
    if(sms_sea_arriving(d)){sms_sea_arrive(d);return;}
    if (!sms_open_world_arriving(d)) return;
    if (pendingStage==d->mMap) {
        TMario* m=gpMarioOriginal;
        const Route& r=walkway->route;
        JGeometry::TVec3<f32> p=transferPoint(r,savedPose.pos);
        float rotation=3.14159265f+atan2f(r.dx,r.dz)-atan2f(savedPose.from.dx,savedPose.from.dz);
        s16 turn=s16(int(rotation*(65536.0f/6.283185307f)));
        m->waitingStart(&p,(savedPose.yaw+turn)*(360.0f/65536.0f));
        m->mPosition=p;m->mPrevPosition=p;m->resetHistory();
        m->changePlayerStatus(savedPose.actorDependent ? MARIO_STATUS_RUN : savedPose.status,0,true);
        if(!savedPose.actorDependent) {
            m->mStatusState=savedPose.state;m->mStatusTimer=savedPose.timer;
        }
        float vu=savedPose.vel.x*savedPose.from.dx+savedPose.vel.z*savedPose.from.dz;
        float vv=-savedPose.vel.x*savedPose.from.dz+savedPose.vel.z*savedPose.from.dx;
        m->mVel.set(-r.dx*vu+r.dz*vv,savedPose.vel.y,-r.dz*vu-r.dx*vv);
        m->mForwardVel=savedPose.speed;
        m->mSlideVelX=m->mVel.x;m->mSlideVelZ=m->mVel.z;
        float ground=gpMapCollisionData->checkGround(p.x,p.y+100,p.z,0,&m->mGroundPlane);
        m->mFloorPosition.set(p.x,ground,p.z);
        m->mFaceAngle.y=savedPose.yaw+turn;m->mModelFaceAngle=m->mFaceAngle.y;
        m->mIntendedYaw=m->mFaceAngle.y;
        if(!savedPose.actorDependent) {
            m->setAnimation(savedPose.animation,1.0f);
            m->getMotionFrameCtrl().setFrame(savedPose.animationFrame);
            m->getMotionFrameCtrl().setRate(savedPose.animationRate);
        }
        if(m->mWaterGun && !savedPose.actorDependent) {
            m->mWaterGun->changeNozzle((TWaterGun::TNozzleType)savedNozzle,false);
            m->mWaterGun->mSecondNozzle=savedSecondNozzle;
        }
        JGeometry::TVec3<f32> eye=transferPoint(r,savedPose.eye),at=transferPoint(r,savedPose.at);
        gpCamera->endDemoCamera();gpCamera->warpPosAndAt(eye,at);
        gpCamera->mFovy=savedPose.fov;gpCamera->unk258=savedPose.cameraYaw+turn;
        refreshCameraMatrices(true);
        // Settle only the HUD's entry animations, without advancing gameplay.
        // Its normal updates continue immediately on the destination frame.
        TGCConsole2* console=d->mConsole;
        console->unk39=0;
        console->startAppearCoin();console->startAppearTank();
        for(int i=0;i<180;++i) {
            console->processAppearCoin(i);console->processAppearTank(i);
        }
        console->unk4F=0;console->unk45=0;console->unk46=1;console->unk50=0;
        console->unk3A=0;console->unk3B=0;
        console->unk3A8->getPane()->hide();
        console->startDisappearTelop();
        gpApplication.mFader->setFadeStatus(TSMSFader::FADE_STATUS_FULLY_FADED_IN);
        releaseFramePending=true;
        testWaypoint=testPathCount-2;testDirection=-1;passageFacing=-savedPose.direction;
        float arrivalU,arrivalV;local(r,p,arrivalU,arrivalV);
        OSReport("[open-world] continuity speed=%.2f -> %.2f local=(%.2f,%.2f) status=%x\n",
            savedPose.speed,m->mForwardVel,arrivalU,arrivalV,m->mStatus);
        gpMarioOriginal->mHealth=savedHealth;
        if (gpMarioOriginal->mWaterGun) gpMarioOriginal->mWaterGun->mCurrentWater=savedWater;
        ++crossings;
        finishingCrossing=true;
        unsigned long long elapsed=port_open_world_milliseconds()-crossingStart;
        OSReport("[open-world] arrived stage=%d episode=%d crossing=%d elapsed_ms=%u\n",
                 d->mMap,d->unk7D,crossings,(unsigned int)elapsed);
        OSReport("[open-world] restored health=%d water=%d\n",savedHealth,(int)savedWater);
        pendingStage=-1;
    } else {
        float u=200,v=0;
        sscanf(getenv("SMS_OPEN_WORLD_TEST_SPAWN"),"%f,%f",&u,&v);
        placeMario(walkway->route,u,v,true);testSpawnUsed=true;
        OSReport("[open-world] test spawn stage=%d u=%.0f v=%.0f\n",d->mMap,u,v);
    }
}

void sms_open_world_tick(TMarDirector* d)
{
    testPadActive=false;
    sms_sea_tick(d);
    if(sms_sea_active() || sms_sea_pending())return;
    if (!sms_open_world_enabled() || !walkway || d->mState!=TMarDirector::STATE_UNK4
        || d->unk124 || pendingStage!=-1 || d->unk4C&0x1ff) return;
    if (finishingCrossing && gpApplication.mFader->isFullyFadedIn()) {
        OSReport("[open-world] ready stage=%d total_ms=%u\n",d->mMap,
            (unsigned int)(port_open_world_milliseconds()-crossingStart));
        finishingCrossing=false;
    }
    TMario* m=gpMarioOriginal;
    float u,v;local(walkway->route,m->mPosition,u,v);
    if (u>routeEnd-600) {
        d->mConsole->startDisappearTelop();
    }
    if (const char* walk=getenv("SMS_OPEN_WORLD_TEST_WALK")) {
        testPadActive=true;testStickX=testStickY=0;
        float du=testPath[testWaypoint].u-u,dv=testPath[testWaypoint].v-v;
        if (!testWalkDone && du*du+dv*dv<90*90) {
            if (testWaypoint==0) {
                OSReport("[open-world] walk test landed stage=%d crossing=%d\n",d->mMap,crossings);
                if (crossings>=atoi(walk)) testWalkDone=true;
                else {testDirection=1;testWaypoint=1;}
            } else testWaypoint+=testDirection;
            if (testWaypoint>=testPathCount) testWaypoint=testPathCount-1;
            du=testPath[testWaypoint].u-u;dv=testPath[testWaypoint].v-v;
        }
        if (!testWalkDone) {
            float dx=walkway->route.dx*du-walkway->route.dz*dv;
            float dz=walkway->route.dz*du+walkway->route.dx*dv;
            float angle=atan2f(dx,dz)-gpCamera->getUnk258()*(6.283185307f/65536.0f);
            testStickX=int(100*sinf(angle));testStickY=int(-100*cosf(angle));
        }
    }
    // Arrival is inside the shelter, ahead of the trigger. Walking toward
    // town arms the return crossing; standing still can never bounce maps.
    float crossingDistance=(u-triggerAt)*seamDU+(v-seamV)*seamDV;
    float crossingLateral=-(u-triggerAt)*seamDV+(v-seamV)*seamDU;
    if (crossingDistance < -450) walkway->armed=true;
    static int logTimer;
    if (getenv("SMS_OPEN_WORLD_LOG") && (++logTimer%60)==0)
        OSReport("[open-world] position stage=%d xyz=(%.1f,%.1f,%.1f) u=%.1f v=%.1f status=%x\n",
                 d->mMap,m->mPosition.x,m->mPosition.y,m->mPosition.z,u,v,m->mStatus);
    if (!walkway->armed || crossingDistance<0 || crossingDistance>250 || fabsf(crossingLateral)>halfWidth-35
        || m->mPosition.y<walkway->route.y-20 || m->mPosition.y>walkway->route.y+650
        || m->mHealth<=0) return;
    int target=d->mMap==PLAZA?HARBOR:PLAZA;
    int episode=target==PLAZA?plazaEpisode:harborEpisode;
    savedHealth=m->mHealth;savedWater=m->mWaterGun?m->mWaterGun->mCurrentWater:0;
    savedNozzle=m->mWaterGun?m->mWaterGun->mCurrentNozzle:0;
    savedSecondNozzle=m->mWaterGun?m->mWaterGun->mSecondNozzle:4;
    OSReport("[open-world] carry health=%d water=%d\n",savedHealth,(int)savedWater);
    savedPose.from=walkway->route;savedPose.pos=m->mPosition;savedPose.vel=m->mVel;
    savedPose.eye=gpCamera->mPosition;savedPose.at=gpCamera->mTarget;
    savedPose.speed=m->mForwardVel;savedPose.fov=gpCamera->mFovy;
    savedPose.yaw=m->mFaceAngle.y;savedPose.cameraYaw=gpCamera->unk258;
    savedPose.animationFrame=m->getMotionFrameCtrl().getFrame();
    savedPose.animationRate=m->getMotionFrameCtrl().getRate();
    savedPose.direction=passageFacing;
    savedPose.status=m->mStatus;savedPose.state=m->mStatusState;
    savedPose.timer=m->mStatusTimer;savedPose.animation=m->mAnimationId;
    savedPose.actorDependent=m->onYoshi() || m->isHolding() || m->getHolder()!=0;
    if (GXPC_CoastalHold) GXPC_CoastalHold(1);
    pendingStage=target;crossingStart=port_open_world_milliseconds();
    d->setNextStage(((target+1)<<8)|episode,0);
    OSReport("[open-world] crossing stage=%d -> stage=%d episode=%d\n",d->mMap,target,episode);
}

void sms_open_world_camera()
{
    passageBlend=0;
    if(getenv("SMS_OPEN_WORLD_TEST_BOATS") && gpCamera && walkway
        && walkway->route.stage==PLAZA && gpMarDirector
        && gpMarDirector->mState==TMarDirector::STATE_UNK4) {
        JGeometry::TVec3<f32> eye(-17200.f,4300.f,4800.f),at(-11700.f,150.f,-900.f);
        gpCamera->warpPosAndAt(eye,at);refreshCameraMatrices(false);return;
    }
    if(sms_sea_camera()){passageBlend=1;return;}
    if(sms_open_world_enabled() && walkway && pendingStage==-1 && gpMarDirector
        && gpMarDirector->mState==TMarDirector::STATE_UNK4)
        passageCamera(walkway->route,gpMarioOriginal);
}

unsigned char sms_open_world_ambient_alpha(unsigned char alpha)
{
    return sms_open_world_enabled() ? (unsigned char)(alpha*(1-passageBlend)) : alpha;
}

extern "C" int sms_open_world_test_pad(signed char* x,signed char* y)
{
    if (!testPadActive) return 0;
    *x=testStickX;*y=testStickY;return 1;
}

// Diagnostic only: observe the native boats; never change their movement.
// Test an expanded, oriented hull against every rendered walkway triangle.
// The expansion allows for the distance traveled between sampled frames.
namespace {
typedef JGeometry::TVec3<f32> ProbeVec;
bool probeSeparates(const ProbeVec& axis,const ProbeVec v[3],const ProbeVec& half) {
    float r=fabsf(axis.x)*half.x+fabsf(axis.y)*half.y+fabsf(axis.z)*half.z;
    float a=axis.x*v[0].x+axis.y*v[0].y+axis.z*v[0].z;
    float b=axis.x*v[1].x+axis.y*v[1].y+axis.z*v[1].z;
    float c=axis.x*v[2].x+axis.y*v[2].y+axis.z*v[2].z;
    return fminf(a,fminf(b,c))>r || fmaxf(a,fmaxf(b,c))<-r;
}
bool probeTriangleBox(const ProbeVec v[3],const ProbeVec& half) {
    const ProbeVec axes[3]={ProbeVec(1.f,0.f,0.f),ProbeVec(0.f,1.f,0.f),ProbeVec(0.f,0.f,1.f)};
    for(int i=0;i<3;++i)if(probeSeparates(axes[i],v,half))return false;
    ProbeVec edges[3],normal;
    for(int i=0;i<3;++i)edges[i]=v[(i+1)%3]-v[i];
    normal.cross(edges[0],edges[1]);
    if(probeSeparates(normal,v,half))return false;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
        ProbeVec axis;axis.cross(edges[i],axes[j]);
        if(probeSeparates(axis,v,half))return false;
    }
    return true;
}
struct BoatProbe {
    TFruitsBoat* boat;
    unsigned ticks,checks,overlaps,loops;
    float phase,maxStep;
    ProbeVec previous;
};
BoatProbe boatProbes[8];
}
void sms_open_world_boat_probe(TFruitsBoat* boat)
{
    static int enabled=getenv("SMS_OPEN_WORLD_TEST_BOATS")!=0;
    if(!enabled || !walkway || walkway->route.stage!=PLAZA || !gpMarDirector
        || gpMarDirector->mState!=TMarDirector::STATE_UNK4 || boat->getBoatType()!=0)return;
    BoatProbe* p=0;
    for(int i=0;i<8;++i)if(!boatProbes[i].boat || boatProbes[i].boat==boat){p=&boatProbes[i];break;}
    if(!p)return;
    const char* route=boat->getTracer()->getGraph()->unkC;
    float phase=boat->getTracer()->unk14;
    if(!p->boat){p->boat=boat;p->previous=boat->mPosition;p->phase=phase;}
    ProbeVec step=boat->mPosition-p->previous;
    p->maxStep=fmaxf(p->maxStep,sqrtf(step.x*step.x+step.y*step.y+step.z*step.z));
    p->previous=boat->mPosition;
    // Native routes can run in either direction through the spline wrap.
    bool loop=fabsf(phase-p->phase)>.5f;
    p->phase=phase;if(loop)++p->loops;
    ++p->ticks;
    if(!(p->ticks%4)) {
        J3DModel* model=boat->getModel();
        J3DJoint* joint=model->getModelData()->getJointNodePointer(0);
        const Vec& lo=joint->getMin();const Vec& hi=joint->getMax();
        ProbeVec center((lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f);
        ProbeVec half((hi.x-lo.x)*.5f,(hi.y-lo.y)*.5f,(hi.z-lo.z)*.5f);
        half.x+=100;half.y+=100;half.z+=100;
        Mtx inverse;
        if(!MTXInverse(model->getBaseTRMtx(),inverse))return;
        ++p->checks;
        ProbeVec hullMin(1e30f,1e30f,1e30f),hullMax(-1e30f,-1e30f,-1e30f);
        for(int k=0;k<8;++k) {
            ProbeVec corner((center.x+(k&1?half.x:-half.x))*boat->mScaling.x,
                (center.y+(k&2?half.y:-half.y))*boat->mScaling.y,
                (center.z+(k&4?half.z:-half.z))*boat->mScaling.z),v;
            MTXMultVec(model->getBaseTRMtx(),&corner,&v);
            hullMin.x=fminf(hullMin.x,v.x);hullMin.y=fminf(hullMin.y,v.y);hullMin.z=fminf(hullMin.z,v.z);
            hullMax.x=fmaxf(hullMax.x,v.x);hullMax.y=fmaxf(hullMax.y,v.y);hullMax.z=fmaxf(hullMax.z,v.z);
        }
        for(int i=0;i<walkway->count;++i) {
            const ProbeVec* triangle=walkway->tris[i].v;
            if(fmaxf(triangle[0].x,fmaxf(triangle[1].x,triangle[2].x))<hullMin.x
                || fminf(triangle[0].x,fminf(triangle[1].x,triangle[2].x))>hullMax.x
                || fmaxf(triangle[0].z,fmaxf(triangle[1].z,triangle[2].z))<hullMin.z
                || fminf(triangle[0].z,fminf(triangle[1].z,triangle[2].z))>hullMax.z)continue;
            ProbeVec v[3];
            for(int k=0;k<3;++k) {
                MTXMultVec(inverse,&walkway->tris[i].v[k],&v[k]);
                v[k].x=v[k].x/boat->mScaling.x-center.x;
                v[k].y=v[k].y/boat->mScaling.y-center.y;
                v[k].z=v[k].z/boat->mScaling.z-center.z;
            }
            if(probeTriangleBox(v,half)){++p->overlaps;break;}
        }
    }
    if(loop || !(p->ticks%600))
        OSReport("[boat-probe] route=%s ticks=%u checks=%u loops=%u overlaps=%u max_step=%.2f phase=%.5f xyz=(%.1f,%.1f,%.1f)\n",
            route,p->ticks,p->checks,p->loops,p->overlaps,p->maxStep,phase,boat->mPosition.x,boat->mPosition.y,boat->mPosition.z);
}
