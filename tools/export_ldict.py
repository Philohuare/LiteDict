#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 ECDICT 的 SQLite 库导出为 LiteDict 专用二进制索引（.ldict）

布局
    [40B 头][entry * n][var * nv][字符串池]
    entry 44B / var 12B，全部小端，字符串 UTF-8、NUL 分隔
    条目按小写词字节序排列，变体按变体字节序排列 -> C 侧可直接二分
"""
import os
import sqlite3
import struct
import sys
import time

SRC = os.environ.get("LDICT_SRC", r"D:\WorkBuddy\2026-11-24-08-53-17\dict-app\data\dict.db")
DST = os.environ.get("LDICT_DST", os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "data", "dict.ldict"))

MAGIC = b"LDICT\x00\x01\x00"


def main():
    if not os.path.exists(SRC):
        print("源库不存在:", SRC)
        return 1
    t0 = time.time()
    con = sqlite3.connect(SRC)
    rows = con.execute(
        "SELECT wl, word, phonetic, definition, translation, tag, exchange,"
        " frq, bnc, collins, oxford FROM d ORDER BY wl"
    ).fetchall()
    print("读入 %d 条, %.1fs" % (len(rows), time.time() - t0))

    pool = bytearray()
    seen = {}

    def put(s):
        """字符串入池，去重，返回偏移"""
        b = (s or "").encode("utf-8")
        if not b:
            return 0xFFFFFFFF
        off = seen.get(b)
        if off is None:
            off = len(pool)
            pool.extend(b)
            pool.append(0)
            seen[b] = off
        return off

    entries = []
    vars_ = []
    for idx, (wl, word, ph, df, tr, tag, ex, frq, bnc, col, oxf) in enumerate(rows):
        entries.append(struct.pack(
            "<IIIIIIIIIHHI",
            put(wl), put(word), put(ph), put(df), put(tr), put(tag), put(ex),
            frq or 0, bnc or 0, col or 0, oxf or 0, len(wl.encode("utf-8"))
        ))
        if ex:
            for seg in ex.split("/"):
                if ":" in seg:
                    k, v = seg.split(":", 1)
                    if k in ("p", "d", "i", "3", "s", "r", "t") and v:
                        vb = v.encode("utf-8")
                        vars_.append((vb, idx))

    print("条目 %d, 变体 %d, 池 %.1f MB" % (
        len(entries), len(vars_), len(pool) / 1048576))

    vars_.sort(key=lambda x: x[0])
    var_pack = b"".join(
        struct.pack("<III", 0, len(vb), idx) for vb, idx in vars_
    )
    # 变体串也进池，偏移要等池最终定型后再填
    var_offs = [put(vb.decode("utf-8")) for vb, idx in vars_]

    off_entries = 40
    off_vars = off_entries + 44 * len(entries)
    off_strings = off_vars + 12 * len(vars_)

    # 回填变体的字符串偏移
    parts = []
    for i, (vb, idx) in enumerate(vars_):
        parts.append(struct.pack("<III", var_offs[i], len(vb), idx))
    var_blob = b"".join(parts)

    header = MAGIC + struct.pack(
        "<IIIIIIII",
        1, len(entries), off_entries, len(vars_), off_vars,
        off_strings, len(pool), 0
    )
    assert len(header) == 40, len(header)

    tmp = DST + ".tmp"
    with open(tmp, "wb") as f:
        f.write(header)
        f.write(b"".join(entries))
        f.write(var_blob)
        f.write(pool)
    if os.path.exists(DST):
        os.remove(DST)
    os.replace(tmp, DST)
    print("输出 %s  %.1f MB  总耗时 %.1fs" % (
        DST, os.path.getsize(DST) / 1048576, time.time() - t0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
