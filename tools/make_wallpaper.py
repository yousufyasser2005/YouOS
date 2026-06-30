from PIL import Image, ImageOps
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else None
DST = "wall.bmp"

if SRC:
    im = Image.open(SRC).convert("RGB")
else:
    im = Image.new("RGB", (1024, 768), (20, 30, 50))

im = ImageOps.fit(im, (1024, 768), Image.LANCZOS)
im.save(DST, "BMP")
print("Wrote", DST, im.size, im.mode)
