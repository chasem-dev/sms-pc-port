#ifndef SMS_OPEN_WORLD_SEA_H
#define SMS_OPEN_WORLD_SEA_H
class TMarDirector;
class MActor;
namespace JDrama { class TGraphics; }
class J3DModel;
J3DModel* sms_sea_mainland_model();
J3DModel* sms_sea_harbor_model();
void sms_sea_world_to_native(float[3][4]);
void sms_open_world_preview_objects(const char*,const float[3][4]);
void sms_sea_setup(TMarDirector*);
bool sms_sea_arriving(TMarDirector*);
void sms_sea_arrive(TMarDirector*);
void sms_sea_tick(TMarDirector*);
bool sms_sea_camera();
bool sms_sea_pending();
bool sms_sea_active();
void sms_sea_draw(unsigned int,JDrama::TGraphics*);
void sms_sea_filter_map();
void sms_sea_sky(MActor*,float [3][4]);
#endif
