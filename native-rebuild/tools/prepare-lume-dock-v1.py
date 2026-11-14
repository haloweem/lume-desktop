"""Export the generated Dock symbol without redrawing its geometry."""
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps

root = Path(__file__).resolve().parents[2]
art = root / "output/imagegen"
assets = root / "native-rebuild/ui/assets/icons/lume-rounded-v1"
source = Image.open(art / "lume-dock-v1-generated.png").convert("RGB")
alpha = Image.open(art / "lume-dock-v1-alpha.png").convert("RGBA")
mask = Image.new("L", source.size)
mask.putdata([255 if g > 180 and g-r > 70 and g-b > 70 else 0 for r, g, b in source.getdata()])
board_box = mask.getbbox()
board = alpha.crop(board_box)
bounds = board.getchannel("A").point(lambda a: 255 if a > 32 else 0).getbbox()
assert bounds, "Missing generated Dock silhouette"
tile = board.crop(bounds)
ratio = 208/max(tile.size)
tile = tile.resize((round(tile.width*ratio), round(tile.height*ratio)), Image.Resampling.LANCZOS)
master = Image.new("RGBA", (256, 256))
master.alpha_composite(tile, ((256-tile.width)//2, (256-tile.height)//2))
master.putdata([(r,min(g,max(r,b)),b,a) if g>max(r,b)+8 else (r,g,b,a) for r,g,b,a in master.getdata()])
selected = ImageOps.colorize(ImageOps.grayscale(master), "#386DAF", "#B6D8FF").convert("RGBA")
selected.putalpha(master.getchannel("A"))
for state, image in (("default", master), ("selected", selected)):
    for size in (24, 48, 72, 256):
        image.resize((size, size), Image.Resampling.LANCZOS).save(assets/f"dock-{state}-{size}.png", optimize=True)
manifest_path = assets / "manifest.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
manifest["icons"] = [entry for entry in manifest["icons"] if entry["id"] != "dock"] + [
    {"id":"dock", "label":"Dock", "master":"dock-default-256.png", "selected":"dock-selected-256.png",
     "sourceBoard":list(board_box), "sourceBox":list(bounds)}]
manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
font = lambda size: ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", size)
preview = Image.new("RGB", (960, 570), "#121212")
d = ImageDraw.Draw(preview)
d.text((32, 22), "Lume · Dock 设置图标", font=font(26), fill="#F1F4F9")
d.text((32, 65), "底座与应用 · 九宫格 · 双滑杆", font=font(16), fill="#91A0B2")
for index, (name, label) in enumerate((("dock", "Dock"), ("launchpad", "启动台"), ("control-center", "控制中心"))):
    x = 52+index*304
    for state, y, size in (("default", 130, 144), ("selected", 335, 72)):
        image = Image.open(assets/f"{name}-{state}-256.png").resize((size,size), Image.Resampling.LANCZOS)
        preview.paste(image, (x+(176-size)//2,y), image)
    d.text((x+88, 293), label, anchor="mt", font=font(20), fill="#D8E1EB")
    for column, bg in enumerate(("#121212", "#343B46", "#FFFFFF")):
        at = x+column*62
        d.rounded_rectangle((at,445,at+48,493), radius=10, fill=bg)
        image = Image.open(assets/f"{name}-default-24.png")
        preview.paste(image, (at+12,457), image)
d.text((32, 530), "透明 PNG · 默认与选中同轮廓 · 24 / 48 / 72 / 256 px", font=font(16), fill="#91A0B2")
preview_path = root/"docs/current/assets/lume-ui-icons-v1-dock-settings.png"
preview.save(preview_path, optimize=True)
for size in (24,48,72,256):
    first=Image.open(assets/f"dock-default-{size}.png").convert("RGBA")
    second=Image.open(assets/f"dock-selected-{size}.png").convert("RGBA")
    assert first.getchannel("A").tobytes()==second.getchannel("A").tobytes()
    assert first.getpixel((0,0))[3]==0
    assert not any(a>32 and g>max(r,b)+12 for r,g,b,a in first.getdata())
print(json.dumps({"newPngs":8,"preview":str(preview_path),"checks":"pass"},ensure_ascii=False))
