#!/usr/bin/env python3
"""Builds an AmigaOS tool icon (.info) from a picture.

The icon has two images:
  * a classic 4-colour planar image (Workbench palette: grey, black, white,
    blue) for systems without colour icons;
  * a GlowIcon (OS 3.5+, also AmigaOS 3.2): a FORM ICON chunk with up to 256
    colours, RLE-compressed as icon.library expects.

Usage: make_info_icon.py <picture> <output.info> [--size 48] [--colours 64]
                         [--stack 16777216] [--crop x0,y0,x1,y1]
Needs Pillow.
"""
import argparse
import struct
import sys

from PIL import Image

# Pillow before 9.1 has no Resampling/Quantize/Dither enums.
LANCZOS = getattr(Image, 'Resampling', Image).LANCZOS
NEAREST = getattr(Image, 'Resampling', Image).NEAREST
MEDIANCUT = getattr(Image, 'Quantize', Image).MEDIANCUT
NO_DITHER = getattr(Image, 'Dither', Image).NONE

WB_PALETTE = [(0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB)]
NO_ICON_POSITION = 0x80000000
WBTOOL = 3


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.bits = 0

    def put(self, value, width):
        for shift in range(width - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> shift) & 1)
            self.bits += 1
            if self.bits == 8:
                self.out.append(self.acc)
                self.acc = 0
                self.bits = 0

    def finish(self):
        if self.bits:
            self.out.append(self.acc << (8 - self.bits))
            self.acc = 0
            self.bits = 0
        return bytes(self.out)


def rle(values, depth):
    """GlowIcon RLE: ByteRun1 over a bit stream; an 8-bit control n < 128
    copies the next n+1 values, n > 128 repeats the next value 257-n times;
    each value is `depth` bits."""
    w = BitWriter()
    i = 0
    n = len(values)
    while i < n:
        run = 1
        while i + run < n and run < 128 and values[i + run] == values[i]:
            run += 1
        if run >= 3:
            w.put(257 - run, 8)
            w.put(values[i], depth)
            i += run
            continue
        start = i
        while i < n and i - start < 128:
            if i + 2 < n and values[i] == values[i + 1] == values[i + 2]:
                break
            i += 1
        w.put(i - start - 1, 8)
        for v in values[start:i]:
            w.put(v, depth)
    return w.finish()


def chunk(tag, data):
    body = struct.pack('>4sI', tag, len(data)) + data
    if len(data) & 1:
        body += b'\0'
    return body


def glow_icon(img, colours):
    q = img.convert('RGB').quantize(colors=colours, method=MEDIANCUT,
                                    dither=NO_DITHER)
    used = sorted(set(q.getdata()))
    remap = {old: new for new, old in enumerate(used)}
    pixels = [remap[p] for p in q.getdata()]
    pal = q.getpalette()[:3 * 256]
    palette = []
    for old in used:
        palette.extend(pal[3 * old:3 * old + 3])
    count = len(used)
    depth = max(1, (count - 1).bit_length())
    image_data = rle(pixels, depth)
    palette_data = rle(palette, 8)
    width, height = img.size
    face = struct.pack('>BBBBH', width - 1, height - 1, 0, 0x11, len(palette) - 1)
    imag = struct.pack('>BBBBBBHH', 0, count - 1, 0x02, 1, 1, depth,
                       len(image_data) - 1, len(palette_data) - 1) + image_data + palette_data
    body = b'ICON' + chunk(b'FACE', face) + chunk(b'IMAG', imag)
    return struct.pack('>4sI', b'FORM', len(body)) + body, count, depth


def classic_planes(img):
    """Two bitplanes in the Workbench palette: bright parts white, dark or
    strongly coloured parts black, the rest blue."""
    rgb = img.convert('RGB')
    width, height = rgb.size
    row_bytes = ((width + 15) // 16) * 2
    planes = [bytearray(row_bytes * height), bytearray(row_bytes * height)]
    for y in range(height):
        for x in range(width):
            r, g, b = rgb.getpixel((x, y))
            lum = (r * 299 + g * 587 + b * 114) // 1000
            if lum > 190:
                pen = 2
            elif lum < 70 or (r > 180 and g < 140):
                pen = 1
            else:
                pen = 3
            for p in range(2):
                if pen & (1 << p):
                    planes[p][y * row_bytes + x // 8] |= 0x80 >> (x % 8)
    return bytes(planes[0] + planes[1])


def disk_object(width, height, stack):
    gadget = struct.pack('>IhhhhHHHIIIIIHI',
                         0, 0, 0, width, height,
                         0x0004,          # GFLG_GADGIMAGE, complement highlight
                         0x0003,          # RELVERIFY | GADGIMMEDIATE
                         0x0001,          # BOOLGADGET
                         1, 0, 0, 0, 0, 0,  # GadgetRender present
                         1)               # UserData: WB_DISKREVISION
    tail = struct.pack('>BBIIIIIII', WBTOOL, 0, 0, 0, NO_ICON_POSITION, NO_ICON_POSITION, 0, 0, stack)
    return struct.pack('>HH', 0xE310, 1) + gadget + tail


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('picture')
    ap.add_argument('output')
    ap.add_argument('--size', type=int, default=48)
    ap.add_argument('--colours', type=int, default=64)
    ap.add_argument('--stack', type=int, default=16777216)
    ap.add_argument('--crop', help='x0,y0,x1,y1 of the picture to use')
    ap.add_argument('--preview', help='also write the colour image as a PNG')
    args = ap.parse_args()
    if not 2 <= args.colours <= 256 or not 1 <= args.size <= 256:
        sys.exit('colours must be 2..256 and size 1..256')

    img = Image.open(args.picture).convert('RGB')
    if args.crop:
        img = img.crop(tuple(int(v) for v in args.crop.split(',')))
    img = img.resize((args.size, args.size), LANCZOS)

    planes = classic_planes(img)
    image_struct = struct.pack('>hhhhhIBBI', 0, 0, args.size, args.size, 2, 1, 0x03, 0x00, 0)
    form, count, depth = glow_icon(img, args.colours)
    data = disk_object(args.size, args.size, args.stack) + image_struct + planes + form
    with open(args.output, 'wb') as f:
        f.write(data)
    if args.preview:
        img.quantize(colors=args.colours, method=MEDIANCUT,
                     dither=NO_DITHER).convert('RGB').resize(
            (args.size * 4, args.size * 4), NEAREST).save(args.preview)
    print('%s: %d bytes, %dx%d, GlowIcon %d colours (depth %d), stack %d'
          % (args.output, len(data), args.size, args.size, count, depth, args.stack))


if __name__ == '__main__':
    main()
