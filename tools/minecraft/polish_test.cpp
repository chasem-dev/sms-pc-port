#include "../../src/minecraft/building.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <fstream>
#include <iterator>
#include <vector>
using namespace minecraft;
static Slot stack(Item item,int count){Slot s={item,count};return s;}
static uint32_t checksum(const std::vector<unsigned char>& b){uint32_t c=0xffffffff;for(size_t i=20;i<b.size();++i){c^=b[i];for(int j=0;j<8;++j)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
static void word(std::vector<unsigned char>& b,int at,uint32_t v){for(int i=0;i<4;++i)b[at+i]=(v>>(i*8))&255;}
int main(int argc,char** argv){
 assert(argc==2);setenv("SMS_MINECRAFT_SAVE",argv[1],1);
 TreeBreak mining;float axe=woodMiningTime(Chest,IronAxe),hand=woodMiningTime(Chest,Empty);assert(axe==.625f&&hand==3.75f);
 assert(!mining.advance(7,true,.3f,axe,IronAxe)&&mining.stage()==4);
 assert(!mining.advance(7,false,.1f,axe,IronAxe)&&mining.target==-1&&mining.seconds==0);
 assert(!mining.advance(7,true,.3f,axe,IronAxe));assert(!mining.advance(8,true,.1f,axe,IronAxe)&&fabsf(mining.seconds-.1f)<.00001f);
 assert(!mining.advance(8,true,.1f,hand,Empty)&&fabsf(mining.seconds-.1f)<.00001f);assert(mining.advance(8,true,hand,hand,Empty));
 std::unique_ptr<State> s(new State),loaded(new State);s->originY=300;s->originSet=true;
 for(int i=0;i<InventorySize;++i)s->inventory.slots[i]=stack(IronAxe,1);
 int chest=s->place(Chest,0,0,0,0);assert(chest>=0);
 for(int i=0;i<ChestSlots;++i)s->cells[chest].slots[i]=stack(WoodenPlanks,64);
 for(int i=0;i<DropCapacity-27;++i)assert(s->spawnDrop(WoodenLog,1,0,350,0,300)>=0);
 assert(!s->breakBlock(chest)&&s->cells[chest].slots[26].count==64&&s->freeDrops()==27);
 s->drops[0].active=false;assert(s->breakBlock(chest)&&s->cells[chest].item==Empty&&s->freeDrops()==0);
 int planks=0,chests=0;for(int i=0;i<DropCapacity;++i){WorldDrop& d=s->drops[i];if(d.item==WoodenPlanks)planks+=d.count;if(d.item==Chest)chests+=d.count;}
 assert(planks==27*64&&chests==1);
 Inventory inventory=s->inventory;assert(inventory.add(WoodenPlanks,64)==64);inventory.slots[35]=stack(WoodenPlanks,60);assert(inventory.add(WoodenPlanks,64)==60&&inventory.slots[35].count==64);
 for(int i=0;i<AchievementCount;++i){assert(s->unlock(Achievement(i)));assert(!s->unlock(Achievement(i)));}
 s->chestArmor=stack(TurboNozzle,1);assert(sms_minecraft_save(s.get()));assert(sms_minecraft_load(loaded.get())==1&&loaded->achievements==7&&loaded->freeDrops()==0&&loaded->chestArmor.item==TurboNozzle);
 int recovered=0;for(int i=0;i<DropCapacity;++i)if(loaded->drops[i].item==WoodenPlanks)recovered+=loaded->drops[i].count;assert(recovered==planks);
 // Strip only V3 trailer from an empty-drop snapshot to recreate V2, then
 // strip its armor field to recreate V1. Both preserve the original slots.
 for(int i=0;i<DropCapacity;++i)s->drops[i].active=false;
 assert(sms_minecraft_save(s.get()));
 std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());f.close();bytes.resize(bytes.size()-8);
 for(int version=2;version>=1;--version){if(version==1){int at=20+5*4+(InventorySize+1+9)*8;bytes.erase(bytes.begin()+at,bytes.begin()+at+8);}word(bytes,8,version);word(bytes,12,bytes.size()-20);word(bytes,16,checksum(bytes));{std::ofstream out(argv[1],std::ios::binary|std::ios::trunc);out.write((const char*)bytes.data(),bytes.size());}assert(sms_minecraft_load(loaded.get())==1&&loaded->achievements==0&&loaded->freeDrops()==DropCapacity&&loaded->inventory.slots[35].item==IronAxe);assert(loaded->chestArmor.item==(version==2?TurboNozzle:Empty));}
 float distance=3000;int face=-1;Box box={0,0,0,80,80,80};assert(rayBox(box,40,40,-900,0,0,1,distance,face)&&distance==900&&face==4);distance=899;assert(!rayBox(box,40,40,-900,0,0,1,distance,face));
 puts("PASS continuous mining cancellation/tool speed, atomic full-chest spill, partial/full inventory conservation, persisted world drops, one-time achievements, V1/V2 migration and camera ray range");
}
