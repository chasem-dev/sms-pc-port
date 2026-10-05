#!/usr/bin/env python3
"""Pack alpha icons from the native SMS_NOZZLE_RENDER capture (GX scale 4).
Run with a native field PPM/PNG and the GMSE01 disc used to render it.
The source models and their shared colored BMT stay on the user's disc.
"""
import argparse,hashlib,json,sys
from pathlib import Path
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from extract_icon import Disc,disc_file,yaz0,rarc_file
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',required=True);p.add_argument('--disc',required=True);a=p.parse_args()
image=Image.open(a.capture).convert('RGBA');bg=image.getpixel((0,0));sx,sy=image.width/640,image.height/480
out=ROOT/'assets/minecraft/sunshine-rendered';out.mkdir(exist_ok=True)
arc=yaz0(disc_file(Disc(a.disc),('data','scene','dolpic0.szs')))
models=['normal_nozzle_item','rocket_nozzle_item','back_nozzle_item'];names=['hover','rocket','turbo'];records=[]
for i,(name,model) in enumerate(zip(names,models)):
 crop=image.crop(tuple(round(v) for v in ((32+i*208)*sx,144*sy,(192+i*208)*sx,304*sy)))
 crop.putdata([pixel if pixel[:3]!=bg[:3] else (0,0,0,0) for pixel in crop.getdata()]);bbox=crop.getbbox();assert bbox,'missing '+name
 crop=crop.crop(bbox);icon=Image.new('RGBA',(256,256));crop.thumbnail((224,224),Image.Resampling.LANCZOS);icon.alpha_composite(crop,((256-crop.width)//2,(256-crop.height)//2))
 path=out/(name+'-nozzle.png');icon.save(path)
 records.append({'path':str(path.relative_to(ROOT/'assets/minecraft')),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'model':model+'.bmd','model_sha256':hashlib.sha256(rarc_file(arc,('mapobj',model+'.bmd'))).hexdigest()})
meta={'source':'Native J3D render of original GMSE01 pickup models with shared nozzleItem.bmt; isometric yaw 2.35619449, pitch 0.61547971 radians','material_sha256':hashlib.sha256(rarc_file(arc,('mapobj','nozzleitem.bmt'))).hexdigest(),'renderer':'src/minecraft/nozzle_preview.cpp','assets':records}
(out/'sources.json').write_text(json.dumps(meta,indent=2)+'\n');print('Saved three colored original-model icons with alpha and source hashes')
