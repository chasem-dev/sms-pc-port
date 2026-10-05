#ifndef SMS_MINECRAFT_H
#define SMS_MINECRAFT_H
class TMario;
class TWaterGun;
class TItemNozzle;
class THitActor;
class TMapObjTree;
class TBGCheckData;
class CPolarSubCamera;
namespace JDrama { struct TGraphics; }
extern "C" {
int sms_minecraft_enabled();
void sms_minecraft_damage(TMario*,int healthLost);
int sms_minecraft_pickup_nozzle(TItemNozzle*,THitActor*);
int sms_minecraft_locked_nozzle(TWaterGun*);
// Return true only for inputs reserved by the active crossover.
int sms_minecraft_event(const void* event);
int sms_minecraft_menu_open();
float sms_minecraft_ground(float x,float y,float z,float native,const TBGCheckData** result);
void sms_minecraft_reset_scene();
void sms_minecraft_update(TMario* mario);
void sms_minecraft_camera(CPolarSubCamera* camera);
void sms_minecraft_attach_fludd(TMario* mario);
void sms_minecraft_draw(TMario* mario, JDrama::TGraphics* graphics);
void sms_minecraft_hud(JDrama::TGraphics* graphics);
void sms_minecraft_register_tree(TMapObjTree* tree);
int sms_minecraft_tree_felled(TMapObjTree* tree);
}
#endif
