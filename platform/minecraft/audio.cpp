// One-shot Minecraft samples share the game's 32 kHz output, device and mute.
#include "minecraft/sound.h"
#include "minecraft/audio_assets.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdint.h>

namespace {
const unsigned VoiceCount = 16;
struct Voice {
    const minecraft_audio_assets::Clip* clip;
    uint64_t phase;
    unsigned step, gain, serial;
};
Voice voices[VoiceCount];
std::mutex mutex;
unsigned serial, randomState = 0x6d696e65;
unsigned randomWord() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}
bool muted() {
    const char* value = std::getenv("SMS_AUDIO");
    return value && (!std::strcmp(value, "0") || !std::strcmp(value, "off"));
}
const float gains[minecraft::SoundCount] = {.30f, .72f, .62f, .65f, .55f, .65f, .65f, .25f, .70f};
const float pitches[minecraft::SoundCount] = {.65f, .80f, .90f, 1.f, 1.f, 1.f, 1.f, 2.f, 1.f};
}

extern "C" void sms_minecraft_sound(int event) {
    if(event < 0 || event >= minecraft::SoundCount || muted())return;
    std::lock_guard<std::mutex> lock(mutex);
    const minecraft_audio_assets::Event& sound = minecraft_audio_assets::Events[event];
    unsigned variant = randomWord() % sound.count;
    const minecraft_audio_assets::Variant& sample = sound.variants[variant];
    Voice* voice = &voices[0];
    for(Voice& candidate : voices) {
        if(!candidate.clip){voice = &candidate; break;}
        if(candidate.serial < voice->serial)voice = &candidate;
    }
    float pitch = pitches[event] * (.95f + (randomWord()%101)/1000.f);
    voice->clip = sample.clip; voice->phase = 0;
    voice->step = unsigned(pitch * 65536);
    voice->gain = unsigned(gains[event] * sample.gain * 32768);
    voice->serial = ++serial;
    if(std::getenv("SMS_MINECRAFT_AUDIO_TRACE"))
        std::fprintf(stderr,"[minecraft-audio] event=%d variant=%u\n",event,variant);
}

extern "C" unsigned sms_minecraft_mix_audio(short* stereo, unsigned frames, unsigned rate) {
    if(!stereo || !rate || muted())return 0;
    std::lock_guard<std::mutex> lock(mutex);
    bool any = false;
    for(const Voice& voice : voices)any = any || voice.clip;
    if(!any)return 0;
    unsigned mixed = 0;
    for(unsigned i=0; i<frames; ++i) {
        int sum = 0; bool active = false;
        for(Voice& voice : voices)if(voice.clip) {
            unsigned index = unsigned(voice.phase >> 16);
            if(index >= voice.clip->frames){voice.clip = 0; continue;}
            const short* pcm = voice.clip->samples;
            int a = pcm[index], b = pcm[std::min(index+1, voice.clip->frames-1)];
            int interpolated = a + int((int64_t(b-a) * (voice.phase & 65535)) / 65536);
            sum += int((int64_t(interpolated) * voice.gain) / 32768);
            voice.phase += uint64_t(voice.step) * minecraft_audio_assets::Rate / rate;
            active = true;
        }
        if(active) {
            ++mixed;
            for(unsigned channel=0; channel<2; ++channel)
                stereo[i*2+channel] = short(std::max(-32768, std::min(32767, int(stereo[i*2+channel])+sum)));
        }
    }
    return mixed;
}
