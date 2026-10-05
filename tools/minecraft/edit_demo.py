#!/usr/bin/env python3
"""Assemble the recorded project walkthrough with captions and game audio."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def ffmpeg():
    if os.environ.get('FFMPEG'):
        return os.environ['FFMPEG']
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()


def clock(seconds):
    centiseconds = round(seconds*100)
    h, remainder = divmod(centiseconds, 360000)
    m, remainder = divmod(remainder, 6000)
    s, cs = divmod(remainder, 100)
    return '%d:%02d:%02d.%02d'%(h,m,s,cs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    scenes = ['steve','combat','tree','building','reload']
    captures = {s:json.loads((root/s/'capture.json').read_text()) for s in scenes}
    titles = []
    def caption(start, end, title, detail):
        titles.append((start,end,'{\\pos(42,902)\\an7\\b1}'+title))
        titles.append((start,end,'{\\pos(42,934)\\an7\\fs18\\b0\\c&HBDD0D3&}'+detail))
    fps = 30000/1001
    intro = 90/fps
    cursor = intro
    offsets = {}
    for scene in scenes:
        offsets[scene] = cursor
        cursor += captures[scene]['duration']
    total = cursor
    titles += [(0,intro,'{\\an5\\pos(640,337)\\fs72\\b1\\fad(350,250)}SUNSHINE × MINECRAFT'),
               (0,intro,'{\\an5\\pos(640,420)\\fs28\\b0\\c&HBDD0D3&\\fad(350,250)}A live gameplay demo of our crossover project')]
    a=offsets['steve'];b=a+captures['steve']['duration']
    caption(a,a+7.1,'Steve in Delfino Plaza','Original Minecraft textures · raised axe and sword grips · brighter plaza')
    caption(a+7.1,b,'Movement + FLUDD','Run, jump and spray with the original Sunshine controls')
    a=offsets['combat'];b=a+captures['combat']['duration']
    caption(a,b,'Diamond sword combat','Swing at nearby enemies to trigger their native stomp reactions')
    a=offsets['tree'];b=a+captures['tree']['duration']
    caption(a,b,'Harvest wood','Hold the axe to crack the trunk and release four collectible logs')
    a=offsets['building'];j=captures['building']
    def build_time(field):return a+(4780+(field-4780)*3//2-j['first_field'])*1001/60000
    sections = [
        (4780,4920,'Clickable inventory + planks','Click to move items; one log crafts four wooden planks'),
        (4920,5140,'Craft + place a crafting table','Arrange four planks, craft the table, then place it on the world grid'),
        (5140,5360,'The 3 × 3 crafting menu','Press E at the table and arrange six planks to craft wooden doors'),
        (5360,5600,'Craft a chest','Eight planks around an empty center create a storage chest'),
        (5600,5760,'Place + fill the chest','Open with E, split the plank stack and store the diamond sword'),
        (5760,5860,'Working wooden doors','Place a door, then press E to open and close it'),
        (5860,5990,'Grid building','Place planks on the ground or stack them above another block'),
        (5990,6240,'Solid blocks + jumping','Placed blocks support Steve and work with the native movement system'),
        (6240,6320,'Doorway interaction','Closed doors block movement; opening the door clears the passage')]
    for lo,hi,title,detail in sections:
        caption(max(a,build_time(lo)),min(offsets['reload'],build_time(hi)),title,detail)
    a=offsets['reload'];b=a+captures['reload']['duration']
    caption(a,b,'Storage survives a restart','A fresh game process restores the placed chest, seven planks and the sword')
    ass = '''[Script Info]
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
    for start,end,title in titles:
        ass+='Dialogue: 0,%s,%s,Caption,,0,0,0,,%s\n'%(clock(start),clock(end),title)
    (root/'captions.ass').write_text(ass)
    # A brief, dimmed live-gameplay freeze introduces the project.
    hero=root/'hero.png'
    subprocess.run([ffmpeg(),'-hide_banner','-loglevel','error','-y','-ss','4.2',
                    '-i',str(root/'steve/clip.mp4'),'-frames:v','1',str(hero)],check=True)
    command=[ffmpeg(),'-hide_banner','-y','-loop','1','-framerate','30000/1001',
             '-i',str(hero),'-f','lavfi','-i','anullsrc=r=48000:cl=stereo']
    for scene in scenes:command+=['-i',str(root/scene/'clip.mp4')]
    filters=[
        '[0:v]trim=end_frame=90,setpts=PTS-STARTPTS,boxblur=5:1,eq=brightness=-0.2,format=yuv420p[v0]',
        '[1:a]atrim=duration=%.9f,asetpts=PTS-STARTPTS[a0]'%intro]
    for i,scene in enumerate(scenes):
        source=i+2
        filters += ['[%d:v]trim=end_frame=%d,setpts=PTS-STARTPTS[v%d]'%(source,captures[scene]['frames'],i+1),
                    '[%d:a]aresample=48000,apad,atrim=duration=%.9f,asetpts=PTS-STARTPTS[a%d]'%(source,captures[scene]['duration'],i+1)]
    streams=''.join('[v%d][a%d]'%(i,i) for i in range(6))
    filters += [streams+'concat=n=6:v=1:a=1[game][sound]',
                "[game]pad=1280:960:0:0:color=0x101b22,drawbox=x=23:y=909:w=4:h=41:color=0x60dec4:t=fill,ass='%s',fade=t=out:st=%.9f:d=0.4[video]"%(root/'captions.ass',total-.4),
                '[sound]loudnorm=I=-16:TP=-1.5:LRA=9,afade=t=out:st=%.9f:d=0.4[audio]'%(total-.4)]
    (root/'edit-filter.txt').write_text(';\n'.join(filters))
    command+=['-filter_complex_script',str(root/'edit-filter.txt'),'-map','[video]',
              '-map','[audio]','-t',str(total),'-r','30000/1001','-c:v','libx264',
              '-preset','slow','-crf','19','-profile:v','high','-level:v','4.0',
              '-pix_fmt','yuv420p','-c:a','aac','-ar','48000','-b:a','192k',
              '-metadata','title=Sunshine × Minecraft — Project Demo',
              '-metadata','comment=Recorded native gameplay: Steve, FLUDD, combat, harvesting, crafting, building and persistent storage.',
              '-movflags','+faststart',str(root/'sunshine-minecraft-project-demo-v1.mp4')]
    with (root/'edit.log').open('wb') as log:
        subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    (root/'edit.json').write_text(json.dumps(dict(duration=total,width=1280,height=960,
        fps=fps,chapters=offsets,captures=captures),indent=2)+'\n')
    print('PASS edited %.2f-second project demo'%total,flush=True)


if __name__=='__main__':main()
