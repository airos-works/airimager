#!/usr/bin/env python3
"""Generate airImager's vector icons (HVIF) straight from code.

Usage: make-icon.py [--preview] OUTPUT_DIR

Writes three icons to OUTPUT_DIR:
  airImager.hvif  the application: a USB stick with a green arrow writing
                  into it (also the Write step's picture)
  image.hvif      the Image step: a disc image, a document with a disc on it
  drive.hvif      the Drive step: a USB stick on its own
With --preview, a 256 px PNG of each goes next to it (needs Pillow).
Light comes from the upper left, as in Haiku's icon style; detail shapes are
left out at small sizes.
"""
from pathlib import Path
import sys

import hvif

C = hvif.hex_color
K = 0.55228475  # cubic Bezier circle constant


class Icon:
    def __init__(self):
        self.styles, self.paths, self.shapes = [], [], []

    def solid(self, color, alpha=255):
        self.styles.append({'color': C(color, alpha)})
        return len(self.styles) - 1

    def linear(self, start, end, colors):
        self.styles.append(hvif.linear_gradient(start, end,
            [(i / (len(colors) - 1), C(color)) for i, color in enumerate(colors)]))
        return len(self.styles) - 1

    def radial(self, center, radius, stops):
        self.styles.append(hvif.radial_gradient(center, radius,
            [(offset, C(color)) for offset, color in stops]))
        return len(self.styles) - 1

    def shape(self, style, points, detail=False):
        self.paths.append({'closed': True, 'points': points})
        item = {'style': style, 'paths': [len(self.paths) - 1]}
        if detail:
            item['lod'] = (0.5, 4.0)
        self.shapes.append(item)

    def ellipse(self, style, cx, cy, rx, ry, detail=False):
        self.shape(style, [
            ((cx + rx, cy), (cx + rx, cy - K * ry), (cx + rx, cy + K * ry)),
            ((cx, cy + ry), (cx + K * rx, cy + ry), (cx - K * rx, cy + ry)),
            ((cx - rx, cy), (cx - rx, cy + K * ry), (cx - rx, cy - K * ry)),
            ((cx, cy - ry), (cx - K * rx, cy - ry), (cx + K * rx, cy - ry)),
        ], detail)

    def rounded(self, style, left, top, right, bottom, r, detail=False):
        k = r * (1 - K)
        self.shape(style, [
            ((left + r, top), (left + k, top), (left + r, top)),
            ((right - r, top), (right - r, top), (right - k, top)),
            ((right, top + r), (right, top + k), (right, top + r)),
            ((right, bottom - r), (right, bottom - r), (right, bottom - k)),
            ((right - r, bottom), (right - k, bottom), (right - r, bottom)),
            ((left + r, bottom), (left + r, bottom), (left + k, bottom)),
            ((left, bottom - r), (left, bottom - k), (left, bottom - r)),
            ((left, top + r), (left, top + r), (left, top + k)),
        ], detail)

    def data(self):
        return {'styles': self.styles, 'paths': self.paths, 'shapes': self.shapes}


def usb_stick(icon, dx=0.0, dy=0.0, scale=1.0):
    """A USB stick standing upright: a metal plug on top, a blue body."""
    def p(x, y):
        return (dx + x * scale, dy + y * scale)

    def rect(style, l, t, r, b, radius, detail=False):
        (l, t), (r, b) = p(l, t), p(r, b)
        icon.rounded(style, l, t, r, b, radius * scale, detail)

    outline = icon.solid('1b2430')
    metal = icon.linear(p(20, 4), p(44, 4), ['dfe5ec', 'ffffff', 'a9b3bf', '7d8896'])
    hole = icon.solid('3a4552')
    body = icon.linear(p(14, 22), p(50, 60), ['5aa9ff', '2f6fd6', '1d4a9e'])
    body_light = icon.linear(p(14, 22), p(14, 34), ['8cc6ff', '3d7fe0'])
    label = icon.linear(p(22, 36), p(42, 52), ['f4f8fd', 'c9d6e6'])
    light = icon.solid('7dffb2')

    rect(outline, 20, 3, 44, 25, 2)
    rect(metal, 21.5, 4.5, 42.5, 24, 1.2)
    rect(hole, 25, 9, 30, 13, 0.6, detail=True)
    rect(hole, 34, 9, 39, 13, 0.6, detail=True)
    rect(outline, 13, 20, 51, 61, 6)
    rect(body, 14.5, 21.5, 49.5, 59.5, 4.8)
    rect(body_light, 14.5, 21.5, 49.5, 30, 4.8, detail=True)
    rect(label, 21, 34, 43, 52, 2.5, detail=True)
    cx, cy = p(44.5, 26.5)
    icon.ellipse(light, cx, cy, 1.6 * scale, 1.6 * scale, detail=True)


def arrow(icon, cx, top, bottom, half_width, head):
    outline = icon.solid('0f3b23')
    fill = icon.linear((cx, top), (cx, bottom), ['b6ffcf', '35d07f', '13924e'])
    stem = half_width * 0.48

    def points(grow):
        w, s = half_width + grow, stem + grow
        return [
            (cx - s, top - grow), (cx + s, top - grow),
            (cx + s, bottom - head), (cx + w, bottom - head),
            (cx, bottom + grow * 1.6),
            (cx - w, bottom - head), (cx - s, bottom - head),
        ]

    icon.shape(outline, points(1.5))
    icon.shape(fill, points(0))


def app_icon():
    icon = Icon()
    usb_stick(icon, dx=-4, dy=2, scale=0.95)
    arrow(icon, 47, 18, 47, 12, 11)
    return icon


def drive_icon():
    icon = Icon()
    usb_stick(icon)
    return icon


def image_icon():
    icon = Icon()
    outline = icon.solid('2b3440')
    page = icon.linear((10, 4), (50, 60), ['ffffff', 'e9eef4', 'cfd8e3'])
    fold = icon.linear((40, 4), (50, 14), ['f7f9fc', 'b7c3d1'])
    disc = icon.radial((32, 38), 16, [(0.0, 'f3f5f8'), (0.35, 'c6d0dc'),
                                      (0.6, 'a3e0ff'), (0.8, 'd6b8ff'),
                                      (1.0, '9aa7b8')])
    hub = icon.solid('ffffff')
    hole = icon.solid('2b3440')
    gleam = icon.solid('ffffff', 170)

    icon.shape(outline, [(8, 2), (42, 2), (54, 14), (54, 62), (8, 62)])
    icon.shape(page, [(10, 4), (41, 4), (52, 15), (52, 60), (10, 60)])
    icon.shape(fold, [(41, 4), (52, 15), (41, 15)], detail=True)
    icon.ellipse(outline, 31, 38, 17.5, 17.5)
    icon.ellipse(disc, 31, 38, 16, 16)
    icon.ellipse(hub, 31, 38, 5.5, 5.5)
    icon.ellipse(hole, 31, 38, 2.4, 2.4)
    icon.shape(gleam, [
        ((20, 33), (20, 33), (22, 28)),
        ((28, 24), (25, 25), (28, 24)),
        ((27, 27), (27, 27), (23, 30)),
    ], detail=True)
    return icon


def main():
    args = sys.argv[1:]
    preview = '--preview' in args
    args = [a for a in args if a != '--preview']
    out = Path(args[0]) if args else Path('resources')
    out.mkdir(parents=True, exist_ok=True)
    for name, make in (('airImager', app_icon), ('image', image_icon),
                       ('drive', drive_icon)):
        data = make().data()
        encoded = hvif.encode(data)
        hvif.decode(encoded)
        path = out / f'{name}.hvif'
        path.write_bytes(encoded)
        if preview:
            hvif.preview(data, 256).save(out / f'{name}.png')
        print(f'{path}: {len(encoded)} bytes')


if __name__ == '__main__':
    main()
