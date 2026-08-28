"""Generate every platform's app icon from assets/hyperwave.svg.

Produces:
  resources/hyperwave.icns   macOS bundle icon
  resources/hyperwave.ico    Windows executable + installer icon
  resources/app_icon.png     the 1024px master, for reference and for anyone
                             who wants to hand-edit rather than regenerate

Everything is derived from the one SVG plus the constants below, so the shape
and the colour stay in step across platforms. Replacing the artwork means
replacing the SVG (or MASTER_OVERRIDE) and re-running this; nothing downstream
needs to know.

The Windows tray icon is NOT generated here. That one is drawn at runtime from
the same SVG by WinBarPresenter, because a notification-area icon is asked for
at 16-24 px and wants the flat mark rather than a downscaled illustration.

Run from the repo root:
    python3 scripts/gen_icons.py
"""
import io
import os
import shutil
import subprocess
import sys

try:
    import cairosvg
    from PIL import Image, ImageDraw
except ImportError as exc:  # pragma: no cover - a setup problem, not a bug
    sys.exit(f"missing dependency: {exc}. pip install cairosvg pillow")

SVG = "assets/hyperwave.svg"
OUT_DIR = "resources"
MASTER = os.path.join(OUT_DIR, "app_icon.png")

# A hand-drawn master wins if one is present, so the generated art can be
# replaced by a designer's without editing this file.
MASTER_OVERRIDE = os.path.join(OUT_DIR, "app_icon_master.png")

SIZE = 1024

# The squircle is inset rather than filling the canvas. macOS composites its own
# shadow in that margin, and an icon drawn edge to edge reads as larger than
# every other icon in the Dock and the Finder.
INSET = 100

# The signature gradient, the same two colours RenderStyle starts with. An app
# whose icon is violet-to-cyan and whose default look is violet-to-cyan is one
# product rather than two.
TOP = (0x22, 0xD3, 0xEE)      # cyan
BOTTOM = (0x7C, 0x3A, 0xED)   # violet

# How much of the squircle's width the wave spans.
MARK_SCALE = 0.66


def squircle_mask(size, radius_exponent=5.0):
    """An Apple-style superellipse: |x|^n + |y|^n = 1, n about 5.

    Not a rounded rectangle. The difference is visible at icon sizes -- a
    rounded rect's corners meet the straight edges at a discontinuity, and next
    to real macOS icons it looks subtly wrong without being obviously so.
    """
    mask = Image.new("L", (size, size), 0)
    pixels = mask.load()
    half = size / 2.0
    for y in range(size):
        ny = (y + 0.5 - half) / half
        for x in range(size):
            nx = (x + 0.5 - half) / half
            if abs(nx) ** radius_exponent + abs(ny) ** radius_exponent <= 1.0:
                pixels[x, y] = 255
    return mask


def vertical_gradient(size, top, bottom):
    grad = Image.new("RGB", (1, size))
    draw = ImageDraw.Draw(grad)
    for y in range(size):
        t = y / max(1, size - 1)
        draw.point((0, y), fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
    return grad.resize((size, size), Image.BICUBIC)


def render_mark(width):
    """The SVG, rasterised white on transparent."""
    png = cairosvg.svg2png(url=SVG, output_width=width, output_height=width)
    mark = Image.open(io.BytesIO(png)).convert("RGBA")
    # The source path is black; recolour to white while keeping its alpha.
    white = Image.new("RGBA", mark.size, (255, 255, 255, 0))
    white.putalpha(mark.getchannel("A"))
    return white


def build_master():
    if os.path.exists(MASTER_OVERRIDE):
        print(f"using {MASTER_OVERRIDE}")
        return Image.open(MASTER_OVERRIDE).convert("RGBA").resize((SIZE, SIZE), Image.LANCZOS)

    body = SIZE - 2 * INSET
    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))

    tile = vertical_gradient(body, TOP, BOTTOM).convert("RGBA")
    tile.putalpha(squircle_mask(body))

    mark_w = int(body * MARK_SCALE)
    mark = render_mark(mark_w)
    tile.alpha_composite(mark, ((body - mark_w) // 2, (body - mark.height) // 2))

    canvas.alpha_composite(tile, (INSET, INSET))
    return canvas


def write_icns(master):
    iconset = os.path.join(OUT_DIR, "hyperwave.iconset")
    shutil.rmtree(iconset, ignore_errors=True)
    os.makedirs(iconset)
    # The exact set iconutil expects. Missing entries are not an error and not a
    # warning either -- they simply produce an icon that goes blurry at one size.
    for px in (16, 32, 64, 128, 256, 512, 1024):
        master.resize((px, px), Image.LANCZOS).save(
            os.path.join(iconset, f"icon_{px}x{px}.png"))
        if px >= 32:
            master.resize((px, px), Image.LANCZOS).save(
                os.path.join(iconset, f"icon_{px // 2}x{px // 2}@2x.png"))

    icns = os.path.join(OUT_DIR, "hyperwave.icns")
    subprocess.run(["iconutil", "-c", "icns", iconset, "-o", icns], check=True)
    shutil.rmtree(iconset, ignore_errors=True)
    return icns


def write_ico(master):
    ico = os.path.join(OUT_DIR, "hyperwave.ico")
    sizes = [(s, s) for s in (16, 24, 32, 48, 64, 128, 256)]
    master.save(ico, format="ICO", sizes=sizes)
    return ico


def main():
    if not os.path.exists(SVG):
        sys.exit(f"no artwork at {SVG}")
    os.makedirs(OUT_DIR, exist_ok=True)

    master = build_master()
    master.save(MASTER)
    print(f"wrote {MASTER}")

    if sys.platform == "darwin":
        print(f"wrote {write_icns(master)}")
    else:
        print("skipping .icns: iconutil is macOS only")
    print(f"wrote {write_ico(master)}")


if __name__ == "__main__":
    main()
