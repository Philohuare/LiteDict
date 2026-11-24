#include "dict.h"
#include <string.h>
#include "store.h"

/* 生词本与历史：纯文本存储，格式 "F\t单词" / "H\t单词" */

#define MAX_ITEMS 500
#define MAX_WORD 128

static char g_fav[MAX_ITEMS][MAX_WORD];
static int g_nfav = 0;
static char g_hist[MAX_ITEMS][MAX_WORD];
static int g_nhist = 0;
static int g_topmost = 0;
static int g_alpha = 100;

static void path_of(char *out, int cap) {
    char p[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, p, MAX_PATH);
    char *s;
    if (n && n < MAX_PATH) {
        s = strrchr(p, '\\');
        if (s) *s = 0;
        wsprintfA(out, "%s\\data\\user.txt", p);
    } else {
        lstrcpynA(out, "data\\user.txt", cap);
    }
}

static void add_to(char arr[MAX_ITEMS][MAX_WORD], int *n, const char *w) {
    if (!w || !*w) return;
    for (int i = 0; i < *n; i++) {
        if (lstrcmpiA(arr[i], w) == 0) {           /* 已存在，移到最前 */
            char tmp[MAX_WORD];
            lstrcpynA(tmp, arr[i], MAX_WORD);
            memmove(&arr[1][0], &arr[0][0], sizeof(arr[0]) * (size_t)i);
            lstrcpynA(arr[0], tmp, MAX_WORD);
            return;
        }
    }
    if (*n >= MAX_ITEMS) (*n)--;
    memmove(&arr[1][0], &arr[0][0], sizeof(arr[0]) * (size_t)(*n));
    lstrcpynA(arr[0], w, MAX_WORD);
    (*n)++;
}

void store_load(void) {
    char path[MAX_PATH];
    path_of(path, MAX_PATH);
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    char buf[65536];
    DWORD got = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(f);
    buf[got] = 0;

    g_nfav = g_nhist = 0;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        /* 手工编辑过的文件可能是 CRLF，行尾的 \r 必须切掉，
         * 否则 "might\r" 这种词在卡片上看着正常、查词永远查不到 */
        int ln = (int)strlen(line);
        while (ln > 0 && (unsigned char)line[ln - 1] <= ' ') line[--ln] = 0;
        if (line[0] && line[1] == '\t') {
            char *w = line + 2;
            if (line[0] == 'F') {
                if (g_nfav < MAX_ITEMS) lstrcpynA(g_fav[g_nfav++], w, MAX_WORD);
            } else if (line[0] == 'H') {
                if (g_nhist < MAX_ITEMS) lstrcpynA(g_hist[g_nhist++], w, MAX_WORD);
            } else if (line[0] == 'T') {
                g_topmost = (w[0] == '1');
            } else if (line[0] == 'A') {
                int v = 0;
                for (char *q = w; *q >= '0' && *q <= '9'; q++) v = v * 10 + (*q - '0');
                if (v >= 40 && v <= 100) g_alpha = v;
            }
        }
        line = nl ? nl + 1 : NULL;
    }
}

void store_save(void) {
    char path[MAX_PATH];
    path_of(path, MAX_PATH);
    CreateDirectoryA("data", NULL);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    char buf[65536];
    int p = 0;
    for (int i = 0; i < g_nfav && p < 60000; i++)
        p += wsprintfA(buf + p, "F\t%s\n", g_fav[i]);
    for (int i = 0; i < g_nhist && p < 65000; i++)
        p += wsprintfA(buf + p, "H\t%s\n", g_hist[i]);
    if (p < 65400)
        p += wsprintfA(buf + p, "T\t%d\n", g_topmost);
    if (p < 65450)
        p += wsprintfA(buf + p, "A\t%d\n", g_alpha);
    DWORD wr = 0;
    WriteFile(f, buf, (DWORD)p, &wr, NULL);
    CloseHandle(f);
}

int store_topmost(void) { return g_topmost; }
void store_set_topmost(int v) {
    g_topmost = v ? 1 : 0;
    store_save();
}

int store_alpha(void) { return g_alpha; }
void store_alpha_set_mem(int v) {
    if (v < 40) v = 40;
    if (v > 100) v = 100;
    g_alpha = v;
}
void store_set_alpha(int v) {
    if (v < 40) v = 40;
    if (v > 100) v = 100;
    g_alpha = v;
    store_save();
}

int store_fav_count(void) { return g_nfav; }
const char *store_fav(int i) { return (i >= 0 && i < g_nfav) ? g_fav[i] : ""; }
int store_hist_count(void) { return g_nhist; }
const char *store_hist(int i) { return (i >= 0 && i < g_nhist) ? g_hist[i] : ""; }

int store_fav_has(const char *w) {
    for (int i = 0; i < g_nfav; i++)
        if (lstrcmpiA(g_fav[i], w) == 0) return 1;
    return 0;
}

void store_fav_add(const char *w) { add_to(g_fav, &g_nfav, w); store_save(); }

void store_fav_del(const char *w) {
    for (int i = 0; i < g_nfav; i++) {
        if (lstrcmpiA(g_fav[i], w) == 0) {
            memmove(&g_fav[i][0], &g_fav[i + 1][0],
                    sizeof(g_fav[0]) * (size_t)(g_nfav - i - 1));
            g_nfav--;
            break;
        }
    }
    store_save();
}

void store_hist_add(const char *w) { add_to(g_hist, &g_nhist, w); store_save(); }

void store_hist_clear(void) {
    g_nhist = 0;
    store_save();
}
