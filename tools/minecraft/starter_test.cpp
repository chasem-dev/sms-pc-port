#include "../../src/minecraft/building.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace minecraft;
static Slot stack(Item item,int n){Slot s={item,n};return s;}
int main(int argc,char** argv){
 assert(argc==2);setenv("SMS_MINECRAFT_SAVE",argv[1],1);
 std::unique_ptr<State> s(new State),loaded(new State);
 assert(s->inventory.slots[0].item==HoverNozzle&&s->inventory.slots[3].item==RocketNozzle&&s->inventory.slots[4].item==TurboNozzle);
 assert(s->chestArmor.item==Empty&&stackLimit(Stick)==64&&!placeable(Stick));
 for(int size=2;size<=3;++size)for(int y=0;y<size-1;++y)for(int x=0;x<size;++x){
  Slot grid[9];for(int i=0;i<9;++i)grid[i]=emptySlot();
  grid[y*size+x]=stack(WoodenPlanks,3);grid[(y+1)*size+x]=stack(WoodenPlanks,2);
  Recipe r=recipe(grid,size);assert(r.item==Stick&&r.count==4);
 }
 Slot grid[9];for(int i=0;i<9;++i)grid[i]=emptySlot();grid[0]=grid[1]=stack(WoodenPlanks,1);assert(recipe(grid,2).item==Empty);
 grid[1]=emptySlot();grid[6]=stack(WoodenPlanks,1);assert(recipe(grid,3).item==Empty);
 s->menu=InventoryMenu;s->craft[1]=stack(WoodenPlanks,2);s->craft[3]=stack(WoodenPlanks,2);
 s->cursor=stack(Stick,61);assert(!s->takeResult()&&s->craft[1].count==2&&s->cursor.count==61);
 s->cursor=emptySlot();assert(s->takeResult()&&s->cursor.count==4&&s->craft[1].count==1&&s->craft[3].count==1);
 assert(s->takeResult()&&s->cursor.count==8&&s->craft[1].item==Empty&&s->craft[3].item==Empty);assert(s->close());
 assert(s->inventory.slots[5].item==Stick&&s->inventory.slots[5].count==8);
 int chest=s->place(Chest,0,0,0,0);assert(chest>=0);s->cells[chest].slots[26]=stack(Stick,64);
 assert(sms_minecraft_save(s.get())&&sms_minecraft_load(loaded.get())==1);
 assert(loaded->inventory.slots[5].item==Stick&&loaded->inventory.slots[5].count==8&&loaded->cells[chest].slots[26].item==Stick);
 assert(loaded->inventory.slots[3].item==RocketNozzle&&loaded->inventory.slots[4].item==TurboNozzle);
 puts("PASS all starting nozzles, shifted 2x2/3x3 stick recipes, invalid shapes, ingredient consumption, full cursor protection and stick/chest persistence");
}
