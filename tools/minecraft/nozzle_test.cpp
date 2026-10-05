#include "../../src/minecraft/building.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include <fstream>
#include <iterator>
using namespace minecraft;
static Slot one(Item item){Slot s={item,1};return s;}
static uint32_t crc(const std::vector<unsigned char>& b){uint32_t c=0xffffffff;for(size_t i=20;i<b.size();++i){c^=b[i];for(int j=0;j<8;++j)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
static void word(std::vector<unsigned char>& b,int at,uint32_t v){for(int i=0;i<4;++i)b[at+i]=(v>>(i*8))&255;}
int main(int argc,char** argv) {
 assert(argc==2);setenv("SMS_MINECRAFT_SAVE",argv[1],1);
 std::unique_ptr<State> a(new State),b(new State);
 assert(nozzleDrop(0x20000026)==HoverNozzle&&nozzleDrop(0x20000022)==RocketNozzle&&nozzleDrop(0x2000002A)==TurboNozzle&&nozzleDrop(0x2000001f)==Empty);
 for(int i=0;i<ItemCount;++i)if(nozzleItem(Item(i))){assert(stackLimit(Item(i))==1);assert(!placeable(Item(i)));assert(a->place(Item(i),0,0,0,0)==-1);}
 assert(menuHit(InventoryMenu,175,134).group==5&&menuHit(TableMenu,175,134).group==0&&menuHit(ChestMenu,175,130).group==3);
 a->cursor=one(IronAxe);assert(!a->clickArmor());assert(a->cursor.item==IronAxe&&a->chestArmor.item==Empty);
 a->cursor=one(RocketNozzle);assert(a->clickArmor());assert(a->cursor.item==Empty&&a->chestArmor.item==RocketNozzle);
 a->cursor=one(TurboNozzle);assert(a->clickArmor(true));assert(a->cursor.item==RocketNozzle&&a->chestArmor.item==TurboNozzle);
 a->cursor=emptySlot();assert(a->clickArmor(true));assert(a->cursor.item==TurboNozzle&&a->chestArmor.item==Empty);assert(a->close());
 assert(a->equipFromInventory(0));assert(a->chestArmor.item==HoverNozzle&&a->inventory.slots[0].item==Empty);
 a->inventory.slots[0]=one(RocketNozzle);assert(a->equipFromInventory(0));assert(a->chestArmor.item==RocketNozzle&&a->inventory.slots[0].item==HoverNozzle);
 assert(!a->equipFromInventory(1));assert(a->chestArmor.item==RocketNozzle);
 for(int i=0;i<InventorySize;++i)a->inventory.slots[i]=one(IronAxe);
 assert(!a->unequipArmor());assert(a->chestArmor.item==RocketNozzle&&a->inventory.add(TurboNozzle,1)==1);
 a->inventory.slots[35]=emptySlot();assert(a->unequipArmor());assert(a->chestArmor.item==Empty&&a->inventory.slots[35].item==RocketNozzle);
 a->chestArmor=one(TurboNozzle);assert(sms_minecraft_save(a.get()));assert(sms_minecraft_load(b.get())==1);assert(b->chestArmor.item==TurboNozzle&&b->inventory.slots[35].item==RocketNozzle);
 b->chestArmor=one(WoodenLog);assert(!sms_minecraft_save(b.get()));
 // Recreate version-one layout by removing the new armor field and updating
 // its checksum: old saves retain their inventory and default to empty armor.
 std::ifstream input(argv[1],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());input.close();
 bytes.resize(bytes.size()-8); // V3 achievements and zero-drop trailer.
 const int armorOffset=20+5*4+(InventorySize+1+9)*8;bytes.erase(bytes.begin()+armorOffset,bytes.begin()+armorOffset+8);
 word(bytes,8,1);word(bytes,12,bytes.size()-20);word(bytes,16,crc(bytes));
 {std::ofstream out(argv[1],std::ios::binary|std::ios::trunc);out.write((const char*)&bytes[0],bytes.size());}
 assert(sms_minecraft_load(b.get())==1&&b->chestArmor.item==Empty&&b->inventory.slots[35].item==RocketNozzle);
 assert(sms_minecraft_save(b.get()));assert(sms_minecraft_load(a.get())==1&&a->chestArmor.item==Empty);
 puts("PASS nozzle drop mapping, stack/placement restrictions, armor click/shift swap, full inventory safety, armor persistence and version-one migration");
}
