#ifndef SMS_MINECRAFT_CORE_H
#define SMS_MINECRAFT_CORE_H
#include <math.h>
#include <stdint.h>
#include <string.h>
namespace minecraft {
enum Item { Empty, Nozzle, IronAxe, DiamondSword, WoodenLog, WoodenPlanks, CraftingTable, WoodenDoor, Chest, RocketNozzle, TurboNozzle, Stick, ItemCount, HoverNozzle=Nozzle };
const int SlotCount = 9;
const int StackSize = 64;
const int InventorySize = 36;
inline bool nozzleItem(Item item) { return item==HoverNozzle||item==RocketNozzle||item==TurboNozzle; }
inline int nozzleType(Item item) { return item==HoverNozzle?4:item==RocketNozzle?1:item==TurboNozzle?5:-1; }
inline Item nozzleDrop(uint32_t actor) { return actor==0x20000026?HoverNozzle:actor==0x20000022?RocketNozzle:actor==0x2000002A?TurboNozzle:Empty; }
inline int stackLimit(Item item) { return nozzleItem(item)||item==IronAxe||item==DiamondSword ? 1 : 64; }
inline int wrapSlot(int index) { return (index % SlotCount + SlotCount) % SlotCount; }
struct Slot {
    Item item;
    int count;
};
struct Inventory {
    Slot slots[InventorySize];
    int selected;
    Inventory() : selected(0) {
        for (int i = 0; i < InventorySize; ++i) { slots[i].item = Empty; slots[i].count = 0; }
        slots[0].item = Nozzle; slots[1].item = IronAxe; slots[2].item = DiamondSword;
        slots[3].item = RocketNozzle; slots[4].item = TurboNozzle;
        for (int i = 0; i < 5; ++i) slots[i].count = 1;
    }
    Item equipped() const { return slots[selected].item; }
    void scroll(int delta) { selected = wrapSlot(selected + delta % SlotCount); }
    int add(Item item,int count) {
        for(int pass=0;pass<2;++pass)for(int i=0;i<InventorySize && count>0;++i) {
            Slot& s=slots[i];
            if((pass==0 && s.item==item)||(pass==1 && s.item==Empty)) {
                int space=stackLimit(item)-s.count, n=count<space?count:space;
                if(n>0){s.item=item;s.count+=n;count-=n;}
            }
        }
        return count;
    }
    bool addLog() { return add(WoodenLog,1)==0; }
    int logs() const {
        int n = 0;
        for (int i = 0; i < InventorySize; ++i) if (slots[i].item == WoodenLog) n += slots[i].count;
        return n;
    }
};
// Coordinates are Sunshine world units. A narrow forward cone prevents
// chopping a tree behind Steve or through a distant plaza landmark.
inline bool reachable(float dx, float dy, float dz, float facing) {
    float distance2 = dx * dx + dz * dz;
    if (distance2 > 220.0f * 220.0f || fabsf(dy) > 160.0f) return false;
    if (distance2 < 40.0f * 40.0f) return true;
    return (dx * sinf(facing) + dz * cosf(facing)) > sqrtf(distance2) * 0.35f;
}
// One continuous hold breaks a palm. A new target or a released input
// starts over, so repeated clicks cannot accumulate hidden damage.
struct TreeBreak {
    int target;
    float seconds,limit;
    Item tool;
    TreeBreak():target(-1),seconds(0),limit(duration()),tool(Empty){}
    void cancel(){target=-1;seconds=0;}
    static float duration(){return 1.4f;}
    bool advance(int candidate,bool held,float dt,float time=duration(),Item selected=IronAxe) {
        if(!held||candidate<0){cancel();return false;}
        if(target!=candidate||tool!=selected||limit!=time){target=candidate;seconds=0;}
        tool=selected;limit=time;
        if(dt>0)seconds+=dt;
        if(seconds>limit)seconds=limit;
        return seconds>=limit;
    }
    int stage()const {
        if(target<0||seconds<=0)return -1;
        int stage=int(seconds*10/limit);return stage>9?9:stage;
    }
};
inline int chopDamage(Item tool) { return tool == IronAxe ? 1 : 0; }
inline bool canChop(Item tool){return tool==Empty||tool==IronAxe;}
inline float woodMiningTime(Item block,Item tool){float hardness=block==WoodenDoor?3.f:(block==Chest||block==CraftingTable)?2.5f:2.f;return hardness*1.5f/(tool==IronAxe?6.f:1.f);}
struct SwordSwing {
    float seconds,cooldown;
    bool pressed,hit;
    SwordSwing():seconds(0),cooldown(0),pressed(false),hit(false){}
    void press(){pressed=true;}
    void cancel(){seconds=0;cooldown=0;pressed=false;hit=false;}
    bool advance(float dt,bool held) {
        seconds-=dt;cooldown-=dt;
        if(seconds<0)seconds=0;
        if(cooldown<0)cooldown=0;
        if(cooldown>0||(!pressed&&!held))return false;
        pressed=false;hit=false;seconds=.3f;cooldown=.38f;return true;
    }
    bool canHit()const{return seconds>0&&!hit;}
};
inline bool swordReach(float dx,float dy,float dz,float facing,float radius,float height) {
    if(radius<0)radius=0;
    if(radius>80)radius=80;
    if(height<0)height=0;
    if(height>160)height=160;
    if(dy>150 || dy+height<0)return false;
    // Project onto the model's forward/right vectors. The swing occupies a
    // 180-long, 120-wide area in front; test the enemy's collision cylinder
    // against that area instead of requiring an exact ray or centre hit.
    float forward=dx*sinf(facing)+dz*cosf(facing);
    if(forward<0)return false;
    float side=fabsf(dx*cosf(facing)-dz*sinf(facing));
    float pastEnd=forward>180?forward-180:0;
    float pastSide=side>60?side-60:0;
    return pastEnd*pastEnd+pastSide*pastSide<=radius*radius;
}
enum Action { Unhandled, Ignore, Select, Scroll, Swing, Backpack, FocusLost, Pointer, Use, Interact, CloseMenu, Shift, Help, PlacementPreview, BuildMode };
struct Input { Action action; int value,x,y,button; Input(Action a=Unhandled,int v=0,int X=-1,int Y=-1,int B=0):action(a),value(v),x(X),y(Y),button(B){} };
inline int32_t eventWord(const void* event, int offset) {
    int32_t word; memcpy(&word, (const unsigned char*)event + offset, sizeof word); return word;
}
// SDL2 event ABI shared with platform/pad. Kept independent of the game
// so actual mouse, keyboard and controller event layouts can be checked.
inline Input decodeEvent(const void* event) {
    const unsigned char* bytes=(const unsigned char*)event;
    int type=eventWord(event,0);
    if(type==0x403) {
        int delta=eventWord(event,20) % SlotCount;
        return Input(Scroll,eventWord(event,24)==1 ? delta : -delta);
    }
    if(type==0x400)return Input(Pointer,0,eventWord(event,20),eventWord(event,24));
    if(type==0x401||type==0x402) {
        int button=bytes[16];
        if(button==1||button==3)return Input(button==1?Swing:Use,type==0x401,eventWord(event,20),eventWord(event,24),button);
    }
    if(type==0x300||type==0x301) {
        int key=eventWord(event,16);bool down=type==0x300;
        if(key>=30&&key<=38) return Input(down&&!bytes[13]?Select:Ignore,key-30);
        if(key==47||key==48) return Input(down&&!bytes[13]?Scroll:Ignore,key==47?-1:1);
        if(key==10) return Input(Swing,down);
        if(key==41)return Input(down&&!bytes[13]?CloseMenu:Ignore,down);
        if(key==225||key==229)return Input(Shift,down);
        if(key==11)return Input(down&&!bytes[13]?Help:Ignore);
        if(key==19)return Input(down&&!bytes[13]?PlacementPreview:Ignore);
        if(key==5)return Input(down&&!bytes[13]?BuildMode:Ignore);
        if(key==43) return Input(down&&!bytes[13]?Backpack:Ignore);
    }
    if(type==0x651||type==0x652) {
        int button=bytes[12];bool down=type==0x651;
        if(button==13||button==14) return Input(down?Scroll:Ignore,button==13?-1:1);
        if(button==11) return Input(Swing,down);
    }
    if(type==0x200&&bytes[12]==13) return Input(FocusLost);
    return Input();
}
}
#endif
