from PIL import Image

SRC = "user/assets/start.png"
DST = "user/src/start_icon.h"
SIZE = 48

im = Image.open(SRC).convert("RGBA")
im = im.resize((SIZE, SIZE), Image.LANCZOS)
pixels = list(im.getdata())

with open(DST, "w") as f:
    print("/* Auto-generated from start.png, regenerate via tools/convert_start_icon.py */", file=f)
    print("#define START_ICON_W", SIZE, file=f)
    print("#define START_ICON_H", SIZE, file=f)
    print("static const unsigned char start_icon_rgba[] = {", file=f)
    for row_start in range(0, len(pixels), SIZE):
        row = pixels[row_start:row_start + SIZE]
        values = []
        for p in row:
            for c in p:
                values.append(str(c))
        print("    " + ",".join(values) + ",", file=f)
    print("};", file=f)

print("Wrote", DST, "-", SIZE, "x", SIZE, "RGBA")
