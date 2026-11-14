"""Normalize the generated two-symbol extension without redrawing the artwork."""
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "output/imagegen"
ASSETS = ROOT / "native-rebuild/ui/assets/icons/lume-rounded-v1"
NAMES = ("launchpad", "control-center")
LABELS = ("启动台", "控制中心")
source = Image.open(ART / "lume-launchpad-control-center-v1-generated.png").convert("RGB")
alpha = Image.open(ART / "lume-launchpad-control-center-v1-alpha.png").convert("RGBA")
assert source.size == alpha.size
mask = Image.new("L", source.size)
mask.putdata([255 if g > 180 and g-r > 70 and g-b > 70 else 0 for r, g, b in source.getdata()])
board_box = mask.getbbox()
if not board_box:
    raise ValueError("Missing green art board")
board = alpha.crop(board_box)
records = []
for index, (name, label) in enumerate(zip(NAMES, LABELS)):
    cell = (round(index*board.width/2), 0, round((index+1)*board.width/2), board.height)
    tile = board.crop(cell)
    bounds = tile.getchannel("A").point(lambda a: 255 if a > 32 else 0).getbbox()
    if not bounds:
        raise ValueError(f"Missing icon: {name}")
    tile = tile.crop(bounds)
    ratio = 208 / max(tile.size)
    tile = tile.resize((round(tile.width*ratio), round(tile.height*ratio)), Image.Resampling.LANCZOS)
    master = Image.new("RGBA", (256, 256))
    master.alpha_composite(tile, ((256-tile.width)//2, (256-tile.height)//2))
    master.putdata([(r, min(g, max(r, b)), b, a) if g > max(r, b)+8 else (r, g, b, a)
                    for r, g, b, a in master.getdata()])
    selected = ImageOps.colorize(ImageOps.grayscale(master), "#386DAF", "#B6D8FF").convert("RGBA")
    selected.putalpha(master.getchannel("A"))
    for state, image in (("default", master), ("selected", selected)):
        for size in (24, 48, 72, 256):
            image.resize((size, size), Image.Resampling.LANCZOS).save(ASSETS / f"{name}-{state}-{size}.png", optimize=True)
    records.append({"id": name, "label": label, "master": f"{name}-default-256.png",
                    "selected": f"{name}-selected-256.png", "sourceBoard": list(board_box),
                    "sourceCell": list(cell), "sourceBox": list(bounds)})

manifest_path = ASSETS / "manifest.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
manifest["icons"] = [entry for entry in manifest["icons"] if entry["id"] not in NAMES] + records
manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

font = lambda size: ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", size)
preview = Image.new("RGB", (1120, 700), "#101114")
draw = ImageDraw.Draw(preview)
draw.text((36, 24), "Lume · 启动台与控制中心图标", font=font(26), fill="#EEF2F8")
draw.text((36, 69), "圆润柔和面形 · 沿用蓝灰配色和左上高光 · 透明 PNG", font=font(16), fill="#91A0B2")
draw.text((36, 124), "现有图标参考", font=font(16), fill="#91A0B2")
for index, (name, label) in enumerate((("settings", "设置"), ("clock", "时钟"), ("quick-launch", "快捷启动"))):
    x = 42 + index*150
    image = Image.open(ASSETS / f"{name}-default-256.png").resize((96, 96), Image.Resampling.LANCZOS)
    preview.paste(image, (x, 179), image)
    draw.text((x+48, 286), label, anchor="mt", font=font(16), fill="#B9C4D0")
draw.line((500, 126, 500, 470), fill="#29303A", width=1)
for index, (name, label) in enumerate(zip(NAMES, LABELS)):
    x = 560 + index*265
    image = Image.open(ASSETS / f"{name}-default-256.png").resize((144, 144), Image.Resampling.LANCZOS)
    preview.paste(image, (x+30, 147), image)
    draw.text((x+102, 310), label, anchor="mt", font=font(20), fill="#EEF2F8")
    draw.text((x+102, 350), "默认态", anchor="mt", font=font(14), fill="#91A0B2")
    image = Image.open(ASSETS / f"{name}-selected-256.png").resize((88, 88), Image.Resampling.LANCZOS)
    preview.paste(image, (x+58, 378), image)
draw.text((36, 510), "24 px 实际显示尺寸 · 黑底 / 灰底 / 白底", font=font(16), fill="#91A0B2")
for index, name in enumerate(NAMES):
    image = Image.open(ASSETS / f"{name}-default-24.png")
    for column, background in enumerate(("#101114", "#343B46", "#FFFFFF")):
        x = 42 + index*530 + column*134
        draw.rounded_rectangle((x, 552, x+104, 616), radius=12, fill=background)
        preview.paste(image, (x+40, 572), image)
draw.text((36, 652), "每枚含默认 / 选中两态及 24、48、72、256 px；选中态保持相同轮廓与透明度。", font=font(15), fill="#91A0B2")
preview_path = ROOT / "docs/current/assets/lume-ui-icons-v1-dock-extension.png"
preview.save(preview_path, optimize=True)

# Validate the actual deliverables, including alpha invariance and key cleanup.
for name in NAMES:
    for size in (24, 48, 72, 256):
        normal = Image.open(ASSETS / f"{name}-default-{size}.png").convert("RGBA")
        selected = Image.open(ASSETS / f"{name}-selected-{size}.png").convert("RGBA")
        assert normal.size == selected.size == (size, size)
        assert normal.getchannel("A").tobytes() == selected.getchannel("A").tobytes()
        assert normal.getpixel((0, 0))[3] == 0
        assert not any(a > 32 and g > max(r, b)+12 for r, g, b, a in normal.getdata())
print(json.dumps({"icons": 2, "pngFiles": 16, "alphaAndSpillChecks": "pass",
                  "preview": str(preview_path)}, ensure_ascii=False))
