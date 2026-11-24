#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""窗口截图工具：抓取指定标题窗口（或全屏）保存为 BMP，零依赖"""
import ctypes
import ctypes.wintypes as wt
import struct
import sys

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32


def find_window(title):
    hwnd = user32.FindWindowW(None, title)
    if hwnd:
        return hwnd
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, l):
        n = user32.GetWindowTextLengthW(h)
        if n:
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(h, buf, n + 1)
            if title.lower() in buf.value.lower() and user32.IsWindowVisible(h):
                found.append(h)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else 0


def grab(hwnd, out):
    rect = wt.RECT()
    if hwnd:
        # DWM 的扩展边框才是可见边界，GetWindowRect 会含阴影
        got = ctypes.windll.dwmapi.DwmGetWindowAttribute(
            hwnd, 9, ctypes.byref(rect), ctypes.sizeof(rect)) == 0
        if not got or rect.right <= rect.left:
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
        x, y = rect.left, rect.top
        w, h = rect.right - rect.left, rect.bottom - rect.top
    else:
        x = y = 0
        w = user32.GetSystemMetrics(0)
        h = user32.GetSystemMetrics(1)

    hdc = user32.GetDC(0)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    gdi32.BitBlt(mem, 0, 0, w, h, hdc, x, y, 0x00CC0020 | 0x40000000)

    class BIH(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                    ("biPlanes", wt.WORD), ("biBitCount", wt.WORD),
                    ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                    ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
                    ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]

    bi = BIH()
    bi.biSize = ctypes.sizeof(BIH)
    bi.biWidth = w
    bi.biHeight = -h
    bi.biPlanes = 1
    bi.biBitCount = 32
    bi.biCompression = 0
    stride = w * 4
    buf = ctypes.create_string_buffer(stride * h)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)

    hdr = struct.pack("<2sIHHI", b"BM", 54 + stride * h, 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 32, 0, stride * h, 2835, 2835, 0, 0)
    with open(out, "wb") as f:
        f.write(hdr)
        f.write(buf.raw)

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(0, hdc)
    print(out, w, "x", h)


if __name__ == "__main__":
    title = sys.argv[1] if len(sys.argv) > 1 else ""
    out = sys.argv[2] if len(sys.argv) > 2 else "shot.bmp"
    h = 0
    if title.isdigit():
        h = int(title)
    elif title:
        h = find_window(title)
    if title and not h:
        print("window not found:", title)
        sys.exit(1)
    grab(h, out)
