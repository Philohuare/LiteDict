/* LiteDict - 纯 C / Win32 单文件词典 */
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <imm.h>
#include <math.h>
#include <wchar.h>
#include <stdio.h>
#include <string.h>
#include "dict.h"
#include "store.h"
#include "online.h"

/* ------------------------------------------------------------------ 主题 */
typedef struct {
    int dark;
    COLORREF bg, panel, text, dim, dim2, line, accent, accentSoft, hover;
    COLORREF goldFg, goldBg, star, panelLine;
} Theme;
static Theme T;

static void theme_init(void) {
    HKEY k;
    DWORD light = 1, sz = sizeof(light);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&light, &sz);
        RegCloseKey(k);
    }
    T.dark = (light == 0);
    if (T.dark) {
        T.bg = RGB(23, 24, 26);       T.panel = RGB(30, 31, 34);
        T.text = RGB(236, 234, 230);  T.dim = RGB(141, 141, 147);
        T.dim2 = RGB(109, 109, 115);  T.line = RGB(44, 45, 49);
        T.accent = RGB(111, 155, 255); T.accentSoft = RGB(32, 41, 60);
        T.hover = RGB(38, 39, 43);
        T.goldFg = RGB(216, 178, 90); T.goldBg = RGB(42, 37, 23);
        T.star = RGB(226, 176, 40);
    } else {
        T.bg = RGB(251, 251, 250);    T.panel = RGB(255, 255, 255);
        T.text = RGB(27, 27, 29);     T.dim = RGB(139, 139, 144);
        T.dim2 = RGB(169, 169, 174);  T.line = RGB(233, 232, 228);
        T.accent = RGB(47, 111, 237); T.accentSoft = RGB(238, 243, 254);
        T.hover = RGB(245, 244, 241);
        T.goldFg = RGB(150, 110, 8);  T.goldBg = RGB(253, 248, 232);
        T.star = RGB(224, 165, 0);
    }
    T.panelLine = T.line;
}

/* ------------------------------------------------------------------ 全局 */
#define NSUG 12
#define MAXHOT 220

enum { MODE_HOME, MODE_ENTRY, MODE_MISS, MODE_FAV };

typedef struct { RECT rc; wchar_t word[80]; int kind; } Hot;

static HWND g_hwnd, g_edit;
static HBRUSH g_brEdit;
static WNDPROC g_oldEdit;
static int g_dpi = 96;
static float S = 1.0f;

static HFONT g_fHw, g_fHw2, g_fPh, g_fBody, g_fPos, g_fSmall, g_fTag, g_fUI, g_fSym, g_fSmallB, g_fIcon;

static int g_mode = MODE_HOME;
static int g_scroll = 0, g_total = 0;
static int g_clientW = 900, g_clientH = 600;
static char g_query[128];

static int g_idx = -1, g_lemma = -1;
static wchar_t W_word[128], W_phone[160], W_trans[2048], W_def[3072];
static wchar_t W_tag[160], W_exch[256], W_lemma[128];

typedef struct { wchar_t pos[16]; wchar_t text[600]; } Sense;
static Sense g_zh[24]; static int g_nzh;
static Sense g_en[24]; static int g_nen;

typedef struct { wchar_t label[32]; wchar_t form[64]; } Form;
static Form g_forms[12]; static int g_nforms;

static int g_rel[8]; static int g_nrel;
static wchar_t W_rel[8][64];

static int g_sug[NSUG]; static int g_nsug;
static wchar_t W_sug[NSUG][64], W_sug_tr[NSUG][120], W_sug_ph[NSUG][64];
static int g_sel = -1;

static Hot g_hot[MAXHOT]; static int g_nhot;

static wchar_t W_fav[64][64];
static wchar_t W_fav_tr[64][160];
static int g_nfavlist;


/* ------------------------------------------------------------------ 工具 */
static int Px(int v) { return (int)(v * S + 0.5f); }

static HFONT MkFontQ(const wchar_t *face, int px, int weight, int italic,
                     BYTE q) {
    return CreateFontW(-Px(px), 0, 0, 0, weight, italic, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       q, DEFAULT_PITCH | FF_DONTCARE, face);
}

static HFONT MkFont(const wchar_t *face, int px, int weight, int italic) {
    return MkFontQ(face, px, weight, italic, CLEARTYPE_QUALITY);
}

static void build_fonts(void) {
    HFONT old[11] = { g_fHw, g_fHw2, g_fPh, g_fBody, g_fPos, g_fSmall,
                      g_fTag, g_fUI, g_fSym, g_fSmallB, g_fIcon };
    g_fHw     = MkFont(L"Georgia", 30, FW_SEMIBOLD, 0);
    g_fHw2    = MkFont(L"Georgia", 23, FW_SEMIBOLD, 0);
    g_fPh     = MkFont(L"Georgia", 15, FW_NORMAL, 0);
    g_fBody   = MkFont(L"Microsoft YaHei UI", 14, FW_NORMAL, 0);
    g_fPos    = MkFont(L"Georgia", 12, FW_NORMAL, 1);
    g_fSmall  = MkFont(L"Microsoft YaHei UI", 11, FW_NORMAL, 0);
    g_fSmallB = MkFont(L"Microsoft YaHei UI", 11, FW_SEMIBOLD, 0);
    g_fTag    = MkFont(L"Microsoft YaHei UI", 10, FW_NORMAL, 0);
    g_fUI     = MkFont(L"Microsoft YaHei UI", 13, FW_NORMAL, 0);
    g_fSym    = MkFontQ(L"Segoe UI Symbol", 14, FW_NORMAL, 0, ANTIALIASED_QUALITY);
    g_fIcon   = MkFontQ(L"Segoe MDL2 Assets", 16, FW_NORMAL, 0, ANTIALIASED_QUALITY);
    for (int i = 0; i < 11; i++) if (old[i]) DeleteObject(old[i]);
}

/* 图标字形精确居中：按墨迹黑盒（glyph black box）而非行高对齐，
 * 否则字体上下的行距留白会让图标在按钮里偏上 */
static void IconIn(HDC dc, wchar_t g, RECT box, HFONT f, COLORREF col) {
    SelectObject(dc, f);
    SetTextColor(dc, col);
    SetBkMode(dc, TRANSPARENT);

    MAT2 mat;
    memset(&mat, 0, sizeof(mat));
    mat.eM11.value = 1;
    mat.eM22.value = 1;

    GLYPHMETRICS gm;
    if (GetGlyphOutlineW(dc, (UINT)g, GGO_METRICS, &gm, 0, NULL, &mat) == GDI_ERROR)
        return;
    if (!gm.gmBlackBoxX || !gm.gmBlackBoxY) return;

    int bw = (int)gm.gmBlackBoxX, bh = (int)gm.gmBlackBoxY;
    int cx = (box.left + box.right) / 2;
    int cy = (box.top + box.bottom) / 2;
    int gx = cx - bw / 2 - (int)gm.gmptGlyphOrigin.x;
    int gy = cy + (int)gm.gmptGlyphOrigin.y - bh / 2;

    UINT old = SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    TextOutW(dc, gx, gy, &g, 1);
    SetTextAlign(dc, old);
}

static int Txt(HDC dc, const wchar_t *s, int x, int y, int w,
               HFONT f, COLORREF col, UINT fl, int dry) {
    if (!s || !*s) return 0;
    SelectObject(dc, f);
    SetTextColor(dc, col);
    SetBkMode(dc, TRANSPARENT);
    RECT r = { x, y, x + w, y };
    DrawTextW(dc, s, -1, &r, fl | DT_CALCRECT | DT_NOPREFIX);
    int h = r.bottom - r.top;
    if (!dry) {
        RECT d = { x, y, x + w, y + h };
        DrawTextW(dc, s, -1, &d, fl | DT_NOPREFIX);
    }
    return h;
}

/* ------------------------------------------------------------------ 圆角
 * GDI 的 RoundRect 是硬边像素，圆角看起来像阶梯。这里改为自建 32bpp DIB，
 * 按有符号距离场逐像素算覆盖率再 AlphaBlend 上去，得到平滑的圆角与细边框。
 */
static HBITMAP g_aaBmp;
static HDC     g_aaDC;
static DWORD  *g_aaBits;
static int     g_aaW, g_aaH;

static int aa_ensure(int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    if (g_aaDC && w <= g_aaW && h <= g_aaH) return 1;
    if (g_aaDC) { DeleteDC(g_aaDC); g_aaDC = NULL; }
    if (g_aaBmp) { DeleteObject(g_aaBmp); g_aaBmp = NULL; }
    int nw = w > 520 ? w : 520, nh = h > 160 ? h : 160;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = nw;
    bi.bmiHeader.biHeight = -nh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    g_aaBmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!g_aaBmp || !bits) {
        if (g_aaBmp) DeleteObject(g_aaBmp);
        g_aaBmp = NULL;
        return 0;
    }
    g_aaBits = (DWORD *)bits;
    g_aaDC = CreateCompatibleDC(NULL);
    if (!g_aaDC) { DeleteObject(g_aaBmp); g_aaBmp = NULL; return 0; }
    SelectObject(g_aaDC, g_aaBmp);
    g_aaW = nw;
    g_aaH = nh;
    return 1;
}

static float cov_of(float d) {
    float c = 0.5f - d;
    if (c < 0.0f) return 0.0f;
    if (c > 1.0f) return 1.0f;
    return c;
}

/* borderPct: 边框透明度百分比，100 为实心 */
static void FillRoundAA(HDC dc, RECT r, int rad, COLORREF fill,
                        COLORREF border, int borderPct) {
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;

    if (rad < 0) rad = 0;
    if (rad * 2 > w) rad = w / 2;
    if (rad * 2 > h) rad = h / 2;

    int bw = Px(1);
    if (bw < 1) bw = 1;
    if (borderPct <= 0) bw = 0;

    /* 极小或超大（多数是整块背景）走原生路径，省掉逐像素开销 */
    if (rad <= 1 || w < 8 || h < 8 || w > 1024 || h > 1024 || !aa_ensure(w, h)) {
        HBRUSH br = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(br); DeleteObject(pen);
        return;
    }

    float hw = w * 0.5f, hh = h * 0.5f;
    float ex = hw - (float)rad, ey = hh - (float)rad;
    float fR = (float)GetRValue(fill), fG = (float)GetGValue(fill),
          fB = (float)GetBValue(fill);
    float bR = (float)GetRValue(border), bG = (float)GetGValue(border),
          bB = (float)GetBValue(border);
    float bp = (float)(borderPct > 100 ? 100 : borderPct) / 100.0f;

    for (int y = 0; y < h; y++) {
        float cy = (float)y + 0.5f - hh;
        float qy = (cy < 0 ? -cy : cy) - ey;
        if (qy < 0) qy = 0;
        float qy2 = qy * qy;
        DWORD *row = g_aaBits + (size_t)y * (size_t)g_aaW;

        for (int x = 0; x < w; x++) {
            float cx = (float)x + 0.5f - hw;
            float qx = (cx < 0 ? -cx : cx) - ex;
            if (qx < 0) qx = 0;
            float d = sqrtf(qx * qx + qy2) - (float)rad;

            float aFill = cov_of(d + (float)bw);
            if (aFill >= 0.999f) {
                row[x] = ((DWORD)0xFF << 24) |
                         ((DWORD)(fR + 0.5f) << 16) |
                         ((DWORD)(fG + 0.5f) << 8) | (DWORD)(fB + 0.5f);
                continue;
            }
            float aEdge = cov_of(d);
            float aBord = (aEdge - aFill) * bp;
            if (aBord < 0.0f) aBord = 0.0f;
            float a = aFill + aBord;
            if (a <= 0.002f) { row[x] = 0; continue; }

            float r2 = (fR * aFill + bR * aBord) / a;
            float g2 = (fG * aFill + bG * aBord) / a;
            float b2 = (fB * aFill + bB * aBord) / a;
            float m = a * 255.0f;
            DWORD ia = (DWORD)(m + 0.5f);
            if (ia > 255) ia = 255;
            DWORD ipr = (DWORD)(r2 * m / 255.0f + 0.5f);
            DWORD ipg = (DWORD)(g2 * m / 255.0f + 0.5f);
            DWORD ipb = (DWORD)(b2 * m / 255.0f + 0.5f);
            if (ipr > 255) ipr = 255;
            if (ipg > 255) ipg = 255;
            if (ipb > 255) ipb = 255;
            row[x] = (ia << 24) | (ipr << 16) | (ipg << 8) | ipb;
        }
    }

    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(dc, r.left, r.top, w, h, g_aaDC, 0, 0, w, h, bf);
}

static void FillRound(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border) {
    FillRoundAA(dc, r, rad, fill, border, 100);
}

/* 无边框、半透明描边的柔和矩形（用于按钮与卡片） */
static void FillRoundSoft(HDC dc, RECT r, int rad, COLORREF fill,
                          COLORREF border, int pct) {
    FillRoundAA(dc, r, rad, fill, border, pct);
}

static COLORREF blendclr(COLORREF a, COLORREF b, int pa) {
    int pb = 100 - pa;
    return RGB((GetRValue(a) * pa + GetRValue(b) * pb) / 100,
               (GetGValue(a) * pa + GetGValue(b) * pb) / 100,
               (GetBValue(a) * pa + GetBValue(b) * pb) / 100);
}

static void HLine(HDC dc, int x1, int x2, int y, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, x1, y, NULL);
    LineTo(dc, x2, y);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void AddHot(RECT rc, const wchar_t *word, int kind, int dry) {
    if (dry || g_nhot >= MAXHOT) return;
    Hot *h = &g_hot[g_nhot++];
    h->rc = rc;
    h->kind = kind;
    if (word) lstrcpynW(h->word, word, 80); else h->word[0] = 0;
}

/* ------------------------------------------------------------------ 解析 */
static int is_alpha(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

static void split_sense(wchar_t *line, Sense *out) {
    out->pos[0] = 0;
    wchar_t *p = line;
    while (*p == L' ') p++;
    wchar_t *q = p;
    int n = 0;
    while (*q && n < 7 && (is_alpha(*q) || *q == L'&')) { q++; n++; }
    if (*q == L'.' && q - p >= 1 && q - p <= 5) {
        int m = (int)(q - p);
        memcpy(out->pos, p, sizeof(wchar_t) * (size_t)m);
        out->pos[m] = L'.';
        out->pos[m + 1] = 0;
        q++;
        while (*q == L' ') q++;
        lstrcpynW(out->text, q, 600);
    } else {
        lstrcpynW(out->text, p, 600);
    }
}

static int split_lines(wchar_t *s, Sense *out, int maxn) {
    int n = 0;
    wchar_t *p = s;
    while (p && *p && n < maxn) {
        wchar_t *nl = wcschr(p, L'\n');
        if (nl) *nl = 0;
        if (*p) split_sense(p, &out[n++]);
        p = nl ? nl + 1 : NULL;
    }
    return n;
}

static void parse_forms(const wchar_t *ex) {
    static const wchar_t keys[] = L"pdi3srt01";
    static const wchar_t *labs[] = { L"过去式", L"过去分词", L"现在分词",
        L"第三人称", L"复数", L"比较级", L"最高级", L"原形", L"原形" };
    g_nforms = 0;
    if (!ex || !*ex) return;
    wchar_t buf[256];
    lstrcpynW(buf, ex, 256);
    wchar_t *p = buf;
    while (p && *p) {
        wchar_t *sl = wcschr(p, L'/');
        if (sl) *sl = 0;
        wchar_t *colon = wcschr(p, L':');
        if (colon && colon - p == 1) {
            int ki = -1;
            for (int i = 0; keys[i]; i++) if (keys[i] == p[0]) { ki = i; break; }
            /* '0'/'1' 是原形类标记，和词头重复，跳过 */
            if (ki == 7 || ki == 8) ki = -1;
            if (ki >= 0 && colon[1] && g_nforms < 12) {
                const wchar_t *form = colon + 1;
                int found = -1;
                for (int i = 0; i < g_nforms; i++)
                    if (lstrcmpiW(g_forms[i].form, form) == 0) { found = i; break; }
                if (found >= 0) {
                    if (wcslen(g_forms[found].label) + wcslen(labs[ki]) + 4 < 30) {
                        wcscat_s(g_forms[found].label, 32, L" / ");
                        wcscat_s(g_forms[found].label, 32, labs[ki]);
                    }
                } else {
                    lstrcpynW(g_forms[g_nforms].label, labs[ki], 32);
                    lstrcpynW(g_forms[g_nforms].form, form, 64);
                    g_nforms++;
                }
            }
        }
        p = sl ? sl + 1 : NULL;
    }
}

/* ------------------------------------------------------------------ 数据 */
static void load_entry(int idx) {
    const DictEntry *e = dict_entry(idx);
    g_idx = idx;
    utf8_to_w(e->word, W_word, 128);
    utf8_to_w(e->phonetic, W_phone, 160);
    utf8_to_w(e->translation, W_trans, 2048);
    utf8_to_w(e->definition, W_def, 3072);
    utf8_to_w(e->tag, W_tag, 160);
    utf8_to_w(e->exchange, W_exch, 256);
    g_nzh = split_lines(W_trans, g_zh, 24);
    g_nen = split_lines(W_def, g_en, 24);
    parse_forms(W_exch);
    g_lemma = -1;
    W_lemma[0] = 0;
}

static void do_lookup(const char *raw) {
    /* 先剪掉首尾空白：历史里可能带 \r、粘贴可能带空格，
     * 带着这些字符去查一定查不到，而界面上看又完全一样 */
    char trimmed[256];
    {
        int len = (int)strlen(raw);
        int st = 0;
        while (st < len && (unsigned char)raw[st] <= ' ') st++;
        while (len > st && (unsigned char)raw[len - 1] <= ' ') len--;
        if (len - st > (int)sizeof(trimmed) - 1) len = st + (int)sizeof(trimmed) - 1;
        memcpy(trimmed, raw + st, (size_t)(len - st));
        trimmed[len - st] = 0;
        raw = trimmed;
    }
    if (!raw[0]) { g_mode = MODE_HOME; return; }

    char low[256];
    int n = dict_lower(raw, (int)strlen(raw), low, sizeof(low));
    if (n <= 0) { g_mode = MODE_HOME; return; }
    lstrcpynA(g_query, raw, 128);

    int idx = dict_find(low, n);
    if (idx >= 0) {
        load_entry(idx);
        /* 命中本身也可能是变形（went 有自己的词条），补一条原形提示 */
        int base = dict_var_base(low, n);
        if (base >= 0 && base != idx) {
            utf8_to_w(dict_entry(base)->word, W_lemma, 128);
            g_lemma = base;
        }
        g_mode = MODE_ENTRY;
        store_hist_add(raw);
        online_fetch(dict_entry(idx)->word);
        return;
    }

    int base = dict_var_base(low, n);
    if (base >= 0) {
        load_entry(base);
        utf8_to_w(dict_entry(base)->word, W_lemma, 128);
        utf8_to_w(raw, W_word, 128);        /* 词头保留用户输入 */
        g_lemma = base;
        g_mode = MODE_ENTRY;
        store_hist_add(raw);
        online_fetch(dict_entry(base)->word);
        return;
    }

    g_mode = MODE_MISS;
    g_idx = -1;
    g_lemma = -1;
    utf8_to_w(raw, W_word, 128);
    g_nrel = dict_prefix(low, n > 2 ? n - 1 : n, g_rel, 8, 1);
    for (int i = 0; i < g_nrel; i++)
        utf8_to_w(dict_entry(g_rel[i])->word, W_rel[i], 64);
}

static int SugRowH(void);

static void update_suggest(void) {
    wchar_t wq[128];
    GetWindowTextW(g_edit, wq, 128);
    char q[512];
    WideCharToMultiByte(CP_UTF8, 0, wq, -1, q, sizeof(q), NULL, NULL);
    int len = (int)strlen(q);
    if (len <= 0) { g_nsug = 0; g_sel = -1; return; }
    g_nsug = dict_prefix(q, len, g_sug, NSUG, 1);
    /* 按窗口可用高度裁剪，避免小窗下列表溢出 */
    {
        int avail = g_clientH - Px(66) - Px(16);
        int maxRows = avail / SugRowH();
        if (maxRows < 2) maxRows = 2;
        if (g_nsug > maxRows) g_nsug = maxRows;
    }
    for (int i = 0; i < g_nsug; i++) {
        const DictEntry *e = dict_entry(g_sug[i]);
        utf8_to_w(e->word, W_sug[i], 64);
        utf8_to_w(e->phonetic, W_sug_ph[i], 64);
        wchar_t tr[600];
        utf8_to_w(e->translation, tr, 600);
        wchar_t *nl = wcschr(tr, L'\n');
        if (nl) *nl = 0;
        lstrcpynW(W_sug_tr[i], tr, 120);
    }
    g_sel = -1;
}

static void goto_word_w(const wchar_t *w) {
    char u[512];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof(u), NULL, NULL);
    g_nsug = 0;
    g_sel = -1;
    SetWindowTextW(g_edit, w);
    g_nsug = 0;
    g_sel = -1;
    g_scroll = 0;
    do_lookup(u);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ------------------------------------------------------------------ 布局 */
typedef struct { int cx, cw, sx, sw, fx, fw, brandW, narrow; } Layout;

static Layout CalcLayout(int clientW) {
    Layout L;
    int maxw = Px(780);
    L.narrow = clientW < Px(600);
    L.cw = clientW - Px(48);
    if (L.cw > maxw) L.cw = maxw;
    if (L.cw < Px(220)) L.cw = clientW - Px(16);
    L.cx = (clientW - L.cw) / 2;
    if (L.cx < Px(8)) L.cx = Px(8);

    /* 生词本按钮在最右 */
    L.fw = L.narrow ? Px(34) : Px(84);
    L.fx = L.cx + L.cw - L.fw;

    int sRight = L.fx - Px(14);
    L.brandW = L.narrow ? 0 : Px(96);
    L.sx = L.cx + L.brandW;
    L.sw = sRight - L.sx;
    if (L.sw < Px(120)) {
        L.sx = L.cx;
        L.sw = sRight - L.cx;
        L.brandW = 0;
    }
    return L;
}

/* 搜索框几何：绘制与 EDIT 定位共用同一份计算 */
typedef struct { RECT box; int ix, iy, isz, tx, tw, capX, capW; } SearchG;

static SearchG CalcSearch(int clientW) {
    Layout L = CalcLayout(clientW);
    SearchG g;
    g.box.left   = L.sx;
    g.box.right  = L.sx + L.sw;
    g.box.top    = Px(15);
    g.box.bottom = Px(51);

    g.isz = Px(13);
    g.ix  = g.box.left + Px(14);
    g.iy  = (g.box.top + g.box.bottom) / 2 - g.isz / 2;
    g.tx  = g.box.left + Px(36);

    g.capW = 0;
    g.capX = g.box.right - Px(15);
    g.tw = g.box.right - Px(15) - g.tx;
    if (g.tw < Px(30)) g.tw = Px(30);
    return g;
}

static void LayoutEdit(int clientW, int headerH) {
    SearchG g = CalcSearch(clientW);
    int eh = Px(23);
    int ey = (g.box.top + g.box.bottom) / 2 - eh / 2;
    MoveWindow(g_edit, g.tx, ey, g.tw, eh, TRUE);
}

static int SugTop(int headerH) { return headerH - Px(4); }

/* 窄窗口下联想改为两行（词+音标 / 释义），行高加大 */
static int SugRowH(void) {
    Layout L = CalcLayout(g_clientW);
    return L.narrow ? Px(46) : Px(34);
}

static int SugHeight(void) {
    return g_nsug ? g_nsug * SugRowH() + Px(10) : 0;
}

/* ------------------------------------------------------------------ 绘制 */
static int DrawSegPh(HDC dc, int x, int y, int w, const wchar_t *s, int dry) {
    wchar_t buf[220];
    if (!s || !*s || w <= 0) return 0;
    _snwprintf_s(buf, 220, _TRUNCATE, L"/%s/", s);
    return Txt(dc, buf, x, y, w, g_fPh, T.dim,
               DT_SINGLELINE | DT_END_ELLIPSIS, dry);
}

static int DrawBadges(HDC dc, int x, int y, int maxw, int dry) {
    if (g_idx < 0) return 0;
    const DictEntry *e = dict_entry(g_idx);
    wchar_t items[10][40]; int n = 0;
    if (e->collins) { _snwprintf_s(items[n], 40, _TRUNCATE, L"COLLINS %d", e->collins); n++; }
    if (e->oxford) { lstrcpynW(items[n], L"OXFORD 3000", 40); n++; }
    wchar_t tag[160];
    lstrcpynW(tag, W_tag, 160);
    wchar_t *p = tag;
    while (p && *p && n < 10) {
        while (*p == L' ') p++;
        if (!*p) break;
        wchar_t *sp = wcschr(p, L' ');
        if (sp) *sp = 0;
        lstrcpynW(items[n], p, 40);
        n++;
        p = sp ? sp + 1 : NULL;
    }
    int cx = x, rowH = 0;
    for (int i = 0; i < n; i++) {
        const wchar_t *src = items[i];
        if (!lstrcmpiW(src, L"zk")) src = L"中考";
        else if (!lstrcmpiW(src, L"gk")) src = L"高考";
        else if (!lstrcmpiW(src, L"ky")) src = L"考研";
        else if (!lstrcmpiW(src, L"toefl")) src = L"TOEFL";
        else if (!lstrcmpiW(src, L"ielts")) src = L"IELTS";
        else if (!lstrcmpiW(src, L"cet4")) src = L"CET4";
        else if (!lstrcmpiW(src, L"cet6")) src = L"CET6";
        else if (!lstrcmpiW(src, L"gre")) src = L"GRE";

        SelectObject(dc, g_fTag);
        SIZE sz; GetTextExtentPoint32W(dc, src, (int)wcslen(src), &sz);
        int bw = sz.cx + Px(14), bh = sz.cy + Px(7);
        if (cx + bw > x + maxw) break;
        RECT r = { cx, y, cx + bw, y + bh };
        int gold = (i < 2 && (e->collins || e->oxford));
        if (!dry) {
            FillRound(dc, r, Px(4),
                      gold ? T.goldBg : T.hover,
                      gold ? (T.dark ? RGB(60, 53, 39) : RGB(237, 223, 176)) : T.line);
            Txt(dc, src, cx + Px(7), y + Px(3), bw, g_fTag,
                gold ? T.goldFg : T.dim, DT_SINGLELINE, 0);
        }
        cx += bw + Px(6);
        rowH = bh;
    }
    return rowH;
}

static int DrawSenseRows(HDC dc, int x, int y, int w, Sense *rows, int n,
                         int isEn, int dry) {
    int colw = Px(44);
    int tw = w - colw;
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            if (!dry) HLine(dc, x, x + w, y, T.line);
            y += Px(1);
        }
        int yy = y + Px(9);
        int hasPos = rows[i].pos[0] != 0;
        if (hasPos)
            Txt(dc, rows[i].pos, x, yy + Px(2), colw, g_fPos, T.accent,
                DT_SINGLELINE, dry);
        int h = Txt(dc, rows[i].text, hasPos ? x + colw : x, yy,
                    hasPos ? tw : w,
                    isEn ? g_fSmall : g_fBody,
                    isEn ? T.dim : T.text, DT_WORDBREAK, dry);
        y = yy + h + Px(9);
    }
    return y;
}

static int Render(HDC dc, int clientW, int top, int scroll, int dry) {
    Layout L = CalcLayout(clientW);
    int x = L.cx, w = L.cw;
    int y0 = top + Px(26) - scroll;
    int y = y0;

    if (g_mode == MODE_ENTRY && g_idx >= 0) {
        SelectObject(dc, g_fHw);
        SIZE bs;
        GetTextExtentPoint32W(dc, W_word, (int)wcslen(W_word), &bs);
        HFONT hwFont = g_fHw;
        if (bs.cx + Px(200) > w) {
            hwFont = g_fHw2;
            SelectObject(dc, g_fHw2);
            GetTextExtentPoint32W(dc, W_word, (int)wcslen(W_word), &bs);
        }
        Txt(dc, W_word, x, y, w - Px(104), hwFont, T.text,
            DT_SINGLELINE | DT_END_ELLIPSIS, dry);
        int hwH = bs.cy;
        if (W_phone[0]) {
            int phoneX = x + bs.cx + Px(12);
            int availW = (x + w - Px(80)) - Px(10) - phoneX;
            if (availW >= Px(70))
                DrawSegPh(dc, phoneX, y + hwH - Px(23), availW, W_phone, dry);
        }

        /* 发音 / 收藏 */
        int bx = x + w - Px(80), by = y + Px(4);
        RECT r1 = { bx, by, bx + Px(34), by + Px(34) };
        RECT r2 = { bx + Px(40), by, bx + Px(74), by + Px(34) };
        if (!dry) {
            int fav = store_fav_has(dict_entry(g_idx)->word);
            FillRoundAA(dc, r1, Px(9), T.panel, T.line, 100);
            FillRoundAA(dc, r2, Px(9), T.panel, T.line, 100);
            IconIn(dc, L'\uE767', r1, g_fIcon, T.dim);
            IconIn(dc, fav ? L'\uE735' : L'\uE734', r2, g_fIcon,
                   fav ? T.star : T.dim);
        }
        AddHot(r1, NULL, 2, dry);
        AddHot(r2, NULL, 3, dry);
        y += hwH + Px(13);

        /* 变形提示 */
        if (g_lemma >= 0 && W_lemma[0]) {
            RECT rb = { x, y, x + w, y + Px(32) };
            if (!dry) {
                FillRound(dc, rb, Px(9), T.accentSoft, T.accentSoft);
                Txt(dc, L"原形", x + Px(12), y + Px(7), Px(40), g_fTag, T.accent,
                    DT_SINGLELINE, 0);
                Txt(dc, W_lemma, x + Px(52), y + Px(5), w - Px(70), g_fSmallB,
                    T.accent, DT_SINGLELINE, 0);
            }
            SelectObject(dc, g_fSmallB);
            SIZE ls; GetTextExtentPoint32W(dc, W_lemma, (int)wcslen(W_lemma), &ls);
            RECT lr = { x + Px(50), y + Px(4), x + Px(54) + ls.cx, y + Px(28) };
            AddHot(lr, W_lemma, 0, dry);
            y += Px(42);
        }

        int bh = DrawBadges(dc, x, y, w, dry);
        y += bh ? bh + Px(18) : Px(6);

        if (g_nzh) {
            if (!dry) HLine(dc, x, x + w, y, T.line);
            y += Px(2);
            y = DrawSenseRows(dc, x, y, w, g_zh, g_nzh, 0, dry);
        }

        if (g_nforms) {
            y += Px(8);
            if (!dry) {
                Txt(dc, L"词形变化", x, y, Px(120), g_fTag, T.dim2, DT_SINGLELINE, 0);
                SelectObject(dc, g_fTag);
                SIZE ts; GetTextExtentPoint32W(dc, L"词形变化", 4, &ts);
                HLine(dc, x + ts.cx + Px(10), x + w, y + Px(7), T.line);
            }
            y += Px(26);
            int fx = x, fy = y;
            for (int i = 0; i < g_nforms; i++) {
                SelectObject(dc, g_fSmall);
                SIZE s1, s2;
                GetTextExtentPoint32W(dc, g_forms[i].label, (int)wcslen(g_forms[i].label), &s1);
                GetTextExtentPoint32W(dc, g_forms[i].form, (int)wcslen(g_forms[i].form), &s2);
                int need = s1.cx + Px(8) + s2.cx + Px(20);
                if (fx + need > x + w && fx > x) { fx = x; fy += Px(24); }
                if (!dry) {
                    Txt(dc, g_forms[i].label, fx, fy, s1.cx + Px(4), g_fSmall,
                        T.dim2, DT_SINGLELINE, 0);
                    Txt(dc, g_forms[i].form, fx + s1.cx + Px(8), fy,
                        s2.cx + Px(4), g_fSmall, T.text, DT_SINGLELINE, 0);
                }
                RECT hr = { fx + s1.cx + Px(6), fy - Px(2),
                            fx + s1.cx + Px(10) + s2.cx, fy + s2.cy + Px(2) };
                AddHot(hr, g_forms[i].form, 0, dry);
                fx += need;
            }
            y = fy + Px(30);
        }

        if (g_nen) {
            y += Px(6);
            if (!dry) {
                Txt(dc, L"英英释义", x, y, Px(120), g_fTag, T.dim2, DT_SINGLELINE, 0);
                SelectObject(dc, g_fTag);
                SIZE ts; GetTextExtentPoint32W(dc, L"英英释义", 4, &ts);
                HLine(dc, x + ts.cx + Px(10), x + w, y + Px(7), T.line);
            }
            y += Px(24);
            y = DrawSenseRows(dc, x, y, w, g_en, g_nen, 1, dry);
        }

        int ost = online_state();
        if (ost) {
            y += Px(8);
            if (!dry) {
                Txt(dc, L"例句与用法", x, y, Px(140), g_fTag, T.dim2, DT_SINGLELINE, 0);
                SelectObject(dc, g_fTag);
                SIZE ts; GetTextExtentPoint32W(dc, L"例句与用法", 5, &ts);
                HLine(dc, x + ts.cx + Px(10), x + w, y + Px(7), T.line);
            }
            y += Px(24);
            if (ost == 1) {
                y += Txt(dc, L"加载中…", x, y, w, g_fSmall, T.dim2, DT_SINGLELINE, dry);
                y += Px(10);
            } else if (ost == 2) {
                y += Txt(dc, online_examples(), x, y, w, g_fSmall, T.dim,
                         DT_WORDBREAK, dry);
                y += Px(10);
            }
        }
    } else if (g_mode == MODE_MISS) {
        Txt(dc, W_word, x, y, w, g_fHw, T.text, DT_SINGLELINE, dry);
        y += Px(56);
        y += Txt(dc, L"未收录该词", x, y, w, g_fBody, T.dim, DT_SINGLELINE, dry);
        y += Px(8);
        y += Txt(dc, L"检查拼写后重试，或试试下面的相近词", x, y, w, g_fSmall, T.dim2,
                 DT_SINGLELINE, dry);
        y += Px(26);
        if (g_nrel) {
            Txt(dc, L"你是不是想找", x, y, Px(200), g_fTag, T.dim2, DT_SINGLELINE, dry);
            y += Px(24);
            int cx2 = x, cy = y;
            for (int i = 0; i < g_nrel; i++) {
                SelectObject(dc, g_fSmall);
                SIZE sz; GetTextExtentPoint32W(dc, W_rel[i], (int)wcslen(W_rel[i]), &sz);
                int bw = sz.cx + Px(24), bh = sz.cy + Px(10);
                if (cx2 + bw > x + w) { cx2 = x; cy += bh + Px(8); }
                RECT rb = { cx2, cy, cx2 + bw, cy + bh };
                if (!dry) {
                    FillRound(dc, rb, bh / 2, T.panel, T.line);
                    Txt(dc, W_rel[i], cx2, cy + Px(5), bw, g_fSmall, T.dim,
                        DT_SINGLELINE | DT_CENTER, 0);
                }
                AddHot(rb, W_rel[i], 0, dry);
                cx2 += bw + Px(8);
            }
            y = cy + Px(40);
        }
    } else if (g_mode == MODE_FAV) {
        wchar_t head[80];
        _snwprintf_s(head, 80, _TRUNCATE, L"生词本 · %d", store_fav_count());
        Txt(dc, head, x, y, w, g_fTag, T.dim2, DT_SINGLELINE, dry);
        y += Px(30);
        if (!g_nfavlist) {
            Txt(dc, L"生词本还是空的，查词后按 Ctrl+D 收藏", x, y, w, g_fBody,
                T.dim2, DT_SINGLELINE, dry);
            y += Px(40);
        }
        for (int i = 0; i < g_nfavlist; i++) {
            RECT rb = { x, y, x + w, y + Px(48) };
            RECT dr = { x + w - Px(42), y + Px(8), x + w - Px(8), y + Px(42) };
            if (!dry) {
                FillRound(dc, rb, Px(10), T.panel, T.line);
                Txt(dc, W_fav[i], x + Px(16), y + Px(12), w - Px(240), g_fSmallB,
                    T.text, DT_SINGLELINE, 0);
                Txt(dc, W_fav_tr[i], x + Px(200), y + Px(15), w - Px(260),
                    g_fSmall, T.dim, DT_SINGLELINE | DT_END_ELLIPSIS, 0);
                IconIn(dc, L'\u00D7', dr, g_fSym, T.dim2);
            }
            AddHot(rb, W_fav[i], 0, dry);
            AddHot(dr, W_fav[i], 1, dry);
            y += Px(54);
        }
    } else {
        Txt(dc, L"F1 聚焦 \u00B7 Enter 查询 \u00B7 Esc 清空 \u00B7 Ctrl+D 收藏",
            x, y, w, g_fTag, T.dim2, DT_SINGLELINE, dry);
        y += Px(40);

        if (store_hist_count() > 0) {
            Txt(dc, L"最近查询", x, y, Px(200), g_fTag, T.dim2, DT_SINGLELINE, dry);
            y += Px(24);
            int cx2 = x, cy = y;
            int cnt = store_hist_count();
            if (cnt > 14) cnt = 14;
            for (int i = 0; i < cnt; i++) {
                wchar_t ww[128];
                utf8_to_w(store_hist(i), ww, 128);
                SelectObject(dc, g_fSmall);
                SIZE sz; GetTextExtentPoint32W(dc, ww, (int)wcslen(ww), &sz);
                int bw = sz.cx + Px(24), bh = sz.cy + Px(10);
                if (cx2 + bw > x + w) { cx2 = x; cy += bh + Px(8); }
                RECT rb = { cx2, cy, cx2 + bw, cy + bh };
                if (!dry) {
                    FillRound(dc, rb, bh / 2, T.panel, T.line);
                    Txt(dc, ww, cx2, cy + Px(5), bw, g_fSmall, T.dim,
                        DT_SINGLELINE | DT_CENTER, 0);
                }
                AddHot(rb, ww, 0, dry);
                cx2 += bw + Px(8);
            }
            y = cy + Px(40);
        }
    }

    return (y - y0) + Px(30);
}

static void DrawSug(HDC dc, int clientW, int headerH) {
    if (!g_nsug) return;
    Layout L = CalcLayout(clientW);
    SearchG SG = CalcSearch(clientW);
    int bx = SG.box.left, bw = SG.box.right - SG.box.left;
    if (bw < Px(160)) { bx = L.cx; bw = L.cw; }

    int rowH = SugRowH();
    int top = SugTop(headerH);
    RECT card = { bx, top, bx + bw, top + SugHeight() };
    FillRound(dc, card, Px(12), T.panel, T.line);

    int twoLine = L.narrow;
    for (int i = 0; i < g_nsug; i++) {
        int ry = top + Px(5) + i * rowH;
        RECT rr = { bx + Px(5), ry, bx + bw - Px(5), ry + rowH - Px(1) };
        if (i == g_sel) FillRound(dc, rr, Px(8), T.accentSoft, T.accentSoft);
        COLORREF wc = (i == g_sel) ? T.accent : T.text;

        if (twoLine) {
            Txt(dc, W_sug[i], bx + Px(14), ry + Px(6), bw - Px(28), g_fUI, wc,
                DT_SINGLELINE, 0);
            if (W_sug_ph[i][0]) {
                SelectObject(dc, g_fUI);
                SIZE s; GetTextExtentPoint32W(dc, W_sug[i],
                                              (int)wcslen(W_sug[i]), &s);
                int px2 = bx + Px(14) + s.cx + Px(10);
                if (px2 < bx + bw - Px(60)) {
                    wchar_t pbuf[80];
                    _snwprintf_s(pbuf, 80, _TRUNCATE, L"/%s/", W_sug_ph[i]);
                    Txt(dc, pbuf, px2, ry + Px(8), bw - (px2 - bx) - Px(10),
                        g_fPh, T.dim2, DT_SINGLELINE | DT_END_ELLIPSIS, 0);
                }
            }
            Txt(dc, W_sug_tr[i], bx + Px(14), ry + Px(26), bw - Px(28), g_fSmall,
                T.dim, DT_SINGLELINE | DT_END_ELLIPSIS, 0);
        } else {
            Txt(dc, W_sug[i], bx + Px(14), ry + Px(7), Px(150), g_fUI, wc,
                DT_SINGLELINE, 0);
            if (W_sug_ph[i][0]) {
                wchar_t pbuf[80];
                _snwprintf_s(pbuf, 80, _TRUNCATE, L"/%s/", W_sug_ph[i]);
                Txt(dc, pbuf, bx + Px(150), ry + Px(9), Px(130), g_fPh, T.dim2,
                    DT_SINGLELINE, 0);
            }
            Txt(dc, W_sug_tr[i], bx + Px(286), ry + Px(9), bw - Px(300), g_fSmall,
                T.dim, DT_SINGLELINE | DT_END_ELLIPSIS, 0);
        }
    }
}

/* ------------------------------------------------------------------ 窗口 */
static void load_fav_list(void) {
    g_nfavlist = store_fav_count();
    if (g_nfavlist > 64) g_nfavlist = 64;
    for (int i = 0; i < g_nfavlist; i++) {
        const char *w = store_fav(i);
        char low[128];
        dict_lower(w, (int)strlen(w), low, sizeof(low));
        int idx = dict_find(low, (int)strlen(low));
        utf8_to_w(w, W_fav[i], 64);
        W_fav_tr[i][0] = 0;
        if (idx >= 0) {
            wchar_t tr[600];
            utf8_to_w(dict_entry(idx)->translation, tr, 600);
            wchar_t *nl = wcschr(tr, L'\n');
            if (nl) *nl = 0;
            lstrcpynW(W_fav_tr[i], tr, 160);
        }
    }
}

static void toggle_fav_of_current(void) {
    if (g_mode != MODE_ENTRY || g_idx < 0) return;
    const char *word = dict_entry(g_idx)->word;
    if (store_fav_has(word)) store_fav_del(word);
    else store_fav_add(word);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void handle_enter(void) {
    if (g_nsug && g_sel >= 0 && g_sel < g_nsug) { goto_word_w(W_sug[g_sel]); return; }
    wchar_t q[128];
    GetWindowTextW(g_edit, q, 128);
    char u[512];
    WideCharToMultiByte(CP_UTF8, 0, q, -1, u, sizeof(u), NULL, NULL);
    if (!u[0]) return;
    g_nsug = 0;
    g_sel = -1;
    g_scroll = 0;
    do_lookup(u);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void handle_escape(void) {
    SetWindowTextW(g_edit, L"");
    g_nsug = 0;
    g_sel = -1;
    g_mode = MODE_HOME;
    g_scroll = 0;
    InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ------------------------------------------------------------- 窗口透明度 */
static void apply_alpha(void) {
    int a = store_alpha();
    if (a > 100) a = 100;
    if (a < 40) a = 40;
    SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)(a * 255 / 100), LWA_ALPHA);
}

static void apply_alpha_step(int d) {
    store_set_alpha(store_alpha() + d);
    apply_alpha();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

/* --------------------------------------------------------- 自绘右键面板
 * 系统 TrackPopupMenu 样式老旧也放不了滑块，这里用 WS_POPUP 自绘。
 * 圆角如果靠 SetWindowRgn，区域只有 1bit，四角必然是阶梯锯齿；
 * 所以改成分层窗口（WS_EX_LAYERED + UpdateLayeredWindow）逐像素带 alpha
 * 上屏，柔和投影也画进同一张表面，边缘才顺滑且不会露出白边。
 */
#define MENU_W    188
#define MENU_H    212
#define MENU_RAD  15
#define MENU_PAD  18
#define MENU_SH   11          /* 投影预留边距 */
#define ALPHA_MIN 40
#define ALPHA_MAX 100

static HWND g_menu;
static int  g_mDrag, g_mHoverClear;
static RECT g_mSw, g_mSliderHit, g_mClear, g_mTrack;

static HDC      g_mDC;
static HBITMAP  g_mBmp;
static DWORD   *g_mBits;
static int      g_mW, g_mH, g_mOff, g_mX, g_mY;

static void menu_render(void);

static void menu_surface_free(void) {
    if (g_mDC) {
        if (g_mBmp) { DeleteObject(g_mBmp); g_mBmp = NULL; }
        DeleteDC(g_mDC);
        g_mDC = NULL;
    }
    g_mBits = NULL;
    g_mW = g_mH = 0;
}

/* 顶向下 32bpp 表面：行序与绘制坐标一致，省掉翻转 */
static int menu_surface(int w, int h) {
    if (g_mDC && g_mW == w && g_mH == h) return 1;
    menu_surface_free();

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    g_mBmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!g_mBmp) return 0;
    g_mDC = CreateCompatibleDC(NULL);
    if (!g_mDC) { DeleteObject(g_mBmp); g_mBmp = NULL; return 0; }
    SelectObject(g_mDC, g_mBmp);
    g_mBits = (DWORD *)bits;
    g_mW = w;
    g_mH = h;
    return 1;
}

/* 圆角矩形的有符号距离：<0 在内部 */
static float rr_dist(float x, float y, float cx, float cy,
                     float hw, float hh, float rad) {
    float qx = fabsf(x - cx) - (hw - rad);
    float qy = fabsf(y - cy) - (hh - rad);
    if (qx < 0) qx = 0;
    if (qy < 0) qy = 0;
    return sqrtf(qx * qx + qy * qy) - rad;
}

static void menu_close(void) {
    if (!g_menu) return;
    DestroyWindow(g_menu);
    g_menu = NULL;
    menu_surface_free();
    g_mDrag = 0;
    g_mHoverClear = 0;
    if (g_hwnd) SetFocus(g_edit);
}

static void menu_set_alpha_at(int mx) {
    int w = g_mTrack.right - g_mTrack.left;
    if (w <= 0) return;
    int p = ALPHA_MIN + (mx - g_mTrack.left) * (ALPHA_MAX - ALPHA_MIN) / w;
    if (p < ALPHA_MIN) p = ALPHA_MIN;
    if (p > ALPHA_MAX) p = ALPHA_MAX;
    if (p == store_alpha()) return;
    store_alpha_set_mem(p);
    apply_alpha();
    menu_render();
}

static void draw_switch(HDC dc, RECT r, int on) {
    int rad = (r.bottom - r.top) / 2;
    COLORREF track = on ? T.accent : blendclr(T.dim2, T.panel, 46);
    FillRoundAA(dc, r, rad, track, track, 0);

    int d = (r.bottom - r.top) - Px(4);
    int kx = on ? r.right - Px(2) - d : r.left + Px(2);
    RECT k = { kx, r.top + Px(2), kx + d, r.bottom - Px(2) };
    COLORREF knob = T.dark ? RGB(230, 230, 232) : T.panel;
    FillRoundAA(dc, k, d / 2, knob, knob, 0);
}

static void menu_content(HDC dc, RECT pr) {
    int pad = Px(MENU_PAD);
    int x0 = pr.left + pad;
    int xr = pr.right - pad;
    int txtw = xr - x0;
    int y = pr.top + pad;

    /* 清空最近查询（放最上，最近用得多） */
    g_mClear.left = x0 - Px(8);
    g_mClear.right = xr + Px(8);
    g_mClear.top = y;
    g_mClear.bottom = y + Px(30);
    if (g_mHoverClear)
        FillRoundAA(dc, g_mClear, Px(7), T.hover, T.hover, 0);

    int has = store_hist_count() > 0;
    Txt(dc, L"清空最近查询", x0, y + Px(6), txtw, g_fUI,
        has ? T.text : T.dim2, DT_SINGLELINE, 0);

    /* 分隔线 */
    y = g_mClear.bottom + Px(20);
    HLine(dc, x0, xr, y, T.line);
    y += Px(20);

    /* 窗口置顶 */
    Txt(dc, L"窗口置顶", x0, y + Px(3), txtw - Px(46), g_fUI, T.text,
        DT_SINGLELINE, 0);
    g_mSw.left = xr - Px(36);
    g_mSw.right = xr;
    g_mSw.top = y + Px(2);
    g_mSw.bottom = g_mSw.top + Px(21);
    draw_switch(dc, g_mSw, store_topmost());

    /* 分隔线 */
    y += Px(26) + Px(22);
    HLine(dc, x0, xr, y, T.line);
    y += Px(20);

    /* 不透明度 */
    Txt(dc, L"不透明度", x0, y, txtw, g_fUI, T.text, DT_SINGLELINE, 0);
    {
        wchar_t buf[16];
        _snwprintf_s(buf, 16, _TRUNCATE, L"%d%%", store_alpha());
        Txt(dc, buf, pr.right - pad - Px(56), y, Px(56), g_fSmall, T.dim,
            DT_SINGLELINE | DT_RIGHT, 0);
    }

    y += Px(28);
    g_mTrack.left = x0;
    g_mTrack.right = xr;
    g_mTrack.top = y;
    g_mTrack.bottom = y + Px(4);
    COLORREF trk = blendclr(T.dim2, T.panel, 40);
    FillRoundAA(dc, g_mTrack, Px(2), trk, trk, 0);

    int span = (g_mTrack.right - g_mTrack.left);
    int fillw = span * (store_alpha() - ALPHA_MIN) / (ALPHA_MAX - ALPHA_MIN);
    RECT fr = { g_mTrack.left, g_mTrack.top, g_mTrack.left + fillw,
                g_mTrack.bottom };
    FillRoundAA(dc, fr, Px(2), T.accent, T.accent, 0);

    int kd = Px(13);
    int kcx = g_mTrack.left + fillw;
    int kcy = (g_mTrack.top + g_mTrack.bottom) / 2;
    RECT kr = { kcx - kd / 2, kcy - kd / 2, kcx + kd / 2 + 1, kcy + kd / 2 + 1 };
    COLORREF kn = T.dark ? RGB(230, 230, 232) : T.panel;
    FillRoundAA(dc, kr, kd / 2, kn, T.accent, 100);

    g_mSliderHit.left = g_mTrack.left - Px(6);
    g_mSliderHit.right = g_mTrack.right + Px(6);
    g_mSliderHit.top = kcy - Px(11);
    g_mSliderHit.bottom = kcy + Px(11);
}

static void menu_render(void) {
    if (!g_menu) return;

    int pw = Px(MENU_W), ph = Px(MENU_H);
    g_mOff = Px(MENU_SH);
    int sw = pw + g_mOff * 2, sh = ph + g_mOff * 2;
    if (!menu_surface(sw, sh)) return;

    HDC dc = g_mDC;

    /* 先按面板色铺满整张表面，边缘与投影区域稍后按覆盖率重算 alpha */
    RECT all = { 0, 0, sw, sh };
    HBRUSH br = CreateSolidBrush(T.panel);
    FillRect(dc, &all, br);
    DeleteObject(br);

    RECT pr = { g_mOff, g_mOff, g_mOff + pw, g_mOff + ph };
    FillRoundAA(dc, pr, Px(MENU_RAD), T.panel, T.line, 100);
    menu_content(dc, pr);

    /* 覆盖率 + 投影 → 预乘 BGRA */
    float cx = g_mOff + pw * 0.5f, cy = g_mOff + ph * 0.5f;
    float hw = pw * 0.5f, hh = ph * 0.5f;
    float rad = (float)Px(MENU_RAD);
    float soff = (float)Px(3), sblur = (float)Px(14);

    for (int y = 0; y < sh; y++) {
        DWORD *row = g_mBits + (size_t)y * (size_t)sw;
        for (int x = 0; x < sw; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float p = 0.5f - rr_dist(px, py, cx, cy, hw, hh, rad);
            if (p < 0.0f) p = 0.0f;
            else if (p > 1.0f) p = 1.0f;

            float s = 0.0f;
            if (p < 0.999f) {
                float ds = rr_dist(px, py, cx, cy + soff, hw, hh, rad);
                float t = ds / sblur;
                if (t < 1.0f) {
                    float k = 1.0f - (t < 0.0f ? 0.0f : t);
                    s = 0.36f * k * k;
                }
            }

            DWORD c = row[x];
            float a = p + s * (1.0f - p);
            if (a <= 0.004f) { row[x] = 0; continue; }

            int ia = (int)(a * 255.0f + 0.5f);
            int ib = (int)((c & 0xFF) * p + 0.5f);
            int ig = (int)(((c >> 8) & 0xFF) * p + 0.5f);
            int ir = (int)(((c >> 16) & 0xFF) * p + 0.5f);
            if (ia > 255) ia = 255;
            row[x] = ((DWORD)ia << 24) | ((DWORD)ir << 16) |
                     ((DWORD)ig << 8) | (DWORD)ib;
        }
    }

    POINT dst = { g_mX, g_mY };
    SIZE sz = { sw, sh };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(g_menu, NULL, &dst, &sz, dc, &src, 0, &bf, ULW_ALPHA);
}

static LRESULT CALLBACK MenuProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        /* 内容全在分层表面上，这里只把绘制区域验证掉 */
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (g_mDrag) { menu_set_alpha_at(pt.x); return 0; }
        int hv = PtInRect(&g_mClear, pt) && store_hist_count() > 0;
        if (hv != g_mHoverClear) {
            g_mHoverClear = hv;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
            menu_render();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_mHoverClear) { g_mHoverClear = 0; menu_render(); }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (PtInRect(&g_mSw, pt)) {
            int v = !store_topmost();
            store_set_topmost(v);
            SetWindowPos(g_hwnd, v ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            InvalidateRect(g_hwnd, NULL, FALSE);
            menu_render();
        } else if (PtInRect(&g_mSliderHit, pt)) {
            g_mDrag = 1;
            SetCapture(h);
            menu_set_alpha_at(pt.x);
        } else if (PtInRect(&g_mClear, pt) && store_hist_count() > 0) {
            store_hist_clear();
            InvalidateRect(g_hwnd, NULL, FALSE);
            menu_close();
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_mDrag) {
            g_mDrag = 0;
            ReleaseCapture();
            store_save();          /* 松手才落盘 */
        }
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { menu_close(); return 0; }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE:
        menu_close();
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void menu_open(void) {
    if (g_menu) { menu_close(); return; }

    static int regd = 0;
    if (!regd) {
        WNDCLASSEXW mc = {0};
        mc.cbSize = sizeof(mc);
        mc.lpfnWndProc = MenuProc;
        mc.hInstance = GetModuleHandleW(NULL);
        mc.hCursor = LoadCursor(NULL, IDC_ARROW);
        mc.lpszClassName = L"LiteDictMenu";
        RegisterClassExW(&mc);
        regd = 1;
    }

    int pw = Px(MENU_W), ph = Px(MENU_H);
    int off = Px(MENU_SH);
    int w = pw + off * 2, hh = ph + off * 2;

    POINT pt; GetCursorPos(&pt);
    RECT wa;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
        wa.left = 0; wa.top = 0;
        wa.right = GetSystemMetrics(SM_CXSCREEN);
        wa.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int x = pt.x - pw + Px(18) - off;
    int y = pt.y - Px(10) - off;
    if (x < wa.left + Px(2)) x = wa.left + Px(2);
    if (x + w > wa.right - Px(2)) x = wa.right - w - Px(2);
    if (y + hh > wa.bottom - Px(2)) y = wa.bottom - hh - Px(2);
    if (y < wa.top + Px(2)) y = wa.top + Px(2);

    g_mX = x;
    g_mY = y;

    g_menu = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
                             L"LiteDictMenu", L"", WS_POPUP, x, y, w, hh,
                             g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_menu) return;

    menu_render();
    ShowWindow(g_menu, SW_SHOWNA);
    SetForegroundWindow(g_menu);
    SetFocus(g_menu);
}

/* ------------------------------------------------------------- 全局呼出 */
#define HOTKEY_ID 1

static void summon(void) {
    if (g_menu) menu_close();

    if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);

    /* 后台进程直接 SetForegroundWindow 会被拒，先挂到前台线程 */
    HWND fg = GetForegroundWindow();
    DWORD ftid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    DWORD mtid = GetCurrentThreadId();
    if (ftid && ftid != mtid) AttachThreadInput(ftid, mtid, TRUE);
    SetForegroundWindow(g_hwnd);
    BringWindowToTop(g_hwnd);
    if (ftid && ftid != mtid) AttachThreadInput(ftid, mtid, FALSE);

    SetFocus(g_edit);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN) {
        int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        int shf  = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl && w == 'D') { toggle_fav_of_current(); return 0; }
        if (ctrl && shf && (w == VK_UP || w == VK_DOWN)) {
            apply_alpha_step(w == VK_UP ? 5 : -5);
            return 0;
        }
        if (w == VK_DOWN) {
            if (g_nsug) { g_sel = (g_sel + 1) % g_nsug; InvalidateRect(g_hwnd, NULL, FALSE); }
            return 0;
        }
        if (w == VK_UP) {
            if (g_nsug) {
                g_sel = (g_sel <= 0) ? g_nsug - 1 : g_sel - 1;
                InvalidateRect(g_hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (w == VK_RETURN) { handle_enter(); return 0; }
        if (w == VK_ESCAPE) { handle_escape(); return 0; }
        if (w == VK_F1) {
            SendMessageW(g_edit, EM_SETSEL, 0, -1);   /* 全选，便于重新输入 */
            return 0;
        }
    }
    if (m == WM_SETFOCUS || m == WM_KILLFOCUS) {
        LRESULT r = CallWindowProcW(g_oldEdit, h, m, w, l);
        InvalidateRect(g_hwnd, NULL, FALSE);   /* 刷新搜索框描边 */
        return r;
    }
    return CallWindowProcW(g_oldEdit, h, m, w, l);
}

/* 自绘滚动条：细、圆角、悬停加深，避免系统滚动条破坏整体观感 */
static int  g_sbNeed = 0;
static int  g_sbDrag = 0, g_sbGrab = 0, g_sbHover = 0;
static RECT g_sbThumb;

static void sb_metrics(RECT *track, RECT *thumb) {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int top = Px(66) + Px(6);
    int bot = rc.bottom - Px(6);
    if (bot <= top) bot = top + Px(20);
    int w = Px(6);
    int x = rc.right - w - Px(4);
    track->left = x; track->right = x + w;
    track->top = top; track->bottom = bot;

    int page = rc.bottom - Px(66);
    if (page < 1) page = 1;
    int total = g_total > page ? g_total : page;
    int trackH = bot - top;
    int thumbH = trackH * page / total;
    int minH = Px(30);
    if (thumbH < minH) thumbH = minH;
    if (thumbH > trackH) thumbH = trackH;

    int maxs = g_total - page;
    int ty = top;
    if (maxs > 0)
        ty = top + (trackH - thumbH) * g_scroll / maxs;

    thumb->left = track->left; thumb->right = track->right;
    thumb->top = ty; thumb->bottom = ty + thumbH;
}

static void update_scroll(int clientH) {
    /* 内容装得下就完全不出现滚动条 */
    g_sbNeed = (g_total > clientH + Px(2));
    int maxs = g_total - clientH;
    if (maxs < 0) maxs = 0;
    if (g_scroll > maxs) g_scroll = maxs;
    if (g_scroll < 0) g_scroll = 0;
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_hwnd = h;
        {
            HMODULE u = GetModuleHandleW(L"user32.dll");
            typedef UINT (WINAPI *PFN)(HWND);
            PFN f = (PFN)GetProcAddress(u, "GetDpiForWindow");
            if (f) g_dpi = (int)f(h);
            if (g_dpi <= 0) g_dpi = 96;
        }
        S = (float)g_dpi / 96.0f;
        theme_init();
        {
            BOOL dk = T.dark ? TRUE : FALSE;
            DwmSetWindowAttribute(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                                  &dk, sizeof(dk));
        }
        build_fonts();
        g_brEdit = CreateSolidBrush(T.panel);

        g_edit = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
            0, 0, 10, 10, h, (HMENU)100, GetModuleHandleW(NULL), NULL);
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_fUI, TRUE);
        g_oldEdit = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC,
                                               (LONG_PTR)EditProc);
        /* 单键 F1 呼出：不带修饰键，所以要 MOD_NOREPEAT 免得长按刷屏 */
        if (!RegisterHotKey(h, HOTKEY_ID, MOD_NOREPEAT, VK_F1))
            RegisterHotKey(h, HOTKEY_ID, 0, VK_F1);

        /* 查的是英文词，把输入法切到英文模式，免得每句都跳出候选框 */
        {
            HIMC himc = ImmGetContext(g_edit);
            if (himc) {
                ImmSetConversionStatus(himc, IME_CMODE_ALPHANUMERIC, IME_SMODE_NONE);
                ImmReleaseContext(g_edit, himc);
            }
        }

        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(h, &rc);
        g_clientW = rc.right;
        g_clientH = rc.bottom;
        LayoutEdit(rc.right, Px(66));
        update_scroll(rc.bottom - Px(66));
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, T.text);
        SetBkColor(dc, T.panel);
        return (LRESULT)g_brEdit;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc0 = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        int cw = rc.right, ch = rc.bottom;
        int headerH = Px(66);

        HDC dc = CreateCompatibleDC(dc0);
        HBITMAP bmp = CreateCompatibleBitmap(dc0, cw, ch);
        HGDIOBJ ob = SelectObject(dc, bmp);

        HBRUSH bg = CreateSolidBrush(T.bg);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        g_nhot = 0;
        g_total = Render(dc, cw, headerH, g_scroll, 1);
        update_scroll(ch - headerH);
        Render(dc, cw, headerH, g_scroll, 0);

        /* 顶栏 */
        RECT hdr = { 0, 0, cw, headerH };
        HBRUSH hb = CreateSolidBrush(T.bg);
        FillRect(dc, &hdr, hb);
        DeleteObject(hb);

        Layout L = CalcLayout(cw);

        if (L.brandW) {
            SelectObject(dc, g_fUI);
            SIZE b1;
            GetTextExtentPoint32W(dc, L"Lite", 4, &b1);
            Txt(dc, L"Lite", L.cx, Px(23), Px(60), g_fUI, T.dim, DT_SINGLELINE, 0);
            Txt(dc, L"Dict", L.cx + b1.cx, Px(23), Px(60), g_fUI, T.text,
                DT_SINGLELINE, 0);
        }

        /* 搜索框 */
        SearchG SG = CalcSearch(cw);
        {
            int foc = (GetFocus() == g_edit);
            FillRoundAA(dc, SG.box, Px(12), T.panel,
                        foc ? T.accent : T.line, foc ? 90 : 100);
            RECT ib = { SG.ix, SG.box.top, SG.ix + SG.isz, SG.box.bottom };
            IconIn(dc, L'\uE721', ib, g_fIcon, foc ? T.accent : T.dim2);
        }

        /* 生词本 */
        RECT fbr = { L.fx, Px(16), L.fx + L.fw, Px(50) };
        {
            int on = (g_mode == MODE_FAV);
            FillRoundAA(dc, fbr, Px(10),
                        on ? blendclr(T.accentSoft, T.panel, 80) : T.panel,
                        on ? blendclr(T.accent, T.panel, 45) : T.line, 100);
            if (L.narrow) {
                IconIn(dc, L'\uE734', fbr, g_fIcon, on ? T.accent : T.dim);
            } else {
                RECT ib = { L.fx, fbr.top, L.fx + Px(32), fbr.bottom };
                IconIn(dc, L'\uE734', ib, g_fIcon, on ? T.accent : T.dim);
                Txt(dc, L"生词本", L.fx + Px(34), Px(24), Px(56), g_fSmall,
                    on ? T.accent : T.dim, DT_SINGLELINE, 0);
            }
        }
        AddHot(fbr, NULL, 4, 0);

        HLine(dc, 0, cw, headerH - 1, T.line);

        DrawSug(dc, cw, headerH);

        if (g_sbNeed) {
            RECT tr, th;
            sb_metrics(&tr, &th);
            g_sbThumb = th;
            int base = T.dark ? 74 : 56;
            COLORREF c = g_sbDrag ? blendclr(T.dim, T.panel, T.dark ? 94 : 82)
                       : g_sbHover ? blendclr(T.dim, T.panel, T.dark ? 82 : 70)
                                   : blendclr(T.dim2, T.panel, base);
            FillRoundAA(dc, th, (th.right - th.left) / 2, c, c, 0);
        }

        BitBlt(dc0, 0, 0, cw, ch, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)l;
        mm->ptMinTrackSize.x = Px(340);
        mm->ptMinTrackSize.y = Px(240);
        return 0;
    }
    case WM_HOTKEY:
        if (w == HOTKEY_ID) { summon(); return 0; }
        return 0;
    case WM_RBUTTONUP:
        menu_open();
        return 0;
    case WM_MOUSEWHEEL: {
        RECT rc; GetClientRect(h, &rc);
        int page = rc.bottom - Px(66);
        g_scroll -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * Px(64);
        int maxs = g_total - page;
        if (maxs < 0) maxs = 0;
        if (g_scroll > maxs) g_scroll = maxs;
        if (g_scroll < 0) g_scroll = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_VSCROLL: {
        RECT rc; GetClientRect(h, &rc);
        int page = rc.bottom - Px(66);
        switch (LOWORD(w)) {
        case SB_LINEDOWN: g_scroll += Px(32); break;
        case SB_LINEUP: g_scroll -= Px(32); break;
        case SB_PAGEDOWN: g_scroll += page; break;
        case SB_PAGEUP: g_scroll -= page; break;
        case SB_THUMBTRACK: g_scroll = HIWORD(w); break;
        }
        int maxs = g_total - page;
        if (maxs < 0) maxs = 0;
        if (g_scroll > maxs) g_scroll = maxs;
        if (g_scroll < 0) g_scroll = 0;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        /* 先判滚动条，避免误触发下面的内容点击 */
        if (g_sbNeed && PtInRect(&g_sbThumb, pt)) {
            g_sbDrag = 1;
            g_sbGrab = pt.y - g_sbThumb.top;
            SetCapture(h);
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        for (int i = g_nhot - 1; i >= 0; i--) {
            if (!PtInRect(&g_hot[i].rc, pt)) continue;
            int k = g_hot[i].kind;
            if (k == 0) goto_word_w(g_hot[i].word);
            else if (k == 1) {
                char u[256];
                WideCharToMultiByte(CP_UTF8, 0, g_hot[i].word, -1, u, sizeof(u), NULL, NULL);
                store_fav_del(u);
                load_fav_list();
                InvalidateRect(h, NULL, FALSE);
            } else if (k == 2) {
                if (g_idx >= 0) online_play(dict_entry(g_idx)->word);
            } else if (k == 3) {
                toggle_fav_of_current();
            } else if (k == 4) {
                if (g_mode == MODE_FAV) {
                    g_mode = MODE_HOME;
                } else {
                    g_mode = MODE_FAV;
                    load_fav_list();
                }
                g_scroll = 0;
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }
        if (g_nsug) {
            int headerH = Px(66);
            int top = SugTop(headerH);
            if (pt.y >= top && pt.y < top + SugHeight()) {
                int i = (pt.y - top - Px(5)) / SugRowH();
                if (i >= 0 && i < g_nsug) { goto_word_w(W_sug[i]); return 0; }
            } else {
                g_nsug = 0;
                g_sel = -1;
                InvalidateRect(h, NULL, FALSE);
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (g_sbDrag) {
            RECT tr, th;
            sb_metrics(&tr, &th);
            RECT rc;
            GetClientRect(h, &rc);
            int page = rc.bottom - Px(66);
            int maxs = g_total - page;
            int span = (tr.bottom - tr.top) - (th.bottom - th.top);
            if (span > 0 && maxs > 0) {
                int want = (pt.y - g_sbGrab - tr.top) * maxs / span;
                if (want < 0) want = 0;
                if (want > maxs) want = maxs;
                if (want != g_scroll) {
                    g_scroll = want;
                    InvalidateRect(h, NULL, FALSE);
                }
            }
            return 0;
        }
        int hv = (g_sbNeed && PtInRect(&g_sbThumb, pt));
        if (hv != g_sbHover) {
            g_sbHover = hv;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_sbHover) { g_sbHover = 0; InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_LBUTTONUP:
        if (g_sbDrag) {
            g_sbDrag = 0;
            ReleaseCapture();
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (w == VK_F1) {
            SetFocus(g_edit);
            SendMessageW(g_edit, EM_SETSEL, 0, -1);
            return 0;
        }
        if (w == VK_NEXT) { g_scroll += 400; InvalidateRect(h, NULL, FALSE); return 0; }
        if (w == VK_PRIOR) { g_scroll -= 400; if (g_scroll < 0) g_scroll = 0;
                             InvalidateRect(h, NULL, FALSE); return 0; }
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == 100 && HIWORD(w) == EN_CHANGE) {
            update_suggest();
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_edit);
        return 0;
    case WM_APP_ONLINE:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(h, HOTKEY_ID);
        store_save();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show) {
    (void)hp; (void)cmd;
    SetProcessDPIAware();

    if (!dict_open()) {
        MessageBoxW(NULL, L"未找到词库文件 data\\dict.ldict\n\n请把 data 文件夹放在 LiteDict.exe 同级目录。",
                    L"LiteDict", MB_ICONERROR);
        return 1;
    }
    store_load();

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"LiteDictWnd";
    wc.hIcon = (HICON)LoadImageW(hi, L"APPICON", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm = (HICON)LoadImageW(hi, L"APPICON", IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    /* 默认小窗：面积约为原 1020x760 的 1/6，靠屏幕右上角便于常驻 */
    HDC sdc = GetDC(NULL);
    int dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(NULL, sdc);
    if (dpi <= 0) dpi = 96;
    float sc = (float)dpi / 96.0f;
    int w = (int)(416 * sc + 0.5f), hgt = (int)(390 * sc + 0.5f);

    RECT wa;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
        wa.left = 0; wa.top = 0;
        wa.right = GetSystemMetrics(SM_CXSCREEN);
        wa.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int px = wa.right - w - (int)(24 * sc + 0.5f);
    int py = wa.top + (int)(24 * sc + 0.5f);
    if (px < wa.left) px = wa.left;

    HWND hwnd = CreateWindowExW(WS_EX_LAYERED, L"LiteDictWnd", L"LiteDict",
        WS_OVERLAPPEDWINDOW,
        px, py, w, hgt, NULL, NULL, hi, NULL);
    if (!hwnd) return 1;
    online_init(hwnd);
    apply_alpha();
    if (store_topmost())
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    SetFocus(g_edit);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    dict_close();
    return 0;
}
