#include "dict.h"
#include <stdio.h>
#include <string.h>

static void show(const char *w) {
    char low[256];
    int n = dict_lower(w, (int)strlen(w), low, sizeof(low));
    int idx = dict_find(low, n);
    if (idx < 0) {
        int base = dict_var_base(low, n);
        if (base >= 0) {
            const DictEntry *e = dict_entry(base);
            printf("%-14s -> 变形 -> %s | %.60s\n", w, e->word, e->translation);
            return;
        }
        printf("%-14s -> 未收录\n", w);
        return;
    }
    const DictEntry *e = dict_entry(idx);
    printf("%-14s -> %s /%s/ collins=%d frq=%u | %.62s\n", w, e->word,
           e->phonetic, e->collins, e->frq, e->translation);
}

int main(void) {
    if (!dict_open()) { printf("词库打开失败\n"); return 1; }
    printf("词条数: %d\n\n", dict_count());

    show("hello");
    show("study");
    show("beautiful");
    show("went");
    show("mice");
    show("serendipity");
    show("might");
    show("may");
    show("mighty");
    show("so");
    show("zzzzqqq");

    printf("\n--- 前缀 stud ---\n");
    int out[12];
    int n = dict_prefix("stud", 4, out, 8, 1);
    for (int i = 0; i < n; i++) {
        const DictEntry *e = dict_entry(out[i]);
        printf("  %-12s %.40s\n", e->word, e->translation);
    }

    printf("\n--- 前缀 app ---\n");
    n = dict_prefix("app", 3, out, 6, 1);
    for (int i = 0; i < n; i++) printf("  %s\n", dict_entry(out[i])->word);

    /* 二分正确性抽检：全表顺序必须严格递增 */
    int bad = 0;
    for (int i = 1; i < dict_count(); i++) {
        const char *a = dict_wl(i - 1), *b = dict_wl(i);
        if (strcmp(a, b) >= 0) { if (bad < 3) printf("顺序异常: %s >= %s\n", a, b); bad++; }
    }
    printf("\n排序检查: %s (异常 %d)\n", bad ? "失败" : "通过", bad);

    dict_close();
    return 0;
}
