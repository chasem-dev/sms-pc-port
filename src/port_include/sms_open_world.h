#ifndef SMS_OPEN_WORLD_H
#define SMS_OPEN_WORLD_H
class TMarDirector;
class TFruitsBoat;
class J3DModel;
J3DModel* sms_open_world_load_model(const char*,const char*);
void sms_open_world_crop_model(J3DModel*,bool);
void sms_open_world_trim_harbor_map(J3DModel*);
void sms_open_world_filter_map();
void sms_open_world_harbor_transform(float[3][4]);
namespace JDrama { class TViewObj; }
JDrama::TViewObj* sms_open_world_draw_object();
void sms_open_world_setup(TMarDirector*);
void sms_open_world_profile(const char*);
void sms_open_world_boat_probe(TFruitsBoat*);
void sms_open_world_start_wipe(unsigned int,float,bool);
void sms_open_world_tick(TMarDirector*);
void sms_open_world_camera();
unsigned char sms_open_world_ambient_alpha(unsigned char);
bool sms_open_world_arriving(TMarDirector*);
void sms_open_world_arrive(TMarDirector*);
extern "C" int sms_open_world_enabled();
#endif
