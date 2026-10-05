#!/usr/bin/env python3
"""Record native gameplay through existing deterministic screenshot/audio hooks.

Requires imageio-ffmpeg (or FFMPEG pointing to a local executable). Frames are
encoded as they arrive, then removed, so full-resolution footage uses little
temporary space. The gameplay executable and user save are never modified.
"""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
BOOT = 'START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800'
FPS = 30000 / 1001


def encoder():
    if os.environ.get('FFMPEG'):
        return os.environ['FFMPEG']
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()


def scenario(name):
    if name == 'steve':
        source = '''import gdb
def val(s): return gdb.parse_and_eval(s)
def put(s,n): gdb.execute('set var %s = %s'%(s,n),to_string=True)
class Demo(gdb.Breakpoint):
 def stop(self):
  if not int(val('gpMarDirector')) or int(val('gpMarDirector->mMap'))!=1 or int(val('gpMarDirector->mState'))!=4 or int(val('gpMarDirector->unk124'))!=0:return False
  f=int(val("'(anonymous namespace)::g_retrace_count'"))
  if 4800<=f<5204:
   angle=int((f-4800)*65536/400)%65536
   if angle>=32768:angle-=65536
   put('gpMarioOriginal->mModelFaceAngle',angle);put('gpMarioOriginal->mFaceAngle.y',angle)
  if f>=5605:
   print('demo: PASS Steve, tool grips, running, jumping and FLUDD',flush=True)
   gdb.execute('kill',to_string=True);return True
  return False
Demo('sms_minecraft_attach_fludd')
'''
        inputs = BOOT + ',KEY_3@4780,KEY_2@5040,STICK_DOWN@5210+90,A@5320+5,R@5440+120'
        return 1, 4780, 5600, source, inputs, {}
    if name == 'tree':
        source = (ROOT/'tools/minecraft/runtime_test.py').read_text()
        inputs = BOOT + ',KEY_G@4570+170,R@4650+80,R@4740+80,KEY_9@4850,WHEEL_DOWN@4860,WHEEL_DOWN@4870,KEY_TAB@4880,KEY_TAB@4900,KEY_3@4910'
        return 1, 4520, 4820, source, inputs, {}
    if name == 'combat':
        source = (ROOT/'tools/minecraft/sword_runtime.py').read_text()
        inputs = BOOT + ',KEY_3@4800,KEY_G@4810+4,KEY_G@4840+4,KEY_2@4860,KEY_G@4870+4,KEY_3@4890,KEY_TAB@4900,KEY_G@4910+4,KEY_TAB@4930,KEY_G@4940+4,KEY_G@4970+4'
        return 2, 4934, 5160, source, inputs, {}
    source = (ROOT/'tools/minecraft/building_runtime.py').read_text()
    if name == 'building':
        # Let each GUI click breathe without changing game time or audio.
        source += '\ndef field():\n return 4780+(int(value("\'(anonymous namespace)::g_retrace_count\'"))-4780)*2//3\n'
        inputs = subprocess.check_output(['python3', str(ROOT/'tools/minecraft/building_inputs.py')], text=True).strip()
        def stretch(m):
            field = int(m.group(1))
            return '@' + str(4780 + (field-4780)*3//2 if field >= 4780 else field)
        inputs = re.sub(r'@(\d+)', stretch, inputs)
        return 1, 4790, 7080, source, inputs, {}
    source = source.replace('f>=4860', 'f>=5010')
    source += '''
class ChestView(gdb.Breakpoint):
 def stop(self):
  if gdb.selected_inferior().pid and state['ready']:
   put('gpMarioOriginal->mFaceAngle.y',16384);put('gpMarioOriginal->mModelFaceAngle',16384)
  return False
ChestView('sms_minecraft_update')
'''
    return 1, 4780, 5000, source, BOOT+',MOUSE_RIGHT_320_240@4800+2', {'SMS_BUILDING_RELOAD':'1'}


def complete_ppm(path):
    if not path.exists():
        return None
    with path.open('rb') as f:
        if f.readline() != b'P6\n':
            return None
        try:
            w, h = map(int, f.readline().split())
            if f.readline().strip() != b'255':
                return None
        except ValueError:
            return None
        size = f.tell()+w*h*3
    if path.stat().st_size != size:
        return None
    return path.read_bytes()


def record(args):
    out = Path(args.output).resolve()
    clip = out/args.scene
    clip.mkdir(parents=True, exist_ok=True)
    shots = clip/'shots'
    shots.mkdir(exist_ok=True)
    map_id, first, last, script, inputs, extra = scenario(args.scene)
    script += '''
class AudioClock(gdb.Breakpoint):
 def stop(self):
  print('demo: first PCM field=%d'%int(gdb.parse_and_eval("'(anonymous namespace)::g_retrace_count'")),flush=True)
  self.enabled=False
  return False
AudioClock('dma_block')
'''
    fields = list(range(first, last, 2))
    card = out/'building'/'card' if args.scene == 'reload' else clip/'card'
    card.mkdir(parents=True, exist_ok=True)
    (clip/'fixture.py').write_text(script)
    env = dict(os.environ)
    env.update(SMS_SETTINGS='/dev/null', SMS_MINECRAFT='1', SMS_CLEAN_SHINE_GATE='1',
               SMS_HEADLESS='1', SMS_SKIP_MOVIES='1', SMS_TEXTURE_PACKS='0',
               SMS_AUDIO='1', SMS_AUDIO_OUT='none', SMS_MINECRAFT_AUDIO_TRACE='1',
               SMS_AUDIO_WAV=str(clip/'game.wav'), SMS_VI_DETERMINISTIC='1',
               SMS_WARP='%d,0,1'%map_id, SMS_FRAME_RATE='60', SMS_GX_SCALE='2',
               SMS_SAVE_DIR=str(card), SMS_SHOT_DIR=str(shots),
               SMS_AUTOPRESS=inputs, SMS_SHOTS=','.join(map(str, fields)))
    env.pop('SMS_BUILDING_RELOAD', None)
    env.update(extra)
    command = ['gdb','-q','-batch','-nx','-ex','set debuginfod enabled off',
               '-ex','set pagination off','-ex','set confirm off',
               '-ex','handle SIG34 nostop noprint','-ex','source '+str(clip/'fixture.py'),
               '-ex','run','--args',str(Path(getattr(args,'executable',ROOT/'build/linux-64/sms')).resolve()),args.disc]
    with (clip/'game.log').open('wb') as game_log, (clip/'encode.log').open('wb') as encode_log:
        game = subprocess.Popen(command, cwd=ROOT, env=env, stdout=game_log, stderr=subprocess.STDOUT)
        encode = subprocess.Popen([encoder(),'-hide_banner','-loglevel','warning','-y',
                  '-f','image2pipe','-vcodec','ppm','-framerate','30000/1001','-i','pipe:0',
                  '-an','-c:v','libx264','-preset','fast','-crf','18','-pix_fmt','yuv420p',
                  '-movflags','+faststart',str(clip/'silent.mp4')], stdin=subprocess.PIPE,
                  stdout=encode_log, stderr=subprocess.STDOUT)
        deadline = time.monotonic()+600
        try:
            for i, field in enumerate(fields):
                path = shots/('field%05d.ppm'%field)
                while True:
                    data = complete_ppm(path)
                    if data:
                        break
                    if game.poll() is not None:
                        raise RuntimeError('game exited before field %d; see %s'%(field,clip/'game.log'))
                    if time.monotonic()>deadline:
                        raise RuntimeError('capture timed out')
                    time.sleep(.03)
                encode.stdin.write(data)
                if i in (0,len(fields)//2,len(fields)-1):
                    (clip/('sample-%d.ppm'%i)).write_bytes(data)
                path.unlink()
                if i%100==0:
                    print('%s: %d/%d recorded frames'%(args.scene,i+1,len(fields)),flush=True)
            encode.stdin.close()
            if encode.wait(timeout=60):
                raise RuntimeError('video encoding failed')
            game.wait(timeout=180)
        finally:
            if game.poll() is None:
                game.kill();game.wait()
            if encode.poll() is None:
                encode.kill();encode.wait()
    log = (clip/'game.log').read_text(errors='replace')
    if ': FAIL ' in log or ': PASS ' not in log:
        raise RuntimeError('live scenario failed: '+str(clip/'game.log'))
    pcm_first = int(re.search(r'demo: first PCM field=(\d+)',log).group(1))
    # gdb deliberately stops the inferior; repair the periodic WAV header to
    # the actual written PCM size before reading or trimming the recording.
    wav = clip/'game.wav'
    size = wav.stat().st_size-44
    assert size>0 and size%4==0
    with wav.open('r+b') as f:
        f.seek(4);f.write(struct.pack('<I',size+36))
        f.seek(40);f.write(struct.pack('<I',size))
    duration = len(fields)/FPS
    subprocess.run([encoder(),'-hide_banner','-loglevel','warning','-y',
        '-i',str(clip/'silent.mp4'),'-ss',str((first-pcm_first)*1001/60000),'-i',str(wav),
        '-map','0:v','-map','1:a','-t',str(duration),'-c:v','copy','-c:a','aac',
        '-b:a','192k','-af','afade=t=in:d=0.08,afade=t=out:st=%.5f:d=0.1'%(duration-.1),
        '-movflags','+faststart',str(clip/'clip.mp4')], check=True)
    (clip/'capture.json').write_text(json.dumps(dict(scene=args.scene,first_field=first,
        last_field=fields[-1],frames=len(fields),fps=FPS,duration=duration,
        pcm_first_field=pcm_first,audio_offset=(first-pcm_first)*1001/60000,
        save_dir=str(card)),indent=2)+'\n')
    print('PASS recorded '+str(clip/'clip.mp4'),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('scene', choices=['steve','tree','building','combat','reload'])
    parser.add_argument('--disc', required=True)
    parser.add_argument('--executable', default=str(ROOT/'build/linux-64/sms'))
    parser.add_argument('--output', default=str(ROOT/'build/minecraft/demo-v1'))
    record(parser.parse_args())
