#!/usr/bin/env python3
"""Generate airImager's vector icons (HVIF) from native artwork and code.

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


def arrow(icon, cx, top, bottom, half_width, head):
    """A flat green overlay stays distinct even in Tracker's 16 px icon."""
    outline = icon.solid('162c0c')
    fill = icon.linear((cx - half_width, top), (cx + half_width, bottom),
                       ['d5ed9c', '83b844', '4e8527'])
    light = icon.solid('ecf8cf')
    stem = half_width * 0.48

    def points(grow):
        w, s = half_width + grow, stem + grow
        return [
            (cx - s, top - grow), (cx + s, top - grow),
            (cx + s, bottom - head - grow), (cx + w, bottom - head - grow),
            (cx + w, bottom - head + 1), (cx, bottom + grow * 1.4),
            (cx - w, bottom - head + 1), (cx - w, bottom - head - grow),
            (cx - s, bottom - head - grow),
        ]

    icon.shape(outline, points(2))
    icon.shape(fill, points(0))
    icon.shape(light, [(cx - stem, top), (cx + stem, top),
                      (cx + stem, top + 1.5), (cx - stem, top + 1.5)], detail=True)


def app_icon():
    icon = drive_icon()
    arrow(icon, 48, 28, 58, 11, 11)
    return icon


def drive_icon():
    # The native airOS/Haiku USB drive, including its original shading and
    # transforms. Keep a local source so regeneration needs no Haiku checkout.
    source = Path(__file__).resolve().parent / 'artwork' / 'usb-drive.hvif'
    data = hvif.decode(source.read_bytes())
    icon = Icon()
    icon.styles, icon.paths, icon.shapes = (data['styles'], data['paths'],
                                          data['shapes'])
    return icon


def image_icon():
    icon = Icon()
    ink = icon.solid('161616')
    shadow = icon.solid('000000', 35)
    page = icon.linear((10, 6), (44, 54), ['ffffff', 'e1e1e1'])
    edge = icon.solid('999999')
    fold = icon.linear((35, 12), (44, 24), ['ffffff', 'bcbcbc'])
    disc = icon.linear((17, 24), (41, 49), ['fafafa', 'c8dce5', '809eaf'])
    hub = icon.solid('f3f5f6')
    band = icon.solid('ffffff', 150)

    icon.shape(shadow, [(8, 49), (46, 59), (61, 55), (22, 44)])
    icon.shape(ink, [(7, 3), (36, 10), (49, 25), (49, 59), (7, 49)])
    icon.shape(edge, [(46, 25), (47, 25), (47, 56), (9, 47), (9, 45)])
    icon.shape(page, [(9, 5.5), (34, 11.5), (46, 25), (46, 55), (9, 46)])
    icon.shape(edge, [(34, 12), (46, 25), (34, 22)])
    icon.shape(fold, [(36, 14), (44, 23), (36, 21)])
    # A disc on the page, with the same gentle slant as the paper.
    start = len(icon.paths)
    icon.ellipse(ink, 28, 34, 15, 15)
    icon.ellipse(disc, 28, 34, 13, 13)
    icon.shape(band, [(28, 34), (17, 27), (23, 22), (28, 34),
                      (39, 41), (33, 46)], detail=True)
    icon.ellipse(ink, 28, 34, 4.5, 4.5)
    icon.ellipse(hub, 28, 34, 3.3, 3.3)
    icon.ellipse(ink, 28, 34, 1.8, 1.8)
    for path in icon.paths[start:]:
        path['points'] = [tuple((x, y + (x - 28) * 0.24 + 1)
                               for x, y in hvif.normalize_point(point))
                          for point in path['points']]
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
