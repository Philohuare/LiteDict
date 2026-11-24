#pragma once
#include <stdint.h>
#include <windows.h>

/* .ldict 二进制词库的读取层：整文件 mmap，二分查找，无第三方依赖 */

typedef struct {
    const char *word;        /* 原样词 */
    const char *phonetic;
    const char *definition;
    const char *translation;
    const char *tag;
    const char *exchange;
    uint32_t frq, bnc;
    uint16_t collins, oxford;
} DictEntry;

/* 打开词库：依次尝试 exe 同目录 data\dict.ldict、当前目录 data\dict.ldict */
int dict_open(void);
void dict_close(void);
int dict_ok(void);
int dict_count(void);

/* 精确查（key 须已转小写） */
int dict_find(const char *wl, int len);

/* 变形 -> 词条索引，找不到返回 -1 */
int dict_var_base(const char *wl, int len);

/* 前缀匹配，返回写入 out 的条数 */
/* by_freq=1 时按 常用度分档 + 词长 排序 */
int dict_prefix(const char *pre, int len, int *out, int maxn, int by_freq);

const DictEntry *dict_entry(int idx);
const char *dict_wl(int idx);
uint32_t dict_wl_len(int idx);

/* ASCII 小写化 */
int dict_lower(const char *in, int inlen, char *out, int cap);

/* UTF-8 -> UTF-16，返回写入字符数；buf 可为 NULL 仅测量 */
int utf8_to_w(const char *s, wchar_t *buf, int cap);
