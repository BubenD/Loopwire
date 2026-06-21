"""Generate resources/edraw.ico — the Edraw logo: a red square frame with a
red "E" inside (transparent background).

The glyph is drawn geometrically (no font dependency) so it stays crisp at
every icon size. Run:  python resources/make_icon.py
"""
import os
from PIL import Image, ImageDraw

RED = (192, 57, 43, 255)   # #c0392b
MASTER = 1024


def render(size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Outer red frame (outline only, transparent interior).
    m = size * 0.10                      # outer padding
    radius = size * 0.10
    stroke = max(1, round(size * 0.075))
    d.rounded_rectangle([m, m, size - 1 - m, size - 1 - m],
                        radius=radius, outline=RED, width=stroke)

    # Red "E" inside the frame: one stem + three bars.
    left, right = size * 0.33, size * 0.67
    top, bottom = size * 0.31, size * 0.69
    stem = size * 0.085
    bar = size * 0.085
    midy = (top + bottom) / 2.0
    d.rectangle([left, top, left + stem, bottom], fill=RED)                    # stem
    d.rectangle([left, top, right, top + bar], fill=RED)                       # top bar
    d.rectangle([left, midy - bar / 2, right * 0.97, midy + bar / 2], fill=RED)  # middle bar
    d.rectangle([left, bottom - bar, right, bottom], fill=RED)                 # bottom bar
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
