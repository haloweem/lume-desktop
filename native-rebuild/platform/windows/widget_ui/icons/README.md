# Native screenshot and menu icons

These 23 transparent PNGs were cropped from the original icon concept sheets
generated at the user's request and selected by the user on 2026-10-12. The
accepted version has thin black strokes. The later hand-traced SVG and boldened
PNG variants were rejected and are not product assets. The concept images were
generated through the user-selected image service; no credential is stored here.

The PNG files are the editable source assets. `generate_png_masks.py` reads
their alpha channels and writes `../icon_art_data.h`. The compiled application
embeds this data and renders it through `icon_art.cpp`; it does not read asset
files or contact the image service at runtime. To verify the generated header:

```powershell
python generate_png_masks.py --check
```

The original concept sheets and visual comparison contact sheets live only in
the local 1.0.3 build-artifacts for this acceptance run. PNG sources and the
generated header are committed so future clean builds are reproducible.
