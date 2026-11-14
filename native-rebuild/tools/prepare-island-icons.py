"""Remove the generated black matte and normalize the eleven island PNG icons.
No glyphs are redrawn. Requires Pillow, NumPy and SciPy.
"""
import argparse, hashlib, json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[2]
NAMES = ['water','stand','sleep','severe','rain','temperature','music','calendar-weather','todos','clock','system']
LABELS = ['喝水','久坐','睡眠','恶劣天气','降雨','降温','音乐','日历','待办事项','时间','性能']
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', default=str(ROOT / 'output/imagegen/island-icons-v2'))
parser.add_argument('--install', action='store_true', help='copy approved PNGs into the production UI')
args = parser.parse_args()
source = Path(args.source)
out = source / 'transparent'
out.mkdir(parents=True, exist_ok=True)
if args.install and not all((source/(name+'-generated.png')).exists() for name in NAMES):
    raise ValueError('Expected all eleven approved source icons before installation')
records = []
for index, name in enumerate(NAMES):
    original = source / (name + '-generated.png')
    if not original.exists(): continue
    picture = Image.open(original).convert('RGB')
    data = np.asarray(picture, dtype=np.float64)
    foreground = np.max(data, axis=2) > 14
    labels, count = ndimage.label(foreground)
    sizes = np.bincount(labels.ravel())
    keep = sizes > 30; keep[0] = False
    foreground = keep[labels]
    core = ndimage.binary_erosion(foreground, iterations=2)
    if not core.any(): raise ValueError('Missing artwork: ' + name)
    distance, nearest = ndimage.distance_transform_edt(~core, return_indices=True)
    interior = data[tuple(nearest)]
    coverage = np.clip(np.sum(data*interior,axis=2) / np.maximum(1,np.sum(interior*interior,axis=2)),0,1)
    coverage[core] = 1
    coverage[(distance>4) | (np.max(data,axis=2)<4)] = 0
    rgb = np.clip(data / np.maximum(coverage[:,:,None], 1/255), 0, 255)
    rgba = np.concatenate((rgb,np.round(coverage[:,:,None]*255)),axis=2).astype(np.uint8)
    rgba[rgba[:,:,3]==0,:3] = 0
    art = Image.fromarray(rgba)
    box = art.getchannel('A').point(lambda a: 255 if a>24 else 0).getbbox()
    art = art.crop(box)
    fit = 216 if index<6 else 208
    ratio = fit / max(art.size)
    art = art.resize((round(art.width*ratio),round(art.height*ratio)),Image.Resampling.LANCZOS)
    master = Image.new('RGBA',(256,256))
    master.alpha_composite(art,((256-art.width)//2,(256-art.height)//2))
    final = out / (name+'.png')
    master.save(final,optimize=True)
    target = ROOT / 'native-rebuild/ui/assets' / ('notifications' if index<6 else 'icons/island-expanded') / (name+'.png')
    if args.install: target.write_bytes(final.read_bytes())
    records.append({'id':name,'label':LABELS[index],'source':original.name,'sourceSha256':hashlib.sha256(original.read_bytes()).hexdigest(),'sourceCrop':box,'canvas':[256,256],'fit':fit,'file':target.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(final.read_bytes()).hexdigest(),'bytes':final.stat().st_size})

font = lambda size: ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', size)
preview = Image.new('RGB',(1200,640),'#101114')
draw = ImageDraw.Draw(preview)
draw.text((32,22),'Lume · 灵动岛统一图标',font=font(26),fill='#f3f5fa')
draw.text((32,66),'保留原有颜色与含义 · 柔和渐变 / 圆润面形',font=font(16),fill='#939dab')
for index,name in enumerate(NAMES):
    file = out/(name+'.png')
    if not file.exists(): continue
    row = 0 if index<6 else 1
    col = index if index<6 else index-6
    x,y = 30+col*194,108+row*258
    draw.rounded_rectangle((x,y,x+176,y+228),radius=18,fill='#1a1d22')
    art=Image.open(file).convert('RGBA')
    preview.paste(art.resize((96,96),Image.Resampling.LANCZOS),(x+40,y+12),art.resize((96,96),Image.Resampling.LANCZOS))
    draw.text((x+88,y+112),LABELS[index],anchor='mt',font=font(17),fill='#eef2f8')
    for offset,size in [(28,24),(70,32),(118,40)]:
        small=art.resize((size,size),Image.Resampling.LANCZOS)
        preview.paste(small,(x+offset,y+156+(40-size)//2),small)
    draw.text((x+88,y+203),'24 / 32 / 40 px',anchor='mt',font=font(12),fill='#8e97a5')
preview.save(source/'icon-preview.png',optimize=True)
manifest={'version':2,'date':'2026-11-09','model':'gpt-image-2','provider':'user-designated compatible Images API','method':'bundled imagegen CLI via existing URL-response adapter; two screenshot references; 1024x1024 high; black-matte extraction and scale normalization only','icons':records}
(source/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+chr(10),encoding='utf-8')
if args.install and len(records)!=11: raise ValueError('Expected all eleven approved icons')
print(json.dumps({'processed':len(records),'installed':args.install,'preview':str(source/'icon-preview.png')},ensure_ascii=False))
