#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 assets/ic256.png (24 位白底) 重建成带 alpha 的圆角图标并打包 .ico

源图是 RGB 白底截图，四角是白色不是透明 —— 直接塞进 ico 后在桌面就成了
白边方角。这里按圆角几何 + 观测像素反解覆盖率（alpha），把白色背景扣掉，
再按预乘 alpha 面积平均缩小，得到各尺寸的干净图标。
"""
import os
import struct
import sys
import zlib

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(ROOT), "dict-app", "assets", "ic256.png")
OUT = os.path.join(ROOT, "res", "app.ico")
RADIUS_PCT = 0.2266      # 源图圆角约 58/256（拟合出来的）


# ---------------------------------------------------------------- PNG 读写
def decode_png(d):
    pos, idat = 8, b""
    w = h = bd = ct = 0
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", data[:10])
        elif typ == b"IDAT":
            idat += data
        elif typ == b"IEND":
            break
    raw = zlib.decompress(idat)
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    stride = w * bpp
    out = bytearray(w * h * bpp)
    prev = bytearray(stride)
    i = 0
    for y in range(h):
        ft = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        if ft == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif ft == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif ft == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif ft == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line

    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        s, o = i * bpp, i * 4
        if bpp >= 3:
            rgba[o:o + 3] = out[s:s + 3]
        else:
            rgba[o] = rgba[o + 1] = rgba[o + 2] = out[s]
        rgba[o + 3] = out[s + 3] if bpp == 4 else 255
    return w, h, rgba


def encode_png(w, h, rgba):
    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4])
                   for y in range(h))

    def chunk(t, d):
        return (struct.pack(">I", len(d)) + t + d
                + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


# ------------------------------------------------------------ 圆角透明化
def rrect_cov(x, y, cx, cy, hw, hh, rad):
    qx = abs(x - cx) - (hw - rad)
    qy = abs(y - cy) - (hh - rad)
    qx = 0.0 if qx < 0 else qx
    qy = 0.0 if qy < 0 else qy
    d = (qx * qx + qy * qy) ** 0.5 - rad
    c = 0.5 - d
    return 0.0 if c < 0 else (1.0 if c > 1 else c)


def round_alpha(w, h, rgba, rad):
    """扣掉白底：内部像素保原色，边界像素的 alpha 由观测色相对白底的偏离反解

    几何只用来找"可信内部"（取最近实色当基色），alpha 全由像素自己说，
    所以圆角半径估得有偏差也不会漏出白边。
    """
    cx, cy = w * 0.5, h * 0.5
    hw, hh = w * 0.5, h * 0.5
    cov = [0.0] * (w * h)
    for y in range(h):
        for x in range(w):
            cov[y * w + x] = rrect_cov(x + 0.5, y + 0.5, cx, cy, hw, hh, rad)

    solid = {}
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if cov[i] >= 0.999:
                o = i * 4
                solid[i] = (rgba[o], rgba[o + 1], rgba[o + 2])

    def nearest_solid(x, y):
        for r in range(1, 12):
            best, bd = None, 1e9
            for yy in range(max(0, y - r), min(h, y + r + 1)):
                for xx in range(max(0, x - r), min(w, x + r + 1)):
                    s = solid.get(yy * w + xx)
                    if s is None:
                        continue
                    d = (xx - x) ** 2 + (yy - y) ** 2
                    if d < bd:
                        bd, best = d, s
            if best:
                return best
        return (30, 33, 38)

    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            i = y * w + x
            o = i * 4
            c = cov[i]
            if c <= 0.0:
                continue
            r_, g_, b_ = rgba[o], rgba[o + 1], rgba[o + 2]
            if c >= 0.999:
                a = 1.0
            else:
                ir, ig, ib = nearest_solid(x, y)
                acc, n = 0.0, 0
                for v, base in ((r_, ir), (g_, ig), (b_, ib)):
                    den = 255.0 - base
                    if den > 8.0:
                        acc += (255.0 - v) / den
                        n += 1
                a = acc / n if n else c
                if a < 0.0:
                    a = 0.0
                if a > 1.0:
                    a = 1.0
                r_, g_, b_ = ir, ig, ib     # 边界色取内侧实色，避免残留白边
            if a <= 0.012:
                continue
            out[o] = r_
            out[o + 1] = g_
            out[o + 2] = b_
            out[o + 3] = int(a * 255 + 0.5)
    return out


# ------------------------------------------------------------ 缩放 / 打包
def resize_premul(rgba, w, h, nw, nh):
    """预乘 alpha 的面积平均，避免透明边缘混进黑边"""
    pre = [0] * (w * h * 4)
    for i in range(w * h):
        o = i * 4
        a = rgba[o + 3]
        pre[o] = rgba[o] * a
        pre[o + 1] = rgba[o + 1] * a
        pre[o + 2] = rgba[o + 2] * a
        pre[o + 3] = a

    out = bytearray(nw * nh * 4)
    sx, sy = w / nw, h / nh
    for y in range(nh):
        fy0, fy1 = y * sy, (y + 1) * sy
        y0, y1 = int(fy0), min(h - 1, int(fy1 - 1e-9))
        for x in range(nw):
            fx0, fx1 = x * sx, (x + 1) * sx
            x0, x1 = int(fx0), min(w - 1, int(fx1 - 1e-9))
            r = g = b = a = wsum = 0.0
            for yy in range(y0, y1 + 1):
                wy = min(fy1, yy + 1) - max(fy0, yy)
                if wy <= 0:
                    continue
                for xx in range(x0, x1 + 1):
                    wx = min(fx1, xx + 1) - max(fx0, xx)
                    if wx <= 0:
                        continue
                    wt = wx * wy
                    o = (yy * w + xx) * 4
                    r += pre[o] * wt
                    g += pre[o + 1] * wt
                    b += pre[o + 2] * wt
                    a += pre[o + 3] * wt
                    wsum += wt
            o = (y * nw + x) * 4
            if wsum <= 0 or a <= 0:
                continue
            ra = a / wsum
            out[o] = min(255, int(r / a + 0.5))
            out[o + 1] = min(255, int(g / a + 0.5))
            out[o + 2] = min(255, int(b / a + 0.5))
            out[o + 3] = min(255, int(ra + 0.5))
    return out


def build_ico(images):
    n = len(images)
    header = struct.pack("<HHH", 0, 1, n)
    offset = 6 + 16 * n
    entries, data = b"", b""
    for size, png in images:
        s = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", s, s, 0, 0, 1, 32, len(png),
                               offset + len(data))
        data += png
    return header + entries + data


def main():
    w, h, pix = decode_png(open(SRC, "rb").read())
    print("source %dx%d" % (w, h))
    rad = w * RADIUS_PCT
    rgba = round_alpha(w, h, pix, rad)

    with open(os.path.join(ROOT, "res", "icon256.png"), "wb") as f:
        f.write(encode_png(w, h, rgba))

    imgs = []
    for size in (256, 128, 96, 64, 48, 40, 32, 24, 20, 16):
        buf = rgba if size == w else resize_premul(rgba, w, h, size, size)
        imgs.append((size, encode_png(size, size, buf)))
    ico = build_ico(imgs)
    with open(OUT, "wb") as f:
        f.write(ico)
    print("wrote", OUT, len(ico), "bytes")


if __name__ == "__main__":
    main()
