#include "../../src/minecraft/core.h"
#include <assert.h>
#include <stdio.h>
int main() {
    using namespace minecraft;
    Inventory inv;
    assert(inv.equipped() == Nozzle);
    inv.scroll(-1); assert(inv.selected == 8);
    inv.scroll(11); assert(inv.selected == 1 && inv.equipped() == IronAxe);
    assert(chopDamage(DiamondSword) == 0 && chopDamage(Nozzle) == 0);
    assert(chopDamage(IronAxe) == 1);
    assert(reachable(0, 0, 150, 0));
    assert(!reachable(0, 0, -150, 0));
    assert(!reachable(0, 0, 221, 0));
    assert(!reachable(0, 161, 100, 0));
    assert(reachable(150, 0, 0, 1.5707963f));
    assert(swordReach(0,0,150,0,30,80));
    assert(swordReach(150,0,0,1.5707963f,30,80));
    assert(!swordReach(0,0,-10,0,30,80));
    assert(!swordReach(150,0,0,0,30,80));
    assert(!swordReach(0,0,211,0,30,80));
    assert(!swordReach(0,151,100,0,30,80));
    assert(!swordReach(0,-100,100,0,30,80));
    assert(swordReach(0,-30,100,0,30,80));
    assert(swordReach(90,0,180,0,30,80)); // Cylinder touches the detector's side.
    assert(!swordReach(91,0,180,0,30,80));
    assert(!swordReach(90,0,210,0,30,80)); // Beyond its rounded corner.
    assert(swordReach(-150,0,0,-1.5707963f,30,80));
    assert(swordReach(0,0,-150,3.14159265f,30,80));
    assert(!swordReach(0,0,150,3.14159265f,30,80));
    assert(swordReach(0,0,100,0,15,10)); // Small ground blobs overlap foot level.
    assert(!swordReach(0,-11,100,0,15,10));
    SwordSwing sword;
    sword.press();assert(sword.advance(1.f/60,false)); // Down+up between updates.
    assert(sword.canHit()&&fabsf(sword.seconds-.3f)<.00001f);
    assert(!sword.advance(.1f,false)&&sword.canHit()); // Released swing still hits.
    sword.hit=true;assert(!sword.canHit());
    sword.press();assert(!sword.advance(.1f,false)); // Buffer a click during cooldown.
    assert(sword.advance(.2f,false)&&sword.canHit());
    assert(!sword.advance(.31f,false)&&!sword.canHit());
    sword.press();sword.cancel();assert(!sword.advance(.5f,false)&&!sword.canHit());
    TreeBreak mining;
    for(int i=0;i<12;++i){assert(!mining.advance(2,true,1.f/30));assert(mining.stage()>=0&&mining.stage()<4);}
    assert(mining.seconds>.39f&&!mining.advance(2,false,1.f/30)&&mining.stage()==-1);
    assert(!mining.advance(2,true,.1f)&&mining.stage()==0); // A click starts from scratch.
    assert(!mining.advance(7,true,.01f)&&mining.target==7&&mining.seconds<.02f);
    assert(!mining.advance(-1,true,.1f)&&mining.stage()==-1); // Lost reach or wrong tool.
    for(int fps=30;fps<=60;fps+=30) {
        mining.cancel();int frames=0;
        while(!mining.advance(1,true,1.f/fps)){++frames;assert(frames<=fps*2);}
        assert(frames>=int(TreeBreak::duration()*fps)-1&&frames<=int(TreeBreak::duration()*fps)+1);
        assert(mining.stage()==9);
    }
    puts("PASS: continuous timed mining, released clicks reset, target/reach interruption, ten crack stages and equal 30/60 fps duration");
    for (int i = 0; i < (InventorySize-5)*StackSize; ++i) assert(inv.addLog());
    assert(inv.logs() == (InventorySize-5)*StackSize && !inv.addLog());
    assert(inv.slots[0].item == Nozzle && inv.slots[1].item == IronAxe && inv.slots[2].item == DiamondSword);
    assert(inv.slots[3].item==RocketNozzle&&inv.slots[3].count==1&&inv.slots[4].item==TurboNozzle&&inv.slots[4].count==1);
    unsigned char event[56] = {};
    int32_t word = 0x403; memcpy(event,&word,4); word = 1; memcpy(event+20,&word,4);
    Input input=decodeEvent(event); assert(input.action==Scroll && input.value==-1);
    memcpy(event+24,&word,4); input=decodeEvent(event);assert(input.action==Scroll&&input.value==1);
    memset(event,0,sizeof event);word=0x300;memcpy(event,&word,4);word=38;memcpy(event+16,&word,4);
    input=decodeEvent(event);assert(input.action==Select&&input.value==8);
    event[13]=1;assert(decodeEvent(event).action==Ignore);
    event[13]=0;word=10;memcpy(event+16,&word,4);assert(decodeEvent(event).action==Swing&&decodeEvent(event).value==1);
    word=0x301;memcpy(event,&word,4);assert(decodeEvent(event).value==0);
    word=0x300;memcpy(event,&word,4);word=8;memcpy(event+16,&word,4);assert(decodeEvent(event).action==Unhandled);
    event[13]=1;assert(decodeEvent(event).action==Unhandled);event[13]=0;
    word=0x301;memcpy(event,&word,4);assert(decodeEvent(event).action==Unhandled);
    word=0x300;memcpy(event,&word,4);word=43;memcpy(event+16,&word,4);assert(decodeEvent(event).action==Backpack);
    event[13]=1;assert(decodeEvent(event).action==Ignore);event[13]=0;
    word=0x301;memcpy(event,&word,4);assert(decodeEvent(event).action==Ignore);
    word=0x300;memcpy(event,&word,4);word=25;memcpy(event+16,&word,4);assert(decodeEvent(event).action==Unhandled);
    event[13]=1;assert(decodeEvent(event).action==Unhandled);event[13]=0;
    word=0x301;memcpy(event,&word,4);assert(decodeEvent(event).action==Unhandled);
    word=0x300;memcpy(event,&word,4);word=19;memcpy(event+16,&word,4);assert(decodeEvent(event).action==PlacementPreview);
    event[13]=1;assert(decodeEvent(event).action==Ignore);event[13]=0;
    word=0x301;memcpy(event,&word,4);assert(decodeEvent(event).action==Ignore);
    word=0x300;memcpy(event,&word,4);word=5;memcpy(event+16,&word,4);assert(decodeEvent(event).action==BuildMode);
    event[13]=1;assert(decodeEvent(event).action==Ignore);event[13]=0;
    word=0x301;memcpy(event,&word,4);assert(decodeEvent(event).action==Ignore);
    word=0x401;memcpy(event,&word,4);event[16]=1;assert(decodeEvent(event).action==Swing);
    word=0x651;memcpy(event,&word,4);event[12]=13;assert(decodeEvent(event).action==Scroll&&decodeEvent(event).value==-1);
    event[12]=14;assert(decodeEvent(event).value==1);
    event[12]=11;assert(decodeEvent(event).action==Swing);
    word=0x200;memcpy(event,&word,4);event[12]=13;assert(decodeEvent(event).action==FocusLost);
    puts("PASS: SDL2 wheel, flipped wheel, 1-9 keys, repeats, attack release, controller and focus loss");
    puts("PASS: hotbar wrap, tools, directional reach, log stacking and full inventory");
}
