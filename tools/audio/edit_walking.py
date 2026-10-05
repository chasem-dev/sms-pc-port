#!/usr/bin/env python3
"""Show the walking audio fix before/after at the same native audio gain."""
import argparse
import json
from pathlib import Path
import subprocess
import imageio_ffmpeg

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('directory',type=Path)
p.add_argument('--version',default='v1')
p.add_argument('--detail',default='Corrected reverb and smoother sound endings')
a=p.parse_args();root=a.directory.resolve();ffmpeg=imageio_ffmpeg.get_ffmpeg_exe()
scenes=[('before-mario','Before this audio fix','Listen after each step and when Mario stops'),
        ('after-mario','Updated core port — Mario',a.detail),
        ('after-steve','Updated core port — Steve','The same shared audio fix also applies to Minecraft mode')]
meta=[json.loads((root/s/'walking/capture.json').read_text()) for s,_,_ in scenes]
def clock(t):
 c=round(t*100);h,c=divmod(c,360000);m,c=divmod(c,6000);s,c=divmod(c,100)
 return f'{h}:{m:02d}:{s:02d}.{c:02d}'
ass='''[Script Info]
ScriptType: v4.00+
PlayResX: 1280
PlayResY: 960
WrapStyle: 2
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Caption,DejaVu Sans,27,&H00F4F8F8,&H00FFFFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,42,42,0,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
'''
cursor=0;chapters=[]
for (scene,title,detail),m in zip(scenes,meta):
 end=cursor+m['duration'];chapters.append({'scene':scene,'start':cursor,'end':end})
 for text in [r'{\pos(42,902)\an7\b1}'+title,r'{\pos(42,934)\an7\fs18\b0\c&HBDD0D3&}'+detail]:
  ass+=f'Dialogue: 0,{clock(cursor)},{clock(end)},Caption,,0,0,0,,{text}\n'
 cursor=end
(root/'walking-captions.ass').write_text(ass)
command=[ffmpeg,'-hide_banner','-y']
for scene,_,_ in scenes:command+=['-i',str(root/scene/'walking/clip.mp4')]
filters=[]
for i,m in enumerate(meta):
 filters += [f'[{i}:v]trim=end_frame={m["frames"]},setpts=PTS-STARTPTS[v{i}]',
             f'[{i}:a]aresample=48000,apad,atrim=duration={m["duration"]:.9f},asetpts=PTS-STARTPTS[a{i}]']
filters += [''.join(f'[v{i}][a{i}]' for i in range(3))+'concat=n=3:v=1:a=1[game][audio]',
            f"[game]pad=1280:960:0:0:color=0x101b22,drawbox=x=23:y=909:w=4:h=41:color=0x60dec4:t=fill,ass='{root/'walking-captions.ass'}'[video]"]
(root/'walking-edit-filter.txt').write_text(';\n'.join(filters))
output=root/('sunshine-walking-audio-fix-'+a.version+'.mp4')
command+=['-filter_complex_script',str(root/'walking-edit-filter.txt'),'-map','[video]','-map','[audio]',
          '-t',str(cursor),'-r','30000/1001','-c:v','libx264','-preset','medium','-crf','18','-threads','2',
          '-pix_fmt','yuv420p','-c:a','aac','-ar','48000','-b:a','192k','-movflags','+faststart',
          '-metadata','title=Sunshine — Walking Audio Fix',
          '-metadata','comment=Native gameplay before and after the shared DSP mixer fix. Identical audio gain in all three clips.',str(output)]
with (root/'walking-edit.log').open('wb') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
(root/'walking-edit.json').write_text(json.dumps({'duration':cursor,'chapters':chapters,'audio_gain':1.0,'captures':meta},indent=2)+'\n')
subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-y','-ss',str(chapters[2]['start']+3.5),'-i',str(output),'-frames:v','1',str(output.with_suffix('.png'))],check=True)
print(f'PASS: {cursor:.2f}s walking comparison, native game audio, same gain throughout',flush=True)
