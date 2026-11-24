#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LiteDict 交互自测：自适应窗口尺寸，覆盖置顶与各页面"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time

u = ctypes.windll.user32
WM_SETTEXT = 0x000C
WM_KEYDOWN = 0x0100
WM_CANCELMODE = 0x001F
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
VK_RETURN = 0x0D
GWL_EXSTYLE = -20
WS_EX_TOPMOST = 0x00000008


def find_window():
    res = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, l):
        n = u.GetWindowTextLengthW(h)
        if n:
            b = ctypes.create_unicode_buffer(n + 1)
            u.GetWindowTextW(h, b, n + 1)
            if b.value == "LiteDict" and u.IsWindowVisible(h):
                r = wt.RECT()
                u.GetWindowRect(h, ctypes.byref(r))
                if 300 < r.right - r.left < 1600:
                    res.append(h)
        return True

    u.EnumWindows(cb, 0)
    return res[0] if res else 0


def client_size(h):
    r = wt.RECT()
    u.GetClientRect(h, ctypes.byref(r))
    return r.right, r.bottom


def layout(cw):
    narrow = cw < 600
    inner = cw - 48
    if inner > 780:
        inner = 780
    if inner < 220:
        inner = cw - 16
    cx = (cw - inner) // 2
    if cx < 8:
        cx = 8
    pw = 32
    fw = 32 if narrow else 84
    fx = cx + inner - fw
    px = fx - 6 - pw
    return {"cx": cx, "cw": inner, "px": px, "pw": pw, "fx": fx, "fw": fw}


def shot(h, name):
    out = "C:/Users/Hu/AppData/Local/Temp/%s.bmp" % name
    subprocess.run([sys.executable, "tools/shot.py", str(h), out], check=True)


def set_text(edit, word):
    u.SendMessageW(edit, WM_SETTEXT, 0, word)
    time.sleep(0.6)


def query(edit, word):
    set_text(edit, word)
    u.PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0)
    time.sleep(1.9)


def click(h, x, y):
    lp = (y << 16) | (x & 0xFFFF)
    u.PostMessageW(h, WM_LBUTTONDOWN, 1, lp)
    time.sleep(0.06)
    u.PostMessageW(h, WM_LBUTTONUP, 0, lp)
    time.sleep(0.9)


def is_topmost(h):
    ex = u.GetWindowLongW(h, GWL_EXSTYLE)
    return bool(ex & WS_EX_TOPMOST)


def main():
    h = find_window()
    if not h:
        print("window not found")
        return 1
    cw, ch = client_size(h)
    L = layout(cw)
    print("hwnd", h, "client", cw, "x", ch, "layout", L)

    u.SendMessageW(h, WM_CANCELMODE, 0, 0)
    u.ShowWindow(h, 9)
    u.SetForegroundWindow(h)
    time.sleep(0.4)

    edit = u.GetDlgItem(h, 100)
    set_text(edit, "")
    time.sleep(0.3)
    shot(h, "s_home")

    btx = L["px"] + L["pw"] // 2
    bty = 33
    print("topmost before:", is_topmost(h))
    click(h, btx, bty)
    print("topmost after :", is_topmost(h))
    shot(h, "s_pinned")

    query(edit, "study")
    shot(h, "s_entry")

    query(edit, "went")
    shot(h, "s_lemma")

    set_text(edit, "app")
    time.sleep(0.5)
    shot(h, "s_suggest")

    query(edit, "serendipity")
    shot(h, "s_long")

    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
