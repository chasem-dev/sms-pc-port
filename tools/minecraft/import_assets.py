#!/usr/bin/env python3
"""Verify pinned original PNGs and pack them for native GX; no network at build/run."""
from pathlib import Path
import colorsys,hashlib,json
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/'assets/minecraft'
records=json.loads((SRC/'sources.json').read_text())['assets']
for r in records:
    assert hashlib.sha256((SRC/r['path']).read_bytes()).hexdigest()==r['sha256'],r['path']
def png(name):return Image.open(SRC/('textures/'+name+'.png')).convert('RGBA')
atlas=Image.new('RGBA',(512,512))
regions=[]
def place(name,image,x,y):
    atlas.paste(image,(x,y));regions.append((name,x,y,*image.size))
place('Inventory',png('gui/container/inventory').crop((0,0,176,166)),0,0)
place('Hotbar',png('gui/widgets').crop((0,0,182,22)),0,168)
place('Selection',png('gui/widgets').crop((0,22,24,46)),184,168)
# Recolor the original XP strips; preserve their pixels, alpha and shading.
# Source PNGs remain untouched. These two strips fit above the crafting GUI.
for name,y,hue,targetY in [('WaterBarEmpty',64,210/360,190),('WaterBarFill',69,198/360,195)]:
    strip=png('gui/icons').crop((0,y,182,y+5))
    pixels=[]
    for yPixel in range(strip.height):
        for xPixel in range(strip.width):
            r,g,b,a=strip.getpixel((xPixel,yPixel))
            _,s,v=colorsys.rgb_to_hsv(r/255,g/255,b/255)
            blue=colorsys.hsv_to_rgb(hue,s,v)
            pixels.append(tuple(round(c*255) for c in blue)+(a,))
    strip.putdata(pixels);place(name,strip,0,targetY)
for name,x in [('HeartEmpty',16),('HeartFull',52),('HeartHalf',61)]:
    place(name,png('gui/icons').crop((x,0,x+9,9)),210+(x!=16)*10+(x==61)*10,168)
place('Font',png('font/ascii'),256,0)
place('Steve',png('entity/player/wide/steve'),384,0)
place('Crosshair',png('gui/icons').crop((0,0,15,15)),464,64)
place('Stick',png('item/stick'),480,64)
for name,path,x in [('Axe','item/iron_axe',384),('Sword','item/diamond_sword',400),('Oak','block/oak_log',416),('OakTop','block/oak_log_top',432)]:
    place(name,png(path),x,64)
place('Crafting',png('gui/container/crafting_table').crop((0,0,176,166)),0,200)
chest=Image.new('RGBA',(176,167))
chest.paste(png('gui/container/generic_54').crop((0,0,176,71)),(0,0))
chest.paste(png('gui/container/generic_54').crop((0,126,176,222)),(0,71))
place('ChestGui',chest,184,200)
place('ChestSkin',png('entity/chest/normal'),448,0)
for name,path,x,y in [('Planks','block/oak_planks',384,80),('TableTop','block/crafting_table_top',400,80),('TableFront','block/crafting_table_front',416,80),('TableSide','block/crafting_table_side',432,80),('DoorBottom','block/oak_door_bottom',384,96),('DoorTop','block/oak_door_top',400,96),('DoorItem','item/oak_door',416,96)]:
    place(name,png(path),x,y)
# Native Sunshine pickup renders, including the game's shared colored BMT.
nozzles=json.loads((SRC/'sunshine-rendered/sources.json').read_text())['assets']
for i,r in enumerate(nozzles):
    path=SRC/r['path'];assert hashlib.sha256(path.read_bytes()).hexdigest()==r['sha256']
    rendered=Image.open(path).convert('RGBA').resize((32,32),Image.Resampling.LANCZOS)
    place(['HoverNozzle','RocketNozzle','TurboNozzle'][i],rendered,384+i*32,128)
# Original pre-advancement achievement popup, never redrawn or generated.
def legacy(path):return Image.open(SRC/('legacy-achievements/textures/'+path+'.png')).convert('RGBA')
place('AchievementToast',legacy('gui/achievement/achievement_background').crop((96,202,256,234)),0,400)
place('AchievementBook',legacy('items/book_normal'),384,160)
for name,path,x in [('AchievementOak','log_oak',400),('AchievementOakTop','log_oak_top',416),('AchievementTableTop','crafting_table_top',432),('AchievementTableFront','crafting_table_front',448),('AchievementTableSide','crafting_table_side',464)]:
    place(name,legacy('blocks/'+path),x,160)
place('AchievementFont',legacy('font/ascii'),256,368)
# GX RGBA8 uses 4x4 blocks: 32 bytes of A/R, then 32 bytes of G/B.
pixels=atlas.load();tiled=bytearray()
for by in range(0,512,4):
    for bx in range(0,512,4):
        block=[pixels[bx+x,by+y] for y in range(4) for x in range(4)]
        tiled.extend(v for r,g,b,a in block for v in (a,r))
        tiled.extend(v for r,g,b,a in block for v in (g,b))
lines=['// Generated from verified Minecraft PNGs and original Sunshine model renders by tools/minecraft/import_assets.py.','#ifndef SMS_MINECRAFT_ASSETS_H','#define SMS_MINECRAFT_ASSETS_H','namespace minecraft_assets {','struct Region { int x,y,w,h; };']
for name,x,y,w,h in regions:lines.append('static const Region %s = {%d,%d,%d,%d};'%(name,x,y,w,h))
def array(name,data,aligned=False):
    lines.append('static const unsigned char %s[]%s = {'%(name,' __attribute__((aligned(32)))' if aligned else ''))
    for i in range(0,len(data),32):lines.append(','.join(str(v) for v in data[i:i+32])+',')
    lines.append('};')
array('Atlas',tiled,True)
for name,path in [('AxePixels','item/iron_axe'),('SwordPixels','item/diamond_sword'),('StickPixels','item/stick')]:array(name,png(path).tobytes())
# A transparent border clips projected cracks to the selected trunk region.
# Keep the source PNGs unchanged; GX clamps outside the border to zero alpha.
for stage in range(10):
    image=Image.new('RGBA',(32,32))
    image.paste(png('block/destroy_stage_%d'%stage),(8,8))
    data=bytearray();pixels=image.load()
    for by in range(0,32,4):
        for bx in range(0,32,4):
            block=[pixels[bx+x,by+y] for y in range(4) for x in range(4)]
            data.extend(v for r,g,b,a in block for v in (a,r))
            data.extend(v for r,g,b,a in block for v in (g,b))
    array('CrackTexture%d'%stage,data,True)
# Character advances from the actual font pixels, just as bitmap fonts require.
font=png('font/ascii');adv=[]
for c in range(128):
    crop=font.crop(((c%16)*8,(c//16)*8,(c%16)*8+8,(c//16)*8+8))
    xs=[x for y in range(8) for x in range(8) if crop.getpixel((x,y))[3]]
    adv.append(4 if c==32 else min(8,max(xs)+2) if xs else 6)
array('FontAdvance',adv)
font=legacy('font/ascii');adv=[]
for c in range(128):
    crop=font.crop(((c%16)*8,(c//16)*8,(c%16)*8+8,(c//16)*8+8))
    xs=[x for y in range(8) for x in range(8) if crop.getpixel((x,y))[3]]
    adv.append(4 if c==32 else min(8,max(xs)+2) if xs else 6)
array('AchievementFontAdvance',adv)
lines.extend(['}','#endif',''])
(ROOT/'src/minecraft/assets.h').write_text('\n'.join(lines))
(ROOT/'build/minecraft').mkdir(parents=True,exist_ok=True)
atlas.save(ROOT/'build/minecraft/asset-atlas.png')
print('Verified %d pinned source assets; generated embedded GX atlas and source pixel arrays'%len(records))
