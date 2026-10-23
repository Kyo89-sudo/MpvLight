"""Icona MpvLight: disco viola visto di fronte, bordo smussato e play bianco in rilievo.

Le misure grandi (40-256 px) hanno riflesso lucido, smussatura e ombre morbide; le piccole
(16-32 px) sono disegnate apposta a quella misura: piu' contrasto, play piu' grande, niente
effetti che a pochi pixel diventerebbero sbavature. Uso: python make_icon.py. Richiede Pillow.
"""
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFilter
import math

ROOT = Path(__file__).parent
TOP = (196, 125, 255)      # viola chiaro in alto a sinistra
BOTTOM = (91, 33, 182)     # viola profondo in basso a destra
RIM_DARK = (46, 16, 101)


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def gradient(size, c0, c1, angle_deg=58):
    n = 128
    small = Image.new('RGBA', (n, n))
    px = small.load()
    a = math.radians(angle_deg)
    dx, dy = math.cos(a), math.sin(a)
    lo = min(0, dx) + min(0, dy)
    hi = max(0, dx) + max(0, dy)
    for y in range(n):
        for x in range(n):
            t = ((x / (n - 1)) * dx + (y / (n - 1)) * dy - lo) / (hi - lo)
            px[x, y] = lerp(c0, c1, t) + (255,)
    return small.resize((size, size), Image.Resampling.BICUBIC)


def circle(size, box):
    m = Image.new('L', (size, size))
    ImageDraw.Draw(m).ellipse(box, fill=255)
    return m


def play_mask(size, pts, r):
    m = Image.new('L', (size, size))
    d = ImageDraw.Draw(m)
    cx = sum(p[0] for p in pts) / 3
    cy = sum(p[1] for p in pts) / 3
    ins = []
    for x, y in pts:
        dx, dy = cx - x, cy - y
        k = r * 2 / math.hypot(dx, dy)
        ins.append((x + dx * k, y + dy * k))
    d.polygon(ins, fill=255)
    d.line(ins + [ins[0]], fill=255, width=max(1, int(r * 2)), joint='curve')
    for x, y in ins:
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    return m


def layer(img, color, mask):
    l = Image.new('RGBA', img.size, color + (0,))
    l.putalpha(mask)
    return Image.alpha_composite(img, l)


def big(S=2048):
    k = S / 1024
    img = Image.new('RGBA', (S, S))
    disc_box = (70 * k, 62 * k, 954 * k, 946 * k)
    disc = circle(S, disc_box)
    # soft drop shadow under the disc
    sh = ImageChops.offset(disc.filter(ImageFilter.GaussianBlur(28 * k)), 0, int(22 * k))
    img = layer(img, (20, 6, 40), sh.point(lambda v: int(v * 0.45)))
    # rim (the disc's thickness, seen from the front as a darker ring)
    img.paste(gradient(S, (150, 90, 235), RIM_DARK, 90), (0, 0), disc)
    face_box = (104 * k, 92 * k, 920 * k, 900 * k)
    face = circle(S, face_box)
    img.paste(gradient(S, TOP, BOTTOM), (0, 0), face)
    # bevel: light on the upper-left edge of the face, shade on the lower-right
    blur = face.filter(ImageFilter.GaussianBlur(26 * k))
    d = int(18 * k)
    light = ImageChops.multiply(ImageChops.subtract(blur, ImageChops.offset(blur, d, d)).point(lambda v: min(255, v * 2)), face)
    shade = ImageChops.multiply(ImageChops.subtract(blur, ImageChops.offset(blur, -d, -d)).point(lambda v: min(255, v * 2)), face)
    img = layer(img, (255, 235, 255), light.point(lambda v: int(v * 0.55)))
    img = layer(img, (40, 8, 90), shade.point(lambda v: int(v * 0.55)))
    # play, optically centred (slightly right of centre)
    pts = [(408 * k, 300 * k), (408 * k, 704 * k), (758 * k, 502 * k)]
    tri = play_mask(S, pts, 46 * k)
    tsh = ImageChops.offset(tri.filter(ImageFilter.GaussianBlur(22 * k)), int(6 * k), int(24 * k))
    img = layer(img, (34, 6, 80), ImageChops.multiply(tsh, face).point(lambda v: int(v * 0.6)))
    # play thickness: a slightly darker copy just below
    img = layer(img, (214, 200, 240), ImageChops.offset(tri, 0, int(10 * k)))
    img.paste(gradient(S, (255, 255, 255), (238, 228, 255), 90), (0, 0), tri)
    # gloss: a soft lens of light on the upper half of the face
    gl = circle(S, (190 * k, 120 * k, 834 * k, 560 * k)).filter(ImageFilter.GaussianBlur(18 * k))
    fade = gradient(S, (255, 255, 255), (0, 0, 0), 90).getchannel('R')
    gl = ImageChops.multiply(ImageChops.multiply(gl, fade), face).point(lambda v: int(v * 0.30))
    img = layer(img, (255, 255, 255), gl)
    return img.resize((S // 2, S // 2), Image.Resampling.LANCZOS)


def small(n):
    """Drawn at 8x the target size, then reduced: crisp, high contrast, big play."""
    S = n * 8
    k = S / 1024
    img = Image.new('RGBA', (S, S))
    disc = circle(S, (24 * k, 24 * k, 1000 * k, 1000 * k))
    img.paste(gradient(S, (128, 70, 220), (40, 12, 92), 90), (0, 0), disc)
    face = circle(S, (80 * k, 70 * k, 944 * k, 930 * k))
    img.paste(gradient(S, (204, 138, 255), (104, 40, 200)), (0, 0), face)
    pts = [(380 * k, 250 * k), (380 * k, 760 * k), (812 * k, 505 * k)]
    tri = play_mask(S, pts, 52 * k)
    if n >= 24:
        img = layer(img, (40, 8, 90), ImageChops.offset(tri, int(8 * k), int(30 * k)).point(lambda v: int(v * 0.5)))
    img = layer(img, (255, 255, 255), tri)
    out = img.resize((n, n), Image.Resampling.LANCZOS)
    return out


def build():
    master = big()
    sizes = (16, 20, 24, 32, 40, 48, 64, 128, 256)
    frames = [small(n) if n <= 32 else master.resize((n, n), Image.Resampling.LANCZOS) for n in sizes]
    master.resize((256, 256), Image.Resampling.LANCZOS).save(ROOT / 'mpvlight.png')
    frames[-1].save(ROOT / 'mpvlight.ico', sizes=[f.size for f in frames], append_images=frames[:-1])
    return master, frames


if __name__ == '__main__':
    build()
