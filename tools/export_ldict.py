#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 ECDICT 的 SQLite 库导出为 LiteDict 专用二进制索引（.ldict）

布局
    [40B 头][entry * n][var * nv][zh 段][字符串池]
    entry 44B / var 12B，全部小端，字符串 UTF-8、NUL 分隔
    条目按小写词字节序排列，变体按变体字节序排列 -> C 侧可直接二分
    zh 段：中文释义反向索引，用于汉译英
        [uint32 n_terms][dir: n*16 (term_off, term_len, post_off, cnt)]
        [postings: uint32 entry_idx ...]
    term 串直接放在字符串池里；post_off 相对于 postings 起点
    头部 reserved 字段存 zh 段偏移（版本 2 起）
"""
import os
import re
import sqlite3
import struct
import sys
import time

SRC = os.environ.get("LDICT_SRC", r"D:\WorkBuddy\2026-11-24-08-53-17\dict-app\data\dict.db")
DST = os.environ.get("LDICT_DST", os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "data", "dict.ldict"))

MAGIC = b"LDICT\x00\x02\x00"

# 释义里去掉行首词性（n. / vt. / a. …）和 [医] 这类领域标记
POS_RE = re.compile(r"^\s*[A-Za-z]{1,5}\.\s*")
BRACKET_RE = re.compile(r"^\[[^\]]{0,10}\]\s*")
SPLIT_RE = re.compile(r"[，,；;、/|]+")
ASCII_RE = re.compile(r"[A-Za-z0-9]")
SUFFIX = ("的", "地", "得", "了")
MAX_TERM_CHARS = 10
MAX_PER_TERM = 24


def zh_terms(tr):
    """从中文释义里切出可反查的词，返回 [(词, 出现次序)]

    次序 = 该词在这条释义里第几个义项，用于排序：
    「放弃」是 abandon 的头号义项，却只是 go 的第 5 个，不能让 go 排前面。
    """
    if not tr:
        return ()
    out = {}
    seq = 0
    for line in tr.split("\n"):
        line = POS_RE.sub("", line)
        line = BRACKET_RE.sub("", line)
        for seg in SPLIT_RE.split(line):
            seq += 1
            seg = seg.strip().strip("　 .·…•-")
            if not seg or ASCII_RE.search(seg):
                continue
            seg = seg.split("（")[0].split("(")[0].strip()
            if not seg or ASCII_RE.search(seg) or len(seg) > MAX_TERM_CHARS:
                continue
            # 查询时也会做同样的结尾助词归一，两边保持一致
            if len(seg) > 2 and seg.endswith(SUFFIX):
                seg = seg[:-1]
            if seg and seg not in out:
                out[seg] = seq
    return out.items()


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
    term_map = {}
    col_of = []
    frq_of = []
    wl_of = []
    for idx, (wl, word, ph, df, tr, tag, ex, frq, bnc, col, oxf) in enumerate(rows):
        entries.append(struct.pack(
            "<IIIIIIIIIHHI",
            put(wl), put(word), put(ph), put(df), put(tr), put(tag), put(ex),
            frq or 0, bnc or 0, col or 0, oxf or 0, len(wl.encode("utf-8"))
        ))
        col_of.append(col or 0)
        frq_of.append(frq or 0)
        wl_of.append(wl)
        for t, pos in zh_terms(tr):
            lst = term_map.get(t)
            if lst is None:
                term_map[t] = [(pos, idx)]
            else:
                lst.append((pos, idx))
        if ex:
            for seg in ex.split("/"):
                if ":" in seg:
                    k, v = seg.split(":", 1)
                    if k in ("p", "d", "i", "3", "s", "r", "t") and v:
                        vb = v.encode("utf-8")
                        vars_.append((vb, idx))

    print("条目 %d, 变体 %d, 池 %.1f MB, 中文词 %d" % (
        len(entries), len(vars_), len(pool) / 1048576, len(term_map)))
    t1 = time.time()

    vars_.sort(key=lambda x: x[0])
    # 变体串也进池，偏移要等池最终定型后再填
    var_offs = [put(vb.decode("utf-8")) for vb, idx in vars_]

    # ---- 中文反向索引 ----
    # 排序：常用词优先 -> 义项次序 -> 柯林斯星级 -> 词频 -> 词长
    #   只按义项次序排会让 "torch of Hymen" 压在 love 前面；
    #   只按词频排又会把 go 排在 abandon 前面（放弃只是 go 的第 N 个义项）。
    # 顺带过滤 app. / U.S. 这类缩写，点到它们体验很差
    def is_common(i):
        return col_of[i] >= 3 or 0 < frq_of[i] <= 20000

    zh_dir = []
    postings = []
    for term in sorted(term_map, key=lambda s: s.encode("utf-8")):
        cand = [i for _, i in term_map[term] if "." not in wl_of[i]]
        if not cand:
            cand = [i for _, i in term_map[term]]
        pos_of = {i: p for p, i in term_map[term]}
        cand.sort(key=lambda i: (0 if is_common(i) else 1, pos_of[i],
                                 -col_of[i],
                                 frq_of[i] if frq_of[i] else 1 << 30,
                                 len(wl_of[i]), i))
        cand = cand[:MAX_PER_TERM]
        off = len(postings)
        postings.extend(cand)
        zh_dir.append((put(term), len(term.encode("utf-8")), off, len(cand)))
    del term_map

    print("中文索引 %d 词 / %d postings, 段 %.1f MB, %.1fs" % (
        len(zh_dir), len(postings), (4 + 16 * len(zh_dir) + 4 * len(postings)) / 1048576,
        time.time() - t1))

    off_entries = 40
    off_vars = off_entries + 44 * len(entries)
    off_zh = off_vars + 12 * len(vars_)
    off_post = off_zh + 4 + 16 * len(zh_dir)
    off_strings = off_post + 4 * len(postings)

    # 回填变体的字符串偏移
    parts = []
    for i, (vb, idx) in enumerate(vars_):
        parts.append(struct.pack("<III", var_offs[i], len(vb), idx))
    var_blob = b"".join(parts)

    zh_blob = b"".join([
        struct.pack("<I", len(zh_dir)),
        b"".join(struct.pack("<IIII", o, l, p, c) for o, l, p, c in zh_dir),
        b"".join(struct.pack("<I", i) for i in postings),
    ])

    header = MAGIC + struct.pack(
        "<IIIIIIII",
        2, len(entries), off_entries, len(vars_), off_vars,
        off_strings, len(pool), off_zh
    )
    assert len(header) == 40, len(header)

    tmp = DST + ".tmp"
    with open(tmp, "wb") as f:
        f.write(header)
        f.write(b"".join(entries))
        f.write(var_blob)
        f.write(zh_blob)
        f.write(pool)
    if os.path.exists(DST):
        os.remove(DST)
    os.replace(tmp, DST)
    print("输出 %s  %.1f MB  总耗时 %.1fs" % (
        DST, os.path.getsize(DST) / 1048576, time.time() - t0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
