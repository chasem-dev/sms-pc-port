#include "../../src/minecraft/sound.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static const unsigned Rate=32000;
static std::vector<short> render(int event) {
    std::vector<short> result,block(560*2);
    sms_minecraft_sound(event);
    bool heard=false,ended=false;
    for(unsigned i=0;i<180;++i) {
        std::fill(block.begin(),block.end(),0);
        unsigned active=sms_minecraft_mix_audio(block.data(),560,Rate);
        if(!active){ended=true;break;}
        for(short sample:block)heard=heard||sample!=0;
        result.insert(result.end(),block.begin(),block.end());
    }
    assert(heard&&ended);
    return result;
}
static void le(FILE* f,unsigned value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i){unsigned char b=(value>>(i*8))&255;assert(fwrite(&b,1,1,f)==1);}
}
int main(int argc,char** argv) {
    assert(argc==2);unsetenv("SMS_AUDIO");
    std::vector<short> demo;
    const char* names[]={"wood hit","wood break","block place","chest open","chest close","door open","door close","item pickup","player hurt"};
    assert(minecraft::replacedDamageVoice(0x7830)&&minecraft::replacedDamageVoice(0x7852));
    assert(!minecraft::replacedDamageVoice(0x7856)&&!minecraft::replacedDamageVoice(0x7820));
    assert(minecraft::mutedNativeEffect(0x1001)&&minecraft::mutedNativeEffect(0x1141));
    assert(!minecraft::mutedNativeEffect(0x113F)&&!minecraft::mutedNativeEffect(0x1000));
    for(int event=0;event<minecraft::SoundCount;++event) {
        std::vector<short> clip=render(event);
        demo.insert(demo.end(),Rate/2,0);
        demo.insert(demo.end(),clip.begin(),clip.end());
        printf("PASS audible %s, bounded duration, stereo mix\n",names[event]);
    }
    // A quiet crossover must preserve existing channel values exactly.
    short native[]={1234,-4321,32767,-32768};
    sms_minecraft_sound(-1);sms_minecraft_sound(minecraft::SoundCount);
    assert(sms_minecraft_mix_audio(native,2,Rate)==0);
    assert(native[0]==1234&&native[1]==-4321&&native[2]==32767&&native[3]==-32768);
    setenv("SMS_AUDIO","0",1);sms_minecraft_sound(minecraft::DoorOpen);
    assert(sms_minecraft_mix_audio(native,2,Rate)==0);unsetenv("SMS_AUDIO");
    assert(sms_minecraft_mix_audio(native,2,Rate)==0);
    // Several rapid actions share the output without wrapping at saturation.
    for(int i=0;i<40;++i)sms_minecraft_sound(minecraft::WoodBreak);
    std::vector<short> zeros(32000*2),loud(32000*2);
    for(unsigned i=0;i<32000;++i){loud[i*2]=32760;loud[i*2+1]=-32760;}
    assert(sms_minecraft_mix_audio(loud.data(),32000,Rate)>0);
    bool clipped=false;
    for(unsigned i=0;i<32000;++i) {
        assert(loud[i*2]>=loud[i*2+1]); // wrapping would reverse the channels
        clipped=clipped||loud[i*2]==32767||loud[i*2+1]==-32768;
    }
    assert(clipped);
    for(int i=0;i<6;++i)sms_minecraft_mix_audio(zeros.data(),32000,Rate);
    assert(sms_minecraft_mix_audio(native,2,Rate)==0);
    // The mixer also advances correctly for a different host sample rate.
    sms_minecraft_sound(minecraft::DoorClose);
    std::vector<short> highRate(48000*2);
    assert(sms_minecraft_mix_audio(highRate.data(),48000,48000)>0);
    for(int i=0;i<6;++i)sms_minecraft_mix_audio(highRate.data(),48000,48000);
    assert(sms_minecraft_mix_audio(native,2,Rate)==0);
    FILE* f=fopen(argv[1],"wb");assert(f);
    assert(fwrite("RIFF",1,4,f)==4);le(f,36+demo.size()*2,4);
    assert(fwrite("WAVEfmt ",1,8,f)==8);le(f,16,4);le(f,1,2);le(f,2,2);
    le(f,Rate,4);le(f,Rate*4,4);le(f,4,2);le(f,16,2);
    assert(fwrite("data",1,4,f)==4);le(f,demo.size()*2,4);
    for(short sample:demo)le(f,(unsigned short)sample,2);
    assert(fclose(f)==0);
    puts("PASS mute, inactive PCM/channel preservation, bounded overlapping voices and alternate rate");
}
