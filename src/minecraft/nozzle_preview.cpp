// Native J3D icon source rendering. Models/textures come from the user's disc.
#include "nozzle_preview.h"
#include <JSystem/J3D/J3DGraphLoader/J3DModelLoader.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DSys.hpp>
#include <JSystem/JKernel/JKRFileLoader.hpp>
#include <Player/WaterGun.hpp>
#include <Player/NozzleBase.hpp>
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <stdio.h>
#include <math.h>
namespace {
J3DModel* models[3];
Vec centers[3];float fits[3];
bool attempted[3];
J3DModel* guiModels[7];
}
void sms_minecraft_nozzle_preview_reset() {
    // These allocations belong to the current scene heap and are freed by it.
    for(int i=0;i<3;++i){models[i]=0;attempted[i]=false;}
    for(int i=0;i<7;++i)guiModels[i]=0;
}
void sms_minecraft_fludd_preview(TWaterGun* gun,const Mtx base) {
    if(!gun||!gun->getModel()||gun->mCurrentNozzle>=6)return;
    J3DModel* sources[2]={gun->getModel(),gun->getCurrentNozzle()->getMActor()?gun->getCurrentNozzle()->getMActor()->getModel():0};
    Mtx inverse,delta,saved,identity;if(!MTXInverse(sources[0]->getBaseTRMtx(),inverse))return;
    MTXConcat(base,inverse,delta);MTXCopy(j3dSys.getViewMtx(),saved);MTXIdentity(identity);j3dSys.setViewMtx(identity);
    for(int part=0;part<2;++part)if(sources[part]) {
        J3DModel* source=sources[part];int index=part?gun->mCurrentNozzle+1:0;
        if(!guiModels[index])guiModels[index]=new J3DModel(source->getModelData(),0,1);
        J3DModel* model=guiModels[index];model->setBaseTRMtx(base);
        for(int i=0;i<source->getModelData()->getJointNum();++i){Mtx pose;MTXConcat(delta,source->getAnmMtx(i),pose);model->setAnmMtx(i,pose);model->setScaleFlag(i,source->getScaleFlag(i));}
        for(int i=0;i<source->getModelData()->getWEvlpMtxNum();++i){Mtx pose;MTXConcat(delta,source->getWeightAnmMtx(i),pose);model->setWeightAnmMtx(i,pose);}
        for(int i=0;i<source->getModelData()->getShapeNum();++i) {
            if(source->getShapePacket(i)->isVisible())model->getShapePacket(i)->show();else model->getShapePacket(i)->hide();
        }
        model->viewCalc();
        for(int i=0;i<model->getModelData()->getMaterialNum();++i)model->getModelData()->getMaterialNodePointer(i)->calc(identity);
        model->makeDL();j3dSys.reinitGX();
        for(int i=0;i<model->getModelData()->getMaterialNum();++i) {
            J3DMatPacket* packet=model->getMatPacket(i);j3dSys.setMatPacket(packet);j3dSys.setTexture(model->getModelData()->getTexture());packet->getMaterial()->load();
            GXSetChanCtrl(GX_COLOR0A0,GX_FALSE,GX_SRC_REG,GX_SRC_REG,0,GX_DF_NONE,GX_AF_NONE);GXSetCullMode(GX_CULL_NONE);GXSetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);GXSetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
            for(J3DShapePacket* shape=packet->getShapePacket();shape;shape=(J3DShapePacket*)shape->getNextPacket())shape->draw();
        }
    }
    j3dSys.setViewMtx(saved);
}
void sms_minecraft_nozzle_preview(int index,float x,float y,float size) {
    if(index<0||index>=3)return;
    if(!attempted[index]) {
        attempted[index]=true;
        const char* names[]={"normal_nozzle_item","rocket_nozzle_item","back_nozzle_item"};
        char path[128];snprintf(path,sizeof path,"/scene/mapObj/%s.bmd",names[index]);
        void* resource=JKRGetResource(path);
        if(!resource){OSReport("[minecraft] missing nozzle preview %s\n",path);return;}
        J3DModelData* data=J3DModelLoaderDataBase::load(resource,J3DMLF_MaterialPEFull|J3DMLF_MaterialTexGenFull);
        if(!data)return;
        void* material=JKRGetResource("/scene/mapObj/nozzleItem.bmt");
        if(!material){OSReport("[minecraft] missing colored nozzle material\n");return;}
        data->setMaterialTable(J3DModelLoaderDataBase::loadMaterialTable(material),J3DMatCopyFlag_All);
        models[index]=new J3DModel(data,0,1);
        const Vec& low=data->getRootNode()->getMin();const Vec& high=data->getRootNode()->getMax();
        centers[index]=(Vec){(low.x+high.x)*.5f,(low.y+high.y)*.5f,(low.z+high.z)*.5f};
        float dx=high.x-low.x,dy=high.y-low.y,dz=high.z-low.z;
        fits[index]=.82f/sqrtf(dx*dx+dy*dy+dz*dz);
        OSReport("[minecraft] native nozzle icon %s bounds %.1f %.1f %.1f\n",names[index],dx,dy,dz);
    }
    J3DModel* model=models[index];if(!model)return;
    Mtx saved,identity,rotate,tilt,base;
    MTXCopy(j3dSys.getViewMtx(),saved);MTXIdentity(identity);j3dSys.setViewMtx(identity);
    MTXRotRad(rotate,'y',2.35619449f);MTXRotRad(tilt,'x',.61547971f);MTXConcat(tilt,rotate,base);
    float scale=size*fits[index];const Vec& center=centers[index];
    for(int row=0;row<3;++row) {
        for(int col=0;col<3;++col)base[row][col]*=scale*(row==1?-1:1);
        base[row][3]=-(base[row][0]*center.x+base[row][1]*center.y+base[row][2]*center.z);
    }
    base[0][3]+=x+size*.5f;base[1][3]+=y+size*.5f;base[2][3]+=5000;
    model->setBaseTRMtx(base);model->calc();model->viewCalc();
    for(int i=0;i<model->getModelData()->getMaterialNum();++i)
        model->getModelData()->getMaterialNodePointer(i)->calc(identity);
    model->makeDL();
    Mtx44 projection;C_MTXOrtho(projection,0,480,0,640,-10000,10000);GXSetProjection(projection,GX_ORTHOGRAPHIC);
    j3dSys.reinitGX();
    for(int i=0;i<model->getModelData()->getMaterialNum();++i) {
        J3DMatPacket* packet=model->getMatPacket(i);
        j3dSys.setMatPacket(packet);j3dSys.setTexture(model->getModelData()->getTexture());
        packet->getMaterial()->load();
        GXSetChanCtrl(GX_COLOR0A0,GX_FALSE,GX_SRC_REG,GX_SRC_REG,0,GX_DF_NONE,GX_AF_NONE);
        GXSetCullMode(GX_CULL_NONE);GXSetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
        // Keep native materials/textures; render their real shape packets.
        for(J3DShapePacket* shape=packet->getShapePacket();shape;shape=(J3DShapePacket*)shape->getNextPacket())shape->draw();
    }
    j3dSys.setViewMtx(saved);
}
