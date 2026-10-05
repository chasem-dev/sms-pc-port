#ifndef SMS_MINECRAFT_SOUND_H
#define SMS_MINECRAFT_SOUND_H
#include <stdint.h>
namespace minecraft {
enum SoundEvent {
    WoodHit, WoodBreak, WoodPlace, ChestOpen, ChestClose, DoorOpen, DoorClose, ItemPickup, PlayerHurt, SoundCount
};
inline bool replacedDamageVoice(uint32_t id) {
    return (id>=0x7818&&id<=0x781A)||(id>=0x781C&&id<=0x781E)||
           (id>=0x7830&&id<=0x7836)||(id>=0x783B&&id<=0x783E)||
           (id>=0x7844&&id<=0x7845)||(id>=0x7849&&id<=0x784A)||
           (id>=0x784F&&id<=0x7855);
}
inline bool mutedNativeEffect(uint32_t id) {
    return id==0x1001||id==0x1141||id==0x4823||id==0x483B||id==0x483C||id==0x4853;
}
}
extern "C" {
void sms_minecraft_sound(int event);
// Host-order, interleaved L/R PCM; mix before the existing WAV/SDL output.
unsigned sms_minecraft_mix_audio(short* stereo, unsigned frames, unsigned rate);
}
#endif
