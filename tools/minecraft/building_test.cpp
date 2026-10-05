#include "../../src/minecraft/building.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <fstream>
using namespace minecraft;
static Slot stack(Item item,int n){Slot s={item,n};return s;}
int main(int argc,char** argv) {
    assert(argc==2);setenv("SMS_MINECRAFT_SAVE",argv[1],1);
    std::unique_ptr<State> state(new State);State& s=*state;
    // Mouse transfers preserve totals and obey item stack limits.
    Slot slot=stack(WoodenPlanks,7),cursor=emptySlot();clickSlot(slot,cursor,true);
    assert(slot.count==3&&cursor.count==4);clickSlot(slot,cursor,true);assert(slot.count==4&&cursor.count==3);
    slot=stack(WoodenPlanks,63);cursor=stack(WoodenPlanks,5);clickSlot(slot,cursor,false);assert(slot.count==64&&cursor.count==4);
    slot=stack(IronAxe,1);cursor=stack(DiamondSword,1);clickSlot(slot,cursor,false);assert(slot.item==DiamondSword&&cursor.item==IronAxe);
    s.menu=InventoryMenu;s.craft[3]=stack(WoodenLog,2);assert(s.result().item==WoodenPlanks&&s.result().count==4);
    assert(s.takeResult());assert(s.cursor.count==4&&s.craft[3].count==1);assert(s.takeResult());assert(s.cursor.count==8&&s.craft[3].item==Empty);
    assert(s.close());assert(s.inventory.slots[5].item==WoodenPlanks&&s.inventory.slots[5].count==8);
    s.menu=InventoryMenu;for(int i=0;i<4;++i)s.craft[i]=stack(WoodenPlanks,1);assert(s.result().item==CraftingTable);assert(s.takeResult());assert(s.close());
    s.menu=TableMenu;for(int i=0;i<9;++i)s.craft[i]=emptySlot();
    for(int y=0;y<3;++y)for(int x=1;x<3;++x)s.craft[y*3+x]=stack(WoodenPlanks,1);
    assert(s.result().item==WoodenDoor&&s.result().count==3);assert(s.takeResult(true));assert(s.close());
    s.menu=TableMenu;for(int i=0;i<9;++i)if(i!=4)s.craft[i]=stack(WoodenPlanks,1);
    assert(s.result().item==Chest);s.craft[4]=stack(WoodenPlanks,1);assert(s.result().item==Empty);s.craft[4]=emptySlot();assert(s.takeResult(true));assert(s.close());
    // Result clicks cannot consume ingredients when the destination is full.
    s.menu=InventoryMenu;s.craft[0]=stack(WoodenLog,1);s.cursor=stack(WoodenPlanks,64);assert(!s.takeResult());assert(s.craft[0].count==1);
    s.cursor=emptySlot();assert(s.close());
    int table=s.place(CraftingTable,-2,0,3,0);assert(table>=0);assert(s.place(Chest,-2,0,3,0)<0);
    int door=s.place(WoodenDoor,0,0,0,0);assert(door>=0&&s.find(0,1,0)==door&&s.place(WoodenPlanks,0,1,0,0)<0);
    Cell& d=s.cells[door];Box closed=cellBox(d,300);d.open=true;Box opened=cellBox(d,300);assert(closed.X-closed.x==80&&closed.Z-closed.z==10);assert(opened.X-opened.x==10&&opened.Z-opened.z==80);
    float distance=400;int face=-1;assert(rayBox(closed,40,340,-80,0,0,1,distance,face)&&face==4);
    int chest=s.place(Chest,2,0,0,1);s.cells[chest].slots[0]=stack(WoodenPlanks,37);s.cells[chest].slots[26]=stack(DiamondSword,1);assert(!s.remove(chest));
    s.originY=300;s.originSet=true;assert(sms_minecraft_save(&s));
    std::unique_ptr<State> recovered(new State);assert(sms_minecraft_load(recovered.get())==1);assert(recovered->cells[chest].slots[0].count==37&&recovered->cells[chest].slots[26].item==DiamondSword);assert(recovered->find(0,1,0)==door&&recovered->cells[door].open);
    s.cells[chest].slots[0].count=38;assert(sms_minecraft_save(&s));assert(sms_minecraft_load(recovered.get())==1&&recovered->cells[chest].slots[0].count==38);
    // Interrupted/corrupted primary loads the previous valid snapshot.
    {std::ofstream f(argv[1],std::ios::binary|std::ios::trunc);f<<"broken save";}
    assert(sms_minecraft_load(recovered.get())==1&&recovered->cells[chest].slots[0].count==37);
    assert(menuHit(InventoryMenu,350,110).group==2);assert(menuHit(TableMenu,215,115).group==2);assert(menuHit(ChestMenu,170,110).group==3);assert(menuHit(TableMenu,420,155).group==4);
    puts("PASS inventory splitting/merging/swap, exact recipes, full destination, grid placement, door collision and chest protection");
    puts("PASS disk roundtrip, chest contents, open door, atomic save and corrupted-primary recovery");
}
