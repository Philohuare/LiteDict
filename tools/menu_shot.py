#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""打开右键面板并截屏，用于检查圆角 / 投影效果"""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_ico import encode_png

u = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

WM_RBUTTONDOWN = 0x0204
WM_RBUTTONUP = 0x0205
SWP_NOSIZE = 0x0001


def find_window():
    res = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, l):
        n = u.GetWindowTextLengthW(h)
        if n:
            b = ctypes.create_unicode_buffer(n + 1)
            u.GetWindowTextW(h, b, n + 1)
            if b.value == "LiteDict" and u.IsWindowVisible(h):
                res.append(h)
        return True

    u.EnumWindows(cb, 0)
    return res[0] if res else 0


def grab_screen(x, y, w, h):
    hdc = u.GetDC(0)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    gdi32.BitBlt(mem, 0, 0, w, h, hdc, x, y, 0x00CC0020 | 0x40000000)

    class BIH(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG),
                    ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                    ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
                    ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]

    bi = BIH()
    bi.biSize = ctypes.sizeof(BIH)
    bi.biWidth = w
    bi.biHeight = -h
    bi.biPlanes = 1
    bi.biBitCount = 32
    stride = w * 4
    buf = ctypes.create_string_buffer(stride * h)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    u.ReleaseDC(0, hdc)

    raw = buf.raw
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        s, o = i * 4, i * 4
        rgba[o] = raw[s + 2]
        rgba[o + 1] = raw[s + 1]
        rgba[o + 2] = raw[s]
        rgba[o + 3] = 255
    return rgba


def grab_window(h):
    r = wt.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    return r


def main():
    scale = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    out = sys.argv[2] if len(sys.argv) > 2 else "menu.png"

    h = find_window()
    if not h:
        print("start LiteDict.exe first")
        return 1
    u.ShowWindow(h, 9)
    u.SetWindowPos(h, -1, 80, 80, 0, 0, SWP_NOSIZE)
    u.SetForegroundWindow(h)
    time.sleep(0.5)

    # 右键在窗口中部靠右触发
    r = grab_window(h)
    px = r.left + 240
    py = r.top + 120
    u.SetCursorPos(px, py)
    time.sleep(0.15)
    u.PostMessageW(h, WM_RBUTTONDOWN, 2, ((py - r.top) << 16) | ((px - r.left) & 0xFFFF))
    u.PostMessageW(h, WM_RBUTTONUP, 0, ((py - r.top) << 16) | ((px - r.left) & 0xFFFF))
    time.sleep(0.7)

    # 截取光标附近区域
    gx, gy, gw, gh = px - 300, py - 60, 420, 300
    img = grab_screen(gx, gy, gw, gh)
    if scale > 1:
        img = zoom(img, gw, gh, scale)
        gw, gh = gw * scale, gh * scale
    with open(out, "wb") as f:
        f.write(encode_png(gw, gh, img))
    print("wrote", out, gw, gh)
    return 0


def zoom(img, w, h, k):
    out = bytearray(w * k * h * k * 4)
    for y in range(h * k):
        sy = y // k
        for x in range(w * k):
            s = (sy * w + x // k) * 4
            o = (y * w * k + x) * 4
            out[o:o + 4] = img[s:s + 4]
    return out


if __name__ == "__main__":
    sys.exit(main())
