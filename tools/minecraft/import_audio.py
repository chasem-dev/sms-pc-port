#!/usr/bin/env python3
"""Verify original Ogg assets and embed 32 kHz PCM; no runtime decoder needed."""
from pathlib import Path
import ctypes as c
import ctypes.util
import hashlib
import json
import math
import wave

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'assets/minecraft/audio'
manifest = json.loads((SRC / 'sources.json').read_text())
for record in [manifest['event_definitions']] + manifest['assets']:
    data = (SRC / record['path']).read_bytes()
    assert hashlib.sha256(data).hexdigest() == record['sha256'], record['path']
    if 'sha1' in record:
        assert hashlib.sha1(data).hexdigest() == record['sha1'], record['path']

class Info(c.Structure):
    _fields_ = [('frames', c.c_int64), ('rate', c.c_int), ('channels', c.c_int),
                ('format', c.c_int), ('sections', c.c_int), ('seekable', c.c_int)]

class Conversion(c.Structure):
    _fields_ = [('input', c.POINTER(c.c_float)), ('output', c.POINTER(c.c_float)),
                ('input_frames', c.c_long), ('output_frames', c.c_long),
                ('input_used', c.c_long), ('output_generated', c.c_long),
                ('end', c.c_int), ('ratio', c.c_double)]

sndfile = c.CDLL(ctypes.util.find_library('sndfile') or 'libsndfile.so.1')
resampler = c.CDLL(ctypes.util.find_library('samplerate') or 'libsamplerate.so.0')
sndfile.sf_open.argtypes = [c.c_char_p, c.c_int, c.POINTER(Info)]
sndfile.sf_open.restype = c.c_void_p
sndfile.sf_readf_float.argtypes = [c.c_void_p, c.POINTER(c.c_float), c.c_int64]
sndfile.sf_readf_float.restype = c.c_int64
sndfile.sf_close.argtypes = [c.c_void_p]
resampler.src_simple.argtypes = [c.POINTER(Conversion), c.c_int, c.c_int]
resampler.src_simple.restype = c.c_int

rate = 32000
lines = ['// Generated from verified Minecraft Ogg assets by import_audio.py.',
         '#ifndef SMS_MINECRAFT_AUDIO_ASSETS_H', '#define SMS_MINECRAFT_AUDIO_ASSETS_H',
         'namespace minecraft_audio_assets {',
         'static const unsigned Rate = 32000;',
         'struct Clip { const short* samples; unsigned frames; };',
         'struct Variant { const Clip* clip; float gain; };',
         'struct Event { const Variant* variants; unsigned count; };']
clips = {}
out = ROOT / 'build/minecraft/audio/clips'
out.mkdir(parents=True, exist_ok=True)
for index, record in enumerate(manifest['assets']):
    info = Info()
    handle = sndfile.sf_open(str(SRC / record['path']).encode(), 0x10, c.byref(info))
    assert handle and info.frames > 0 and 0 < info.channels <= 2, record['path']
    pcm = (c.c_float * (info.frames * info.channels))()
    assert sndfile.sf_readf_float(handle, pcm, info.frames) == info.frames
    sndfile.sf_close(handle)
    mono = (c.c_float * info.frames)(*[sum(pcm[i*info.channels+j] for j in range(info.channels))/info.channels for i in range(info.frames)])
    capacity = math.ceil(info.frames * rate / info.rate) + 512
    converted = (c.c_float * capacity)()
    conversion = Conversion(mono, converted, info.frames, capacity, 0, 0, 1, rate/info.rate)
    assert resampler.src_simple(c.byref(conversion), 0, 1) == 0
    samples = [max(-32768, min(32767, round(converted[i]*32768))) for i in range(conversion.output_generated)]
    assert samples and any(samples), record['path']
    name = 'Samples%d' % index
    lines.append('static const short %s[] = {' % name)
    for i in range(0, len(samples), 32):
        lines.append(','.join(map(str, samples[i:i+32]))+',')
    lines.append('};')
    lines.append('static const Clip Clip%d = {%s,%d};' % (index, name, len(samples)))
    clips[record['path'][:-4]] = index
    import struct
    with wave.open(str(out / (record['path'].replace('/', '-')[:-4]+'.wav')), 'wb') as wav:
        wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(rate)
        wav.writeframes(struct.pack('<%dh' % len(samples), *samples))

event_names = ['block.wood.hit', 'block.wood.break', 'block.wood.place', 'block.chest.open',
               'block.chest.close', 'block.wooden_door.open', 'block.wooden_door.close', 'entity.item.pickup', 'entity.player.hurt']
variants, groups = [], []
for event in event_names:
    sounds = manifest['events'][event]['sounds']
    groups.append((len(variants), len(sounds)))
    for sound in sounds:
        sound = {'name': sound} if isinstance(sound, str) else sound
        variants.append('{&Clip%d,%sf}' % (clips[sound['name']], str(float(sound.get('volume', 1)))))
lines.append('static const Variant Variants[] = {' + ','.join(variants) + '};')
lines.append('static const Event Events[] = {' + ','.join('{Variants+%d,%d}' % group for group in groups) + '};')
lines.extend(['}', '#endif', ''])
(ROOT / 'src/minecraft/audio_assets.h').write_text('\n'.join(lines))
print('Verified and embedded %d original Minecraft sound samples at %d Hz' % (len(clips), rate))
