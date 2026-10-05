// A versioned, checksummed sidecar. No native structs/pointers are serialized.
#include "minecraft/building.h"
#include <vector>
#include <string>
#include <memory>
#include "port_host.h"
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cmath>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif
namespace {
using namespace minecraft;
std::string path() {
    if(const char* p=std::getenv("SMS_MINECRAFT_SAVE"))return p;
    std::string dir;
    if(const char* p=std::getenv("SMS_SAVE_DIR"))dir=p;
    else if(const char* p=std::getenv("XDG_DATA_HOME"))dir=std::string(p)+"/sms-port/card-a";
#ifdef _WIN32
    else if(const char* p=std::getenv("APPDATA"))dir=std::string(p)+"/sms-port/card-a";
#endif
    else dir=std::string(std::getenv("HOME")?std::getenv("HOME"):".")+"/.local/share/sms-port/card-a";
    return dir+"/minecraft-world.dat";
}
uint32_t crc(const std::vector<unsigned char>& data) {
    uint32_t c=0xffffffff;
    for(unsigned char b:data){c^=b;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&-(c&1));}
    return ~c;
}
void word(std::vector<unsigned char>& b,uint32_t v){for(int i=0;i<4;++i)b.push_back((v>>(8*i))&255);}
void slot(std::vector<unsigned char>& b,const Slot& s){word(b,s.item);word(b,s.count);}
void scalar(std::vector<unsigned char>& b,float value){uint32_t bits;memcpy(&bits,&value,4);word(b,bits);}
struct Reader {
    const std::vector<unsigned char>& b;size_t at=0;bool ok=true;
    explicit Reader(const std::vector<unsigned char>& bytes):b(bytes){}
    uint32_t word(){if(at+4>b.size()){ok=false;return 0;}uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(b[at++])<<(8*i);return v;}
    Slot slot(){Slot s={Item(word()),int(word())};if(!validSlot(s))ok=false;return s;}
    float scalar(){uint32_t bits=word();float f;memcpy(&f,&bits,4);return f;}
};
bool valid(const State& s) {
    if(s.inventory.selected<0||s.inventory.selected>=9||s.menu<NoMenu||s.menu>ChestMenu||!std::isfinite(s.originY)||fabs(s.originY)>100000)return false;
    if(!validSlot(s.chestArmor)||(s.chestArmor.item!=Empty&&!nozzleItem(s.chestArmor.item)))return false;
    if(!validSlot(s.cursor))return false;
    for(const Slot& p:s.inventory.slots)if(!validSlot(p))return false;
    for(const Slot& p:s.craft)if(!validSlot(p))return false;
    if(s.achievements&~((1u<<AchievementCount)-1))return false;
    for(const WorldDrop& d:s.drops)if(d.active) {
        Slot stack={d.item,d.count};if(d.item==Empty||!validSlot(stack))return false;
        const float values[]={d.x,d.y,d.z,d.ground,d.vy,d.age};
        for(float value:values)if(!std::isfinite(value)||fabs(value)>1000000)return false;
        if(d.age<0)return false;
    }
    if(s.menu==ChestMenu&&(s.activeChest<0||s.activeChest>=WorldCapacity||s.cells[s.activeChest].item!=Chest))return false;
    for(int i=0;i<WorldCapacity;++i) {
        const Cell& c=s.cells[i];if(c.item==Empty)continue;
        if(!placeable(c.item)||c.x < -10000||c.x > 10000||c.y < -1000||c.y > 1000||c.z < -10000||c.z > 10000||c.facing<0||c.facing>3||c.hits<0||c.hits>2)return false;
        for(const Slot& p:c.slots)if(!validSlot(p)||(c.item!=Chest&&p.item!=Empty))return false;
        for(int j=0;j<i;++j)if(s.cells[j].item!=Empty&&s.cells[j].x==c.x&&s.cells[j].z==c.z) {
            int top=c.y+(c.item==WoodenDoor?2:1),otherTop=s.cells[j].y+(s.cells[j].item==WoodenDoor?2:1);
            if(c.y<otherTop&&s.cells[j].y<top)return false;
        }
    }
    return true;
}
bool read(const std::string& name,std::vector<unsigned char>& bytes) {
    FILE* f=fopen(name.c_str(),"rb");if(!f)return false;
    if(fseek(f,0,SEEK_END)||ftell(f)<20||ftell(f)>500000){fclose(f);return false;}
    long size=ftell(f);rewind(f);bytes.resize(size);bool good=fread(bytes.data(),1,bytes.size(),f)==bytes.size();fclose(f);return good;
}
bool decode(const std::vector<unsigned char>& bytes,State* dst) {
    if(bytes.size()<20||memcmp(bytes.data(),"MCWORLD3",8))return false;
    Reader r{bytes};r.at=8;uint32_t version=r.word(),size=r.word(),sum=r.word();
    if((version<1||version>3)||size!=bytes.size()-20)return false;
    std::vector<unsigned char> payload(bytes.begin()+20,bytes.end());if(crc(payload)!=sum)return false;
    std::unique_ptr<State> s(new State);
    s->inventory.selected=r.word();s->menu=Menu(r.word());s->activeChest=int32_t(r.word());
    uint32_t bits=r.word();memcpy(&s->originY,&bits,4);s->originSet=r.word()!=0;
    for(Slot& p:s->inventory.slots)p=r.slot();
    s->cursor=r.slot();
    for(Slot& p:s->craft)p=r.slot();
    if(version>=2)s->chestArmor=r.slot();
    unsigned count=r.word();if(count>WorldCapacity)return false;
    for(unsigned n=0;n<count&&r.ok;++n) {
        unsigned index=r.word();if(index>=WorldCapacity||s->cells[index].item!=Empty)return false;
        Cell& c=s->cells[index];c.item=Item(r.word());c.x=int32_t(r.word());c.y=int32_t(r.word());c.z=int32_t(r.word());
        c.facing=r.word();c.open=r.word()!=0;c.hits=r.word();for(Slot& p:c.slots)p=r.slot();
        if(c.item==Empty)return false;
    }
    if(version>=3) {
        s->achievements=r.word();count=r.word();if(count>DropCapacity)return false;
        for(unsigned n=0;n<count&&r.ok;++n) {
            unsigned index=r.word();if(index>=DropCapacity||s->drops[index].active)return false;
            WorldDrop& d=s->drops[index];Slot stack=r.slot();d.item=stack.item;d.count=stack.count;
            d.x=r.scalar();d.y=r.scalar();d.z=r.scalar();d.ground=r.scalar();d.vy=r.scalar();d.age=r.scalar();d.active=true;
        }
    }
    if(!r.ok||r.at!=bytes.size()||!valid(*s))return false;
    *dst=*s;return true;
}
}
extern "C" int sms_minecraft_load(minecraft::State* state) {
    std::vector<unsigned char> bytes;std::string p=path();
    if(read(p,bytes)&&decode(bytes,state)){fprintf(stderr,"[minecraft] loaded world and inventory: %s\n",p.c_str());return 1;}
    if(read(p+".bak",bytes)&&decode(bytes,state)){fprintf(stderr,"[minecraft] recovered previous world save\n");return 1;}
    struct stat st;
    if(stat(p.c_str(),&st)==0){fprintf(stderr,"[minecraft] invalid world save; preserving it\n");return -1;}return 0;
}
extern "C" int sms_minecraft_save(const minecraft::State* s) {
    if(!valid(*s))return 0;
    std::vector<unsigned char> b;word(b,s->inventory.selected);word(b,s->menu);word(b,s->activeChest);
    uint32_t bits;memcpy(&bits,&s->originY,4);word(b,bits);word(b,s->originSet);
    for(const Slot& p:s->inventory.slots)slot(b,p);
    slot(b,s->cursor);
    for(const Slot& p:s->craft)slot(b,p);
    slot(b,s->chestArmor);
    int count=0;for(const Cell& c:s->cells)count+=c.item!=Empty;word(b,count);
    for(int i=0;i<WorldCapacity;++i) {
        const Cell& c=s->cells[i];if(c.item==Empty)continue;
        word(b,i);word(b,c.item);word(b,c.x);word(b,c.y);word(b,c.z);word(b,c.facing);word(b,c.open);word(b,c.hits);
        for(const Slot& p:c.slots)slot(b,p);
    }
    word(b,s->achievements);count=0;for(const WorldDrop& d:s->drops)count+=d.active;word(b,count);
    for(int i=0;i<DropCapacity;++i)if(s->drops[i].active) {
        const WorldDrop& d=s->drops[i];word(b,i);Slot stack={d.item,d.count};slot(b,stack);
        scalar(b,d.x);scalar(b,d.y);scalar(b,d.z);scalar(b,d.ground);scalar(b,d.vy);scalar(b,d.age);
    }
    std::vector<unsigned char> header{'M','C','W','O','R','L','D','3'};word(header,3);word(header,b.size());word(header,crc(b));header.insert(header.end(),b.begin(),b.end());
    std::string p=path();size_t slash=p.find_last_of("/\\");std::string parent=slash==std::string::npos?"":p.substr(0,slash);
    for(size_t i=1;i<=parent.size();++i)if(i==parent.size()||parent[i]=='/'||parent[i]=='\\') {
        if(port_mkdir(parent.substr(0,i).c_str(),0755)&&errno!=EEXIST)return 0;
    }
    FILE* f=fopen((p+".tmp").c_str(),"wb");if(!f)return 0;
    bool good=fwrite(header.data(),1,header.size(),f)==header.size()&&fflush(f)==0;
#ifdef _WIN32
    if(good)good=_commit(_fileno(f))==0;
#else
    if(good)good=fsync(fileno(f))==0;
#endif
    if(fclose(f)!=0)good=false;
    if(!good){remove((p+".tmp").c_str());return 0;}
    // Copy only a validated prior snapshot into the recovery file.
    std::vector<unsigned char> old;std::unique_ptr<State> previous(new State);
    if(read(p,old)&&decode(old,previous.get())) {
        FILE* backup=fopen((p+".bak.tmp").c_str(),"wb");
        if(backup){bool done=fwrite(old.data(),1,old.size(),backup)==old.size();if(fclose(backup))done=false;
#ifdef _WIN32
            if(done)remove((p+".bak").c_str());
#endif
            if(done)rename((p+".bak.tmp").c_str(),(p+".bak").c_str());}
    }
#ifdef _WIN32
    remove(p.c_str());
#endif
    if(rename((p+".tmp").c_str(),p.c_str()))return 0;
#ifndef _WIN32
    int fd=open((parent.empty()?std::string("."):parent).c_str(),O_RDONLY|O_DIRECTORY);if(fd>=0){fsync(fd);close(fd);}
#endif
    return 1;
}
