#ifndef SMS_MINECRAFT_BUILDING_H
#define SMS_MINECRAFT_BUILDING_H
#include "core.h"
namespace minecraft {
const float BlockSize=80;
const int WorldCapacity=512,ChestSlots=27;
const int DropCapacity=512;
enum Achievement { TakingInventory,GettingWood,Benchmaking,AchievementCount };
struct WorldDrop {
    Item item;int count;float x,y,z,ground,vy,age;bool active;
    WorldDrop():item(Empty),count(0),x(0),y(0),z(0),ground(0),vy(0),age(0),active(false){}
};
enum Menu { NoMenu,InventoryMenu,TableMenu,ChestMenu };
inline Slot emptySlot(){Slot s={Empty,0};return s;}
inline bool validSlot(const Slot& s){return s.item>=Empty&&s.item<ItemCount&&s.count>=0&&s.count<=stackLimit(s.item)&&((s.item==Empty)==(s.count==0));}
inline bool placeable(Item item){return item>=WoodenLog&&item<=Chest;}
inline void clickSlot(Slot& slot,Slot& cursor,bool right) {
    if(cursor.item==Empty) {
        if(slot.item==Empty)return;
        int n=right?(slot.count+1)/2:slot.count;
        cursor.item=slot.item;cursor.count=n;slot.count-=n;
        if(!slot.count)slot=emptySlot();
    }else if(slot.item==Empty||slot.item==cursor.item) {
        int n=right?1:cursor.count;
        if(n>stackLimit(cursor.item)-slot.count)n=stackLimit(cursor.item)-slot.count;
        if(n>0){slot.item=cursor.item;slot.count+=n;cursor.count-=n;if(!cursor.count)cursor=emptySlot();}
    }else {Slot old=slot;slot=cursor;cursor=old;}
}
inline int addSlots(Slot* slots,int size,Item item,int count) {
    for(int pass=0;pass<2;++pass)for(int i=0;i<size&&count;++i) {
        Slot& s=slots[i];if((pass==0&&s.item==item)||(pass==1&&s.item==Empty)) {
            int n=stackLimit(item)-s.count;if(n>count)n=count;
            if(n){s.item=item;s.count+=n;count-=n;}
        }
    }
    return count;
}
struct Recipe { Item item; int count; Recipe(Item i=Empty,int n=0):item(i),count(n){} };
inline Recipe recipe(const Slot* grid,int size) {
    int cells=size*size,used=0,logs=0,minX=size,minY=size,maxX=-1,maxY=-1;
    for(int i=0;i<cells;++i)if(grid[i].item!=Empty) {
        ++used;logs+=grid[i].item==WoodenLog;
        int x=i%size,y=i/size;if(x<minX)minX=x;if(x>maxX)maxX=x;if(y<minY)minY=y;if(y>maxY)maxY=y;
    }
    if(used==1&&logs==1)return Recipe(WoodenPlanks,4);
    if(!used||logs)return Recipe();
    for(int i=0;i<cells;++i)if(grid[i].item!=Empty&&grid[i].item!=WoodenPlanks)return Recipe();
    if(used==2&&maxX==minX&&maxY-minY==1)return Recipe(Stick,4);
    if(used==4&&maxX-minX==1&&maxY-minY==1)return Recipe(CraftingTable,1);
    if(size==3&&used==6&&maxX-minX==1&&maxY-minY==2)return Recipe(WoodenDoor,3);
    if(size==3&&used==8&&grid[4].item==Empty)return Recipe(Chest,1);
    return Recipe();
}
struct Cell {
    Item item;int x,y,z,facing,hits;bool open;
    Slot slots[ChestSlots];
    Cell():item(Empty),x(0),y(0),z(0),facing(0),hits(0),open(false){for(int i=0;i<ChestSlots;++i)slots[i]=emptySlot();}
};
struct State {
    Inventory inventory;Slot cursor,chestArmor,craft[9];Cell cells[WorldCapacity];
    WorldDrop drops[DropCapacity];uint32_t achievements;
    Menu menu;int activeChest;float originY;bool originSet;
    State():cursor(emptySlot()),chestArmor(emptySlot()),achievements(0),menu(NoMenu),activeChest(-1),originY(0),originSet(false){for(int i=0;i<9;++i)craft[i]=emptySlot();}
    bool unlock(Achievement achievement){uint32_t bit=1u<<achievement;if(achievements&bit)return false;achievements|=bit;return true;}
    int freeDrops()const{int free=0;for(int i=0;i<DropCapacity;++i)free+=!drops[i].active;return free;}
    int spawnDrop(Item item,int count,float x,float y,float z,float ground) {
        Slot stack={item,count};if(item==Empty||!validSlot(stack))return -1;
        for(int i=0;i<DropCapacity;++i)if(!drops[i].active){WorldDrop& d=drops[i];d=WorldDrop();d.item=item;d.count=count;d.x=x;d.y=y;d.z=z;d.ground=ground;d.vy=100;d.active=true;return i;}
        return -1;
    }
    // Reserve every required entity before removing a block or full chest.
    // Inventory space has no bearing on whether a block can be broken.
    bool breakBlock(int index) {
        if(index<0||index>=WorldCapacity||cells[index].item==Empty)return false;
        const Cell& c=cells[index];int needed=1;
        if(c.item==Chest)for(int i=0;i<ChestSlots;++i)needed+=c.slots[i].item!=Empty;
        if(freeDrops()<needed)return false;
        float x=c.x*BlockSize+40,y=originY+c.y*BlockSize,z=c.z*BlockSize+40;
        spawnDrop(c.item,1,x,y+40,z,y);
        if(c.item==Chest)for(int i=0;i<ChestSlots;++i)if(c.slots[i].item!=Empty)
            spawnDrop(c.slots[i].item,c.slots[i].count,x+(i%3-1)*12,y+45,z+(i/3%3-1)*12,y);
        cells[index]=Cell();return true;
    }
    bool clickArmor(bool right=false) {
        if(cursor.item!=Empty&&!nozzleItem(cursor.item))return false;
        clickSlot(chestArmor,cursor,right);return true;
    }
    bool equipFromInventory(int index) {
        if(index<0||index>=InventorySize||!nozzleItem(inventory.slots[index].item))return false;
        Slot old=chestArmor;chestArmor=inventory.slots[index];inventory.slots[index]=old;return true;
    }
    bool unequipArmor() {
        if(chestArmor.item==Empty)return true;
        if(inventory.add(chestArmor.item,1))return false;
        chestArmor=emptySlot();return true;
    }
    int gridSize()const{return menu==TableMenu?3:2;}
    Recipe result()const{return recipe(craft,gridSize());}
    bool takeResult(bool bulk=false) {
        Recipe r=result();if(r.item==Empty)return false;
        if(bulk) {
            Inventory copy=inventory;if(copy.add(r.item,r.count))return false;inventory=copy;
        }else {
            if(cursor.item!=Empty&&cursor.item!=r.item)return false;
            if(cursor.count+r.count>stackLimit(r.item))return false;
            cursor.item=r.item;cursor.count+=r.count;
        }
        for(int i=0;i<gridSize()*gridSize();++i)if(craft[i].item!=Empty) {
            if(--craft[i].count==0)craft[i]=emptySlot();
        }
        return true;
    }
    bool close() {
        Inventory copy=inventory;
        if(cursor.item!=Empty&&copy.add(cursor.item,cursor.count))return false;
        for(int i=0;i<9;++i)if(craft[i].item!=Empty&&copy.add(craft[i].item,craft[i].count))return false;
        inventory=copy;cursor=emptySlot();for(int i=0;i<9;++i)craft[i]=emptySlot();
        menu=NoMenu;activeChest=-1;return true;
    }
    int find(int x,int y,int z)const {
        for(int i=0;i<WorldCapacity;++i) {
            const Cell& c=cells[i];
            if(c.item!=Empty&&c.x==x&&c.z==z&&(c.y==y||(c.item==WoodenDoor&&c.y+1==y)))return i;
        }
        return -1;
    }
    int place(Item item,int x,int y,int z,int facing) {
        if(!placeable(item)||find(x,y,z)>=0||(item==WoodenDoor&&find(x,y+1,z)>=0))return -1;
        for(int i=0;i<WorldCapacity;++i)if(cells[i].item==Empty) {
            cells[i]=Cell();Cell& c=cells[i];c.item=item;c.x=x;c.y=y;c.z=z;c.facing=facing&3;return i;
        }
        return -1;
    }
    bool remove(int index) {
        if(index<0||index>=WorldCapacity||cells[index].item==Empty)return false;
        Cell& c=cells[index];if(c.item==Chest)for(int i=0;i<ChestSlots;++i)if(c.slots[i].item!=Empty)return false;
        if(inventory.add(c.item,1))return false;
        c=Cell();return true;
    }
};
// Shared UI geometry, used by renderer, live input and automated checks.
struct Hit {int group,index;Hit(int g=0,int i=-1):group(g),index(i){} };
inline Hit gridHit(float x,float y,float left,float top,int cols,int rows,int start,int group) {
    if(x<left||y<top||x>=left+cols*36||y>=top+rows*36)return Hit();
    int col=int((x-left)/36),row=int((y-top)/36);
    if(int(x-left)%36>=32||int(y-top)%36>=32)return Hit();
    return Hit(group,start+row*cols+col);
}
inline Hit menuHit(Menu menu,float x,float y) {
    const float left=144,top=66;Hit h;
    h=gridHit(x,y,left+16,top+284+(menu==ChestMenu?2:0),9,1,0,1);if(h.group)return h;
    h=gridHit(x,y,left+16,top+168+(menu==ChestMenu?2:0),9,3,9,1);if(h.group)return h;
    if(menu==ChestMenu)return gridHit(x,y,left+16,top+36,9,3,0,3);
    if(menu==InventoryMenu) {h=gridHit(x,y,left+16,top+52,1,1,0,5);if(h.group)return h;}
    int size=menu==TableMenu?3:2;
    h=gridHit(x,y,left+(size==3?60:196),top+(size==3?34:36),size,size,0,2);if(h.group)return h;
    float ox=left+(size==3?248:308),oy=top+(size==3?70:56);
    if(x>=ox&&x<ox+32&&y>=oy&&y<oy+32)return Hit(4,0);
    return Hit();
}
struct Box {float x,y,z,X,Y,Z;};
inline Box cellBox(const Cell& c,float originY) {
    Box b={c.x*BlockSize,originY+c.y*BlockSize,c.z*BlockSize,(c.x+1)*BlockSize,originY+(c.y+1)*BlockSize,(c.z+1)*BlockSize};
    if(c.item==Chest) {b.x+=5;b.X-=5;b.z+=5;b.Z-=5;b.Y-=10;}
    if(c.item==WoodenDoor) {
        b.Y+=BlockSize;
        int facing=(c.facing+(c.open?1:0))&3;
        if(facing==0)b.Z=b.z+10;else if(facing==1)b.x=b.X-10;else if(facing==2)b.z=b.Z-10;else b.X=b.x+10;
    }
    return b;
}
inline bool overlaps(const Box& a,const Box& b){return a.x<b.X&&a.X>b.x&&a.y<b.Y&&a.Y>b.y&&a.z<b.Z&&a.Z>b.z;}
inline bool rayBox(const Box& b,float x,float y,float z,float dx,float dy,float dz,float& distance,int& face) {
    float lo=0,hi=distance;int hitFace=-1;
    const float mins[3]={b.x,b.y,b.z},maxs[3]={b.X,b.Y,b.Z},p[3]={x,y,z},d[3]={dx,dy,dz};
    for(int a=0;a<3;++a) {
        if(fabsf(d[a])<.00001f){if(p[a]<mins[a]||p[a]>maxs[a])return false;continue;}
        float near=(mins[a]-p[a])/d[a],far=(maxs[a]-p[a])/d[a];int f=a*2;
        if(near>far){float t=near;near=far;far=t;++f;}
        if(near>lo){lo=near;hitFace=f;}if(far<hi)hi=far;if(lo>hi)return false;
    }
    if(lo>=distance||hi<0)return false;
    distance=lo;face=hitFace;return true;
}
}
extern "C" int sms_minecraft_load(minecraft::State* state);
extern "C" int sms_minecraft_save(const minecraft::State* state);
#endif
