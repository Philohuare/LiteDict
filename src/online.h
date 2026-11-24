#pragma once
#include <windows.h>

#define WM_APP_ONLINE (WM_APP + 1)

void online_init(HWND notify);

/* 异步取例句；state: 0 无 / 1 加载中 / 2 就绪 */
void online_fetch(const char *word);
int  online_state(void);
const wchar_t *online_examples(void);

/* 发音：优先真人音频，失败退回系统 TTS */
void online_play(const char *word);
