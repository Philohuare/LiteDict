#define UNICODE
#define _UNICODE
#include <windows.h>

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)(COLOR_WINDOW + 1));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(20, 20, 20));
        HFONT f = CreateFontW(-32, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                              0, 0, 5, 0, L"Georgia");
        HFONT old = SelectObject(dc, f);
        DrawTextW(dc, L"study", -1, &r, DT_CENTER | DT_TOP | DT_SINGLELINE);
        SelectObject(dc, old);
        DeleteObject(f);

        f = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                        0, 0, 5, 0, L"Microsoft YaHei UI");
        old = SelectObject(dc, f);
        r.top += 60;
        DrawTextW(dc, L"n. 学习, 研究, 学科 · 中文渲染测试 ABC 123", -1, &r,
                  DT_CENTER | DT_TOP | DT_SINGLELINE);
        SelectObject(dc, old);
        DeleteObject(f);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show) {
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"LiteDictTest";
    RegisterClassW(&wc);
    HWND h = CreateWindowW(L"LiteDictTest", L"LiteDict 编译测试", WS_OVERLAPPEDWINDOW,
                           100, 100, 700, 300, NULL, NULL, hi, NULL);
    ShowWindow(h, show);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
