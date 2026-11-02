"""Prepare the AI sprite as reusable RGBA navigation icons.

This only crops, normalizes size, and tints the generated artwork; no replacement
glyphs are drawn. Supply the generated green sheet and the skill's alpha result.
"""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

NAMES = ['home', 'music', 'todo', 'quick-launch', 'clock', 'notifications', 'settings']
LABELS = ['首页', '音乐', '待办', '快捷启动', '时钟', '通知', '设置']

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--alpha', required=True)
parser.add_argument('--out', required=True)
parser.add_argument('--preview', required=True)
args = parser.parse_args()
source = Image.open(args.source).convert('RGB')
alpha = Image.open(args.alpha).convert('RGBA')
assert source.size == alpha.size
mask = Image.new('L', source.size)
mask.putdata([255 if g > 180 and g-r > 70 and g-b > 70 else 0 for r,g,b in source.getdata()])
left,top,right,bottom = mask.getbbox()
# The provider may letterbox a landscape image. Derive the green art board,
# rather than keeping the non-transparent black pillarboxes as icon content.
board = alpha.crop((left,top,right,bottom))
width,height = board.size
out = Path(args.out)
out.mkdir(parents=True,exist_ok=True)
records = []
for index,(name,label) in enumerate(zip(NAMES,LABELS)):
    col,row = index%4,index//4
    tile = board.crop((round(col*width/4),round(row*height/2),round((col+1)*width/4),round((row+1)*height/2)))
    bbox = tile.getchannel('A').point(lambda x: 255 if x>32 else 0).getbbox()
    if bbox is None:
        raise ValueError(f'Missing generated symbol: {name}')
    tile = tile.crop(bbox)
    scale = 208/max(tile.size)
    tile = tile.resize((round(tile.width*scale),round(tile.height*scale)),Image.Resampling.LANCZOS)
    master = Image.new('RGBA',(256,256))
    master.alpha_composite(tile,((256-tile.width)//2,(256-tile.height)//2))
    master.putdata([(r,min(g,max(r,b)),b,a) if g>max(r,b)+8 else (r,g,b,a)
                    for r,g,b,a in master.getdata()])
    selected = ImageOps.colorize(ImageOps.grayscale(master),'#386DAF','#B6D8FF').convert('RGBA')
    selected.putalpha(master.getchannel('A'))
    for state,art in [('default',master),('selected',selected)]:
        for size in [24,48,72,256]:
            target=out/f'{name}-{state}-{size}.png'
            art.resize((size,size),Image.Resampling.LANCZOS).save(target,optimize=True)
    records.append({'id':name,'label':label,'master':f'{name}-default-256.png',
                    'selected':f'{name}-selected-256.png','sourceBox':bbox})
(out/'manifest.json').write_text(json.dumps({'version':1,'style':'Lume Rounded Soft Fill',
    'displaySize':24,'hitArea':44,'variants':['default','selected'],
    'sizes':[24,48,72,256],'icons':records},ensure_ascii=False,indent=2),encoding='utf-8')

# A review board checks the actual 24px UI scale as well as enlarged artwork.
font_path='C:/Windows/Fonts/msyh.ttc'
font=lambda s:ImageFont.truetype(font_path,s)
preview=Image.new('RGB',(1400,640),'#101114')
d=ImageDraw.Draw(preview)
d.text((38,26),'Lume · 统一 UI 图标 / v1',font=font(26),fill='#EEF2F8')
d.text((38,72),'圆润面形 · 蓝灰默认态 · 浅蓝选中态 · 24 px 导航尺寸',font=font(16),fill='#91A0B2')
for index,(name,label) in enumerate(zip(NAMES,LABELS)):
    x=40+index*190
    for state,y in [('default',135),('selected',310)]:
        art=Image.open(out/f'{name}-{state}-256.png')
        preview.paste(art.resize((104,104),Image.Resampling.LANCZOS),(x+28,y),art.resize((104,104),Image.Resampling.LANCZOS))
        d.text((x+80,y+115),label,anchor='mt',font=font(16),fill='#B9C4D0')
    d.rounded_rectangle((x+34,497,x+78,541),radius=10,fill='#20252E')
    art=Image.open(out/f'{name}-default-24.png')
    preview.paste(art,(x+44,507),art)
    d.rounded_rectangle((x+92,497,x+136,541),radius=10,fill='#26374F')
    art=Image.open(out/f'{name}-selected-24.png')
    preview.paste(art,(x+102,507),art)
d.text((38,590),'上：默认态放大预览     中：选中态放大预览     下：24 px 实际显示尺寸（44 px 点击区）',font=font(16),fill='#91A0B2')
Path(args.preview).parent.mkdir(parents=True,exist_ok=True)
preview.save(args.preview)
print(json.dumps({'icons':len(records),'pngFiles':len(list(out.glob('*.png'))),
    'artBoard':[left,top,right,bottom],'preview':args.preview},ensure_ascii=False))
