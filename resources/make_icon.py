"""Generate resources/edraw.ico — the Edraw logo: a solid red rounded square
with a white "E" inside (transparent outside the square).

The glyph is drawn geometrically (no font dependency) so it stays crisp at
every icon size. Run:  python resources/make_icon.py
"""
import os
from PIL import Image, ImageDraw

RED = (224, 49, 39, 255)     # bright red square (#e03127)
EDGE = (150, 30, 22, 255)    # subtle darker red border (#961e16)
WHITE = (245, 246, 248, 255) # the "E"
MASTER = 1024


def render(size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Solid red rounded square with a thin darker edge.
    m = size * 0.06                      # outer padding
    radius = size * 0.16
    border = max(1, round(size * 0.025))
    d.rounded_rectangle([m, m, size - 1 - m, size - 1 - m], radius=radius,
                        fill=RED, outline=EDGE, width=border)

    # White "E": one stem + three bars.
    left, right = size * 0.34, size * 0.69
    top, bottom = size * 0.30, size * 0.70
    stem = size * 0.10
    bar = size * 0.10
    midy = (top + bottom) / 2.0
    d.rectangle([left, top, left + stem, bottom], fill=WHITE)                     # stem
    d.rectangle([left, top, right, top + bar], fill=WHITE)                        # top bar
    d.rectangle([left, midy - bar / 2, right * 0.96, midy + bar / 2], fill=WHITE) # middle bar
    d.rectangle([left, bottom - bar, right, bottom], fill=WHITE)                  # bottom bar
    return img


def main():
    # Render crisply at 256 then let PIL embed all the standard sizes.
    master = render(256)
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    out = os.path.join(os.path.dirname(__file__), "edraw.ico")
    master.save(out, format="ICO", sizes=sizes)
    print("wrote", out)


if __name__ == "__main__":
    main()
