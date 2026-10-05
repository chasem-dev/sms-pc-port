#ifndef SMS_NOZZLE_PREVIEW_H
#define SMS_NOZZLE_PREVIEW_H
#include <dolphin/mtx.h>
class TWaterGun;
// Developer capture: native J3D pickups, used to regenerate static item icons.
void sms_minecraft_nozzle_preview_reset();
void sms_minecraft_nozzle_preview(int index,float x,float y,float size);
// Copies the live pose into scene-owned GUI model instances; never changes
// native FLUDD joints, attachment state or water-emission matrices.
void sms_minecraft_fludd_preview(TWaterGun* gun,const Mtx base);
#endif
