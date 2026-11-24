#include "online.h"
#include <winhttp.h>
#include <sapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static HWND g_notify = NULL;
static CRITICAL_SECTION g_cs;
static volatile LONG g_state = 0;          /* 0 无 / 1 加载中 / 2 就绪 */
static wchar_t g_examples[4096];
static char g_word[128];

/* ------------------------------------------------------------ HTTP */
static char *http_get(const wchar_t *host, const wchar_t *path, DWORD *outLen) {
    *outLen = 0;
    HINTERNET s = WinHttpOpen(L"LiteDict/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return NULL;
    WinHttpSetTimeouts(s, 4000, 4000, 5000, 6000);

    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c) { WinHttpCloseHandle(s); return NULL; }

    HINTERNET r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE);
    if (!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(s); return NULL; }

    char *buf = NULL;
    if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        size_t cap = 65536, len = 0;
        buf = (char *)malloc(cap);
        if (buf) {
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0) break;
                if (len + avail + 1 > cap) {
                    while (len + avail + 1 > cap) cap *= 2;
                    char *nb = (char *)realloc(buf, cap);
                    if (!nb) break;
                    buf = nb;
                }
                DWORD got = 0;
                if (!WinHttpReadData(r, buf + len, avail, &got) || got == 0) break;
                len += got;
                if (len > 5 * 1024 * 1024) break;
            }
            buf[len] = 0;
            *outLen = (DWORD)len;
        }
    }
    WinHttpCloseHandle(r);
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return buf;
}

/* ------------------------------------------------------------ JSON */
/* p 指向字符串内容起点（开引号之后），解析到宽字符，返回收尾位置 */
static const char *json_str(const char *p, wchar_t *out, int cap) {
    int n = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': if (n < cap - 1) out[n++] = L' '; break;
            case 't': if (n < cap - 1) out[n++] = L' '; break;
            case 'r': break;
            case 'u': {
                unsigned v = 0;
                for (int i = 0; i < 4 && p[1]; i++) {
                    p++;
                    char c = *p;
                    v <<= 4;
                    if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
                    else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
                }
                if (n < cap - 1) out[n++] = (wchar_t)v;
                break;
            }
            default: if (n < cap - 1) out[n++] = (wchar_t)(unsigned char)*p; break;
            }
            p++;
        } else if ((unsigned char)*p >= 0x80) {
            /* 多字节 UTF-8：整段解码，避免把 ’ 之类拆成 Latin-1 乱码 */
            unsigned char u = (unsigned char)*p;
            int len = (u >= 0xF0) ? 4 : (u >= 0xE0) ? 3 : (u >= 0xC0) ? 2 : 1;
            wchar_t wc = 0;
            if (MultiByteToWideChar(CP_UTF8, 0, p, len, &wc, 1) == 1) {
                if (n < cap - 1) out[n++] = wc;
                p += len;
            } else {
                p++;
            }
        } else {
            if (n < cap - 1) out[n++] = (wchar_t)(unsigned char)*p;
            p++;
        }
    }
    out[n] = 0;
    return (*p == '"') ? p + 1 : p;
}

static const char *find_key(const char *p, const char *key) {
    size_t kl = strlen(key);
    for (; *p; p++) {
        if (*p == '"' && strncmp(p + 1, key, kl) == 0 && p[1 + kl] == '"') {
            const char *q = p + 2 + kl;
            while (*q == ' ') q++;
            if (*q == ':') q++;
            while (*q == ' ') q++;
            if (*q == '"') return q + 1;
            return q;
        }
    }
    return NULL;
}

static const char *first_audio(const char *json) {
    const char *p = json;
    while ((p = find_key(p, "audio")) != NULL) {
        if (*p == 'h' && strncmp(p, "http", 4) == 0) return p;
        p++;
    }
    return NULL;
}

static const wchar_t *pos_abbr(const wchar_t *p) {
    if (!lstrcmpiW(p, L"noun")) return L"n.";
    if (!lstrcmpiW(p, L"verb")) return L"v.";
    if (!lstrcmpiW(p, L"adjective")) return L"a.";
    if (!lstrcmpiW(p, L"adverb")) return L"ad.";
    if (!lstrcmpiW(p, L"interjection")) return L"int.";
    if (!lstrcmpiW(p, L"pronoun")) return L"pron.";
    if (!lstrcmpiW(p, L"preposition")) return L"prep.";
    if (!lstrcmpiW(p, L"conjunction")) return L"conj.";
    if (!lstrcmpiW(p, L"determiner")) return L"det.";
    if (!lstrcmpiW(p, L"numeral")) return L"num.";
    return p;
}

static void parse_json(const char *json) {
    wchar_t out[4096];
    int used = 0;
    out[0] = 0;

    const char *p = json;
    int shown = 0;

    while (p && shown < 4) {
        const char *pk = strstr(p, "\"partOfSpeech\"");
        if (!pk) break;
        const char *v = strchr(pk, ':');
        if (!v) break;
        v++;
        while (*v == ' ') v++;

        wchar_t pos[32];
        const char *after = v;
        if (*v == '"') after = json_str(v + 1, pos, 32);
        else pos[0] = 0;

        const char *nextpk = strstr(pk + 14, "\"partOfSpeech\"");
        const char *pend = nextpk ? nextpk : json + strlen(json);

        const char *q = after;
        int got = 0;
        while (q < pend && got < 2 && shown < 4) {
            const char *d = strstr(q, "\"definition\"");
            if (!d || d >= pend) break;
            const char *dv = strchr(d, ':');
            if (!dv) break;
            dv++;
            while (*dv == ' ') dv++;
            wchar_t def[512];
            const char *nd = dv;
            if (*dv == '"') nd = json_str(dv + 1, def, 512);
            else def[0] = 0;

            wchar_t eg[400];
            eg[0] = 0;
            const char *e = strstr(nd, "\"example\"");
            if (e && e < pend) {
                const char *ev = strchr(e, ':');
                if (ev) {
                    ev++;
                    while (*ev == ' ') ev++;
                    if (*ev == '"') json_str(ev + 1, eg, 400);
                }
            }
            if (eg[0] && def[0]) {
                int w = _snwprintf_s(out + used, 4096 - (size_t)used, _TRUNCATE,
                                     L"%ls%ls %ls\n    \u201C%ls\u201D\n",
                                     shown ? L"\n" : L"", pos_abbr(pos), def, eg);
                if (w > 0) used += w;
                if (used < 0) used = 0;
                if (used > 3700) { used = 3700; out[used] = 0; break; }
                got++;
                shown++;
            }
            q = nd;
        }
        p = pend;
    }
    out[used] = 0;

    EnterCriticalSection(&g_cs);
    lstrcpynW(g_examples, out, 4096);
    LeaveCriticalSection(&g_cs);
}

/* ------------------------------------------------------------ 线程 */
static void keep_letters(wchar_t *s) {
    for (wchar_t *p = s; *p; p++)
        if (!((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z')))
            *p = L'\0';
}

static char *take_arg(LPVOID param) {
    char *w = (char *)malloc(128);
    if (!w) { free(param); return NULL; }
    lstrcpynA(w, (const char *)param, 128);
    free(param);
    return w;
}

static DWORD WINAPI fetch_thread(LPVOID param) {
    char *word = take_arg(param);
    if (!word) return 0;

    wchar_t wword[128], wpath[256];
    MultiByteToWideChar(CP_UTF8, 0, word, -1, wword, 128);
    keep_letters(wword);
    free(word);
    if (!wword[0]) { InterlockedExchange(&g_state, 0); return 0; }
    _snwprintf_s(wpath, 256, _TRUNCATE, L"/api/v2/entries/en/%ls", wword);

    DWORD len = 0;
    char *json = http_get(L"api.dictionaryapi.dev", wpath, &len);

    int ok = 0;
    if (json && len > 10) {
        parse_json(json);
        EnterCriticalSection(&g_cs);
        ok = g_examples[0] != 0;
        LeaveCriticalSection(&g_cs);
    }
    if (json) free(json);
    InterlockedExchange(&g_state, ok ? 2 : 0);

    if (g_notify) PostMessageW(g_notify, WM_APP_ONLINE, 0, 0);
    return 0;
}

void online_init(HWND notify) {
    g_notify = notify;
    InitializeCriticalSection(&g_cs);
}

void online_fetch(const char *word) {
    if (!word || !*word) return;
    EnterCriticalSection(&g_cs);
    int same = (g_state == 2 || g_state == 1) && lstrcmpiA(g_word, word) == 0;
    lstrcpynA(g_word, word, 128);
    if (same) { LeaveCriticalSection(&g_cs); return; }
    g_examples[0] = 0;
    LeaveCriticalSection(&g_cs);

    InterlockedExchange(&g_state, 1);
    char *arg = (char *)malloc(128);
    if (!arg) return;
    lstrcpynA(arg, word, 128);
    HANDLE t = CreateThread(NULL, 0, fetch_thread, arg, 0, NULL);
    if (t) CloseHandle(t); else free(arg);

    if (g_notify) PostMessageW(g_notify, WM_APP_ONLINE, 0, 0);
}

int online_state(void) { return (int)g_state; }

const wchar_t *online_examples(void) {
    static wchar_t out[4096];
    EnterCriticalSection(&g_cs);
    lstrcpynW(out, g_examples, 4096);
    LeaveCriticalSection(&g_cs);
    return out;
}

/* ------------------------------------------------------------ 发音 */
static void speak_tts(const char *word) {
    wchar_t w[128];
    MultiByteToWideChar(CP_UTF8, 0, word, -1, w, 128);

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ISpVoice *voice = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL,
                                   &IID_ISpVoice, (void **)&voice)) && voice) {
        voice->lpVtbl->Speak(voice, w, SPF_DEFAULT, NULL);
        voice->lpVtbl->Release(voice);
    }
    if (hr == S_OK || hr == S_FALSE) CoUninitialize();
}

static DWORD WINAPI play_thread(LPVOID param) {
    char *word = take_arg(param);
    if (!word) return 0;

    wchar_t wword[128], wpath[256];
    MultiByteToWideChar(CP_UTF8, 0, word, -1, wword, 128);
    keep_letters(wword);
    if (!wword[0]) { free(word); return 0; }
    _snwprintf_s(wpath, 256, _TRUNCATE, L"/api/v2/entries/en/%ls", wword);

    DWORD len = 0;
    char *json = http_get(L"api.dictionaryapi.dev", wpath, &len);
    char url[512];
    url[0] = 0;
    if (json) {
        const char *a = first_audio(json);
        if (a) {
            int i = 0;
            while (*a && *a != '"' && i < 500) url[i++] = *a++;
            url[i] = 0;
        }
        free(json);
    }

    int played = 0;
    if (url[0]) {
        wchar_t wurl[600];
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 600);
        const wchar_t *path = wcsstr(wurl, L"/media/");
        if (path) {
            DWORD alen = 0;
            char *audio = http_get(L"api.dictionaryapi.dev", path, &alen);
            if (audio && alen > 800) {
                wchar_t tmp[MAX_PATH], file[MAX_PATH];
                GetTempPathW(MAX_PATH, tmp);
                GetTempFileNameW(tmp, L"ldt", 0, file);
                HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL,
                                       CREATE_ALWAYS, 0, NULL);
                if (f != INVALID_HANDLE_VALUE) {
                    DWORD wr = 0;
                    WriteFile(f, audio, alen, &wr, NULL);
                    CloseHandle(f);
                    if (PlaySoundW(file, NULL, SND_FILENAME | SND_SYNC)) played = 1;
                    DeleteFileW(file);
                }
            }
            if (audio) free(audio);
        }
    }
    if (!played) speak_tts(word);
    free(word);
    return 0;
}

void online_play(const char *word) {
    if (!word || !*word) return;
    char *arg = (char *)malloc(128);
    if (!arg) return;
    lstrcpynA(arg, word, 128);
    HANDLE t = CreateThread(NULL, 0, play_thread, arg, 0, NULL);
    if (t) CloseHandle(t); else free(arg);
}
