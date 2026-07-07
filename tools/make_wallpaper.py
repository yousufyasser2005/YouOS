from PIL import Image, ImageOps
import sys

# Usage: make_wallpaper.py [source_image] [width] [height]
# Width/height must match the OS's actual runtime resolution (check the
# boot log's "[OK] Framebuffer: WxHxBPP" line, or the About window in
# the desktop) since load_wallpaper_bmp() requires an exact match.
SRC = None
W, H = 1280, 800
args = sys.argv[1:]
if args and not args[0].isdigit():
    SRC = args[0]
    args = args[1:]
if len(args) >= 2:
    W, H = int(args[0]), int(args[1])

DST = "wall.bmp"

if SRC:
    im = Image.open(SRC).convert("RGB")
else:
    im = Image.new("RGB", (W, H), (20, 30, 50))

im = ImageOps.fit(im, (W, H), Image.LANCZOS)
im.save(DST, "BMP")
print("Wrote", DST, im.size, im.mode)
