#include "dict.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    uint32_t wl, word, phone, def, trans, tag, exch;
    uint32_t frq, bnc;
    uint16_t collins, oxford;
    uint32_t wl_len;
} RawEntry;

typedef struct {
    uint32_t off, len, base;
} RawVar;

/* 中文反向索引目录项：term 串在字符串池里 */
typedef struct {
    uint32_t off, len, poff, cnt;
} RawZh;

typedef struct {
    char magic[8];
    uint32_t version, n_entries, off_entries, n_vars, off_vars, off_strings;
    uint32_t strings_bytes, reserved;   /* reserved: 版本 2 起 = zh 段偏移 */
} RawHeader;
#pragma pack(pop)

static HANDLE g_file = INVALID_HANDLE_VALUE;
static HANDLE g_map = NULL;
static const unsigned char *g_base = NULL;
static size_t g_size = 0;
static const RawHeader *g_hdr = NULL;
static const RawEntry *g_ent = NULL;
static const RawVar *g_var = NULL;
static const char *g_str = NULL;
static const RawZh *g_zh = NULL;
static const uint32_t *g_zhpost = NULL;
static uint32_t g_nzh = 0;

static int try_path(const char *path) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 64) { CloseHandle(f); return 0; }

    HANDLE m = CreateFileMappingA(f, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!m) { CloseHandle(f); return 0; }

    const unsigned char *b = (const unsigned char *)MapViewOfFile(
        m, FILE_MAP_READ, 0, 0, 0);
    if (!b) { CloseHandle(m); CloseHandle(f); return 0; }

    const RawHeader *h = (const RawHeader *)b;
    if (memcmp(h->magic, "LDICT", 5) != 0) {
        UnmapViewOfFile(b); CloseHandle(m); CloseHandle(f);
        return 0;
    }

    g_file = f;
    g_map = m;
    g_base = b;
    g_size = (size_t)sz.QuadPart;
    g_hdr = h;
    g_ent = (const RawEntry *)(b + h->off_entries);
    g_var = (const RawVar *)(b + h->off_vars);
    g_str = (const char *)(b + h->off_strings);

    /* 版本 2 起多一段中文反向索引；老词库照常能用，只是没有汉译英 */
    g_zh = NULL;
    g_zhpost = NULL;
    g_nzh = 0;
    if (h->version >= 2 && h->reserved) {
        size_t at = (size_t)h->reserved;
        if (at + 4 <= g_size) {
            uint32_t n = *(const uint32_t *)(b + at);
            size_t dir = (size_t)n * 16;
            if (at + 4 + dir <= g_size) {
                g_nzh = n;
                g_zh = (const RawZh *)(b + at + 4);
                g_zhpost = (const uint32_t *)(b + at + 4 + dir);
            }
        }
    }
    return 1;
}

static void dir_of_self(char *out, int cap) {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (!n || n >= MAX_PATH) { out[0] = 0; return; }
    char *p = strrchr(path, '\\');
    if (p) *p = 0;
    lstrcpynA(out, path, cap);
}

int dict_open(void) {
    char dir[MAX_PATH], path[MAX_PATH * 2];

    dir_of_self(dir, MAX_PATH);
    if (dir[0]) {
        wsprintfA(path, "%s\\data\\dict.ldict", dir);
        if (try_path(path)) return 1;
        wsprintfA(path, "%s\\data\\litedict.ldict", dir);
        if (try_path(path)) return 1;
    }
    if (try_path("data\\dict.ldict")) return 1;
    if (try_path("dict.ldict")) return 1;
    return 0;
}

void dict_close(void) {
    if (g_base) UnmapViewOfFile(g_base);
    if (g_map) CloseHandle(g_map);
    if (g_file != INVALID_HANDLE_VALUE) CloseHandle(g_file);
    g_base = NULL; g_map = NULL; g_file = INVALID_HANDLE_VALUE; g_hdr = NULL;
}

int dict_ok(void) { return g_hdr != NULL; }
int dict_count(void) { return g_hdr ? (int)g_hdr->n_entries : 0; }

const char *dict_wl(int i) { return g_str + g_ent[i].wl; }
uint32_t dict_wl_len(int i) { return g_ent[i].wl_len; }

static const char *str_at(uint32_t off) {
    return (off == 0xFFFFFFFFu) ? "" : g_str + off;
}

const DictEntry *dict_entry(int idx) {
    static DictEntry e;
    const RawEntry *r = &g_ent[idx];
    e.word = str_at(r->word);
    e.phonetic = str_at(r->phone);
    e.definition = str_at(r->def);
    e.translation = str_at(r->trans);
    e.tag = str_at(r->tag);
    e.exchange = str_at(r->exch);
    e.frq = r->frq;
    e.bnc = r->bnc;
    e.collins = r->collins;
    e.oxford = r->oxford;
    return &e;
}

/* 与词条 i 的小写词比较；返回 <0 / 0 / >0 */
static int cmp_entry(int i, const char *key, int klen) {
    uint32_t elen = g_ent[i].wl_len;
    int n = (klen < (int)elen) ? klen : (int)elen;
    int c = memcmp(g_str + g_ent[i].wl, key, (size_t)n);
    if (c) return c;
    if ((int)elen < klen) return -1;
    if ((int)elen > klen) return 1;
    return 0;
}

int dict_find(const char *wl, int len) {
    if (!g_hdr) return -1;
    int lo = 0, hi = (int)g_hdr->n_entries - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        int c = cmp_entry(mid, wl, len);
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/* 第一个 >= key 的位置 */
static int lower_bound(const char *key, int klen) {
    int lo = 0, hi = (int)g_hdr->n_entries;
    while (lo < hi) {
        int mid = lo + ((hi - lo) >> 1);
        if (cmp_entry(mid, key, klen) < 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}

int dict_var_base(const char *wl, int len) {
    if (!g_hdr || !g_hdr->n_vars) return -1;
    int lo = 0, hi = (int)g_hdr->n_vars - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        const RawVar *v = &g_var[mid];
        int n = ((int)v->len < len) ? (int)v->len : len;
        int c = memcmp(g_str + v->off, wl, (size_t)n);
        if (c == 0) {
            if ((int)v->len < len) c = -1;
            else if ((int)v->len > len) c = 1;
        }
        if (c == 0) return (int)v->base;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/* ------------------------------------------------------------ 汉译英 */

int dict_zh_count(void) { return (int)g_nzh; }

static int cmp_zh(int i, const char *key, int klen) {
    const RawZh *z = &g_zh[i];
    int elen = (int)z->len;
    int n = (klen < elen) ? klen : elen;
    int c = memcmp(g_str + z->off, key, (size_t)n);
    if (c) return c;
    if (elen < klen) return -1;
    if (elen > klen) return 1;
    return 0;
}

static int zh_lower_bound(const char *key, int klen) {
    int lo = 0, hi = (int)g_nzh;
    while (lo < hi) {
        int mid = lo + ((hi - lo) >> 1);
        if (cmp_zh(mid, key, klen) < 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}

int dict_zh_find(const char *utf8, int len, int *out, int maxn) {
    if (!g_zh || !g_nzh || len <= 0 || maxn <= 0) return 0;
    int lo = 0, hi = (int)g_nzh - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        int c = cmp_zh(mid, utf8, len);
        if (c == 0) {
            const RawZh *z = &g_zh[mid];
            int n = (int)z->cnt;
            if (n > maxn) n = maxn;
            /* postings 在导出时已按 星级 -> 词频 排好 */
            for (int i = 0; i < n; i++)
                out[i] = (int)g_zhpost[z->poff + (uint32_t)i];
            return n;
        }
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

int dict_zh_prefix(const char *utf8, int len, int *out, int maxn) {
    if (!g_zh || !g_nzh || len <= 0 || maxn <= 0) return 0;
    int i = zh_lower_bound(utf8, len);
    int m = 0;
    for (; i < (int)g_nzh && m < maxn; i++) {
        const RawZh *z = &g_zh[i];
        if ((int)z->len < len) break;
        if (memcmp(g_str + z->off, utf8, (size_t)len) != 0) break;
        for (uint32_t k = 0; k < z->cnt && m < maxn; k++) {
            int idx = (int)g_zhpost[z->poff + k];
            int dup = 0;
            for (int j = 0; j < m; j++) if (out[j] == idx) { dup = 1; break; }
            if (!dup) out[m++] = idx;
        }
    }
    return m;
}

int dict_zh_norm(const char *in, int len, char *out, int cap) {
    static const char *suf[4] = { "\xE7\x9A\x84", "\xE5\x9C\xB0",
                                  "\xE5\xBE\x97", "\xE4\xBA\x86" };  /* 的地得了 */
    int st = 0, en = len;
    while (st < en && (unsigned char)in[st] <= ' ') st++;
    while (en > st && (unsigned char)in[en - 1] <= ' ') en--;
    for (;;) {
        int cut = 0;
        if (en - st >= 6) {                 /* 至少留一个字 */
            for (int i = 0; i < 4; i++) {
                if (memcmp(in + en - 3, suf[i], 3) == 0) { en -= 3; cut = 1; break; }
            }
        }
        if (!cut) break;
    }
    int n = en - st;
    if (n > cap - 1) n = cap - 1;
    memcpy(out, in + st, (size_t)n);
    out[n] = 0;
    return n;
}

static int freq_bucket(uint32_t frq) {    if (frq == 0) return 4;
    if (frq <= 5000) return 0;
    if (frq <= 15000) return 1;
    if (frq <= 40000) return 2;
    return 3;
}

typedef struct { int idx, bucket; uint32_t len, frq; } Cand;

/* a 是否优于 b：常用度分档 -> 词长 -> 词频 */
static int cand_better(const Cand *a, const Cand *b) {
    if (a->bucket != b->bucket) return a->bucket < b->bucket;
    if (a->len != b->len) return a->len < b->len;
    if (a->frq != b->frq) return a->frq < b->frq;
    return a->idx < b->idx;
}

int dict_prefix(const char *pre, int len, int *out, int maxn, int by_freq) {
    if (!g_hdr || len <= 0 || maxn <= 0) return 0;
    int start = lower_bound(pre, len);
    int total = (int)g_hdr->n_entries;

    if (!by_freq) {                       /* 纯字典序，顺序取 */
        int m = 0;
        for (int i = start; i < total && m < maxn; i++) {
            if ((int)g_ent[i].wl_len < len) break;
            if (memcmp(g_str + g_ent[i].wl, pre, (size_t)len) != 0) break;
            out[m++] = i;
        }
        return m;
    }

    /* 扫描整个前缀段，用插入排序维护 top-maxn，避免候选池截断 */
    Cand *top = (Cand *)malloc(sizeof(Cand) * (size_t)maxn);
    if (!top) return 0;
    int tn = 0, scanned = 0;

    for (int i = start; i < total; i++) {
        uint32_t elen = g_ent[i].wl_len;
        if ((int)elen < len) break;
        if (memcmp(g_str + g_ent[i].wl, pre, (size_t)len) != 0) break;
        if (++scanned > 300000) break;     /* 安全上限 */

        Cand c;
        c.idx = i;
        c.bucket = freq_bucket(g_ent[i].frq);
        c.len = elen;
        c.frq = g_ent[i].frq;
        if (tn < maxn) {
            int p = tn++;
            while (p > 0 && cand_better(&c, &top[p - 1])) { top[p] = top[p - 1]; p--; }
            top[p] = c;
        } else if (cand_better(&c, &top[maxn - 1])) {
            int p = maxn - 1;
            while (p > 0 && cand_better(&c, &top[p - 1])) { top[p] = top[p - 1]; p--; }
            top[p] = c;
        }
    }

    for (int i = 0; i < tn; i++) out[i] = top[i].idx;
    free(top);
    return tn;
}

int dict_lower(const char *in, int inlen, char *out, int cap) {
    int n = 0;
    for (int i = 0; i < inlen && n < cap - 1; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n;
}

int utf8_to_w(const char *s, wchar_t *buf, int cap) {
    if (!s) s = "";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, buf ? cap : 0);
    if (n > 0 && buf) buf[n - 1] = 0;
    return n > 0 ? n - 1 : 0;
}
