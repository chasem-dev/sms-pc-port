#ifndef SMS_OPEN_WORLD_SEA_H
#define SMS_OPEN_WORLD_SEA_H
class TMarDirector;
namespace JDrama { class TGraphics; }
void sms_sea_setup(TMarDirector*);
bool sms_sea_arriving(TMarDirector*);
void sms_sea_arrive(TMarDirector*);
void sms_sea_tick(TMarDirector*);
bool sms_sea_camera();
bool sms_sea_pending();
bool sms_sea_active();
void sms_sea_draw(unsigned int,JDrama::TGraphics*);
void sms_sea_filter_map();
#endif
