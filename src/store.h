#pragma once
void store_load(void);
void store_save(void);
int store_fav_count(void);
const char *store_fav(int i);
int store_hist_count(void);
const char *store_hist(int i);
int store_fav_has(const char *w);
void store_fav_add(const char *w);
void store_fav_del(const char *w);
void store_hist_add(const char *w);
void store_hist_clear(void);
int store_topmost(void);
void store_set_topmost(int v);
int store_alpha(void);              /* 不透明度百分比 40..100 */
void store_set_alpha(int v);
void store_alpha_set_mem(int v);   /* 只改内存，拖动滑块时用 */
